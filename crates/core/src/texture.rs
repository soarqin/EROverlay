use eroverlay_abi::*;
use std::{
    collections::{HashMap, VecDeque},
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, AtomicU64, Ordering},
        mpsc::{self, Receiver, SyncSender},
    },
    thread::{self, JoinHandle},
};

const MAX_TEXTURES: usize = 768;
const MAX_QUEUED_BYTES: u64 = 64 * 1024 * 1024;

// One reservation follows source bytes through the input job, core preparation
// and renderer-owned source payloads. GPU upload resources have a separate
// renderer budget/fence lifetime. Only the last source owner refunds this budget.
// The counter is independent of Shared: Records must not form an Arc cycle.
struct Reservation {
    counter: Arc<AtomicU64>,
    bytes: u64,
}
impl Reservation {
    fn acquire(counter: &Arc<AtomicU64>, bytes: usize) -> Option<Arc<Self>> {
        counter
            .try_update(Ordering::AcqRel, Ordering::Acquire, |n| {
                n.checked_add(bytes as u64)
                    .filter(|&n| n <= MAX_QUEUED_BYTES)
            })
            .ok()?;
        Some(Arc::new(Self {
            counter: counter.clone(),
            bytes: bytes as u64,
        }))
    }
}
impl Drop for Reservation {
    fn drop(&mut self) {
        self.counter.fetch_sub(self.bytes, Ordering::AcqRel);
    }
}
pub struct Pixels {
    pub width: u32,
    pub height: u32,
    pub bytes: Vec<u8>,
}
enum Prepared {
    Dds(eroverlay_renderer::PreparedTexture),
    Rgba(Pixels),
}
impl Prepared {
    fn width(&self) -> u32 {
        match self {
            Self::Dds(t) => t.width,
            Self::Rgba(p) => p.width,
        }
    }
    fn height(&self) -> u32 {
        match self {
            Self::Dds(t) => t.height,
            Self::Rgba(p) => p.height,
        }
    }
    fn byte_len(&self) -> usize {
        match self {
            Self::Dds(t) => t.byte_len(),
            Self::Rgba(p) => p.bytes.len(),
        }
    }
}
enum Source {
    Dds(Vec<u8>),
    Rgba(u32, u32, Vec<u8>),
}
struct Job {
    id: u64,
    source: Source,
    // Drops after source, including when queue submission or publication fails.
    _reservation: Arc<Reservation>,
}
struct Record {
    view: TextureView,
    pixels: Option<Prepared>,
    // Field order drops pixel storage before refunding the reservation.
    reservation: Option<Arc<Reservation>>,
}
impl Record {
    fn fail(&mut self) {
        self.view.status = TEXTURE_FAILED;
        self.pixels = None;
        self.reservation = None;
    }
    fn ready(&mut self, bytes: u64) {
        self.view.status = TEXTURE_READY;
        self.view.memory_bytes = bytes;
        self.reservation = None;
    }
}
struct Registry {
    records: HashMap<u64, Record>,
    pending: VecDeque<u64>,
    uploading: VecDeque<u64>,
    last_synchronize: Option<u64>,
}
impl Registry {
    fn reset(&mut self, mut retire: impl FnMut(u64)) {
        for (&id, record) in &mut self.records {
            // Renderer-owned source bytes must be gone before refunding leases.
            retire(id);
            record.fail();
        }
        self.pending.clear();
        self.uploading.clear();
        self.last_synchronize = None;
    }
    fn begin_transfer(&mut self, sequence: u64) -> bool {
        if self.last_synchronize == Some(sequence) {
            return false;
        }
        self.last_synchronize = Some(sequence);
        true
    }
    fn discard_retired_pending(&mut self) {
        self.pending.retain(|id| self.records.contains_key(id));
    }
}
struct Shared {
    registry: Mutex<Registry>,
    queued_bytes: Arc<AtomicU64>,
    stopping: AtomicBool,
}
impl Shared {
    fn new() -> Self {
        Self {
            registry: Mutex::new(Registry {
                records: HashMap::with_capacity(MAX_TEXTURES),
                pending: VecDeque::with_capacity(MAX_TEXTURES),
                uploading: VecDeque::with_capacity(MAX_TEXTURES),
                last_synchronize: None,
            }),
            queued_bytes: Arc::new(AtomicU64::new(0)),
            stopping: AtomicBool::new(false),
        }
    }
}
pub struct Textures {
    shared: Arc<Shared>,
    sender: Option<SyncSender<Job>>,
    retire: SyncSender<u64>,
    retired: Mutex<Receiver<u64>>,
    worker: Option<JoinHandle<()>>,
    next: AtomicU64,
    dds_requests: AtomicU64,
    rgba_requests: AtomicU64,
    accepted: AtomicU64,
    poll_busy: AtomicU64,
    synchronize_busy: AtomicU64,
}
pub struct Diagnostics {
    pub preparing: usize,
    pub prepared: usize,
    pub uploading: usize,
    pub ready: usize,
    pub failed: usize,
    pub source_bytes: u64,
    pub dds_requests: u64,
    pub rgba_requests: u64,
    pub rejected: u64,
    pub poll_busy: u64,
    pub synchronize_busy: u64,
}
impl std::fmt::Debug for Diagnostics {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        f.debug_struct("TextureQueues")
            .field("preparing", &self.preparing)
            .field("prepared", &self.prepared)
            .field("uploading", &self.uploading)
            .field("ready", &self.ready)
            .field("failed", &self.failed)
            .field("source_bytes", &self.source_bytes)
            .field("dds_requests", &self.dds_requests)
            .field("rgba_requests", &self.rgba_requests)
            .field("rejected", &self.rejected)
            .field("poll_busy", &self.poll_busy)
            .field("synchronize_busy", &self.synchronize_busy)
            .finish()
    }
}

impl Textures {
    pub fn new() -> Self {
        let (sender, receiver) = mpsc::sync_channel(16);
        let (retire, retired) = mpsc::sync_channel(2 * MAX_TEXTURES);
        let shared = Arc::new(Shared::new());
        let owned = shared.clone();
        let worker = thread::Builder::new()
            .name("EROverlay texture preparation".into())
            .spawn(move || prepare(owned, receiver))
            .expect("texture worker");
        Self {
            shared,
            sender: Some(sender),
            retire,
            retired: Mutex::new(retired),
            worker: Some(worker),
            next: AtomicU64::new(1),
            dds_requests: AtomicU64::new(0),
            rgba_requests: AtomicU64::new(0),
            accepted: AtomicU64::new(0),
            poll_busy: AtomicU64::new(0),
            synchronize_busy: AtomicU64::new(0),
        }
    }
    fn insert(&self, reservation: Arc<Reservation>) -> u64 {
        let Ok(mut registry) = self.shared.registry.try_lock() else {
            return 0;
        };
        if registry.records.len() >= MAX_TEXTURES {
            return 0;
        }
        let id = self.next.fetch_add(1, Ordering::Relaxed);
        let view = TextureView {
            status: TEXTURE_PENDING,
            width: 0,
            height: 0,
            memory_bytes: 0,
            reserved: 0,
        };
        registry.records.insert(
            id,
            Record {
                view,
                pixels: None,
                reservation: Some(reservation),
            },
        );
        id
    }
    pub fn dds(&self, bytes: &[u8]) -> u64 {
        self.dds_requests.fetch_add(1, Ordering::Relaxed);
        if bytes.is_empty() || bytes.len() > MAX_TEXTURE_BYTES {
            return 0;
        }
        let Some(reservation) = Reservation::acquire(&self.shared.queued_bytes, bytes.len()) else {
            return 0;
        };
        let id = self.insert(reservation.clone());
        if id == 0 {
            return 0;
        }
        let job = Job {
            id,
            source: Source::Dds(bytes.to_vec()),
            _reservation: reservation,
        };
        if self.sender.as_ref().unwrap().try_send(job).is_err() {
            // Creation may originate from an egui atlas update on render.
            // Reuse the bounded retirement queue rather than waiting on the
            // preparation worker's registry publication lock.
            self.release(id);
            return 0;
        }
        self.accepted.fetch_add(1, Ordering::Relaxed);
        id
    }
    pub fn rgba(&self, width: u32, height: u32, bytes: &[u8]) -> u64 {
        self.rgba_requests.fetch_add(1, Ordering::Relaxed);
        let size = (width as usize)
            .checked_mul(height as usize)
            .and_then(|n| n.checked_mul(4));
        if width == 0
            || height == 0
            || width > 16384
            || height > 16384
            || size != Some(bytes.len())
            || bytes.len() > MAX_TEXTURE_BYTES
        {
            return 0;
        }
        let Some(reservation) = Reservation::acquire(&self.shared.queued_bytes, bytes.len()) else {
            return 0;
        };
        let id = self.insert(reservation.clone());
        if id == 0 {
            return 0;
        }
        if self
            .sender
            .as_ref()
            .unwrap()
            .try_send(Job {
                id,
                source: Source::Rgba(width, height, bytes.to_vec()),
                _reservation: reservation,
            })
            .is_err()
        {
            self.release(id);
            return 0;
        }
        self.accepted.fetch_add(1, Ordering::Relaxed);
        id
    }
    pub fn poll(&self, id: u64) -> TextureView {
        let Ok(registry) = self.shared.registry.try_lock() else {
            self.poll_busy.fetch_add(1, Ordering::Relaxed);
            return TextureView {
                status: TEXTURE_PENDING,
                ..Default::default()
            };
        };
        registry.records.get(&id).map_or(
            TextureView {
                status: TEXTURE_FAILED,
                ..Default::default()
            },
            |r| r.view,
        )
    }
    pub fn release(&self, id: u64) {
        if id != 0 {
            let _ = self.retire.try_send(id);
        }
    }
    pub fn synchronize(&self, renderer: &mut eroverlay_renderer::Renderer, sequence: u64) {
        let Ok(mut registry) = self.shared.registry.try_lock() else {
            self.synchronize_busy.fetch_add(1, Ordering::Relaxed);
            return;
        };
        let mut removed = false;
        if let Ok(retired) = self.retired.try_lock() {
            for _ in 0..2 * MAX_TEXTURES {
                let Ok(id) = retired.try_recv() else {
                    break;
                };
                if let Some(record) = registry.records.remove(&id) {
                    renderer.retire_texture(id);
                    drop(record);
                    removed = true;
                }
            }
        }
        if removed {
            // A stream of created/released textures must not accumulate stale
            // IDs faster than the eight-entry transfer scan consumes them.
            registry.discard_retired_pending();
        }
        // Only pending/in-flight IDs are visited; an idle frame never scans the
        // resident texture table. Released IDs are lazily discarded from queues.
        let count = registry.uploading.len();
        for _ in 0..count {
            let id = registry.uploading.pop_front().unwrap();
            if let Some(r) = registry.records.get_mut(&id) {
                if renderer.texture_ready(id) {
                    r.ready(renderer.texture_bytes(id));
                } else {
                    registry.uploading.push_back(id);
                }
            }
        }
        // Below/above UI callbacks and multiple plugins share one transfer
        // allowance. Retirement/readiness checks above remain available to both.
        if !registry.begin_transfer(sequence) {
            return;
        }
        let mut budget: usize = 4 * 1024 * 1024;
        let mut uploads = 0;
        let count = registry.pending.len();
        for _ in 0..count.min(8) {
            let id = registry.pending.pop_front().unwrap();
            let Some(record) = registry.records.get_mut(&id) else {
                continue;
            };
            let Some(p) = record.pixels.as_ref() else {
                continue;
            };
            if uploads != 0 && (p.byte_len() > budget || uploads >= 4) {
                registry.pending.push_front(id);
                break;
            }
            let p = record.pixels.take().unwrap();
            let len = p.byte_len();
            let accepted = match p {
                Prepared::Dds(texture) => renderer.upload_prepared(id, texture),
                Prepared::Rgba(p) => renderer.upload_rgba_owned(id, p.width, p.height, p.bytes),
            };
            if accepted {
                registry.uploading.push_back(id);
                budget = budget.saturating_sub(len);
                uploads += 1;
            } else {
                record.fail();
            }
        }
    }
    pub fn reset(&self, renderer: &mut eroverlay_renderer::Renderer) -> bool {
        // Called under the exclusive callback gate during renderer recreation.
        let Ok(mut registry) = self.shared.registry.try_lock() else {
            return false;
        };
        registry.reset(|id| renderer.retire_texture(id));
        true
    }
    /// Low-frequency update-worker snapshot only. Never waits for preparation
    /// or walks the resident table from a production render callback.
    pub fn diagnostics(&self) -> Option<Diagnostics> {
        let registry = self.shared.registry.try_lock().ok()?;
        let ready = registry
            .records
            .values()
            .filter(|r| r.view.status == TEXTURE_READY)
            .count();
        let failed = registry
            .records
            .values()
            .filter(|r| r.view.status == TEXTURE_FAILED)
            .count();
        let prepared = registry.pending.len();
        let uploading = registry.uploading.len();
        let dds_requests = self.dds_requests.load(Ordering::Relaxed);
        let rgba_requests = self.rgba_requests.load(Ordering::Relaxed);
        Some(Diagnostics {
            preparing: registry
                .records
                .len()
                .saturating_sub(ready + failed + prepared + uploading),
            prepared,
            uploading,
            ready,
            failed,
            source_bytes: self.shared.queued_bytes.load(Ordering::Relaxed),
            dds_requests,
            rgba_requests,
            rejected: (dds_requests + rgba_requests)
                .saturating_sub(self.accepted.load(Ordering::Relaxed)),
            poll_busy: self.poll_busy.load(Ordering::Relaxed),
            synchronize_busy: self.synchronize_busy.load(Ordering::Relaxed),
        })
    }
}
fn prepare(shared: Arc<Shared>, receiver: Receiver<Job>) {
    while let Ok(job) = receiver.recv() {
        // Bind the lease first so it drops last, even when stopping leaves the
        // source unconsumed or cancellation discards an already prepared image.
        let _reservation = job._reservation;
        let source = job.source;
        let id = job.id;
        let decoded = if shared.stopping.load(Ordering::Acquire) {
            None
        } else {
            match source {
                Source::Dds(bytes) => eroverlay_renderer::PreparedTexture::from_dds(bytes)
                    .ok()
                    .map(Prepared::Dds),
                Source::Rgba(width, height, mut bytes) => {
                    for pixel in bytes.as_chunks_mut::<4>().0 {
                        let alpha = u32::from(pixel[3]);
                        for channel in &mut pixel[..3] {
                            *channel = ((u32::from(*channel) * alpha + 127) / 255) as u8;
                        }
                    }
                    Some(Prepared::Rgba(Pixels {
                        width,
                        height,
                        bytes,
                    }))
                }
            }
        };
        let mut registry = shared.registry.lock().unwrap_or_else(|e| e.into_inner());
        let Some(record) = registry.records.get_mut(&id) else {
            continue;
        };
        // A reset/retire during preparation cancels publication. The job retains
        // its own lease until its source/decoded payload has actually been freed.
        if record.view.status != TEXTURE_PENDING || record.reservation.is_none() {
            continue;
        }
        if let Some(prepared) = decoded {
            let len = prepared.byte_len();
            record.view.width = prepared.width();
            record.view.height = prepared.height();
            record.view.memory_bytes = len as u64;
            record.pixels = Some(prepared);
            registry.pending.push_back(id);
        } else {
            record.fail();
        }
    }
}
impl Drop for Textures {
    fn drop(&mut self) {
        self.shared.stopping.store(true, Ordering::Release);
        self.sender.take();
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn paused_preparer() -> (Textures, Receiver<Job>) {
        let (sender, receiver) = mpsc::sync_channel(16);
        let (retire, retired) = mpsc::sync_channel(2 * MAX_TEXTURES);
        (
            Textures {
                shared: Arc::new(Shared::new()),
                sender: Some(sender),
                retire,
                retired: Mutex::new(retired),
                worker: None,
                next: AtomicU64::new(1),
                dds_requests: AtomicU64::new(0),
                rgba_requests: AtomicU64::new(0),
                accepted: AtomicU64::new(0),
                poll_busy: AtomicU64::new(0),
                synchronize_busy: AtomicU64::new(0),
            },
            receiver,
        )
    }
    #[test]
    fn aggregate_reservation_refunds_only_after_last_owner() {
        let counter = Arc::new(AtomicU64::new(0));
        let first = Reservation::acquire(&counter, MAX_TEXTURE_BYTES).unwrap();
        let job = first.clone();
        let second = Reservation::acquire(&counter, MAX_TEXTURE_BYTES).unwrap();
        assert_eq!(counter.load(Ordering::Acquire), MAX_QUEUED_BYTES);
        assert!(Reservation::acquire(&counter, 1).is_none());
        drop(first);
        assert_eq!(counter.load(Ordering::Acquire), MAX_QUEUED_BYTES);
        drop(second);
        assert_eq!(counter.load(Ordering::Acquire), MAX_TEXTURE_BYTES as u64);
        drop(job);
        assert_eq!(counter.load(Ordering::Acquire), 0);
    }
    #[test]
    fn prepared_and_renderer_owned_bytes_stay_reserved_until_ready() {
        let (mut t, receiver) = paused_preparer();
        let id = t.rgba(1, 1, &[255; 4]);
        assert_ne!(id, 0);
        t.sender.take();
        prepare(t.shared.clone(), receiver);
        assert_eq!(t.shared.queued_bytes.load(Ordering::Acquire), 4);
        // Preparation did not refund its input budget: fill the rest without
        // allocating large test images, then exercise the real creation path.
        let rest =
            Reservation::acquire(&t.shared.queued_bytes, MAX_QUEUED_BYTES as usize - 4).unwrap();
        assert_eq!(t.rgba(1, 1, &[255; 4]), 0);
        drop(rest);
        let mut registry = t.shared.registry.lock().unwrap();
        let record = registry.records.get_mut(&id).unwrap();
        let renderer_owned = record.pixels.take().unwrap();
        assert_eq!(t.shared.queued_bytes.load(Ordering::Acquire), 4);
        drop(renderer_owned); // Upload copied and discarded the source payload.
        record.ready(64 * 1024);
        assert_eq!(record.view.status, TEXTURE_READY);
        assert_eq!(record.view.memory_bytes, 64 * 1024);
        assert_eq!(t.shared.queued_bytes.load(Ordering::Acquire), 0);
    }
    #[test]
    fn retirement_and_reset_cannot_refund_or_resurrect_inflight_jobs() {
        for reset in [false, true] {
            let (mut t, receiver) = paused_preparer();
            let id = t.rgba(1, 1, &[255; 4]);
            assert_ne!(id, 0);
            t.sender.take();
            {
                let mut registry = t.shared.registry.lock().unwrap();
                if reset {
                    registry.pending.push_back(id);
                    registry.uploading.push_back(id);
                    assert!(registry.begin_transfer(10));
                    registry.reset(|retiring| assert_eq!(retiring, id));
                    assert!(registry.pending.is_empty());
                    assert!(registry.uploading.is_empty());
                    assert!(registry.last_synchronize.is_none());
                } else {
                    registry.records.remove(&id);
                }
            }
            // The worker's queued payload still owns its half of the lease.
            assert_eq!(t.shared.queued_bytes.load(Ordering::Acquire), 4);
            prepare(t.shared.clone(), receiver);
            assert_eq!(t.shared.queued_bytes.load(Ordering::Acquire), 0);
            let registry = t.shared.registry.lock().unwrap();
            assert!(registry.pending.is_empty());
            if reset {
                assert_eq!(registry.records[&id].view.status, TEXTURE_FAILED);
                assert!(registry.records[&id].pixels.is_none());
            } else {
                assert!(registry.records.is_empty());
            }
        }
    }
    #[test]
    fn failed_queue_submission_refunds_after_deferred_retirement() {
        let (t, receiver) = paused_preparer();
        drop(receiver);
        assert_eq!(t.rgba(1, 1, &[255; 4]), 0);
        assert_eq!(t.shared.queued_bytes.load(Ordering::Acquire), 4);
        let id = t.retired.lock().unwrap().try_recv().unwrap();
        t.shared.registry.lock().unwrap().records.remove(&id);
        assert_eq!(t.shared.queued_bytes.load(Ordering::Acquire), 0);
    }
    #[test]
    fn reset_retires_prepared_storage_before_refunding_its_lease() {
        let (mut t, receiver) = paused_preparer();
        let id = t.rgba(1, 1, &[255; 4]);
        t.sender.take();
        prepare(t.shared.clone(), receiver);
        let mut registry = t.shared.registry.lock().unwrap();
        registry.reset(|retiring| {
            assert_eq!(retiring, id);
            assert_eq!(t.shared.queued_bytes.load(Ordering::Acquire), 4);
        });
        assert!(registry.records[&id].pixels.is_none());
        assert_eq!(registry.records[&id].view.status, TEXTURE_FAILED);
        assert_eq!(t.shared.queued_bytes.load(Ordering::Acquire), 0);
    }
    #[test]
    fn retired_ids_do_not_grow_the_pending_work_queue() {
        let shared = Shared::new();
        let mut registry = shared.registry.lock().unwrap();
        for _ in 0..100 {
            registry.pending.extend(1..=MAX_TEXTURES as u64);
            registry.discard_retired_pending();
            assert!(registry.pending.is_empty());
        }
    }
    #[test]
    fn two_native_phases_share_one_pending_transfer_allowance() {
        let shared = Shared::new();
        let mut registry = shared.registry.lock().unwrap();
        assert!(registry.begin_transfer(1));
        assert!(!registry.begin_transfer(1));
        assert!(registry.begin_transfer(2));
        assert!(!registry.begin_transfer(2));
    }
    #[test]
    fn invalid_shapes_fail_and_budget_is_bounded() {
        let t = Textures::new();
        assert_eq!(t.rgba(u32::MAX, 4, &[0; 4]), 0);
        assert_eq!(t.rgba(1, 1, &[0; 3]), 0);
        let id = t.rgba(1, 1, &[255, 128, 64, 128]);
        assert_ne!(id, 0);
        for _ in 0..100 {
            if t.poll(id).width == 1 {
                break;
            }
            std::thread::sleep(std::time::Duration::from_millis(1));
        }
        assert_eq!(t.poll(id).width, 1);
        let registry = t.shared.registry.lock().unwrap();
        let Some(Prepared::Rgba(pixels)) = registry.records[&id].pixels.as_ref() else {
            panic!("missing prepared RGBA");
        };
        assert_eq!(pixels.bytes, [128, 64, 32, 128]);
        assert_eq!(t.shared.queued_bytes.load(Ordering::Acquire), 4);
        drop(registry);
        assert_eq!(t.poll(u64::MAX).status, TEXTURE_FAILED);
        t.release(id);
    }
    #[test]
    fn dds_preparation_keeps_compressed_payload_and_mips() {
        let mut dds = vec![0u8; 136];
        dds[..4].copy_from_slice(b"DDS ");
        for (offset, value) in [
            (4, 124u32),
            (12, 4),
            (16, 4),
            (28, 1),
            (76, 32),
            (84, 0x31545844),
        ] {
            dds[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        }
        let t = Textures::new();
        let id = t.dds(&dds);
        assert_ne!(id, 0);
        for _ in 0..100 {
            if t.poll(id).width == 4 {
                break;
            }
            std::thread::sleep(std::time::Duration::from_millis(1));
        }
        let registry = t.shared.registry.lock().unwrap();
        let Some(Prepared::Dds(prepared)) = registry.records[&id].pixels.as_ref() else {
            panic!("DDS was not preserved");
        };
        assert_eq!(prepared.byte_len(), 136);
        assert_eq!(prepared.mip_count(), 1);
        assert_eq!(t.shared.queued_bytes.load(Ordering::Acquire), 136);
        let counter = t.shared.queued_bytes.clone();
        drop(registry);
        drop(t);
        assert_eq!(counter.load(Ordering::Acquire), 0);
    }
    #[test]
    fn diagnostics_distinguish_preparation_failure_and_polling_without_new_uploads() {
        let (mut textures, receiver) = paused_preparer();
        let good = textures.rgba(1, 1, &[255; 4]);
        let bad = textures.dds(b"not a DDS");
        assert_ne!(good, 0);
        assert_ne!(bad, 0);
        assert_eq!(textures.rgba(1, 1, &[255; 3]), 0);
        let queued = textures.diagnostics().unwrap();
        assert_eq!(
            (queued.preparing, queued.prepared, queued.uploading),
            (2, 0, 0)
        );
        assert_eq!(
            (queued.dds_requests, queued.rgba_requests, queued.rejected),
            (1, 2, 1)
        );
        textures.sender.take();
        prepare(textures.shared.clone(), receiver);
        for _ in 0..1000 {
            assert_eq!(textures.poll(good).status, TEXTURE_PENDING);
        }
        let prepared = textures.diagnostics().unwrap();
        assert_eq!(
            (prepared.preparing, prepared.prepared, prepared.failed),
            (0, 1, 1)
        );
        assert_eq!(prepared.source_bytes, 4);
        assert_eq!((prepared.dds_requests, prepared.rgba_requests), (1, 2));
        assert_eq!(textures.poll(bad).status, TEXTURE_FAILED);
    }
}
