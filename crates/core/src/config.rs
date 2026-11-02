use std::{collections::HashMap, fs, path::Path};

#[derive(Default)]
pub struct Config {
    values: HashMap<String, String>,
}
impl Config {
    pub fn load(directory: &Path) -> Self {
        let mut result = Self::default();
        result.values.insert(
            "runtime.directory".into(),
            directory.to_string_lossy().into_owned(),
        );
        if let Ok(text) = fs::read_to_string(directory.join("EROverlay.ini")) {
            result.parse("", &text);
        }
        for name in ["common", "input", "style", "minimap", "boss"] {
            let path = directory.join("configs").join(format!("{name}.ini"));
            if let Ok(text) = fs::read_to_string(path) {
                result.parse(name, &text);
            }
        }
        result.values.insert(
            "runtime.game_language".into(),
            eroverlay_game::game_language().unwrap_or_else(|| "english".into()),
        );
        result
    }
    fn parse(&mut self, file: &str, text: &str) {
        let mut section = "";
        for line in text.trim_start_matches('\u{feff}').lines() {
            let line = line.trim();
            if line.is_empty() || line.starts_with(['#', ';']) {
                continue;
            }
            if let Some(name) = line.strip_prefix('[').and_then(|v| v.strip_suffix(']')) {
                section = name.trim();
            } else if let Some((key, value)) = line.split_once('=') {
                let key = key.trim();
                if !key.is_empty() {
                    let local = if section.is_empty() {
                        key.to_owned()
                    } else {
                        format!("{section}.{key}")
                    };
                    let value = value.trim().to_owned();
                    let full = if file.is_empty() {
                        local.clone()
                    } else {
                        format!("{file}.{local}")
                    };
                    self.values.insert(full, value.clone());
                    if file == "minimap" {
                        self.values.insert(local, value);
                    }
                }
            }
        }
    }
    pub fn get(&self, key: &str) -> Option<&str> {
        self.values.get(key).map(String::as_str)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn matches_sectioned_configuration_and_preserves_values() {
        let mut c = Config::default();
        c.parse(
            "",
            "[common]\nfont=old.ttf\n[minimap.controls]\ntoggle=F8\n",
        );
        assert_eq!(c.get("common.font"), Some("old.ttf"));
        c.parse("minimap", "\u{feff}#comment\n[controls]\ntoggle = CTRL+M\n[preset.Large]\nwidth=90%\nmargin_left=\n[diagnostics]\nlog_file=D:/路径/log.txt\n");
        assert_eq!(c.get("minimap.controls.toggle"), Some("CTRL+M"));
        assert_eq!(c.get("preset.Large.width"), Some("90%"));
        assert_eq!(c.get("preset.Large.margin_left"), Some(""));
        assert_eq!(c.get("diagnostics.log_file"), Some("D:/路径/log.txt"));
        assert!(c.get("preset.large.width").is_none());
    }
}
