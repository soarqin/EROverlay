//! Font selection shared by the egui plugins, with a single proportional fallback.
pub use epaint_default_fonts::UBUNTU_LIGHT as FALLBACK_FONT;
use std::sync::Arc;

pub fn minimal_font_definitions() -> egui::FontDefinitions {
    let mut definitions = egui::FontDefinitions::empty();
    let name = "Ubuntu-Light".to_owned();
    definitions.font_data.insert(
        name.clone(),
        Arc::new(egui::FontData::from_static(
            epaint_default_fonts::UBUNTU_LIGHT,
        )),
    );
    for family in definitions.families.values_mut() {
        family.push(name.clone());
    }
    definitions
}

// All characters in the current resource error messages. ASCII/numbers always
// have the embedded fallback; only load another large CJK file when necessary.
pub const STATUS_GLYPHS: &str = "游戏 GFX 或图集布局无法解析地图瓦片目录资源读取失败，正在重试处理";

pub fn supports_status_glyphs(definitions: &egui::FontDefinitions) -> bool {
    let faces: Vec<_> = definitions.families[&egui::FontFamily::Proportional]
        .iter()
        .filter_map(|name| {
            let data = &definitions.font_data[name];
            ttf_parser::Face::parse(data.font.as_ref(), data.index).ok()
        })
        .collect();
    STATUS_GLYPHS
        .chars()
        .all(|c| faces.iter().any(|face| face.glyph_index(c).is_some()))
}

// The coverage check runs at font initialization only. Rendering selects static
// messages without font queries, parsing, formatting or allocations.

pub fn font_candidates(
    directory: &std::path::Path,
    custom: &str,
    windows: &std::path::Path,
    charset: &str,
) -> Vec<std::path::PathBuf> {
    let mut paths = if custom.is_empty() {
        Vec::new()
    } else {
        custom_font_candidates(directory, custom)
    };
    paths.push(windows.join(match charset {
        "ja" => "meiryo.ttc",
        "ko" => "malgun.ttf",
        "th" => "LeelawUI.ttf",
        _ => "msyh.ttc",
    }));
    paths
}

pub fn load_font_definitions(
    paths: &[std::path::PathBuf],
    cjk_path: &std::path::Path,
    mut read: impl FnMut(&std::path::Path) -> Option<Vec<u8>>,
) -> (egui::FontDefinitions, Vec<std::path::PathBuf>) {
    let mut definitions = minimal_font_definitions();
    let mut loaded = Vec::new();
    let mut cjk_attempted = false;
    let mut needs_cjk = true;
    for path in paths {
        cjk_attempted |= path == cjk_path;
        let Some(bytes) = read(path) else { continue };
        let Ok(face) = ttf_parser::Face::parse(&bytes, 0) else {
            continue;
        };
        needs_cjk = !STATUS_GLYPHS.chars().all(|c| face.glyph_index(c).is_some());
        definitions.font_data.insert(
            "overlay-font".to_owned(),
            Arc::new(egui::FontData::from_owned(bytes)),
        );
        definitions
            .families
            .get_mut(&egui::FontFamily::Proportional)
            .unwrap()
            .insert(0, "overlay-font".to_owned());
        loaded.push(path.clone());
        break;
    }
    if needs_cjk
        && !cjk_attempted
        && let Some(bytes) = read(cjk_path)
        && ttf_parser::Face::parse(&bytes, 0).is_ok()
    {
        definitions.font_data.insert(
            "overlay-cjk".to_owned(),
            Arc::new(egui::FontData::from_owned(bytes)),
        );
        let family = definitions
            .families
            .get_mut(&egui::FontFamily::Proportional)
            .unwrap();
        family.insert(family.len() - 1, "overlay-cjk".to_owned());
        loaded.push(cjk_path.to_owned());
    }
    (definitions, loaded)
}

pub fn custom_font_candidates(directory: &std::path::Path, name: &str) -> Vec<std::path::PathBuf> {
    let path = std::path::PathBuf::from(name);
    let path = if path.is_absolute() {
        path
    } else {
        directory.join(path)
    };
    let mut paths = vec![path.clone()];
    for extension in [".ttf", ".ttc", ".otf"] {
        let mut name = path.as_os_str().to_owned();
        name.push(extension);
        paths.push(name.into());
    }
    paths
}

pub fn font_charset(config: &str, language: &str) -> &'static str {
    match config.get(..2).unwrap_or("") {
        "en" => "en",
        "ja" => "ja",
        "ko" => "ko",
        "pl" => "pl",
        "ru" => "ru",
        "th" => "th",
        "zh" => "zh",
        _ => match language {
            "jpnJP" | "japanese" => "ja",
            "korKR" | "koreana" => "ko",
            "polPL" | "polish" => "pl",
            "rusRU" | "russian" => "ru",
            "thaTH" | "thai" => "th",
            "zhoCN" | "schinese" => "zh",
            _ => "en",
        },
    }
}
