//! Update-side PARAM facts for Boss discovery. A defeat flag is not proof of a
//! named Boss health bar; the event reader must establish that relationship.
use crate::{
    memory::read_memory,
    params::{ParamRow, param_rows},
};

// SoloParamRepository groups, not the unrelated PARAMDEF editor indices.
pub const GAME_AREA_GROUP: u32 = 29;
pub const PLAY_REGION_GROUP: u32 = 38;
pub const BONFIRE_GROUP: u32 = 43;
pub const BONFIRE_TAB_GROUP: u32 = 44;
pub const BONFIRE_SUBCATEGORY_GROUP: u32 = 45;
pub const WORLD_MAP_POINT_GROUP: u32 = 87;
pub const MAP_DEFAULT_INFO_GROUP: u32 = 133;

#[derive(Clone, Debug, PartialEq)]
pub struct GameAreaRecord {
    pub row: u32,
    pub flag: u32,
    pub map: [u8; 3],
    pub pos: [f32; 3],
    /// Co-op objective text, not an NpcName ID.
    pub found_text: i32,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct PlayRegionRecord {
    pub row: u32,
    pub boss_area: u32,
    pub bosses: [u32; 16],
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct TextChoice {
    pub id: i32,
    pub kind: u8,
    pub enable_flag: u32,
    pub disable_flag: u32,
}

#[derive(Clone, Debug, PartialEq)]
pub struct BonfireRecord {
    pub row: u32,
    pub map: [u8; 3],
    pub pos: [f32; 3],
    pub subcategory: i32,
    pub cleared_flag: u32,
    pub unlock_flag: u32,
    pub texts: [TextChoice; 8],
}

#[derive(Clone, Debug, PartialEq)]
pub struct MapPointRecord {
    pub row: u32,
    pub map: [u8; 3],
    pub pos: [f32; 3],
    pub icon: u16,
    pub cleared_flag: u32,
    pub texts: [TextChoice; 8],
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct SubcategoryRecord {
    pub row: u32,
    pub text_id: i32,
    pub tab: u16,
    pub sort: u16,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct TabRecord {
    pub row: u32,
    pub text_id: i32,
    pub sort: i32,
}

fn typed_rows(table: usize, kind: &str, version: u16) -> Option<Vec<ParamRow>> {
    let mut header = [0; 48];
    if !read_memory(table, &mut header)
        || header[44] != 0
        || u16::from_le_bytes(header[8..10].try_into().ok()?) != version
    {
        return None;
    }
    let mut name = [0; 64];
    if header[45] & 0x80 != 0 {
        let offset = usize::try_from(u64::from_le_bytes(header[16..24].try_into().ok()?)).ok()?;
        if !(48..=0x1000_0000 - name.len()).contains(&offset)
            || !read_memory(table.checked_add(offset)?, &mut name[..kind.len() + 1])
        {
            return None;
        }
    } else {
        name[..32].copy_from_slice(&header[12..44]);
    }
    if name.get(..kind.len())? != kind.as_bytes() || name[kind.len()] != 0 {
        return None;
    }
    param_rows(table)
}

fn map(row: &ParamRow, offset: usize) -> Option<[u8; 3]> {
    let map = [
        row.field(offset)?,
        row.field(offset + 1)?,
        row.field(offset + 2)?,
    ];
    map.iter().all(|v| *v <= 99).then_some(map)
}

fn position(row: &ParamRow, offset: usize) -> Option<[f32; 3]> {
    let pos = [
        row.field::<f32>(offset)?,
        row.field::<f32>(offset + 4)?,
        row.field::<f32>(offset + 8)?,
    ];
    pos.iter().all(|v| v.is_finite()).then_some(pos)
}

fn texts(row: &ParamRow) -> Option<[TextChoice; 8]> {
    let mut texts = [TextChoice::default(); 8];
    for (i, text) in texts.iter_mut().enumerate() {
        *text = TextChoice {
            id: row.field(48 + i * 12)?,
            kind: row.field(144 + i)?,
            enable_flag: row.field(52 + i * 12)?,
            disable_flag: row.field(56 + i * 12)?,
        };
    }
    Some(texts)
}

/// GAME_AREA_PARAM_ST v3: explicit defeat flag, local map and local position.
/// Rows sharing a flag are deliberately retained for the caller to consolidate.
pub fn game_areas(table: usize) -> Option<Vec<GameAreaRecord>> {
    typed_rows(table, "GAME_AREA_PARAM_ST", 3)?
        .iter()
        .map(|row| {
            Some(GameAreaRecord {
                row: row.id,
                flag: row.field(0x44)?,
                pos: position(row, 0x48)?,
                map: map(row, 0x54)?,
                found_text: row.field(0x38)?,
            })
        })
        .collect()
}

/// PLAY_REGION_PARAM_ST v9: game-authored play-region/Boss relationships.
/// These IDs locate already-discovered encounters; they do not prove a health bar.
pub fn play_regions(table: usize) -> Option<Vec<PlayRegionRecord>> {
    typed_rows(table, "PLAY_REGION_PARAM_ST", 9)?
        .iter()
        .map(|row| {
            if row.size < 272 {
                return None;
            }
            let mut bosses = [0; 16];
            for (i, boss) in bosses.iter_mut().enumerate() {
                *boss = row.field(172 + i * 4)?;
            }
            Some(PlayRegionRecord {
                row: row.id,
                boss_area: row.field(20)?,
                bosses,
            })
        })
        .collect()
}

/// BONFIRE_WARP_PARAM_ST v6. These shared prefix fields predate the DLC.
pub fn bonfires(table: usize) -> Option<Vec<BonfireRecord>> {
    typed_rows(table, "BONFIRE_WARP_PARAM_ST", 6)?
        .iter()
        .map(|row| {
            Some(BonfireRecord {
                row: row.id,
                map: map(row, 32)?,
                pos: position(row, 36)?,
                subcategory: row.field(20)?,
                cleared_flag: row.field(24)?,
                unlock_flag: row.field(4)?,
                texts: texts(row)?,
            })
        })
        .collect()
}

pub fn map_points(table: usize) -> Option<Vec<MapPointRecord>> {
    typed_rows(table, "WORLD_MAP_POINT_PARAM_ST", 6)?
        .iter()
        .map(|row| {
            Some(MapPointRecord {
                row: row.id,
                map: map(row, 32)?,
                pos: position(row, 36)?,
                icon: row.field(12)?,
                cleared_flag: row.field(20)?,
                texts: texts(row)?,
            })
        })
        .collect()
}

pub fn subcategories(table: usize) -> Option<Vec<SubcategoryRecord>> {
    typed_rows(table, "BONFIRE_WARP_SUB_CATEGORY_PARAM_ST", 1)?
        .iter()
        .map(|row| {
            Some(SubcategoryRecord {
                row: row.id,
                text_id: row.field(4)?,
                tab: row.field(8)?,
                sort: row.field(10)?,
            })
        })
        .collect()
}

pub fn tabs(table: usize) -> Option<Vec<TabRecord>> {
    typed_rows(table, "BONFIRE_WARP_TAB_PARAM_ST", 2)?
        .iter()
        .map(|row| {
            Some(TabRecord {
                row: row.id,
                text_id: row.field(4)?,
                sort: row.field(8)?,
            })
        })
        .collect()
}

/// MAP_DEFAULT_INFO_PARAM_ST v6 uses decimal mAA_BB_CC_DD row IDs. This is a
/// resource inventory, not a Boss list; some maps legitimately have no EMEVD.
pub fn event_maps(table: usize) -> Option<Vec<[u8; 4]>> {
    let mut maps = Vec::new();
    for row in typed_rows(table, "MAP_DEFAULT_INFO_PARAM_ST", 6)? {
        if row.id == 0 {
            continue;
        }
        if row.id > 99_999_999 {
            return None;
        }
        maps.push([
            (row.id / 1_000_000) as u8,
            (row.id / 10_000 % 100) as u8,
            (row.id / 100 % 100) as u8,
            (row.id % 100) as u8,
        ]);
    }
    maps.sort_unstable();
    maps.dedup();
    Some(maps)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn table(kind: &str, version: u16, data: &[u8]) -> Vec<u8> {
        let mut bytes = vec![0; 16 + 0x80 + data.len() + kind.len() + 1];
        let length = bytes.len() - 16;
        bytes[..4].copy_from_slice(&(length as u32).to_le_bytes());
        let b = &mut bytes[16..];
        let strings = 0x80 + data.len();
        b[..4].copy_from_slice(&(strings as u32).to_le_bytes());
        b[8..10].copy_from_slice(&version.to_le_bytes());
        b[10..12].copy_from_slice(&1u16.to_le_bytes());
        b[16..24].copy_from_slice(&(strings as u64).to_le_bytes());
        b[45] = 0x85;
        b[46] = 2;
        b[0x40..0x44].copy_from_slice(&10000850u32.to_le_bytes());
        b[0x48..0x50].copy_from_slice(&0x80u64.to_le_bytes());
        b[0x80..strings].copy_from_slice(data);
        b[strings..strings + kind.len()].copy_from_slice(kind.as_bytes());
        bytes
    }

    #[test]
    fn explicit_area_fields_and_type_version_bounds() {
        let mut data = vec![0; 96];
        data[0x44..0x48].copy_from_slice(&10000850u32.to_le_bytes());
        data[0x48..0x4c].copy_from_slice(&(-16.94f32).to_le_bytes());
        data[0x54] = 10;
        let mut bytes = table("GAME_AREA_PARAM_ST", 3, &data);
        let ptr = bytes.as_ptr() as usize + 16;
        let area = &game_areas(ptr).unwrap()[0];
        assert_eq!(
            (area.row, area.flag, area.map),
            (10000850, 10000850, [10, 0, 0])
        );
        assert_eq!(area.pos[0], -16.94);
        assert!(bonfires(ptr).is_none());
        bytes[16 + 8] = 4;
        assert!(game_areas(ptr).is_none());
        let bad = table("GAME_AREA_PARAM_ST", 3, &data[..80]);
        assert!(game_areas(bad.as_ptr() as usize + 16).is_none());
        data[0x48..0x4c].copy_from_slice(&f32::NAN.to_le_bytes());
        let bad = table("GAME_AREA_PARAM_ST", 3, &data);
        assert!(game_areas(bad.as_ptr() as usize + 16).is_none());
    }

    #[test]
    fn play_region_repository_group_uses_the_existing_bounded_native_chain() {
        let mut fixture = crate::runtime_perf::NativeMemory::new();
        fixture.ptr(0x9000 + 0x88 + PLAY_REGION_GROUP as usize * 72, 0x8000);
        fixture.ptr(0x8080, 0x8200);
        fixture.ptr(0x8280, fixture.table);
        assert_eq!(
            fixture.service().find_param_table(PLAY_REGION_GROUP),
            fixture.bytes.as_ptr() as usize + fixture.table
        );
        fixture.bindings.layout.repository_groups = PLAY_REGION_GROUP;
        assert_eq!(fixture.service().find_param_table(PLAY_REGION_GROUP), 0);
    }

    #[test]
    fn play_region_fields_require_the_typed_v9_complete_row() {
        let mut data = vec![0; 272];
        data[20..24].copy_from_slice(&6100u32.to_le_bytes());
        data[172..176].copy_from_slice(&10000850u32.to_le_bytes());
        data[232..236].copy_from_slice(&1034500800u32.to_le_bytes());
        let bytes = table("PLAY_REGION_PARAM_ST", 9, &data);
        let rows = play_regions(bytes.as_ptr() as usize + 16).unwrap();
        assert_eq!((rows[0].row, rows[0].boss_area), (10000850, 6100));
        assert_eq!(rows[0].bosses[0], 10000850);
        assert_eq!(rows[0].bosses[15], 1034500800);
        assert!(rows[0].bosses[1..15].iter().all(|id| *id == 0));
        for (kind, version, body) in [
            ("GAME_AREA_PARAM_ST", 9, &data[..]),
            ("PLAY_REGION_PARAM_ST", 8, &data[..]),
            ("PLAY_REGION_PARAM_ST", 9, &data[..271]),
        ] {
            let invalid = table(kind, version, body);
            assert!(play_regions(invalid.as_ptr() as usize + 16).is_none());
        }
    }

    #[test]
    #[ignore = "requires explicitly exported original PARAM assets in EROVERLAY_BOSS_PARAM_DIR"]
    fn archived_param_assets_use_production_readers() {
        let root = std::path::PathBuf::from(std::env::var_os("EROVERLAY_BOSS_PARAM_DIR").unwrap());
        let read = |name: &str| {
            let body = std::fs::read(root.join(format!("{name}.param"))).unwrap();
            let mut native = vec![0; 16];
            native[..4].copy_from_slice(&(body.len() as u32).to_le_bytes());
            native.extend_from_slice(&body);
            native
        };
        let a = read("GameAreaParam");
        let areas = game_areas(a.as_ptr() as usize + 16).unwrap();
        assert!(
            areas
                .iter()
                .any(|a| a.flag == 10000850 && a.map == [10, 0, 0])
        );
        let flags: std::collections::HashSet<_> = areas
            .iter()
            .filter(|a| a.flag != 0)
            .map(|a| a.flag)
            .collect();
        let b = read("BonfireWarpParam");
        let graces = bonfires(b.as_ptr() as usize + 16).unwrap();
        let p = read("WorldMapPointParam");
        let points = map_points(p.as_ptr() as usize + 16).unwrap();
        let s = read("BonfireWarpSubCategoryParam");
        let sub = subcategories(s.as_ptr() as usize + 16).unwrap();
        let t = read("BonfireWarpTabParam");
        let tab = tabs(t.as_ptr() as usize + 16).unwrap();
        let m = read("MapDefaultInfoParam");
        let maps = event_maps(m.as_ptr() as usize + 16).unwrap();
        let r = read("PlayRegionParam");
        let regions = play_regions(r.as_ptr() as usize + 16).unwrap();
        assert_eq!(regions.len(), 593);
        assert!(regions.iter().any(|r| r.bosses.contains(&10000850)));
        assert!(maps.contains(&[61, 48, 38, 0]));
        assert!(
            graces
                .iter()
                .any(|g| sub.iter().any(|s| s.row == g.subcategory as u32))
        );
        assert!(
            sub.iter()
                .any(|s| tab.iter().any(|t| t.row == s.tab as u32))
        );
        println!(
            "areas={} nonzero_flags={} bonfires={} points={} subcategories={} tabs={} event_maps={} play_regions={}",
            areas.len(),
            flags.len(),
            graces.len(),
            points.len(),
            sub.len(),
            tab.len(),
            maps.len(),
            regions.len()
        );
    }
}
