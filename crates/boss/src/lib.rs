#![allow(non_snake_case)]
#![deny(unsafe_op_in_unsafe_fn)]
mod challenge;
mod data;
mod host;
mod revival;
mod settings;
mod ui;

use eroverlay_abi as abi;
use host::Host;
use std::{
    ffi::c_void,
    panic::{AssertUnwindSafe, catch_unwind},
    path::PathBuf,
    sync::{
        Arc, Mutex,
        atomic::{AtomicBool, Ordering},
    },
    time::{Duration, Instant},
};

pub(crate) struct Snapshot {
    pub catalog: Option<Arc<data::Catalog>>,
    pub states: Arc<[u8]>,
    pub revivable: Arc<[u8]>,
    pub state: Option<abi::BossState>,
    pub settings: Arc<settings::Settings>,
    pub kills: u32,
    pub total: u32,
    pub deaths: u32,
    pub best: u32,
    pub tries: u32,
    pub compact: String,
    pub status: &'static str,
    pub revision: u64,
    pub result: Option<bool>,
}
enum Action {
    Settings(settings::Settings),
    Revive { flag: u32, context: u64 },
    Reload,
    BeginChallenge,
}
struct Update {
    data: data::Data,
    root: PathBuf,
    settings: Arc<settings::Settings>,
    keys: [u32; 2],
    catalog: Option<Arc<data::Catalog>>,
    catalog_revision: u64,
    challenge: challenge::Challenge,
    revival: revival::Revival,
    next: Option<Instant>,
    revision: u64,
    result: Option<bool>,
}
struct Instance {
    host: Host,
    update: Mutex<Update>,
    ui: Mutex<ui::Ui>,
    snapshot: Mutex<Option<Arc<Snapshot>>>,
    actions: Mutex<Vec<Action>>,
    full: AtomicBool,
    shown: AtomicBool,
}
fn guard(f: impl FnOnce() -> i32) -> i32 {
    catch_unwind(AssertUnwindSafe(f)).unwrap_or(abi::ERR_PANIC)
}
unsafe extern "C" fn init(api: *const abi::HostApi, out: *mut *mut c_void) -> i32 {
    guard(|| {
        if api.is_null() || out.is_null() {
            return abi::ERR_INVALID;
        }
        unsafe { out.write(std::ptr::null_mut()) };
        // SAFETY: the ABI contract guarantees the fixed header. Do not copy an
        // optional appendix until size, version and capabilities are validated.
        let magic = unsafe { std::ptr::addr_of!((*api).magic).read_unaligned() };
        let size = unsafe { std::ptr::addr_of!((*api).size).read_unaligned() };
        let version = unsafe { std::ptr::addr_of!((*api).version).read_unaligned() };
        if !abi::valid_header(magic, size, version, abi::BOSS_STATE_HOST_SIZE) {
            return abi::ERR_ABI;
        }
        let host = Host(unsafe { api.read_unaligned() });
        let required = abi::CAP_GAME
            | abi::CAP_FILES
            | abi::CAP_TEXTURES
            | abi::CAP_MESH
            | abi::CAP_INPUT
            | abi::CAP_UI_INPUT
            | abi::CAP_BOSS_STATE;
        if host.0.capabilities & required != required
            || host.0.read_boss_state.is_none()
            || host.0.read_event_flags.is_none()
            || host.0.revive_boss.is_none()
            || host.0.ui_input_poll.is_none()
            || host.0.ui_input_clear.is_none()
            || host.0.ui_input_capture.is_none()
            || host.0.find_param_table.is_none()
            || host.0.request_file.is_none()
            || host.0.poll_file.is_none()
            || host.0.release_file.is_none()
            || host.0.read_display_state.is_none()
            || host.0.texture_create_rgba.is_none()
            || host.0.texture_poll.is_none()
            || host.0.texture_release.is_none()
            || host.0.submit_mesh.is_none()
            || host.0.set_render_options.is_none()
        {
            return abi::ERR_ABI;
        }
        let root = PathBuf::from(host.config("runtime.directory").unwrap_or_default());
        let settings = Arc::new(settings::Settings::load(host, &root));
        let mut challenge = challenge::Challenge::default();
        challenge.load(&root);
        let revival = match revival::Revival::load(&root) {
            Ok(v) => v,
            Err(e) => {
                host.log(&format!("boss-revival: {e}; revival disabled\n"));
                revival::Revival::default()
            }
        };
        let keys = [
            abi::parse_chord(&settings.toggle_full_mode),
            abi::parse_chord(&settings.toggle),
        ];
        let instance = Instance {
            host,
            update: Mutex::new(Update {
                data: data::Data::new(host),
                root,
                keys,
                catalog: None,
                catalog_revision: 0,
                settings: settings.clone(),
                challenge,
                revival,
                next: None,
                revision: 0,
                result: None,
            }),
            ui: Mutex::new(ui::Ui::new(host, &settings)),
            snapshot: Mutex::new(None),
            actions: Mutex::new(Vec::with_capacity(8)),
            full: AtomicBool::new(false),
            shown: AtomicBool::new(true),
        };
        unsafe { out.write(Box::into_raw(Box::new(instance)).cast()) };
        host.log("Boss Rust/egui initialized; native UI_SCENE above game UI\n");
        abi::OK
    })
}
unsafe extern "C" fn update(instance: *mut c_void) -> i32 {
    guard(|| {
        let Some(instance) = (unsafe { instance.cast::<Instance>().as_ref() }) else {
            return abi::ERR_INVALID;
        };
        let Ok(mut u) = instance.update.try_lock() else {
            return abi::ERR_BUSY;
        };
        let visible = instance.host.visible();
        if !visible {
            instance.host.capture(0);
        }
        if visible && instance.host.key(u.keys[0]) {
            instance.full.fetch_xor(true, Ordering::Relaxed);
            instance.host.clear_input();
            if !instance.full.load(Ordering::Relaxed) {
                instance.host.capture(0);
            }
        }
        if visible && instance.host.key(u.keys[1]) {
            instance.shown.fetch_xor(true, Ordering::Relaxed);
            if !instance.shown.load(Ordering::Relaxed) {
                instance.host.capture(0);
                instance.host.clear_input();
            }
        }
        if let Ok(mut actions) = instance.actions.try_lock() {
            for action in actions.drain(..) {
                match action {
                    Action::Settings(mut s) => {
                        s.sanitize();
                        if s != *u.settings {
                            if let Err(e) = s.save(&u.root) {
                                instance
                                    .host
                                    .log(&format!("boss-settings: save failed: {e}\n"));
                            }
                            u.keys = [
                                abi::parse_chord(&s.toggle_full_mode),
                                abi::parse_chord(&s.toggle),
                            ];
                            u.settings = Arc::new(s);
                            u.next = None;
                        }
                    }
                    Action::Revive { flag, context } => {
                        let allowed = u.settings.allow_revive && u.revival.recipe(flag).is_some();
                        let ok=allowed&&instance.host.0.revive_boss.is_some_and(|f|unsafe{f(instance.host.0.user,context,flag,
                        u.revival.recipe(flag).unwrap().as_ptr(),u.revival.recipe(flag).unwrap().len())}==abi::OK);
                        u.result = Some(ok);
                        u.next = None;
                        if !ok {
                            instance.host.log("boss-revival: context or flags unavailable; no revival performed\n");
                        }
                    }
                    Action::Reload => {
                        u.data.reload();
                        u.catalog = None;
                        u.next = None;
                        u.result = None;
                    }
                    Action::BeginChallenge => {
                        if let Some(s) = instance.host.state() {
                            u.challenge.begin(&s);
                            let root = u.root.clone();
                            if let Err(e) = u.challenge.save(&root) {
                                instance
                                    .host
                                    .log(&format!("boss-challenge: save failed: {e}\n"));
                            }
                            u.next = None;
                        }
                    }
                }
            }
        }
        // File retirement and bounded initialization continue while a panel is
        // closed. Metadata discovery starts only after a valid save is present.
        if let Some(catalog) = u.data.update() {
            u.catalog = Some(catalog);
        }
        if u.catalog_revision != u.data.revision() {
            u.catalog_revision = u.data.revision();
            u.next = None;
        }
        let catalog = u.catalog.clone();
        let now = Instant::now();
        if u.next.is_some_and(|next| now < next) {
            return abi::OK;
        }
        u.next = Some(now + Duration::from_millis(500));
        let state = instance.host.state();
        let mut states = vec![2; catalog.as_ref().map_or(0, |c| c.rows.len())];
        if state.is_some()
            && let Some(c) = catalog.as_ref()
        {
            let ids: Vec<_> = c.rows.iter().map(|r| r.flag).collect();
            instance.host.flags(&ids, &mut states);
        }
        let total = catalog
            .as_ref()
            .map_or(0, |c| c.rows.iter().filter(|r| r.flag != 0).count())
            as u32;
        let kills = states.iter().filter(|v| **v == 1).count() as u32;
        let settings = u.settings.clone();
        if settings.challenge_mode {
            let root = u.root.clone();
            u.challenge.check_external(&root);
            if let Some(s) = state.as_ref()
                && u.challenge.update(s, kills, settings.challenge_death_count)
                && let Err(e) = u.challenge.save(&root)
            {
                instance
                    .host
                    .log(&format!("boss-challenge: save failed: {e}\n"));
            }
        }
        let deaths = state.as_ref().map_or(0, |s| {
            if settings.challenge_mode {
                u.challenge.deaths(s)
            } else {
                s.deaths
            }
        });
        let compact = format_status(
            if settings.challenge_mode {
                &settings.challenge_status_text
            } else {
                &settings.boss_kill_text
            },
            kills,
            total,
            deaths,
            u.challenge.best,
            u.challenge.tries,
            state.as_ref().map_or(0, |s| s.in_game_time_ms),
        );
        let revivable: Arc<[u8]> = catalog.as_ref().map_or_else(
            || Arc::from([]),
            |c| {
                c.rows
                    .iter()
                    .map(|r| u8::from(u.revival.recipe(r.flag).is_some()))
                    .collect::<Vec<_>>()
                    .into()
            },
        );
        u.revision = u.revision.wrapping_add(1);
        let snapshot = Arc::new(Snapshot {
            catalog,
            states: states.into(),
            revivable,
            state,
            settings,
            kills,
            total,
            deaths,
            best: u.challenge.best,
            tries: u.challenge.tries,
            compact,
            status: u.data.status(),
            revision: u.revision,
            result: u.result,
        });
        if let Ok(mut slot) = instance.snapshot.try_lock() {
            *slot = Some(snapshot);
        }
        abi::OK
    })
}
fn format_status(
    template: &str,
    kills: u32,
    total: u32,
    deaths: u32,
    best: u32,
    tries: u32,
    millis: u32,
) -> String {
    let seconds = millis / 1000;
    let igt = if seconds >= 3600 {
        format!(
            "{}:{:02}:{:02}",
            seconds / 3600,
            seconds / 60 % 60,
            seconds % 60
        )
    } else {
        format!("{:02}:{:02}", seconds / 60, seconds % 60)
    };
    let mut text = template.replace("$n", "\n");
    for (key, value) in [
        ("kills", kills),
        ("total", total),
        ("deaths", deaths),
        ("pb", best),
        ("tries", tries),
    ] {
        text = text.replace(&format!("{{{key}}}"), &value.to_string());
    }
    text.replace("{igt}", &igt)
}
unsafe extern "C" fn render(instance: *mut c_void, frame: *const abi::FrameInfo) -> i32 {
    guard(|| {
        if instance.is_null() || frame.is_null() {
            return abi::ERR_INVALID;
        }
        // SAFETY: callers supply the fixed size word even for a short table.
        // Reject it before constructing or copying the remaining frame fields.
        if (unsafe { std::ptr::addr_of!((*frame).size).read_unaligned() } as usize)
            < size_of::<abi::FrameInfo>()
        {
            return abi::ERR_INVALID;
        }
        let frame = unsafe { frame.read_unaligned() };
        if frame.layer != abi::LAYER_ABOVE_UI
            || !frame.width.is_finite()
            || !frame.height.is_finite()
            || frame.width <= 0.0
            || frame.height <= 0.0
            || !frame.delta_seconds.is_finite()
        {
            return abi::ERR_INVALID;
        }
        let instance = unsafe { &*instance.cast::<Instance>() };
        let snapshot = instance.snapshot.try_lock().ok().and_then(|s| s.clone());
        let Some(snapshot) = snapshot else {
            instance.host.capture(0);
            return abi::OK;
        };
        let Ok(mut ui) = instance.ui.try_lock() else {
            return abi::ERR_BUSY;
        };
        ui.render(instance, &snapshot, &frame)
    })
}
unsafe extern "C" fn reset(instance: *mut c_void, _generation: u64) -> i32 {
    guard(|| {
        let Some(i) = (unsafe { instance.cast::<Instance>().as_ref() }) else {
            return abi::ERR_INVALID;
        };
        let Ok(mut ui) = i.ui.try_lock() else {
            return abi::ERR_BUSY;
        };
        ui.reset(i.host);
        abi::OK
    })
}
unsafe extern "C" fn destroy(instance: *mut c_void) {
    let _ = catch_unwind(AssertUnwindSafe(|| {
        if !instance.is_null() {
            // SAFETY: core excludes callbacks and destroys every successful instance once.
            let i = unsafe { Box::from_raw(instance.cast::<Instance>()) };
            i.host.capture(0);
            i.host.clear_input();
            if let Ok(mut ui) = i.ui.lock() {
                ui.reset(i.host);
            }
        }
    }));
}
static NAME: &[u8] = b"Boss Rust/egui";
#[unsafe(no_mangle)]
pub extern "C" fn getEROverlayPluginV1(version: u32) -> *const abi::PluginApi {
    if version != abi::ABI_VERSION {
        return std::ptr::null();
    }
    static API: Api = Api(abi::PluginApi {
        magic: abi::ABI_MAGIC,
        size: size_of::<abi::PluginApi>() as u32,
        version: abi::ABI_VERSION,
        required_capabilities: abi::CAP_GAME
            | abi::CAP_FILES
            | abi::CAP_TEXTURES
            | abi::CAP_MESH
            | abi::CAP_INPUT
            | abi::CAP_UI_INPUT
            | abi::CAP_BOSS_STATE,
        capabilities: abi::CAP_CONCURRENT,
        layer: abi::LAYER_ABOVE_UI,
        reserved: 0,
        name: abi::ByteSlice {
            data: NAME.as_ptr(),
            len: NAME.len(),
        },
        init: Some(init),
        update: Some(update),
        render: Some(render),
        renderer_reset: Some(reset),
        destroy: Some(destroy),
    });
    &API.0
}
struct Api(abi::PluginApi);
// SAFETY: the exported immutable function table references static names/code only.
unsafe impl Sync for Api {}

#[cfg(test)]
mod contract_tests {
    use super::*;
    #[test]
    fn abi_rejects_old_host_and_short_frame_before_reading_past_headers() {
        let mut old = [0u8; abi::HOST_V1_BASE_SIZE];
        old[..8].copy_from_slice(&abi::ABI_MAGIC.to_ne_bytes());
        old[8..12].copy_from_slice(&(abi::HOST_V1_BASE_SIZE as u32).to_ne_bytes());
        old[12..16].copy_from_slice(&abi::ABI_VERSION.to_ne_bytes());
        let mut out = std::ptr::dangling_mut::<c_void>();
        assert_eq!(unsafe { init(old.as_ptr().cast(), &mut out) }, abi::ERR_ABI);
        assert!(out.is_null());
        let size = 4u32;
        assert_eq!(
            unsafe {
                render(
                    std::ptr::dangling_mut::<c_void>(),
                    (&size as *const u32).cast(),
                )
            },
            abi::ERR_INVALID
        );
        assert!(getEROverlayPluginV1(99).is_null());
    }
    #[test]
    fn status_preserves_templates_and_formats_igt_without_frame_reads() {
        assert_eq!(
            format_status(
                "{kills}/{total}$n{deaths} {igt} {pb} {tries}",
                3,
                211,
                2,
                7,
                1,
                3_661_000
            ),
            "3/211\n2 1:01:01 7 1"
        );
        assert_eq!(format_status("{igt}", 0, 0, 0, 0, 0, 61_000), "01:01");
    }
}
