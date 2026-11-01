//! Update-thread game services. Rendering consumes copied POD snapshots only.
#![deny(unsafe_op_in_unsafe_fn)]
pub mod assets;
mod files;
mod flags;
mod language;
mod map;
pub mod memory;
pub mod params;
pub mod scan;
pub use eroverlay_abi::{DisplayState, GameLayout, MapState};
pub use files::{FileDiagnostics, FilePoll};
use flags::EventFlagCache;
pub use flags::{FlagDiagnostics, FlagReason};
pub use language::game_language;
use memory::{pointer, read};
use std::time::{Duration, Instant};

#[repr(C)]
#[derive(Default, Clone, Copy, Debug)]
pub struct Bindings {
    pub submit: u32,
    pub flush: u32,
    pub enqueue: u32,
    pub cancel: u32,
    pub manager: u32,
    pub allocator: u32,
    pub retire_pool_owner: u32,
    pub menu: u32,
    pub game_data: u32,
    pub event_flags: u32,
    pub field: u32,
    pub repository: u32,
    pub reveal: u32,
    pub layout: GameLayout,
}
/// Resolve native game contracts from a safely owned, mapped PE image.
pub fn scan_image(image: &[u8], base: usize) -> Result<Bindings, String> {
    scan::resolve(image, base)
}
pub struct GameServices {
    base: usize,
    bindings: Option<Bindings>,
    files: files::GameFiles,
    flags: EventFlagCache,
    state: Option<MapState>,
    display: DisplayState,
    generation: u64,
    last_view: usize,
    last_player: usize,
    last_flags: usize,
    last_table: usize,
    last_reveal: u8,
    masks: [u32; 3],
    mask_refresh: Option<Instant>,
    mask_valid: bool,
    mask_failure: SnapshotReason,
    diagnostic: GameDiagnostics,
}
/// Stable diagnostic stages, copied under the existing update-thread lock.
/// Keys contain no game-owned strings and logging need not run every update.
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum SnapshotReason {
    #[default]
    NoBindings,
    DisplayUnavailable,
    LoadingOrLogo,
    FullScreenMap,
    MapContext,
    WorldMap,
    Reveal,
    ParamUnavailable,
    ParamFormat,
    ParamRow,
    FlagManager,
    FlagRead,
    ContextChanged,
    Ready,
}
impl SnapshotReason {
    pub fn key(self) -> &'static str {
        match self {
            Self::NoBindings => "no_bindings",
            Self::DisplayUnavailable => "display_unavailable",
            Self::LoadingOrLogo => "loading_or_logo",
            Self::FullScreenMap => "full_screen_map",
            Self::MapContext => "map_context",
            Self::WorldMap => "world_map",
            Self::Reveal => "reveal",
            Self::ParamUnavailable => "param_unavailable",
            Self::ParamFormat => "param_format",
            Self::ParamRow => "param_row",
            Self::FlagManager => "flag_manager",
            Self::FlagRead => "flag_read",
            Self::ContextChanged => "context_changed",
            Self::Ready => "ready",
        }
    }
}
#[derive(Clone, Copy, Debug, Default)]
pub struct GameDiagnostics {
    pub reason: SnapshotReason,
    pub updates: u64,
    pub ready_updates: u64,
    pub mask_attempts: u64,
    pub mask_failures: u64,
    pub mask_backoffs: u64,
    pub backing_off: bool,
    pub param_refreshes: u64,
    pub param_rows: u32,
    pub mask_rows: u32,
    pub last_failed_flag: u32,
    pub view: usize,
    pub player: usize,
    pub piece_table: usize,
    pub flag_manager: usize,
    pub flags: FlagDiagnostics,
    pub map_valid: bool,
    pub display_valid: bool,
    pub loading_or_logo: bool,
    pub full_screen_map: bool,
    pub last_update_us: u64,
    pub last_masks_us: u64,
}
/// Immutable address/layout copy for render-time display checks. The executable
/// is process-owned and remains mapped for the lifetime of every callback.
#[derive(Clone, Copy)]
pub struct DisplayReader {
    base: usize,
    bindings: Option<Bindings>,
}
impl DisplayReader {
    pub fn read(&self) -> DisplayState {
        self.bindings.map_or(
            DisplayState {
                valid: 0,
                loading_or_logo: 1,
                full_screen_map: 0,
            },
            |b| map::display(self.base, &b),
        )
    }
}
impl GameServices {
    pub fn discover() -> Result<Self, String> {
        let base = memory::main_module();
        let image = memory::image_snapshot(base)?;
        let bindings = scan_image(&image, base)?;
        Ok(Self::with_bindings(base, Some(bindings)))
    }
    pub fn offline() -> Self {
        Self::with_bindings(0, None)
    }
    fn with_bindings(base: usize, bindings: Option<Bindings>) -> Self {
        Self {
            base,
            bindings,
            files: files::GameFiles::new(base, bindings),
            flags: EventFlagCache::default(),
            state: None,
            display: DisplayState {
                valid: 0,
                loading_or_logo: 1,
                full_screen_map: 0,
            },
            generation: 1,
            last_view: 0,
            last_player: 0,
            last_flags: 0,
            last_table: 0,
            last_reveal: 0,
            masks: [0; 3],
            mask_refresh: None,
            mask_valid: false,
            mask_failure: SnapshotReason::ParamUnavailable,
            diagnostic: GameDiagnostics::default(),
        }
    }
    pub fn compatible(&self) -> bool {
        self.bindings.is_some()
    }
    pub fn game_layout(&self) -> Option<GameLayout> {
        self.bindings.map(|b| b.layout)
    }
    pub fn read_display_state(&self) -> DisplayState {
        self.display_reader().read()
    }
    pub fn display_reader(&self) -> DisplayReader {
        DisplayReader {
            base: self.base,
            bindings: self.bindings,
        }
    }
    pub fn display_state(&self) -> DisplayState {
        self.display
    }
    pub fn diagnostics(&self) -> GameDiagnostics {
        let mut d = self.diagnostic;
        d.flags = self.flags.diagnostics();
        d.map_valid = self.state.is_some();
        d.display_valid = self.display.valid != 0;
        d.loading_or_logo = self.display.loading_or_logo != 0;
        d.full_screen_map = self.display.full_screen_map != 0;
        d
    }
    pub fn file_diagnostics(&self) -> FileDiagnostics {
        self.files.diagnostics()
    }
    pub fn map_state(&self) -> Option<MapState> {
        self.state
    }
    /// Fresh bounded location/context read for consumers that recheck after
    /// reading game-owned collections. Masks/generation belong to the latest
    /// update snapshot; a different live owner/player fails until that refresh.
    /// This does no discovery, flag traversal, PARAM parsing or allocation.
    pub fn read_map_state(&self) -> Option<MapState> {
        let cached = self.state?;
        let b = self.bindings?;
        let c = map::context(self.base, &b)?;
        if c.view != self.last_view || c.player != self.last_player {
            return None;
        }
        let mut state = map::world_map(c.view)?;
        state.generation = cached.generation;
        state.active_masks = cached.active_masks;
        self.read_camera(&b, &mut state);
        let display = self.read_display_state();
        if display.valid == 0
            || display.loading_or_logo != 0
            || display.full_screen_map != 0
            || pointer(self.base + b.menu as usize) != c.menu
            || pointer(self.base + b.game_data as usize) != c.game_data
            || !map::unchanged(&c, &b.layout)
        {
            return None;
        }
        Some(state)
    }
    pub fn find_param_table(&self, group: u32) -> usize {
        let Some(b) = self.bindings else {
            return 0;
        };
        if ![43, 87, 88, 141].contains(&group) || group >= b.layout.repository_groups {
            return 0;
        }
        let repository = pointer(self.base + b.repository as usize);
        let entry = if repository != 0 {
            pointer(repository + 0x88 + group as usize * 72)
        } else {
            0
        };
        let cap = if entry != 0 { pointer(entry + 0x80) } else { 0 };
        if cap != 0 { pointer(cap + 0x80) } else { 0 }
    }
    pub fn event_flag(&mut self, id: u32) -> bool {
        // Keep the public GameFiles::readEventFlag sentinel behavior. The
        // internal active-mask reader otherwise handles all u32 IDs normally.
        if id == u32::MAX {
            return false;
        }
        let Some(b) = self.bindings else {
            return false;
        };
        self.flags
            .read(pointer(self.base + b.event_flags as usize), id)
            .unwrap_or(false)
    }
    pub fn request_file(
        &mut self,
        path: &str,
        part: Option<&str>,
        flags: u32,
        max_bytes: usize,
    ) -> u64 {
        self.files.request(path, part, flags, max_bytes)
    }
    pub fn poll_file(&self, token: u64) -> FilePoll<'_> {
        self.files.poll(token)
    }
    pub fn release_file(&mut self, token: u64) {
        self.files.release(token);
    }
    pub fn stop(&mut self) -> bool {
        self.files.stop()
    }
    pub fn update_snapshot(&mut self) {
        let start = Instant::now();
        self.diagnostic.updates = self.diagnostic.updates.wrapping_add(1);
        self.diagnostic.backing_off = false;
        self.update_snapshot_inner();
        self.diagnostic.last_update_us = start.elapsed().as_micros().min(u64::MAX as u128) as u64;
    }
    fn update_snapshot_inner(&mut self) {
        self.files.pump();
        self.display = self.read_display_state();
        self.state = None;
        let Some(b) = self.bindings else {
            self.diagnostic.reason = SnapshotReason::NoBindings;
            return;
        };
        let context = map::context(self.base, &b);
        let (view, player) = context.map_or((0, 0), |c| (c.view, c.player));
        if view != self.last_view || player != self.last_player {
            self.generation = self.generation.wrapping_add(1);
            self.last_view = view;
            self.last_player = player;
            self.mask_refresh = None;
            self.mask_valid = false;
            self.flags.reset();
        }
        self.diagnostic.view = view;
        self.diagnostic.player = player;
        let Some(c) = context else {
            self.diagnostic.reason = SnapshotReason::MapContext;
            return;
        };
        if self.display.valid == 0 {
            self.diagnostic.reason = SnapshotReason::DisplayUnavailable;
            return;
        }
        if self.display.loading_or_logo != 0 {
            self.diagnostic.reason = SnapshotReason::LoadingOrLogo;
            return;
        }
        if self.display.full_screen_map != 0 {
            self.diagnostic.reason = SnapshotReason::FullScreenMap;
            return;
        }
        let Some(mut state) = map::world_map(c.view) else {
            self.diagnostic.reason = SnapshotReason::WorldMap;
            return;
        };
        let Some(reveal) = read::<u8>(self.base + b.reveal as usize) else {
            self.diagnostic.reason = SnapshotReason::Reveal;
            return;
        };
        let flags = pointer(self.base + b.event_flags as usize);
        let table = self.find_param_table(88);
        self.diagnostic.flag_manager = flags;
        self.diagnostic.piece_table = table;
        let now = Instant::now();
        if self.mask_refresh.is_none_or(|t| now >= t)
            || flags != self.last_flags
            || table != self.last_table
            || reveal != self.last_reveal
        {
            self.diagnostic.mask_attempts = self.diagnostic.mask_attempts.wrapping_add(1);
            let mask_start = Instant::now();
            let result = self.read_masks(table, flags, reveal != 0);
            self.diagnostic.last_masks_us =
                mask_start.elapsed().as_micros().min(u64::MAX as u128) as u64;
            // Record attempted sources on failure too. Otherwise the changed
            // source condition defeats this 200ms retry deadline every update.
            self.last_flags = flags;
            self.last_table = table;
            self.last_reveal = reveal;
            self.mask_refresh = Some(now + Duration::from_millis(200));
            match result {
                Ok(masks) => {
                    self.masks = masks;
                    self.mask_valid = true;
                }
                Err(reason) => {
                    self.mask_valid = false;
                    self.mask_failure = reason;
                    self.diagnostic.reason = reason;
                    self.diagnostic.mask_failures = self.diagnostic.mask_failures.wrapping_add(1);
                    return;
                }
            }
        }
        if !self.mask_valid {
            self.diagnostic.reason = self.mask_failure;
            self.diagnostic.backing_off = true;
            self.diagnostic.mask_backoffs = self.diagnostic.mask_backoffs.wrapping_add(1);
            return;
        }
        if !map::unchanged(&c, &b.layout) {
            self.diagnostic.reason = SnapshotReason::ContextChanged;
            return;
        }
        state.generation = self.generation;
        state.active_masks = self.masks;
        self.read_camera(&b, &mut state);
        self.state = Some(state);
        self.diagnostic.reason = SnapshotReason::Ready;
        self.diagnostic.ready_updates = self.diagnostic.ready_updates.wrapping_add(1);
    }
    fn read_camera(&self, b: &Bindings, state: &mut MapState) {
        let field = pointer(self.base + b.field as usize);
        let camera = if field != 0 { pointer(field + 0x20) } else { 0 };
        let camera = if camera != 0 {
            pointer(camera + 0x18)
        } else {
            0
        };
        if camera != 0
            && let (Some(cos), Some(sin)) = (read::<f32>(camera + 0x10), read::<f32>(camera + 0x18))
            && cos.is_finite()
            && sin.is_finite()
        {
            state.camera_yaw_cos = cos;
            state.camera_yaw_sin = sin;
        }
    }
    fn read_masks(
        &mut self,
        table: usize,
        manager: usize,
        reveal: bool,
    ) -> Result<[u32; 3], SnapshotReason> {
        use SnapshotReason::*;
        self.diagnostic.param_rows = 0;
        self.diagnostic.mask_rows = 0;
        if reveal {
            return Ok([u32::MAX; 3]);
        }
        if manager == 0 {
            return Err(FlagManager);
        }
        if table == 0 {
            return Err(ParamUnavailable);
        }
        self.diagnostic.param_refreshes = self.diagnostic.param_refreshes.wrapping_add(1);
        let rows = params::param_rows(table).ok_or(ParamFormat)?;
        self.diagnostic.param_rows = rows.len() as u32;
        if rows.len() > 8192 {
            return Err(ParamFormat);
        }
        let mut masks = [0u32; 3];
        for row in rows {
            let map = row.id / 100;
            let bit = row.id % 100;
            if bit >= 32 || ![0, 1, 10].contains(&map) {
                continue;
            }
            self.diagnostic.mask_rows += 1;
            let event = row.field::<i32>(4).ok_or(ParamRow)?;
            let id = if event == -1 { 0 } else { event as u32 };
            let Some(on) = self.flags.read(manager, id) else {
                self.diagnostic.last_failed_flag = id;
                return Err(FlagRead);
            };
            if on {
                masks[if map == 10 { 2 } else { map as usize }] |= 1 << bit;
            }
        }
        Ok(masks)
    }
}

#[cfg(test)]
mod tests {
    use super::runtime_perf::NativeMemory;
    use super::*;
    #[test]
    fn production_snapshot_uses_native_repository_param_and_flag_layouts() {
        let f = NativeMemory::new();
        let mut service = f.service();
        assert_eq!(
            service.find_param_table(88),
            f.bytes.as_ptr() as usize + f.table
        );
        service.update_snapshot();
        assert_eq!(service.map_state().unwrap().active_masks, [1, 0, 1]);
    }
    #[test]
    fn production_missing_param_backs_off_and_pointer_publication_recovers_immediately() {
        let mut f = NativeMemory::new();
        f.bytes[0x38..0x40].fill(0);
        let mut s = f.service();
        for _ in 0..60 {
            s.update_snapshot();
            assert!(s.map_state().is_none());
            assert!(s.read_map_state().is_none());
        }
        let d = s.diagnostics();
        assert_eq!(d.reason, SnapshotReason::ParamUnavailable);
        assert_eq!(
            (d.mask_attempts, d.mask_failures, d.mask_backoffs),
            (1, 1, 59)
        );
        f.ptr(0x38, 0x9000);
        s.update_snapshot();
        assert_eq!(s.read_map_state().unwrap().active_masks, [1, 0, 1]);
        assert_eq!(s.diagnostics().mask_attempts, 2);
        assert_eq!(s.diagnostics().reason, SnapshotReason::Ready);
    }
    #[test]
    fn production_invalid_flag_divisor_waits_and_recovers_at_retry_without_bypassing_gate() {
        let mut f = NativeMemory::new();
        f.word(0xf001c, 0);
        let mut s = f.service();
        for _ in 0..60 {
            s.update_snapshot();
            assert!(s.map_state().is_none());
        }
        let d = s.diagnostics();
        assert_eq!(d.reason, SnapshotReason::FlagRead);
        assert_eq!(d.flags.reason, FlagReason::DivisorUnavailable);
        assert_eq!((d.param_rows, d.mask_rows, d.last_failed_flag), (3, 1, 8));
        assert_eq!((d.mask_attempts, d.flags.failures), (1, 1));
        f.word(0xf001c, 8);
        s.update_snapshot();
        assert!(
            s.map_state().is_none(),
            "same source waits for bounded retry"
        );
        s.mask_refresh = Some(Instant::now());
        s.update_snapshot();
        assert_eq!(s.map_state().unwrap().active_masks, [1, 0, 1]);
        assert_eq!(s.diagnostics().reason, SnapshotReason::Ready);
    }
    #[test]
    fn production_long_wait_retries_remain_invalid_then_regular_cache_is_reused() {
        let mut f = NativeMemory::new();
        f.bytes[f.table + 0x2d] = 0;
        let mut s = f.service();
        for _ in 0..100 {
            // Simulate 100 scheduled deadlines without blocking the test.
            s.mask_refresh = Some(Instant::now());
            s.update_snapshot();
            assert!(s.map_state().is_none());
        }
        assert_eq!(s.diagnostics().reason, SnapshotReason::ParamFormat);
        assert_eq!(
            (s.diagnostics().mask_attempts, s.diagnostics().mask_failures),
            (100, 100)
        );
        f.bytes[f.table + 0x2d] = 0x84;
        s.mask_refresh = Some(Instant::now());
        s.update_snapshot();
        for _ in 0..60 {
            s.update_snapshot();
        }
        assert_eq!(s.diagnostics().mask_attempts, 101);
        assert_eq!(s.diagnostics().ready_updates, 61);
        assert_eq!(s.read_map_state().unwrap().active_masks, [1, 0, 1]);
    }
    #[test]
    fn production_new_context_invalidates_masks_and_missing_flag_manager_cannot_publish_old_masks()
    {
        let mut f = NativeMemory::new();
        let mut s = f.service();
        s.update_snapshot();
        let generation = s.map_state().unwrap().generation;
        f.ptr(0x4100, 0x8000);
        f.bytes[0x30..0x38].fill(0);
        s.update_snapshot();
        assert!(s.map_state().is_none());
        assert_eq!(s.diagnostics().reason, SnapshotReason::FlagManager);
        f.ptr(0x30, 0xf0000);
        s.update_snapshot();
        let map = s.read_map_state().unwrap();
        assert_ne!(map.generation, generation);
        assert_eq!(map.active_masks, [1, 0, 1]);
    }
    #[test]
    fn production_active_masks_accept_cpp_readable_small_stride_and_recover_from_zero_stride() {
        let mut f = NativeMemory::new();
        f.word(0xf001c, 16);
        f.word(0xf0020, 0);
        f.word(0xf0228, 1);
        f.word(0xf0230, 0);
        f.ptr(0xf0028, 0xf0300);
        f.bytes[0xf0300] = 0x40;
        for i in 0..3 {
            f.word(f.table + 0x104 + i * 8, 17);
        }
        let mut s = f.service();
        s.update_snapshot();
        assert!(s.map_state().is_none());
        let d = s.diagnostics();
        assert_eq!(d.flags.reason, FlagReason::StrideInvalid);
        assert_eq!(
            (
                d.flags.divisor,
                d.flags.stride,
                d.flags.category,
                d.flags.storage_kind,
                d.flags.storage_index
            ),
            (16, 0, 1, 1, 0)
        );
        f.word(0xf0020, 1);
        s.mask_refresh = Some(Instant::now());
        s.update_snapshot();
        assert_eq!(s.read_map_state().unwrap().active_masks, [1, 1, 1]);
    }
    #[test]
    fn bindings_layout_and_truncated_pe_fail_closed() {
        assert_eq!(size_of::<Bindings>(), 92);
        assert!(scan_image(&[0; 64], 0x140000000).is_err());
    }
    #[test]
    fn offline_is_hidden_and_does_not_expose_game_calls() {
        let mut s = GameServices::offline();
        s.update_snapshot();
        assert!(!s.compatible());
        assert!(s.map_state().is_none());
        assert_eq!(s.display_state().loading_or_logo, 1);
        assert_eq!(s.find_param_table(88), 0);
        assert!(s.stop());
    }
    #[cfg(feature = "runtime-read-fixture")]
    mod cpp_differential {
        use super::*;
        #[repr(C)]
        #[derive(Default)]
        struct CppMapResult {
            phase: u32,
            masks: [u32; 3],
            raw_map_id: u32,
            map_id: i32,
            x: f32,
            y: f32,
            angle: f32,
            underground: i32,
            death_valid: u32,
            death_x: f32,
            death_y: f32,
            death_map_id: i32,
            view_model: usize,
        }
        unsafe extern "C" {
            fn er_cpp_flag_fixture(manager: usize, id: u32, out: *mut u32) -> bool;
            fn er_cpp_param_fixture(table: usize, id: u32, offset: usize, out: *mut u32) -> bool;
            fn er_cpp_read_map_fixture(
                base: usize,
                menu_rva: u32,
                game_data_rva: u32,
                layout: *const GameLayout,
                table: usize,
                manager: usize,
                reveal: bool,
                out: *mut CppMapResult,
            );
        }
        fn cpp_flag(manager: usize, id: u32) -> Option<bool> {
            let mut value = 0;
            // SAFETY: controlled fixture uses owned process memory and a live output.
            unsafe { er_cpp_flag_fixture(manager, id, &mut value) }.then_some(value != 0)
        }
        #[test]
        fn production_update_matches_cpp_map_readers_across_readiness_and_visibility_states() {
            assert_eq!(size_of::<CppMapResult>(), 64);
            for scenario in [
                "ready",
                "no_param",
                "bad_param",
                "no_manager",
                "zero_divisor",
                "loading",
                "full_map",
                "no_view",
                "bad_rtti",
                "nonfinite",
                "reveal",
            ] {
                let mut f = NativeMemory::new();
                f.word(0x5014, 0x3c2b2900);
                f.word(0x5024, 10);
                f.word(0x5028, 123.5f32.to_bits());
                f.word(0x502c, (-50.25f32).to_bits());
                f.word(0x5030, 0xffffff01);
                f.word(0x5034, 135.0f32.to_bits());
                f.bytes[0x50a9] = 1;
                f.word(0x50ac, 23.0f32.to_bits());
                f.word(0x50b0, 45.0f32.to_bits());
                f.word(0x50b4, 1);
                match scenario {
                    "no_param" => f.bytes[0x38..0x40].fill(0),
                    "bad_param" => f.bytes[f.table + 0x2d] = 0,
                    "no_manager" => f.bytes[0x30..0x38].fill(0),
                    "zero_divisor" => f.word(0xf001c, 0),
                    "loading" => f.bytes[0x1718] = 1,
                    "full_map" => f.bytes[0x1000 + 0x78 + 0x10 + 61] = 1,
                    "no_view" => f.bytes[0x4100..0x4108].fill(0),
                    "bad_rtti" => f.bytes[0x6210] = b'x',
                    "nonfinite" => f.word(0x5028, f32::NAN.to_bits()),
                    "reveal" => {
                        f.bytes[0x40] = 1;
                        f.bytes[0x30..0x38].fill(0);
                        f.bytes[0x38..0x40].fill(0);
                    }
                    _ => {}
                }
                let mut s = f.service();
                let mut cpp = CppMapResult::default();
                // SAFETY: all native input and output memory is owned by this test.
                unsafe {
                    er_cpp_read_map_fixture(
                        s.base,
                        f.bindings.menu,
                        f.bindings.game_data,
                        &f.bindings.layout,
                        s.find_param_table(88),
                        pointer(s.base + f.bindings.event_flags as usize),
                        f.bytes[0x40] != 0,
                        &mut cpp,
                    );
                }
                s.update_snapshot();
                assert_eq!(
                    s.map_state().is_some(),
                    cpp.phase == 0,
                    "scenario={scenario} cpp_phase={} rust_reason={:?}",
                    cpp.phase,
                    s.diagnostics().reason
                );
                if let Some(rust) = s.map_state() {
                    assert_eq!(rust.active_masks, cpp.masks, "{scenario}");
                    assert_eq!(
                        (
                            rust.raw_map_id,
                            rust.map_id,
                            rust.x,
                            rust.y,
                            rust.ori_deg,
                            rust.underground
                        ),
                        (
                            cpp.raw_map_id,
                            cpp.map_id,
                            cpp.x,
                            cpp.y,
                            cpp.angle,
                            cpp.underground
                        )
                    );
                    assert_eq!(
                        (
                            rust.death_valid,
                            rust.death_x,
                            rust.death_y,
                            rust.death_map_id,
                            rust.view_model
                        ),
                        (
                            cpp.death_valid,
                            cpp.death_x,
                            cpp.death_y,
                            cpp.death_map_id,
                            cpp.view_model
                        )
                    );
                }
            }
        }
        #[test]
        fn native_flags_match_cpp_for_readable_small_stride_and_max_internal_id() {
            let mut f = NativeMemory::new();
            let manager = f.bytes.as_ptr() as usize + 0xf0000;
            f.word(0xf001c, 16);
            f.word(0xf0020, 1);
            f.word(0xf0228, 1);
            f.word(0xf0230, 0);
            f.ptr(0xf0028, 0xf0300);
            f.bytes[0xf0300] = 0x40;
            let cpp = cpp_flag(manager, 17);
            let mut flags = EventFlagCache::default();
            let rust = flags.read(manager, 17);
            println!(
                "cpp_flag_probe divisor=16 stride=1 id=17 cpp={cpp:?} rust={rust:?} reason={:?}",
                flags.diagnostics().reason
            );
            assert_eq!(cpp, Some(true));
            assert_eq!(rust, cpp);
            f.word(0xf001c, 8);
            f.word(0xf0220, u32::MAX / 8);
            f.word(0xf0228, 2);
            f.ptr(0xf0230, 0xf0300);
            f.bytes[0xf0300] = 1;
            flags.reset();
            let cpp = cpp_flag(manager, u32::MAX);
            let rust = flags.read(manager, u32::MAX);
            println!("cpp_flag_probe id=MAX cpp={cpp:?} rust={rust:?}");
            assert_eq!(cpp, Some(true));
            assert_eq!(rust, cpp);
            assert!(
                !f.service().event_flag(u32::MAX),
                "public API retains sentinel behavior"
            );
        }
        #[test]
        fn native_flag_tree_storage_and_bounds_match_cpp() {
            for scenario in [
                "zero_id",
                "no_manager",
                "no_divisor",
                "no_tree",
                "no_category",
                "unknown_type",
                "direct_null",
                "zero_stride",
                "index_overflow",
                "unreadable_direct",
                "cycle",
                "packed_bit",
                "direct_bit",
            ] {
                let mut f = NativeMemory::new();
                let mut manager = f.bytes.as_ptr() as usize + 0xf0000;
                let mut id = 8;
                match scenario {
                    "zero_id" => {
                        id = 0;
                        manager = 0;
                    }
                    "no_manager" => manager = 0,
                    "no_divisor" => f.word(0xf001c, 0),
                    "no_tree" => f.bytes[0xf0038..0xf0040].fill(0),
                    "no_category" => f.word(0xf0220, 2),
                    "unknown_type" => f.word(0xf0228, 9),
                    "direct_null" => f.bytes[0xf0230..0xf0238].fill(0),
                    "zero_stride" => {
                        f.word(0xf0228, 1);
                        f.word(0xf0020, 0);
                    }
                    "index_overflow" => {
                        f.word(0xf0228, 1);
                        f.word(0xf0020, 2);
                        f.word(0xf0230, u32::MAX);
                    }
                    "unreadable_direct" => {
                        f.bytes[0xf0230..0xf0238].copy_from_slice(&1u64.to_le_bytes())
                    }
                    "cycle" => f.ptr(0xf0200, 0xf0200),
                    "packed_bit" => {
                        f.word(0xf0228, 1);
                        f.word(0xf0230, 1);
                        f.ptr(0xf0028, 0xf0300);
                        f.bytes[0xf0301] = 0x80;
                    }
                    _ => {}
                }
                let cpp = cpp_flag(manager, id);
                let rust = EventFlagCache::default().read(manager, id);
                assert_eq!(rust, cpp, "scenario={scenario}");
            }
        }
        #[test]
        fn native_param_field_matches_cpp_for_every_supported_directory_variant() {
            // Re-encode the same native rows in all 12-/24-byte directory forms.
            for (format, extra, wide) in [
                (2, 0, false),
                (3, 0, false),
                (4, 0, false),
                (4, 2, true),
                (5, 2, false),
                (0x85, 2, true),
            ] {
                let mut f = NativeMemory::new();
                let t = f.table;
                f.bytes[t..t + 0x200].fill(0);
                f.word(t, 0x1f0);
                f.bytes[t + 10..t + 12].copy_from_slice(&3u16.to_le_bytes());
                f.bytes[t + 0x2d] = format;
                f.bytes[t + 0x2e] = extra;
                let start = if format == 2 { 0x30 } else { 0x40 };
                for i in 0..3 {
                    let p = t + start + i * if wide { 24 } else { 12 };
                    f.word(p, i as u32);
                    f.word(p + if wide { 8 } else { 4 }, (0x100 + i * 8) as u32);
                    f.word(t + 0x104 + i * 8, 17 + i as u32);
                }
                let rows = params::param_rows(f.bytes.as_ptr() as usize + t).unwrap();
                for row in rows {
                    let mut cpp = 0;
                    // SAFETY: controlled native PARAM and writable scalar output.
                    let ok = unsafe {
                        er_cpp_param_fixture(f.bytes.as_ptr() as usize + t, row.id, 4, &mut cpp)
                    };
                    assert!(ok, "format={format:x} extra={extra}");
                    assert_eq!(row.field::<u32>(4), Some(cpp));
                }
            }
        }
    }
}

#[cfg(test)]
mod runtime_perf;
