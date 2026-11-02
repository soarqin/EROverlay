use crate::host::Host;
use serde::{Deserialize, Serialize};
use std::path::{Path, PathBuf};

#[derive(Clone, Debug, PartialEq, Serialize, Deserialize)]
#[serde(default, deny_unknown_fields)]
pub struct Settings {
    pub toggle_full_mode: String,
    pub toggle: String,
    pub allow_revive: bool,
    pub challenge_mode: bool,
    pub challenge_death_count: u32,
    pub boss_kill_text: String,
    pub challenge_status_text: String,
    pub width: f32,
    pub height: f32,
    pub opacity: f32,
    pub font_size: f32,
}
impl Default for Settings {
    fn default() -> Self {
        Self {
            toggle_full_mode: "=".into(),
            toggle: "none".into(),
            allow_revive: false,
            challenge_mode: false,
            challenge_death_count: 0,
            boss_kill_text: "{kills}/{total}".into(),
            challenge_status_text: "PB: {pb}/{total}  Tries: {tries}$nCurrent: {kills}/{total}"
                .into(),
            width: 440.0,
            height: 800.0,
            opacity: 0.95,
            font_size: 20.0,
        }
    }
}
impl Settings {
    pub fn load(host: Host, root: &Path) -> Self {
        let mut s = Self::default();
        for (key, value) in [
            ("toggle_full_mode", &mut s.toggle_full_mode),
            ("toggle", &mut s.toggle),
            ("boss_kill_text", &mut s.boss_kill_text),
            ("challenge_status_text", &mut s.challenge_status_text),
        ] {
            if let Some(v) = host.config(&format!("boss.{key}")) {
                *value = v;
            }
        }
        for (key, value) in [
            ("allow_revive", &mut s.allow_revive),
            ("challenge_mode", &mut s.challenge_mode),
        ] {
            if let Some(v) = host.config(&format!("boss.{key}")) {
                *value = v.eq_ignore_ascii_case("true") || v == "1";
            }
        }
        if let Some(v) = host
            .config("boss.challenge_death_count")
            .and_then(|v| v.parse().ok())
        {
            s.challenge_death_count = v;
        }
        for (key, value) in [
            ("width", &mut s.width),
            ("height", &mut s.height),
            ("opacity", &mut s.opacity),
            ("font_size", &mut s.font_size),
        ] {
            if let Some(v) = host
                .config(&format!("boss.{key}"))
                .and_then(|v| v.parse().ok())
            {
                *value = v;
            }
        }
        let path = root.join("configs/boss-ui.json");
        if let Ok(meta) = path.metadata()
            && meta.len() <= 32 * 1024
        {
            match std::fs::read(&path)
                .ok()
                .and_then(|v| serde_json::from_slice(&v).ok())
            {
                Some(saved) => s = saved,
                None => {
                    host.log("boss-settings: cannot parse configs/boss-ui.json; using boss.ini\n")
                }
            }
        }
        s.sanitize();
        s
    }
    pub fn sanitize(&mut self) {
        let finite = |v: f32, default: f32, min: f32, max: f32| {
            if v.is_finite() {
                v.clamp(min, max)
            } else {
                default
            }
        };
        self.width = finite(self.width, 440.0, 280.0, 2000.0);
        self.height = finite(self.height, 800.0, 220.0, 2000.0);
        self.opacity = finite(self.opacity, 0.95, 0.1, 1.0);
        self.font_size = finite(self.font_size, 20.0, 12.0, 40.0);
        self.challenge_death_count = self.challenge_death_count.min(1_000_000);
        self.boss_kill_text
            .truncate(self.boss_kill_text.floor_char_boundary(2048));
        self.challenge_status_text
            .truncate(self.challenge_status_text.floor_char_boundary(2048));
    }
    pub fn save(&self, root: &Path) -> std::io::Result<()> {
        let bytes = serde_json::to_vec_pretty(self).map_err(std::io::Error::other)?;
        write_atomic(root.join("configs/boss-ui.json"), &bytes)
    }
}
pub fn write_atomic(path: PathBuf, bytes: &[u8]) -> std::io::Result<()> {
    if let Some(parent) = path.parent() {
        std::fs::create_dir_all(parent)?;
    }
    let temp = path.with_extension("tmp");
    std::fs::write(&temp, bytes)?;
    // ReplaceFile would require an existing destination; MoveFileEx handles both.
    #[cfg(windows)]
    {
        use std::os::windows::ffi::OsStrExt;
        unsafe extern "system" {
            fn MoveFileExW(from: *const u16, to: *const u16, flags: u32) -> i32;
        }
        let from: Vec<u16> = temp.as_os_str().encode_wide().chain(Some(0)).collect();
        let to: Vec<u16> = path.as_os_str().encode_wide().chain(Some(0)).collect();
        // SAFETY: both UTF-16 buffers are NUL terminated and live through the call.
        if unsafe { MoveFileExW(from.as_ptr(), to.as_ptr(), 1 | 8) } == 0 {
            return Err(std::io::Error::last_os_error());
        }
        Ok(())
    }
    #[cfg(not(windows))]
    {
        std::fs::rename(temp, path)
    }
}
