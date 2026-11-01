use crate::config::Config;
use std::{
    fs::OpenOptions,
    io::{BufWriter, Write},
    path::Path,
    sync::{
        atomic::{AtomicU64, Ordering},
        mpsc::{self, SyncSender},
    },
    thread::{self, JoinHandle},
    time::{Duration, Instant},
};

#[derive(Clone, Copy)]
pub enum Stage {
    Update,
    Game,
    UpdatePlugins,
    Render,
    Transfer,
    RenderPlugin,
    Gpu,
    RendererInit,
}
const STAGES: usize = 8;
const NAMES: [&str; STAGES] = [
    "update",
    "game",
    "update_plugins",
    "render",
    "texture_sync",
    "render_plugin_staging",
    "gpu_submit",
    "renderer_init",
];
#[derive(Default)]
struct Meter {
    calls: AtomicU64,
    samples: AtomicU64,
    nanos: AtomicU64,
    maximum: AtomicU64,
    rpm: AtomicU64,
    queries: AtomicU64,
    bytes: AtomicU64,
    failed: AtomicU64,
}
/// Optional sampled production-path counters. Formatting only occurs in the
/// update worker's low-frequency report, never in a render callback.
pub struct Diagnostics {
    enabled: bool,
    stages: [Meter; STAGES],
    last_tick: AtomicU64,
    last_reason: AtomicU64,
    render_skips: AtomicU64,
    gate_skips: AtomicU64,
    rebuilds: AtomicU64,
    resets: AtomicU64,
    unconsumed_layers: AtomicU64,
}
pub struct Probe<'a> {
    meter: &'a Meter,
    start: Instant,
    reads: eroverlay_game::memory::ReadStats,
}
impl Diagnostics {
    pub fn new(enabled: bool) -> Self {
        Self {
            enabled,
            stages: std::array::from_fn(|_| Meter::default()),
            last_tick: AtomicU64::new(0),
            last_reason: AtomicU64::new(u64::MAX),
            render_skips: AtomicU64::new(0),
            gate_skips: AtomicU64::new(0),
            rebuilds: AtomicU64::new(0),
            resets: AtomicU64::new(0),
            unconsumed_layers: AtomicU64::new(0),
        }
    }
    pub fn stage(&self, stage: Stage) -> Option<Probe<'_>> {
        if !self.enabled {
            return None;
        }
        let meter = &self.stages[stage as usize];
        // First call and one in sixteen thereafter; no clock/syscall snapshot
        // for the other calls. Nested stages intentionally overlap total time.
        if meter.calls.fetch_add(1, Ordering::Relaxed) & 15 != 0 {
            return None;
        }
        Some(Probe {
            meter,
            start: Instant::now(),
            reads: eroverlay_game::memory::syscall_counters(),
        })
    }
    pub fn render_skip(&self) {
        if self.enabled {
            self.render_skips.fetch_add(1, Ordering::Relaxed);
        }
    }
    pub fn gate_skip(&self) {
        if self.enabled {
            self.gate_skips.fetch_add(1, Ordering::Relaxed);
        }
    }
    pub fn rebuild(&self) {
        if self.enabled {
            self.rebuilds.fetch_add(1, Ordering::Relaxed);
        }
    }
    pub fn reset_failure(&self) {
        if self.enabled {
            self.resets.fetch_add(1, Ordering::Relaxed);
        }
    }
    pub fn unconsumed_layer(&self) {
        if self.enabled {
            self.unconsumed_layers.fetch_add(1, Ordering::Relaxed);
        }
    }
    pub fn should_report(&self, tick: u64, reason: u64) -> bool {
        if !self.enabled {
            return false;
        }
        let last = self.last_tick.load(Ordering::Relaxed);
        let elapsed = tick.wrapping_sub(last);
        if last != 0
            && (elapsed < 60
                || (elapsed < 120 && reason == self.last_reason.load(Ordering::Relaxed)))
        {
            return false;
        }
        self.last_tick.store(tick, Ordering::Relaxed);
        self.last_reason.store(reason, Ordering::Relaxed);
        true
    }
    pub fn report(&self) -> String {
        use std::fmt::Write;
        let mut text = format!(
            "core-stages sampled=1/16 render_lock_skips={} callback_gate_skips={} renderer_rebuilds={} reset_failures={} unconsumed_layers={}",
            self.render_skips.swap(0, Ordering::Relaxed),
            self.gate_skips.swap(0, Ordering::Relaxed),
            self.rebuilds.swap(0, Ordering::Relaxed),
            self.resets.swap(0, Ordering::Relaxed),
            self.unconsumed_layers.swap(0, Ordering::Relaxed)
        );
        for (name, meter) in NAMES.iter().zip(&self.stages) {
            let samples = meter.samples.swap(0, Ordering::Relaxed);
            let nanos = meter.nanos.swap(0, Ordering::Relaxed);
            let _ = write!(
                text,
                " {name}[calls={} samples={samples} avg_us={:.1} max_us={:.1} rpm={} vq={} bytes={} failed={}]",
                meter.calls.swap(0, Ordering::Relaxed),
                nanos as f64 / samples.max(1) as f64 / 1000.0,
                meter.maximum.swap(0, Ordering::Relaxed) as f64 / 1000.0,
                meter.rpm.swap(0, Ordering::Relaxed),
                meter.queries.swap(0, Ordering::Relaxed),
                meter.bytes.swap(0, Ordering::Relaxed),
                meter.failed.swap(0, Ordering::Relaxed)
            );
        }
        text
    }
}
impl Drop for Probe<'_> {
    fn drop(&mut self) {
        let nanos = self.start.elapsed().as_nanos().min(u64::MAX as u128) as u64;
        let reads = eroverlay_game::memory::syscall_counters().since(self.reads);
        self.meter.nanos.fetch_add(nanos, Ordering::Relaxed);
        self.meter.maximum.fetch_max(nanos, Ordering::Relaxed);
        self.meter.rpm.fetch_add(reads.rpm_calls, Ordering::Relaxed);
        self.meter
            .queries
            .fetch_add(reads.virtual_queries, Ordering::Relaxed);
        self.meter.bytes.fetch_add(reads.bytes, Ordering::Relaxed);
        self.meter
            .failed
            .fetch_add(reads.failed_reads, Ordering::Relaxed);
        self.meter.samples.fetch_add(1, Ordering::Relaxed);
    }
}

/// Diagnostics are optional, bounded, and written on a dedicated worker.
pub struct Log {
    sender: Option<SyncSender<Vec<u8>>>,
    worker: Option<JoinHandle<()>>,
}
impl Log {
    pub fn new(config: &Config) -> Self {
        let console = config
            .get("common.console")
            .is_some_and(|v| v.eq_ignore_ascii_case("true"));
        if console {
            let _ = unsafe { windows::Win32::System::Console::AllocConsole() };
        }
        let path = config
            .get("minimap.diagnostics.log_file")
            .filter(|v| !v.is_empty());
        let output = path
            .and_then(|p| {
                OpenOptions::new()
                    .create(true)
                    .append(true)
                    .open(Path::new(p))
                    .ok()
            })
            .map(BufWriter::new);
        if !console && output.is_none() {
            return Self {
                sender: None,
                worker: None,
            };
        }
        let (sender, receiver) = mpsc::sync_channel::<Vec<u8>>(64);
        let worker = thread::Builder::new()
            .name("EROverlay diagnostics".into())
            .spawn(move || {
                let mut output = output;
                loop {
                    match receiver.recv_timeout(Duration::from_secs(1)) {
                        Ok(bytes) => {
                            if let Some(out) = output.as_mut() {
                                let _ = out.write_all(&bytes);
                                if !bytes.ends_with(b"\n") {
                                    let _ = out.write_all(b"\n");
                                }
                            }
                            if console {
                                eprintln!("[EROverlay Rust] {}", String::from_utf8_lossy(&bytes));
                            }
                        }
                        Err(mpsc::RecvTimeoutError::Timeout) => {
                            if let Some(out) = output.as_mut() {
                                let _ = out.flush();
                            }
                        }
                        Err(mpsc::RecvTimeoutError::Disconnected) => {
                            if let Some(out) = output.as_mut() {
                                let _ = out.flush();
                            }
                            break;
                        }
                    }
                }
            })
            .ok();
        Self {
            sender: worker.as_ref().map(|_| sender),
            worker,
        }
    }
    pub fn enabled(&self) -> bool {
        self.sender.is_some()
    }
    pub fn write(&self, message: &[u8]) {
        if let Some(sender) = self.sender.as_ref() {
            let _ = sender.try_send(message[..message.len().min(4096)].to_vec());
        }
    }
}
impl Drop for Log {
    fn drop(&mut self) {
        self.sender.take();
        if let Some(worker) = self.worker.take() {
            let _ = worker.join();
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn diagnostic_probes_are_disabled_or_sampled_and_reports_are_rate_limited() {
        let disabled = Diagnostics::new(false);
        for tick in 1..=1000 {
            assert!(disabled.stage(Stage::Render).is_none());
            assert!(!disabled.should_report(tick, tick));
        }
        assert_eq!(
            disabled.stages[Stage::Render as usize]
                .calls
                .load(Ordering::Relaxed),
            0
        );
        let enabled = Diagnostics::new(true);
        eroverlay_game::memory::set_tracking(true);
        let source = 42u32;
        for _ in 0..32 {
            if let Some(probe) = enabled.stage(Stage::RenderPlugin) {
                assert_eq!(
                    eroverlay_game::memory::read::<u32>(&source as *const _ as usize),
                    Some(42)
                );
                drop(probe);
            }
        }
        eroverlay_game::memory::set_tracking(false);
        let text = enabled.report();
        assert!(text.contains("render_plugin_staging[calls=32 samples=2"));
        assert!(text.contains("rpm=2 vq=0 bytes=8 failed=0"));
        assert!(enabled.should_report(1, 0));
        for tick in 2..61 {
            assert!(!enabled.should_report(tick, tick));
        }
        assert!(enabled.should_report(61, 1));
        assert!(!enabled.should_report(121, 1));
        assert!(enabled.should_report(181, 1));
    }

    #[test]
    fn configured_log_appends_and_flushes_on_worker_shutdown() {
        let root = std::path::PathBuf::from(env!("CARGO_MANIFEST_DIR"))
            .join("../../target")
            .join(format!("core-log-check-{}", std::process::id()));
        std::fs::create_dir_all(&root).unwrap();
        let log_file = root.join("runtime.log");
        std::fs::write(&log_file, b"existing\n").unwrap();
        std::fs::write(
            root.join("EROverlay.ini"),
            format!("[minimap.diagnostics]\nlog_file={}\n", log_file.display()),
        )
        .unwrap();
        let log = Log::new(&Config::load(&root));
        assert!(log.enabled());
        log.write(b"core-stages sample");
        drop(log);
        assert_eq!(
            std::fs::read(&log_file).unwrap(),
            b"existing\ncore-stages sample\n"
        );
        std::fs::remove_file(log_file).unwrap();
        std::fs::remove_file(root.join("EROverlay.ini")).unwrap();
        std::fs::remove_dir(root).unwrap();
    }
}
