use crate::{
    Bindings, assets,
    memory::{pointer, read},
};
use eroverlay_abi::{FILE_FAILED, FILE_PENDING, FILE_READY, FILE_UNSUPPORTED, MAX_FILE_BYTES};
use std::{
    collections::HashMap,
    ffi::c_void,
    sync::{
        Arc, Mutex,
        atomic::{AtomicU32, AtomicUsize, Ordering},
    },
    time::{Duration, Instant},
};
const QUEUED: u32 = 0;
const PENDING: u32 = 1;
const COMPLETE: u32 = 2;
const FAILED: u32 = 3;
fn reserve_budget(budget: &AtomicUsize, count: usize) -> bool {
    let mut used = budget.load(Ordering::Acquire);
    loop {
        let Some(next) = used
            .checked_add(count)
            .filter(|next| *next <= MAX_FILE_BYTES)
        else {
            return false;
        };
        match budget.compare_exchange_weak(used, next, Ordering::AcqRel, Ordering::Acquire) {
            Ok(_) => return true,
            Err(current) => used = current,
        }
    }
}
#[repr(C)]
struct NativeRequest {
    path: *const u16,
    allocator: usize,
    alignment: u64,
    complete: unsafe extern "C" fn(u32, *mut c_void, *mut c_void, u64),
    stage: usize,
    context: *mut c_void,
    flags: u32,
    parameter: u16,
    padding: u16,
    priority: u32,
    tail: u32,
}
struct PartRequest {
    name: Option<String>,
    max_bytes: usize,
}
struct Completion {
    parts: Vec<Option<Vec<u8>>>,
    native_status: i32,
}
// A separate allocation is shared with native callbacks. The owner never forms
// mutable references to it after submit; publication uses atomics and a mutex.
struct CallbackState {
    parts: Vec<PartRequest>,
    allocator: AtomicUsize,
    status: AtomicU32,
    completion: Mutex<Completion>,
    budget: Arc<AtomicUsize>,
    owned_bytes: AtomicUsize,
}
impl Drop for CallbackState {
    fn drop(&mut self) {
        self.budget
            .fetch_sub(self.owned_bytes.load(Ordering::Relaxed), Ordering::Relaxed);
    }
}
struct Item {
    path: Vec<u16>,
    flags: u32,
    native_id: u64,
    submitted: bool,
    manager: usize,
    users: usize,
    cancel_sent: bool,
    callback: Box<CallbackState>,
}
struct Token {
    item: u64,
    part: usize,
    max_bytes: usize,
}
pub struct FilePoll<'a> {
    pub status: u32,
    pub bytes: &'a [u8],
    pub native_status: i32,
}
/// Current bounded request inventory. Called by low-frequency update logging,
/// never from render; callback publication is sampled without waiting on it.
#[derive(Clone, Copy, Debug, Default)]
pub struct FileDiagnostics {
    pub native_status: i32,
    pub queued: u32,
    pub submitted: u32,
    pub pending: u32,
    pub complete: u32,
    pub failed: u32,
    pub tokens: u32,
    pub owned_bytes: usize,
    pub status_unavailable: u32,
}
pub struct GameFiles {
    base: usize,
    bindings: Option<Bindings>,
    items: HashMap<u64, Box<Item>>,
    tokens: HashMap<u64, Token>,
    next: u64,
    stopping: bool,
    budget: Arc<AtomicUsize>,
}

unsafe extern "C" fn complete(status: u32, context: *mut c_void, buffer: *mut c_void, size: u64) {
    if context.is_null() {
        return;
    }
    // SAFETY: this separate callback allocation lives through native retirement.
    let state = unsafe { &*(context.cast::<CallbackState>()) };
    let result = std::panic::catch_unwind(std::panic::AssertUnwindSafe(|| {
        if status != 1 || buffer.is_null() || size == 0 || size > MAX_FILE_BYTES as u64 * 4 {
            return None;
        }
        // SAFETY: completion transfers a readable native allocation until free.
        let bytes = unsafe { std::slice::from_raw_parts(buffer.cast::<u8>(), size as usize) };
        let entries = if state.parts.iter().any(|p| p.name.is_some()) {
            Some(assets::parse_tpf(bytes).ok()?)
        } else {
            None
        };
        let mut copied = 0usize;
        let mut results = Vec::with_capacity(state.parts.len());
        for part in &state.parts {
            let selected = if let Some(name) = &part.name {
                let mut hits = entries.as_ref()?.iter().filter(|entry| entry.name == *name);
                let first = hits.next();
                if hits.next().is_some() {
                    None
                } else {
                    first.map(|entry| entry.dds)
                }
            } else {
                Some(bytes)
            };
            let selected = selected
                .filter(|b| b.len() <= part.max_bytes && b.len() <= MAX_FILE_BYTES - copied);
            results.push(selected.and_then(|bytes| {
                let count = bytes.len();
                if !reserve_budget(&state.budget, count) {
                    return None;
                }
                let mut owned = Vec::new();
                if owned.try_reserve_exact(count).is_err() {
                    state.budget.fetch_sub(count, Ordering::Release);
                    return None;
                }
                state.owned_bytes.fetch_add(count, Ordering::Relaxed);
                owned.extend_from_slice(bytes);
                copied += count;
                Some(owned)
            }));
        }
        Some(results)
    }));
    if !buffer.is_null() {
        let allocator = state.allocator.load(Ordering::Acquire);
        let table = pointer(allocator);
        let release = if table != 0 {
            pointer(table + 13 * 8)
        } else {
            0
        };
        if release != 0 {
            // SAFETY: allocator slot 13 is validated by the scanned request ABI.
            let free: unsafe extern "C" fn(usize, *mut c_void) =
                unsafe { std::mem::transmute(release) };
            unsafe { free(allocator, buffer) };
        }
    }
    let parts = result.ok().flatten();
    let ok = parts.is_some();
    if let Ok(mut completion) = state.completion.lock() {
        completion.native_status = status as i32;
        completion.parts = parts.unwrap_or_default();
    } else {
        state.status.store(FAILED, Ordering::Release);
        return;
    }
    // This is the callback's final state access. Native generation retirement
    // additionally proves the return address is no longer inside this DLL.
    state
        .status
        .store(if ok { COMPLETE } else { FAILED }, Ordering::Release);
}
impl GameFiles {
    pub fn diagnostics(&self) -> FileDiagnostics {
        let mut d = FileDiagnostics {
            tokens: self.tokens.len() as u32,
            owned_bytes: self.budget.load(Ordering::Acquire),
            ..Default::default()
        };
        for item in self.items.values() {
            d.submitted += u32::from(item.submitted);
            let status = item.callback.status.load(Ordering::Acquire);
            match status {
                QUEUED => d.queued += 1,
                PENDING => d.pending += 1,
                COMPLETE => d.complete += 1,
                FAILED => d.failed += 1,
                _ => {}
            }
            if matches!(status, COMPLETE | FAILED) {
                if let Ok(completion) = item.callback.completion.try_lock() {
                    // Prefer a native failure status over successful completions.
                    if d.native_status == 0 || completion.native_status != 1 {
                        d.native_status = completion.native_status;
                    }
                } else {
                    d.status_unavailable += 1;
                }
            }
        }
        d
    }
    pub fn new(base: usize, bindings: Option<Bindings>) -> Self {
        Self {
            base,
            bindings,
            items: HashMap::new(),
            tokens: HashMap::new(),
            next: 1,
            stopping: false,
            budget: Arc::new(AtomicUsize::new(0)),
        }
    }
    pub fn request(&mut self, path: &str, part: Option<&str>, flags: u32, max_bytes: usize) -> u64 {
        if self.stopping
            || self.tokens.len() >= 128
            || path.is_empty()
            || path.contains('\0')
            || path.encode_utf16().count() > 1024
            || flags & !0x40 != 0
            || part.is_some_and(|p| {
                p.is_empty() || p.contains('\0') || p.encode_utf16().count() > 4096
            })
        {
            return 0;
        }
        let mut canonical = path.to_owned();
        if let Some(colon) = canonical.find(':')
            && colon > 1
            && canonical.find(['/', '\\']) == Some(colon + 1)
        {
            canonical.make_ascii_lowercase();
            canonical = canonical.replace('\\', "/");
        }
        let path: Vec<u16> = canonical.encode_utf16().chain(Some(0)).collect();
        let max_bytes = if max_bytes == 0 {
            MAX_FILE_BYTES
        } else {
            max_bytes.min(MAX_FILE_BYTES)
        };
        let token = self.next;
        self.next = self.next.wrapping_add(1).max(1);
        // Batch named parts queued in one update into one archive read. This
        // preserves a bounded selected-DDS copy budget without retaining TPFs.
        let existing = part.and_then(|_| {
            self.items
                .iter()
                .find(|(_, item)| {
                    item.path == path
                        && item.flags == flags
                        && item.callback.status.load(Ordering::Acquire) == QUEUED
                        && item.callback.parts.iter().all(|p| p.name.is_some())
                })
                .map(|(id, _)| *id)
        });
        // Released tokens may still own queued/native callbacks until pump()
        // proves retirement. Match the C++ limit on retained requests, while
        // allowing another named part to reuse an already queued archive.
        if existing.is_none() && self.items.len() >= 128 {
            return 0;
        }
        let id = existing.unwrap_or(token);
        let item = self.items.entry(id).or_insert_with(|| {
            Box::new(Item {
                path,
                flags,
                native_id: 0,
                submitted: false,
                manager: 0,
                users: 0,
                cancel_sent: false,
                callback: Box::new(CallbackState {
                    parts: Vec::new(),
                    allocator: AtomicUsize::new(0),
                    status: AtomicU32::new(if self.bindings.is_some() {
                        QUEUED
                    } else {
                        FAILED
                    }),
                    completion: Mutex::new(Completion {
                        parts: Vec::new(),
                        native_status: 0,
                    }),
                    budget: Arc::clone(&self.budget),
                    owned_bytes: AtomicUsize::new(0),
                }),
            })
        });
        let index = if let Some(index) = item
            .callback
            .parts
            .iter()
            .position(|p| p.name.as_deref() == part)
        {
            item.callback.parts[index].max_bytes =
                item.callback.parts[index].max_bytes.max(max_bytes);
            index
        } else {
            let index = item.callback.parts.len();
            item.callback.parts.push(PartRequest {
                name: part.map(str::to_owned),
                max_bytes,
            });
            index
        };
        item.users += 1;
        self.tokens.insert(
            token,
            Token {
                item: id,
                part: index,
                max_bytes,
            },
        );
        token
    }
    fn retired(base: usize, b: &Bindings, item: &Item) -> bool {
        if !item.submitted {
            return true;
        }
        let owner = pointer(base + b.retire_pool_owner as usize);
        let pool = if owner != 0 { pointer(owner) } else { 0 };
        let generations = if pool != 0 { pointer(pool + 0x50) } else { 0 };
        generations != 0
            && read::<u16>(generations + (item.native_id as usize & 0xffff) * 2)
                .is_some_and(|generation| generation != ((item.native_id >> 16) & 0xffff) as u16)
    }
    pub fn pump(&mut self) {
        if self.items.is_empty() {
            return;
        }
        let Some(b) = self.bindings else {
            self.items.retain(|_, item| item.users != 0);
            return;
        };
        let manager = pointer(self.base + b.manager as usize);
        let allocator = pointer(self.base + b.allocator as usize);
        let ready = manager != 0 && allocator != 0 && read::<u8>(manager + 0xe38) == Some(0);
        let mut submitted = false;
        for item in self.items.values_mut() {
            let state = item.callback.status.load(Ordering::Acquire);
            if state == QUEUED {
                if self.stopping || item.users == 0 {
                    item.callback.status.store(FAILED, Ordering::Release);
                } else if ready {
                    item.manager = manager;
                    item.callback.allocator.store(allocator, Ordering::Release);
                    item.callback.status.store(PENDING, Ordering::Release);
                    let request = NativeRequest {
                        path: item.path.as_ptr(),
                        allocator,
                        alignment: 128,
                        complete,
                        stage: 0,
                        context: (&*item.callback as *const CallbackState).cast_mut().cast(),
                        flags: item.flags,
                        parameter: 0,
                        padding: 0,
                        priority: 0,
                        tail: 0,
                    };
                    // SAFETY: only scan-validated x64 game functions are invoked.
                    let submit: unsafe extern "C" fn(usize, *mut u64, *const NativeRequest) -> i32 =
                        unsafe { std::mem::transmute(self.base + b.submit as usize) };
                    let result = unsafe { submit(manager, &mut item.native_id, &request) };
                    item.submitted = result == 0;
                    submitted |= item.submitted;
                    if result != 0 {
                        if let Ok(mut c) = item.callback.completion.lock() {
                            c.native_status = result;
                        }
                        item.callback.status.store(FAILED, Ordering::Release);
                    }
                }
            } else if state == PENDING
                && (item.users == 0 || self.stopping)
                && !item.cancel_sent
                && ready
                && item.manager == manager
            {
                item.cancel_sent = true;
                // SAFETY: cancel target and queue layout were validated together.
                let enqueue: unsafe extern "C" fn(usize, usize, u64) =
                    unsafe { std::mem::transmute(self.base + b.enqueue as usize) };
                unsafe {
                    enqueue(
                        manager + 0x20,
                        self.base + b.cancel as usize,
                        item.native_id,
                    )
                };
            }
        }
        if submitted {
            // SAFETY: flush is part of the same validated native request ABI.
            let flush: unsafe extern "C" fn(usize) =
                unsafe { std::mem::transmute(self.base + b.flush as usize) };
            unsafe { flush(manager) };
        }
        self.items.retain(|_, item| {
            !((item.users == 0 || self.stopping)
                && item.callback.status.load(Ordering::Acquire) >= COMPLETE
                && Self::retired(self.base, &b, item))
        });
    }
    pub fn poll(&self, token: u64) -> FilePoll<'_> {
        let failed = |status| FilePoll {
            status,
            bytes: &[],
            native_status: 0,
        };
        let Some(token) = self.tokens.get(&token) else {
            return failed(FILE_FAILED);
        };
        let Some(item) = self.items.get(&token.item) else {
            return failed(FILE_FAILED);
        };
        if self.bindings.is_none() {
            return failed(FILE_UNSUPPORTED);
        }
        let status = item.callback.status.load(Ordering::Acquire);
        if status < COMPLETE {
            return failed(FILE_PENDING);
        }
        let Ok(completion) = item.callback.completion.lock() else {
            return failed(FILE_FAILED);
        };
        let Some(bytes) = completion
            .parts
            .get(token.part)
            .and_then(Option::as_ref)
            .filter(|b| b.len() <= token.max_bytes)
        else {
            return FilePoll {
                status: FILE_FAILED,
                bytes: &[],
                native_status: completion.native_status,
            };
        };
        // SAFETY: final publication makes completion immutable. The returned
        // borrow excludes mutable release/pump until the caller finishes reading.
        let bytes = unsafe { std::slice::from_raw_parts(bytes.as_ptr(), bytes.len()) };
        FilePoll {
            status: if status == COMPLETE {
                FILE_READY
            } else {
                FILE_FAILED
            },
            bytes,
            native_status: completion.native_status,
        }
    }
    pub fn release(&mut self, token: u64) {
        if let Some(token) = self.tokens.remove(&token)
            && let Some(item) = self.items.get_mut(&token.item)
        {
            item.users = item.users.saturating_sub(1);
            // No native reader has seen QUEUED parts yet. Keep only names with
            // live consumers, so a retained archive cannot accumulate released
            // names/copy budgets across repeated request/release calls. Once
            // PENDING, the callback's separate allocation remains immutable.
            if item.callback.status.load(Ordering::Acquire) == QUEUED {
                let remaining_max = self
                    .tokens
                    .values()
                    .filter(|t| t.item == token.item && t.part == token.part)
                    .map(|t| t.max_bytes)
                    .max();
                if let Some(max) = remaining_max {
                    item.callback.parts[token.part].max_bytes = max;
                } else {
                    item.callback.parts.remove(token.part);
                    for remaining in self.tokens.values_mut() {
                        if remaining.item == token.item && remaining.part > token.part {
                            remaining.part -= 1;
                        }
                    }
                }
            }
        }
    }
    /// False keeps all callback storage alive and forbids unloading this DLL.
    pub fn stop(&mut self) -> bool {
        self.stopping = true;
        self.tokens.clear();
        for item in self.items.values_mut() {
            item.users = 0;
        }
        let deadline = Instant::now() + Duration::from_secs(5);
        loop {
            self.pump();
            if self.items.is_empty() {
                return true;
            }
            if Instant::now() >= deadline {
                return false;
            }
            std::thread::sleep(Duration::from_millis(5));
        }
    }
}
impl Drop for GameFiles {
    fn drop(&mut self) {
        if !self.stop() {
            for (_, item) in self.items.drain() {
                let _ = Box::leak(item);
            }
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::sync::atomic::AtomicUsize;
    static MANAGER: AtomicUsize = AtomicUsize::new(0);
    static ALLOCATOR: AtomicUsize = AtomicUsize::new(0);
    static OWNER: AtomicUsize = AtomicUsize::new(0);
    static FREED: AtomicUsize = AtomicUsize::new(0);
    type PendingCallback = (
        unsafe extern "C" fn(u32, *mut c_void, *mut c_void, u64),
        usize,
    );
    static PENDING_CALLBACK: Mutex<Option<PendingCallback>> = Mutex::new(None);
    #[link(name = "kernel32")]
    unsafe extern "system" {
        fn GetProcessHeap() -> isize;
        fn HeapAlloc(heap: isize, flags: u32, size: usize) -> *mut c_void;
        fn HeapFree(heap: isize, flags: u32, allocation: *mut c_void) -> i32;
    }
    unsafe extern "C" fn submit(_: usize, id: *mut u64, request: *const NativeRequest) -> i32 {
        // SAFETY: this controlled harness is called with stack-owned native request.
        let request = unsafe { &*request };
        unsafe { *id = 0x1234_0005 };
        *PENDING_CALLBACK.lock().unwrap() = Some((request.complete, request.context as usize));
        0
    }
    unsafe extern "C" fn flush(_: usize) {}
    unsafe extern "C" fn enqueue(_: usize, _: usize, _: u64) {}
    unsafe extern "C" fn release(_: usize, allocation: *mut c_void) {
        // SAFETY: the test transfers exactly one HeapAlloc allocation to callback.
        unsafe { HeapFree(GetProcessHeap(), 0, allocation) };
        FREED.fetch_add(1, Ordering::Relaxed);
    }
    #[test]
    fn request_validation_and_unavailable_shutdown() {
        assert_eq!(size_of::<NativeRequest>(), 64);
        let mut files = GameFiles::new(0, None);
        assert_eq!(files.request("", None, 0, 0), 0);
        assert_eq!(files.request("menu:/x", None, 1, 0), 0);
        let id = files.request("menu:/x", None, 0, 0);
        assert_ne!(id, 0);
        assert_eq!(files.poll(id).status, FILE_UNSUPPORTED);
        files.release(id);
        files.pump();
        assert!(files.stop());
    }
    #[test]
    fn aggregate_file_copy_budget_is_bounded() {
        let budget = AtomicUsize::new(MAX_FILE_BYTES - 4);
        assert!(reserve_budget(&budget, 4));
        assert!(!reserve_budget(&budget, 1));
        assert_eq!(budget.load(Ordering::Relaxed), MAX_FILE_BYTES);
        assert!(!reserve_budget(&budget, usize::MAX));
    }
    #[test]
    fn released_requests_keep_native_capacity_until_pump_retires_them() {
        let mut files = GameFiles::new(0, Some(Bindings::default()));
        for index in 0..128 {
            let token = files.request(&format!("menu:/retained-{index}"), None, 0, 64);
            assert_ne!(token, 0);
            files.release(token);
        }
        assert!(files.tokens.is_empty());
        assert_eq!(files.items.len(), 128);
        assert_eq!(files.request("menu:/over-capacity", None, 0, 64), 0);
        files.pump();
        assert!(files.items.is_empty());
        let token = files.request("menu:/after-retirement", None, 0, 64);
        assert_ne!(token, 0);
        files.release(token);
        assert!(files.stop());
    }
    #[test]
    fn full_native_capacity_still_allows_parts_to_share_a_queued_archive() {
        let mut files = GameFiles::new(0, Some(Bindings::default()));
        let first = files.request("menu:/shared.tpf", Some("first"), 0x40, 64);
        assert_ne!(first, 0);
        for index in 0..127 {
            let token = files.request(&format!("menu:/retained-{index}"), None, 0, 64);
            assert_ne!(token, 0);
            files.release(token);
        }
        assert_eq!(files.items.len(), 128);
        let second = files.request("menu:/shared.tpf", Some("second"), 0x40, 64);
        assert_ne!(second, 0);
        assert_eq!(files.items.len(), 128);
        assert_eq!(files.request("menu:/over-capacity", None, 0, 64), 0);
        files.release(first);
        files.release(second);
        assert!(files.stop());
    }
    #[test]
    fn queued_named_parts_share_one_native_read() {
        let mut files = GameFiles::new(0, Some(Bindings::default()));
        let a = files.request("MENU:/hi\\01_common.tpf", Some("a"), 0x40, 100);
        let b = files.request("menu:/hi/01_common.tpf", Some("b"), 0x40, 100);
        assert_ne!(a, b);
        assert_eq!(files.items.len(), 1);
        let item = files.items.values().next().unwrap();
        assert_eq!(item.callback.parts.len(), 2);
        files.release(a);
        files.release(b);
        assert!(files.stop());
    }
    #[test]
    fn released_queued_parts_stay_bounded_and_preserve_active_token_indices_and_limits() {
        let mut files = GameFiles::new(0, Some(Bindings::default()));
        let anchor = files.request("menu:/shared.tpf", Some("anchor"), 0, 64);
        for index in 0..1000 {
            let token = files.request("menu:/shared.tpf", Some(&format!("unused-{index}")), 0, 128);
            assert_ne!(token, 0);
            files.release(token);
            let item = files.items.values().next().unwrap();
            assert_eq!(
                (item.users, item.callback.parts.len(), files.tokens.len()),
                (1, 1, 1)
            );
        }
        let larger = files.request("menu:/shared.tpf", Some("anchor"), 0, 1024);
        let medium = files.request("menu:/shared.tpf", Some("anchor"), 0, 256);
        let last = files.request("menu:/shared.tpf", Some("last"), 0, 32);
        let item_id = files.tokens[&anchor].item;
        assert_eq!(files.items[&item_id].callback.parts[0].max_bytes, 1024);
        files.release(larger);
        assert_eq!(files.items[&item_id].callback.parts[0].max_bytes, 256);
        files.release(medium);
        assert_eq!(files.items[&item_id].callback.parts[0].max_bytes, 64);
        files.release(anchor);
        assert_eq!(files.tokens[&last].part, 0);
        assert_eq!(
            files.items[&item_id].callback.parts[0].name.as_deref(),
            Some("last")
        );
        files.release(last);
        assert!(files.items[&item_id].callback.parts.is_empty());
        assert!(files.stop());
    }
    #[test]
    fn asynchronous_native_completion_cannot_retire_until_callback_return_generation() {
        let base = crate::memory::main_module();
        let rva = |pointer: usize| u32::try_from(pointer - base).unwrap();
        let mut manager = vec![0u8; 0xe40];
        let mut vtable = [0usize; 14];
        vtable[13] = release as *const () as usize;
        let allocator = [vtable.as_ptr() as usize];
        let mut pool = [0usize; 12];
        let mut generations = [0u16; 16];
        generations[5] = 0x1234;
        pool[0x50 / 8] = generations.as_ptr() as usize;
        let owner = [pool.as_ptr() as usize];
        MANAGER.store(manager.as_mut_ptr() as usize, Ordering::SeqCst);
        ALLOCATOR.store(allocator.as_ptr() as usize, Ordering::SeqCst);
        OWNER.store(owner.as_ptr() as usize, Ordering::SeqCst);
        FREED.store(0, Ordering::SeqCst);
        let b = Bindings {
            manager: rva(&MANAGER as *const _ as usize),
            allocator: rva(&ALLOCATOR as *const _ as usize),
            retire_pool_owner: rva(&OWNER as *const _ as usize),
            submit: rva(submit as *const () as usize),
            flush: rva(flush as *const () as usize),
            enqueue: rva(enqueue as *const () as usize),
            ..Default::default()
        };
        let mut files = GameFiles::new(base, Some(b));
        let token = files.request("menu:/fixture", None, 0, 64);
        let d = files.diagnostics();
        assert_eq!(
            (
                d.queued,
                d.pending,
                d.complete,
                d.failed,
                d.tokens,
                d.owned_bytes
            ),
            (1, 0, 0, 0, 1, 0)
        );
        files.pump();
        assert_eq!(files.poll(token).status, FILE_PENDING);
        let d = files.diagnostics();
        assert_eq!(
            (d.queued, d.submitted, d.pending, d.complete, d.tokens),
            (0, 1, 1, 0, 1)
        );
        let (callback, context) = PENDING_CALLBACK.lock().unwrap().take().unwrap();
        // SAFETY: owned 64-byte allocation is transferred to the completion.
        let allocation = unsafe { HeapAlloc(GetProcessHeap(), 0, 64) };
        assert!(!allocation.is_null());
        unsafe { std::ptr::write_bytes(allocation, 0xa5, 64) };
        let pointer = allocation as usize;
        std::thread::spawn(move || unsafe {
            callback(1, context as *mut c_void, pointer as *mut c_void, 64)
        })
        .join()
        .unwrap();
        let data = files.poll(token);
        assert_eq!(data.status, FILE_READY);
        assert_eq!(data.bytes, &[0xa5; 64]);
        let d = files.diagnostics();
        assert_eq!(
            (
                d.native_status,
                d.pending,
                d.complete,
                d.failed,
                d.tokens,
                d.owned_bytes
            ),
            (1, 0, 1, 0, 1, 64)
        );
        assert_eq!(FREED.load(Ordering::Relaxed), 1);
        files.release(token);
        files.pump();
        assert_eq!(files.diagnostics().tokens, 0);
        assert_eq!(
            files.diagnostics().owned_bytes,
            64,
            "native return generation still retains copied bytes"
        );
        assert_eq!(
            files.items.len(),
            1,
            "completion alone cannot prove callback code returned"
        );
        generations[5] = 0x1235;
        std::hint::black_box(&generations);
        files.pump();
        assert!(files.items.is_empty());
        assert_eq!(files.diagnostics().owned_bytes, 0);
        // A submitted named archive must copy only its still-active selection.
        // In particular, release must remap b's token after removing a's slot.
        generations[5] = 0x1234;
        std::hint::black_box(&generations);
        let a = files.request("menu:/fixture.tpf", Some("a"), 0, 64);
        let b = files.request("menu:/fixture.tpf", Some("b"), 0, 64);
        let larger_b = files.request("menu:/fixture.tpf", Some("b"), 0, 128);
        files.release(a);
        assert_eq!(files.items[&files.tokens[&b].item].callback.parts.len(), 1);
        files.pump();
        files.release(larger_b);
        assert_eq!(
            files.items[&files.tokens[&b].item].callback.parts[0].max_bytes, 128,
            "PENDING callback selection stays immutable"
        );
        let (callback, context) = PENDING_CALLBACK.lock().unwrap().take().unwrap();
        let mut tpf = [0u8; 72];
        tpf[..4].copy_from_slice(b"TPF\0");
        tpf[8..12].copy_from_slice(&2u32.to_le_bytes());
        tpf[13] = 3;
        for (entry, data, name) in [(16, 64u32, 56u32), (36, 68, 60)] {
            tpf[entry..entry + 4].copy_from_slice(&data.to_le_bytes());
            tpf[entry + 4..entry + 8].copy_from_slice(&4u32.to_le_bytes());
            tpf[entry + 12..entry + 16].copy_from_slice(&name.to_le_bytes());
        }
        tpf[56] = b'a';
        tpf[60] = b'b';
        tpf[64..68].fill(0xa4);
        tpf[68..72].fill(0xb4);
        // SAFETY: this fixture transfers a single owned native allocation.
        let allocation = unsafe { HeapAlloc(GetProcessHeap(), 0, tpf.len()) };
        assert!(!allocation.is_null());
        unsafe {
            std::ptr::copy_nonoverlapping(tpf.as_ptr(), allocation.cast(), tpf.len());
            callback(1, context as *mut c_void, allocation, tpf.len() as u64)
        };
        assert_eq!(files.poll(b).bytes, &[0xb4; 4]);
        assert_eq!(
            files.diagnostics().owned_bytes,
            4,
            "released a is not copied"
        );
        assert_eq!(FREED.load(Ordering::Relaxed), 2);
        files.release(b);
        generations[5] = 0x1235;
        std::hint::black_box(&generations);
        files.pump();
        assert!(files.items.is_empty());
        assert_eq!(files.diagnostics().owned_bytes, 0);
        assert!(files.stop());
    }
}
