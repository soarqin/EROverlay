use std::collections::{HashMap, HashSet};

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub enum Shape {
    #[default]
    Rect,
    Rounded,
    Circle,
}

#[derive(Clone, Copy, Debug, Default)]
pub struct Margin {
    pub value: f32,
    pub percent: bool,
}

#[derive(Clone, Debug)]
pub struct Preset {
    pub name: String,
    pub width: f32,
    pub height: f32,
    pub zoom: f32,
    pub opacity: f32,
    pub center: bool,
    pub left: bool,
    pub bottom: bool,
    pub x: Margin,
    pub y: Margin,
    pub rotate: bool,
    pub shape: Shape,
    pub rounding: Margin,
    pub map_scale: f32,
    pub decoration_scale: f32,
    pub player_scale: f32,
    pub compass_scale: f32,
}

impl Default for Preset {
    fn default() -> Self {
        Self {
            name: "compact".into(),
            width: 0.3,
            height: 0.3,
            zoom: 0.75,
            opacity: 0.8,
            center: false,
            left: false,
            bottom: false,
            x: Margin::default(),
            y: Margin::default(),
            rotate: false,
            shape: Shape::Rect,
            rounding: Margin {
                value: 0.2,
                percent: true,
            },
            map_scale: 1.0,
            decoration_scale: 1.0,
            player_scale: 1.0,
            compass_scale: 1.0,
        }
    }
}

#[derive(Clone, Debug)]
pub struct Settings {
    pub keys: [String; 4],
    pub graces: bool,
    pub landmarks: bool,
    pub death: bool,
    pub beacons: bool,
    pub others: bool,
    pub full_map: bool,
    pub border: [u8; 4],
    pub border_width: f32,
    pub presets: Vec<Preset>,
}

fn number(value: &str) -> Option<f32> {
    let text = value.trim();
    let percent = text.ends_with('%');
    let number: f32 = text.strip_suffix('%').unwrap_or(text).trim().parse().ok()?;
    number
        .is_finite()
        .then_some(if percent { number / 100.0 } else { number })
}

fn margin(value: &str) -> Option<Margin> {
    let value = value.trim();
    let pixels = value.as_bytes()[value.len().saturating_sub(2)..].eq_ignore_ascii_case(b"px");
    let text = if pixels {
        &value[..value.len() - 2]
    } else {
        value
    };
    Some(Margin {
        value: number(text)?,
        percent: value.ends_with('%'),
    })
}

#[cfg(test)]
pub fn parse_ini(text: &str) -> HashMap<String, String> {
    let mut values = HashMap::new();
    let mut section = String::new();
    for line in text.trim_start_matches('\u{feff}').lines() {
        let line = line.trim();
        if line.is_empty() || line.starts_with(['#', ';']) {
            continue;
        }
        if line.starts_with('[') && line.ends_with(']') {
            section = line[1..line.len() - 1].trim().to_owned();
        } else if let Some((key, value)) = line.split_once('=') {
            values.insert(format!("{section}.{}", key.trim()), value.trim().to_owned());
        }
    }
    values
}

impl Settings {
    pub fn load(values: &HashMap<String, String>) -> Self {
        let get = |key: &str| values.get(key).map(String::as_str).unwrap_or("");
        let boolean = |key: &str, fallback: bool| match get(key).to_ascii_lowercase().as_str() {
            "true" | "1" | "yes" | "on" => true,
            "false" | "0" | "no" | "off" => false,
            _ => fallback,
        };
        let scalar = |key: &str, fallback: f32, maximum: f32| {
            number(get(key))
                .filter(|v| *v >= 0.0 && *v <= maximum)
                .unwrap_or(fallback)
        };
        let keys = ["toggle", "cycle", "toggle_graces", "toggle_landmarks"].map(|key| {
            values
                .get(&format!("controls.{key}"))
                .cloned()
                .unwrap_or_else(|| {
                    if key.starts_with("toggle_") {
                        "N".into()
                    } else {
                        "M".into()
                    }
                })
        });
        let mut result = Self {
            keys,
            graces: boolean("markers.graces", true),
            landmarks: boolean("markers.landmarks", true),
            death: boolean("markers.death", true),
            beacons: boolean("markers.beacons", true),
            others: boolean("markers.others", false),
            full_map: boolean("map.full_map", false),
            border: [255, 255, 255, 100],
            border_width: scalar("border.width", 1.5, f32::MAX),
            presets: Vec::new(),
        };
        let color: Option<Vec<u8>> = get("border.color")
            .split(',')
            .map(|p| p.trim().parse().ok())
            .collect();
        if let Some(color) = color.filter(|c| c.len() == 4) {
            result.border.copy_from_slice(&color);
        }
        let order = get("presets.order").trim();
        let order = if order.is_empty() {
            "compact,large"
        } else {
            order
        };
        let mut seen = HashSet::new();
        for name in order.split(',').map(str::trim) {
            if name.is_empty()
                || name.len() > 64
                || !name
                    .bytes()
                    .all(|c| c.is_ascii_alphanumeric() || c == b'_' || c == b'-')
                || !seen.insert(name)
            {
                continue;
            }
            let prefix = format!("preset.{name}.");
            if !values.keys().any(|k| k.starts_with(&prefix)) {
                continue;
            }
            let key = |field: &str| format!("{prefix}{field}");
            let mut preset = Preset {
                name: name.into(),
                ..Preset::default()
            };
            preset.width = scalar(&key("width"), preset.width, f32::MAX);
            preset.height = scalar(&key("height"), preset.height, f32::MAX);
            preset.zoom = scalar(&key("zoom"), preset.zoom, f32::MAX);
            preset.opacity = scalar(&key("opacity"), preset.opacity, 1.0);
            preset.rotate = boolean(&key("rotate"), false);
            preset.center = get(&key("position")).eq_ignore_ascii_case("center");
            if !preset.center {
                let left = margin(get(&key("margin_left")));
                let right = margin(get(&key("margin_right")));
                let top = margin(get(&key("margin_top")));
                let bottom = margin(get(&key("margin_bottom")));
                preset.left = left.is_some();
                preset.bottom = top.is_none() && bottom.is_some();
                preset.x = left.or(right).unwrap_or_default();
                preset.y = top.or(bottom).unwrap_or_default();
            }
            preset.shape = match get(&key("shape")).to_ascii_lowercase().as_str() {
                "circle" => Shape::Circle,
                "rounded" => Shape::Rounded,
                _ => Shape::Rect,
            };
            if let Some(rounding) = margin(get(&key("rounding"))).filter(|m| m.value >= 0.0) {
                preset.rounding = rounding;
            }
            preset.map_scale = scalar(&key("map_scale"), 1.0, f32::MAX);
            preset.decoration_scale = scalar(&key("decoration_scale"), 1.0, f32::MAX);
            preset.player_scale = scalar(&key("player_scale"), 1.0, f32::MAX);
            preset.compass_scale = scalar(&key("compass_scale"), 1.0, f32::MAX);
            result.presets.push(preset);
        }
        if result.presets.is_empty() {
            result.presets.push(Preset::default());
            result.presets.push(Preset {
                name: "large".into(),
                width: 0.9,
                height: 0.9,
                zoom: 1.5,
                opacity: 0.6,
                center: true,
                ..Preset::default()
            });
        }
        let toggle = eroverlay_abi::parse_chord(&result.keys[0]);
        if toggle != 0
            && toggle == eroverlay_abi::parse_chord(&result.keys[1])
            && result.presets.iter().all(|p| p.zoom != 0.0)
        {
            let mut hidden = result.presets.last().unwrap().clone();
            hidden.name = "hidden".into();
            hidden.zoom = 0.0;
            result.presets.push(hidden);
        }
        result
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn shipped_configuration_preserves_units_and_hidden_step() {
        let settings = Settings::load(&parse_ini(include_str!("../../../configs/minimap.ini")));
        assert_eq!(settings.presets.len(), 3);
        assert_eq!(settings.presets[0].width, 0.32);
        assert_eq!(settings.presets[0].x.value, 10.0);
        assert_eq!(settings.presets[0].shape, Shape::Circle);
        assert!(settings.presets[1].center);
        assert_eq!(settings.presets[2].zoom, 0.0);
    }
    #[test]
    fn malformed_numbers_and_duplicate_names_are_safe() {
        let settings = Settings::load(&parse_ini(
            "[presets]\norder=x,x,../bad\n[preset.x]\nopacity=NaN\nzoom=inf\nmargin_left=-5%\nmargin_right=20px\n[controls]\ntoggle=none",
        ));
        assert_eq!(settings.presets.len(), 1);
        assert_eq!(settings.presets[0].opacity, 0.8);
        assert_eq!(settings.presets[0].zoom, 0.75);
        assert!(settings.presets[0].left);
        assert_eq!(settings.presets[0].x.value, -0.05);
    }
    #[test]
    fn synonymous_shortcuts_append_hidden_preset() {
        let settings = Settings::load(&parse_ini("[controls]\ntoggle=CONTROL+M\ncycle=CTRL+M"));
        assert_eq!(settings.presets.last().unwrap().zoom, 0.0);
    }
}
