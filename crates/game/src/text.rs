//! Bounded PC FMG parsing and current-language text overlays. Only update-side
//! owned buffers are accepted; the game's VFS performs DCX decompression.
use crate::assets::{self, AssetError, Result};
use std::collections::BTreeMap;

pub type Fmg = BTreeMap<i32, Option<String>>;
const MAX_STRINGS: usize = 1_000_000;

fn u32_at(b: &[u8], at: usize) -> Result<u32> {
    Ok(u32::from_le_bytes(
        assets::range(b, at, 4)?.try_into().unwrap(),
    ))
}
fn i32_at(b: &[u8], at: usize) -> Result<i32> {
    Ok(i32::from_le_bytes(
        assets::range(b, at, 4)?.try_into().unwrap(),
    ))
}
fn u64_at(b: &[u8], at: usize) -> Result<u64> {
    Ok(u64::from_le_bytes(
        assets::range(b, at, 8)?.try_into().unwrap(),
    ))
}

/// Elden Ring's little-endian, UTF-16, 64-bit-offset FMG v2 format.
/// Null entries are preserved so layered callers can distinguish absent text.
pub fn parse_fmg(bytes: &[u8]) -> Result<Fmg> {
    if bytes.len() > 64 * 1024 * 1024 {
        return Err(AssetError::Budget);
    }
    if assets::range(bytes, 0, 4)? != [0, 0, 2, 0]
        || assets::range(bytes, 8, 4)? != [1, 0, 0, 0]
        || u32_at(bytes, 20)? != 0xff
        || u64_at(bytes, 32)? != 0
    {
        return Err(AssetError::Unsupported);
    }
    if u32_at(bytes, 4)? as usize != bytes.len() {
        return Err(AssetError::Invalid);
    }
    let groups = u32_at(bytes, 12)? as usize;
    let count = u32_at(bytes, 16)? as usize;
    if groups > count || count > MAX_STRINGS {
        return Err(AssetError::Budget);
    }
    let offsets = usize::try_from(u64_at(bytes, 24)?).map_err(|_| AssetError::Budget)?;
    let group_end = 40 + groups * 16;
    if offsets < group_end || offsets & 7 != 0 {
        return Err(AssetError::Invalid);
    }
    assets::range(bytes, 40, groups * 16)?;
    assets::range(bytes, offsets, count * 8)?;
    let strings_start = offsets + count * 8;
    let mut result = BTreeMap::new();
    let mut consumed = 0;
    let mut chars = 0usize;
    for i in 0..groups {
        let group = 40 + i * 16;
        let index = u32_at(bytes, group)? as usize;
        let first = i32_at(bytes, group + 4)?;
        let last = i32_at(bytes, group + 8)?;
        let length = i64::from(last) - i64::from(first) + 1;
        if length <= 0 || u32_at(bytes, group + 12)? != 0 {
            return Err(AssetError::Invalid);
        }
        let length = usize::try_from(length).map_err(|_| AssetError::Budget)?;
        if index > count || length > count - index || length > count - consumed {
            return Err(AssetError::Invalid);
        }
        for j in 0..length {
            let offset = usize::try_from(u64_at(bytes, offsets + (index + j) * 8)?)
                .map_err(|_| AssetError::Budget)?;
            let text = if offset == 0 {
                None
            } else {
                if offset < strings_start {
                    return Err(AssetError::Invalid);
                }
                let text = assets::wide_string(bytes, offset)?;
                chars = chars.checked_add(text.len()).ok_or(AssetError::Budget)?;
                if chars > 16 * 1024 * 1024 {
                    return Err(AssetError::Budget);
                }
                Some(text)
            };
            // Conflicting duplicate IDs in Mod FMGs have no unambiguous name.
            result
                .entry(first + j as i32)
                .and_modify(|prior| {
                    if *prior != text {
                        *prior = None;
                    }
                })
                .or_insert(text);
        }
        consumed += length;
    }
    if consumed != count {
        return Err(AssetError::Invalid);
    }
    Ok(result)
}

/// Steam language names and existing locale config values resolve to the same
/// native VFS directory. Unknown languages stay explicit instead of borrowing
/// another language's entire offline catalogue.
pub fn language_directory(language: &str) -> Option<&'static str> {
    match language.to_ascii_lowercase().as_str() {
        "english" | "engus" => Some("engUS"),
        "german" | "deude" => Some("deuDE"),
        "french" | "frafr" => Some("fraFR"),
        "italian" | "itait" => Some("itaIT"),
        "japanese" | "jpnjp" => Some("jpnJP"),
        "koreana" | "korean" | "korkr" => Some("korKR"),
        "polish" | "polpl" => Some("polPL"),
        "portuguese" | "brazilian" | "porbr" => Some("porBR"),
        "russian" | "rusru" => Some("rusRU"),
        "latam" | "spaar" => Some("spaAR"),
        "spanish" | "spaes" => Some("spaES"),
        "thai" | "thath" => Some("thaTH"),
        "schinese" | "zhocn" => Some("zhoCN"),
        "tchinese" | "zhotw" => Some("zhoTW"),
        "arabic" | "araae" => Some("araAE"),
        _ => None,
    }
}

/// DLC02 binders contain base, DLC01 and DLC02 FMG members. Try the first pair;
/// use its corresponding base binder only when that DLC binder is unavailable.
pub fn binder_paths(language: &str) -> Option<[String; 4]> {
    let lang = language_directory(language)?;
    Some([
        format!("msg:/{lang}/item_dlc02.msgbnd"),
        format!("msg:/{lang}/menu_dlc02.msgbnd"),
        format!("msg:/{lang}/item.msgbnd"),
        format!("msg:/{lang}/menu.msgbnd"),
    ])
}

#[derive(Default, Clone)]
pub struct TextCatalog {
    npc: BTreeMap<i32, (u8, String)>,
    place: BTreeMap<i32, (u8, String)>,
    menu: BTreeMap<i32, (u8, String)>,
}
impl TextCatalog {
    /// Accepts a decompressed BND4. Parsing completes before mutating the cache;
    /// malformed Mod text cannot leave a half-applied catalogue.
    pub fn merge_binder(&mut self, bytes: &[u8]) -> Result<usize> {
        let mut parsed = Vec::new();
        for entry in assets::parse_binder(bytes)? {
            let filename = entry.name.rsplit(['/', '\\']).next().unwrap_or_default();
            let Some(stem) = filename.strip_suffix(".fmg") else {
                continue;
            };
            let (stem, layer) = if let Some(stem) = stem.strip_suffix("_dlc02") {
                (stem, 2)
            } else if let Some(stem) = stem.strip_suffix("_dlc01") {
                (stem, 1)
            } else {
                (stem, 0)
            };
            let kind = match stem {
                "NpcName" => 0,
                "PlaceName" => 1,
                "GR_MenuText" => 2,
                _ => continue,
            };
            if parsed.iter().any(|(k, l, _)| *k == kind && *l == layer) {
                return Err(AssetError::Invalid);
            }
            parsed.push((kind, layer, parse_fmg(entry.bytes)?));
        }
        let mut count = 0;
        for (kind, layer, entries) in parsed {
            let target = match kind {
                0 => &mut self.npc,
                1 => &mut self.place,
                _ => &mut self.menu,
            };
            for (id, text) in entries {
                let Some(text) = text else { continue };
                if target.get(&id).is_none_or(|(prior, _)| *prior <= layer) {
                    target.insert(id, (layer, text));
                    count += 1;
                }
            }
        }
        Ok(count)
    }
    pub fn npc(&self, id: i32) -> Option<&str> {
        self.npc.get(&id).map(|(_, s)| s.as_str())
    }
    pub fn place(&self, id: i32) -> Option<&str> {
        self.place.get(&id).map(|(_, s)| s.as_str())
    }
    pub fn menu(&self, id: i32) -> Option<&str> {
        self.menu.get(&id).map(|(_, s)| s.as_str())
    }
    pub fn is_empty(&self) -> bool {
        self.npc.is_empty() && self.place.is_empty() && self.menu.is_empty()
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn fmg(first: i32, strings: &[Option<&str>]) -> Vec<u8> {
        let mut b = vec![0; 56 + strings.len() * 8];
        b[..4].copy_from_slice(&[0, 0, 2, 0]);
        b[8] = 1;
        b[12..16].copy_from_slice(&1u32.to_le_bytes());
        b[16..20].copy_from_slice(&(strings.len() as u32).to_le_bytes());
        b[20] = 0xff;
        b[24..32].copy_from_slice(&56u64.to_le_bytes());
        b[44..48].copy_from_slice(&first.to_le_bytes());
        b[48..52].copy_from_slice(&(first + strings.len() as i32 - 1).to_le_bytes());
        for (i, text) in strings.iter().enumerate() {
            if let Some(text) = text {
                let off = b.len() as u64;
                b[56 + i * 8..64 + i * 8].copy_from_slice(&off.to_le_bytes());
                b.extend(text.encode_utf16().chain([0]).flat_map(u16::to_le_bytes));
            }
        }
        let len = b.len() as u32;
        b[4..8].copy_from_slice(&len.to_le_bytes());
        b
    }
    fn binder(entries: &[(&str, Vec<u8>)]) -> Vec<u8> {
        let mut b = vec![0; 64 + entries.len() * 36];
        b[..4].copy_from_slice(b"BND4");
        b[10] = 1;
        b[12..16].copy_from_slice(&(entries.len() as u32).to_le_bytes());
        b[16..24].copy_from_slice(&64u64.to_le_bytes());
        b[32..40].copy_from_slice(&36u64.to_le_bytes());
        for (i, (name, data)) in entries.iter().enumerate() {
            let p = 64 + i * 36;
            let name_offset = b.len() as u32;
            b[p + 32..p + 36].copy_from_slice(&name_offset.to_le_bytes());
            b.extend(name.encode_utf16().chain([0]).flat_map(u16::to_le_bytes));
            let data_offset = b.len() as u32;
            b[p + 24..p + 28].copy_from_slice(&data_offset.to_le_bytes());
            for at in [p + 8, p + 16] {
                b[at..at + 8].copy_from_slice(&(data.len() as u64).to_le_bytes());
            }
            b.extend(data);
        }
        b
    }
    #[test]
    fn fmg_sparse_null_utf16_and_corrupt_ranges() {
        let b = fmg(100, &[Some("玛莲妮亚"), None, Some(""), Some("🌑")]);
        let parsed = parse_fmg(&b).unwrap();
        assert_eq!(parsed[&100].as_deref(), Some("玛莲妮亚"));
        assert_eq!(parsed[&101], None);
        assert_eq!(parsed[&102].as_deref(), Some(""));
        assert_eq!(parsed[&103].as_deref(), Some("🌑"));
        for length in 0..b.len() {
            assert!(parse_fmg(&b[..length]).is_err());
        }
        let mut corrupt = b.clone();
        corrupt[56..64].copy_from_slice(&u64::MAX.to_le_bytes());
        assert!(parse_fmg(&corrupt).is_err());
        let mut corrupt = b.clone();
        corrupt[40..44].copy_from_slice(&1u32.to_le_bytes());
        assert!(parse_fmg(&corrupt).is_err());
        let mut corrupt = b;
        corrupt[88..90].copy_from_slice(&0xd800u16.to_le_bytes());
        assert!(parse_fmg(&corrupt).is_err());
    }
    #[test]
    fn language_paths_use_native_directories_without_unknown_language_guessing() {
        assert_eq!(
            binder_paths("schinese").unwrap()[0],
            "msg:/zhoCN/item_dlc02.msgbnd"
        );
        assert_eq!(language_directory("engUS"), Some("engUS"));
        assert!(binder_paths("unknown-language").is_none());
    }
    #[test]
    fn binder_layers_preserve_null_fallback_and_do_not_depend_on_member_order() {
        let base = fmg(100, &[Some("base"), Some("base second")]);
        let dlc = fmg(100, &[None, Some("DLC override")]);
        let bytes = binder(&[("NpcName_dlc01.fmg", dlc), ("NpcName.fmg", base)]);
        let mut catalog = TextCatalog::default();
        catalog.merge_binder(&bytes).unwrap();
        assert_eq!(catalog.npc(100), Some("base"));
        assert_eq!(catalog.npc(101), Some("DLC override"));
        let later_base = binder(&[(
            "NpcName.fmg",
            fmg(100, &[Some("new base"), Some("wrong layer")]),
        )]);
        catalog.merge_binder(&later_base).unwrap();
        assert_eq!(catalog.npc(100), Some("new base"));
        assert_eq!(catalog.npc(101), Some("DLC override"));
        let malformed = binder(&[
            ("NpcName_dlc02.fmg", fmg(100, &[Some("partial")])),
            ("PlaceName.fmg", vec![]),
        ]);
        assert!(catalog.merge_binder(&malformed).is_err());
        assert_eq!(catalog.npc(100), Some("new base"));
    }
    #[test]
    #[ignore = "requires decompressed game item/menu BND paths in EROVERLAY_BOSS_TEXT_BINDERS, separated by semicolons"]
    fn archived_text_binders_use_production_parser() {
        let input = std::env::var("EROVERLAY_BOSS_TEXT_BINDERS").unwrap();
        let mut texts = TextCatalog::default();
        for path in input.split(';') {
            let b = std::fs::read(path).unwrap();
            println!(
                "{} text entries from {}",
                texts.merge_binder(&b).unwrap(),
                path
            );
        }
        assert!(texts.npc(904750000).is_some());
        assert!(texts.npc(905210000).is_some());
        assert!(texts.place(10000).is_some());
        println!(
            "Godrick={:?}; DLC boss={:?}; region={:?}",
            texts.npc(904750000),
            texts.npc(905210000),
            texts.place(10000)
        );
    }
}
