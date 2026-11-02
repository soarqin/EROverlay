//! Update-only membership index for decompressed PC MSBE v73/v74 scenes.
//!
//! This proves scene membership, not whether a quest can reach an encounter.
//! A missing local actor is not proof of absence in another map or a proxy bar.
use crate::assets::{AssetError, range, wide_string};
use std::{collections::BTreeSet, ops::Range};

const MAX_BYTES: usize = 64 * 1024 * 1024;
const MAX_ENTRIES: usize = 262_144;
const TABLES: [&str; 6] = [
    "MODEL_PARAM_ST",
    "EVENT_PARAM_ST",
    "POINT_PARAM_ST",
    "ROUTE_PARAM_ST",
    "LAYER_PARAM_ST",
    "PARTS_PARAM_ST",
];
type Result<T> = std::result::Result<T, AssetError>;

/// Native mapstudio selection uses this area bitset, gated by
/// CS.DLC.IsEnableDlcFileMount. Areas outside it always use the base alias.
/// The switch is not read here: possible DLC2 areas retain both aliases, so
/// an actor in either is positive evidence and a failed alias stays unknown.
pub fn scene_mounts(map_id: u32) -> &'static [&'static str] {
    const DLC2_AREAS: u64 = 0x2000_0f00_1270_0000;
    let area = map_id >> 24;
    if area <= 61 && DLC2_AREAS & (1u64 << area) != 0 {
        &["mapstudio", "mapstudio_dlc2"]
    } else {
        &["mapstudio"]
    }
}

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum ActorKind {
    Enemy,
    DummyEnemy,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct SceneActor {
    pub kind: ActorKind,
    pub entity_id: u32,
    pub group_ids: [u32; 8],
    pub npc_param_id: i32,
    pub char_init_id: i32,
    pub activation_condition: i32,
    /// Raw GameEditionDisable enum: membership does not imply enabled gameplay.
    pub edition_disable: u32,
}

#[derive(Debug, Clone, Default)]
pub struct SceneIndex {
    pub actors: Vec<SceneActor>,
    /// Includes groups of non-character parts. Never treated as an actor.
    pub other_part_entities: BTreeSet<u32>,
    /// Regions belong to a different namespace from characters.
    pub region_entities: BTreeSet<u32>,
    pub part_count: usize,
    pub version: u32,
}
impl SceneIndex {
    /// Exact character or character-group membership, including DummyEnemy.
    /// Dummy membership is retained conservatively; it does not prove playability.
    pub fn contains_actor(&self, id: u32) -> bool {
        valid_id(id)
            && self
                .actors
                .iter()
                .any(|actor| actor.entity_id == id || actor.group_ids.contains(&id))
    }
}

fn valid_id(id: u32) -> bool {
    id != 0 && id != u32::MAX
}
fn u32_at(b: &[u8], p: usize) -> Result<u32> {
    Ok(u32::from_le_bytes(range(b, p, 4)?.try_into().unwrap()))
}
fn i32_at(b: &[u8], p: usize) -> Result<i32> {
    Ok(i32::from_le_bytes(range(b, p, 4)?.try_into().unwrap()))
}
fn offset_at(b: &[u8], p: usize) -> Result<usize> {
    usize::try_from(u64::from_le_bytes(range(b, p, 8)?.try_into().unwrap()))
        .map_err(|_| AssetError::Invalid)
}
fn relative(b: &[u8], p: usize, header: usize, size: usize) -> Result<usize> {
    let offset = offset_at(b, p)?;
    if offset < header {
        return Err(AssetError::Invalid);
    }
    range(b, offset, size)?;
    Ok(offset)
}
fn optional_relative(b: &[u8], p: usize, header: usize) -> Result<()> {
    if offset_at(b, p)? != 0 {
        relative(b, p, header, 1)?;
    }
    Ok(())
}
fn required_string(b: &[u8], p: usize, header: usize) -> Result<()> {
    let offset = relative(b, p, header, 2)?;
    wide_string(b, offset)?;
    Ok(())
}

struct Table {
    entries: Vec<Range<usize>>,
    next: usize,
    version: u32,
}
fn table(bytes: &[u8], start: usize, expected: &str, total: &mut usize) -> Result<Table> {
    if start & 7 != 0 || start < 16 {
        return Err(AssetError::Invalid);
    }
    let version = u32_at(bytes, start)?;
    if ![73, 74].contains(&version) {
        return Err(AssetError::Unsupported);
    }
    let count = u32_at(bytes, start + 4)? as usize;
    if count == 0 {
        return Err(AssetError::Invalid);
    }
    if count - 1 > MAX_ENTRIES || *total > MAX_ENTRIES - (count - 1) {
        return Err(AssetError::Budget);
    }
    *total += count - 1;
    let offsets_end = start
        .checked_add(16 + count * 8)
        .ok_or(AssetError::Invalid)?;
    range(bytes, start, offsets_end - start)?;
    let next = offset_at(bytes, offsets_end - 8)?;
    let end = if next == 0 { bytes.len() } else { next };
    if end <= offsets_end || end > bytes.len() || (next != 0 && next & 7 != 0) {
        return Err(AssetError::Invalid);
    }
    let name = offset_at(bytes, start + 8)?;
    if name < offsets_end || name >= end {
        return Err(AssetError::Invalid);
    }
    let mut offsets = Vec::with_capacity(count);
    let mut previous = name;
    for index in 0..count - 1 {
        let entry = offset_at(bytes, start + 16 + index * 8)?;
        if entry <= previous || entry & 7 != 0 || entry >= end {
            return Err(AssetError::Invalid);
        }
        offsets.push(entry);
        previous = entry;
    }
    let name_end = offsets.first().copied().unwrap_or(end);
    if wide_string(&bytes[..name_end], name)? != expected {
        return Err(AssetError::Invalid);
    }
    offsets.push(end);
    let entries: Vec<_> = offsets.windows(2).map(|pair| pair[0]..pair[1]).collect();
    // Every MSBE entry starts with a relative name pointer. Bound this even
    // for tables whose contents are otherwise not needed by the actor index.
    for entry in &entries {
        required_string(&bytes[entry.clone()], 0, 8)?;
    }
    Ok(Table {
        entries,
        next,
        version,
    })
}

/// Reads only the common part/region fields and EnemyBase layout verified
/// against SoulsFormats MSBE. No files, decompression or engine calls occur.
/// Unknown versions/types fail without returning a partial "missing" index.
pub fn parse_msb(bytes: &[u8]) -> Result<SceneIndex> {
    if bytes.len() > MAX_BYTES {
        return Err(AssetError::Budget);
    }
    if range(bytes, 0, 16)? != b"MSB \x01\0\0\0\x10\0\0\0\0\0\x01\xff" {
        return Err(AssetError::Unsupported);
    }
    let mut next = 16;
    let mut total = 0;
    let mut tables = Vec::with_capacity(TABLES.len());
    for name in TABLES {
        let current = table(bytes, next, name, &mut total)?;
        next = current.next;
        tables.push(current);
    }
    if tables
        .iter()
        .any(|table| table.version != tables[0].version)
    {
        return Err(AssetError::Unsupported);
    }
    if next != 0 || !tables[4].entries.is_empty() {
        return Err(AssetError::Invalid);
    }
    let mut result = SceneIndex {
        part_count: tables[5].entries.len(),
        version: tables[0].version,
        ..SceneIndex::default()
    };
    for entry in &tables[5].entries {
        let b = &bytes[entry.clone()];
        range(b, 0, 0xc0)?;
        let kind = u32_at(b, 0x0c)?;
        if ![0, 2, 4, 5, 9, 10, 11, 13].contains(&kind) {
            return Err(AssetError::Unsupported);
        }
        if u32_at(b, 0x4c)? != 0 || range(b, 0xa8, 24)? != [0; 24] {
            return Err(AssetError::Invalid);
        }
        required_string(b, 0, 0xc0)?;
        required_string(b, 0x18, 0xc0)?;
        for p in (0x50..=0xa0).step_by(8) {
            optional_relative(b, p, 0xc0)?;
        }
        let entity = relative(b, 0x60, 0xc0, 0x40)?;
        let data = relative(b, 0x68, 0xc0, if matches!(kind, 2 | 10) { 0xc0 } else { 1 })?;
        let entity_id = u32_at(b, entity)?;
        let mut group_ids = [0; 8];
        for (index, id) in group_ids.iter_mut().enumerate() {
            *id = u32_at(b, entity + 0x1c + index * 4)?;
        }
        if matches!(kind, 2 | 10) {
            if u32_at(b, 0x44)? > 3 {
                return Err(AssetError::Unsupported);
            }
            if range(b, data, 8)? != [0; 8]
                || b[data + 0x14] != 0
                || b[data + 0x15] > 1
                || range(b, data + 0x22, 2)? != [0; 2]
                || u32_at(b, data + 0x30)? != 0
                || range(b, data + 0x50, 40)? != [0; 40]
                || range(b, data + 0x78, 8)? != 0x80u64.to_le_bytes()
                || u32_at(b, data + 0x80)? != 0
                || range(b, data + 0xb0, 16)? != [0; 16]
            {
                return Err(AssetError::Invalid);
            }
            for p in (data + 0x88..data + 0xb0).step_by(8) {
                if range(b, p, 8)? != [0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 10, 0] {
                    return Err(AssetError::Invalid);
                }
            }
            result.actors.push(SceneActor {
                kind: if kind == 2 {
                    ActorKind::Enemy
                } else {
                    ActorKind::DummyEnemy
                },
                entity_id,
                group_ids,
                npc_param_id: i32_at(b, data + 0x0c)?,
                char_init_id: i32_at(b, data + 0x18)?,
                activation_condition: i32_at(b, data + 0x2c)?,
                edition_disable: u32_at(b, 0x44)?,
            });
        } else {
            result.other_part_entities.extend(
                std::iter::once(entity_id)
                    .chain(group_ids)
                    .filter(|id| valid_id(*id)),
            );
        }
    }
    for entry in &tables[2].entries {
        let b = &bytes[entry.clone()];
        range(b, 0, 0x68)?;
        required_string(b, 0, 0x68)?;
        for p in [0x30, 0x38] {
            let offset = relative(b, p, 0x68, 2)?;
            let count = u16::from_le_bytes(range(b, offset, 2)?.try_into().unwrap()) as usize;
            if count > i16::MAX as usize {
                return Err(AssetError::Invalid);
            }
            range(b, offset + 2, count * 2)?;
        }
        optional_relative(b, 0x48, 0x68)?;
        optional_relative(b, 0x58, 0x68)?;
        relative(b, 0x60, 0x68, 0x20)?;
        let entity = relative(b, 0x50, 0x68, 16)?;
        if range(b, entity + 9, 7)? != [0; 7] {
            return Err(AssetError::Invalid);
        }
        let id = u32_at(b, entity + 4)?;
        if valid_id(id) {
            result.region_entities.insert(id);
        }
    }
    Ok(result)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn native_area_bitset_limits_uncertain_dlc_aliases() {
        let conditional = [20, 21, 22, 25, 28, 40, 41, 42, 43, 61];
        for area in 0..=255u32 {
            for suffix in [0, 0x00ff_ffff] {
                let mounts = scene_mounts((area << 24) | suffix);
                assert_eq!(mounts[0], "mapstudio");
                assert_eq!(
                    mounts.len(),
                    if conditional.contains(&area) { 2 } else { 1 }
                );
                if mounts.len() == 2 {
                    assert_eq!(mounts[1], "mapstudio_dlc2");
                }
            }
        }
        assert_eq!(
            scene_mounts(u32::from_be_bytes([31, 0, 0, 0])),
            &["mapstudio"]
        );
        assert_eq!(
            scene_mounts(u32::from_be_bytes([60, 38, 54, 0])),
            &["mapstudio"]
        );
        assert_eq!(scene_mounts(u32::from_be_bytes([61, 49, 44, 0])).len(), 2);
    }

    fn put32(b: &mut [u8], p: usize, value: u32) {
        b[p..p + 4].copy_from_slice(&value.to_le_bytes());
    }
    fn put64(b: &mut [u8], p: usize, value: usize) {
        b[p..p + 8].copy_from_slice(&(value as u64).to_le_bytes());
    }
    fn part(kind: u32, entity: u32, group: u32) -> Vec<u8> {
        let mut b = vec![0; 0x200];
        put64(&mut b, 0, 0xc0);
        put32(&mut b, 0x0c, kind);
        put64(&mut b, 0x18, 0xc0);
        put64(&mut b, 0x60, 0xc8);
        put64(&mut b, 0x68, 0x108);
        put32(&mut b, 0xc8, entity);
        put32(&mut b, 0xc8 + 0x1c, group);
        put32(&mut b, 0x108 + 0x0c, 1234);
        put64(&mut b, 0x108 + 0x78, 0x80);
        for p in (0x108 + 0x88..0x108 + 0xb0).step_by(8) {
            b[p..p + 8].copy_from_slice(&[0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 10, 0]);
        }
        b.resize(0x1c8, 0);
        b
    }
    fn region(entity: u32) -> Vec<u8> {
        let mut b = vec![0; 0xa0];
        for p in [0, 0x30, 0x38] {
            put64(&mut b, p, 0x68);
        }
        put64(&mut b, 0x50, 0x70);
        put64(&mut b, 0x60, 0x80);
        put32(&mut b, 0x74, entity);
        b
    }
    fn fixture() -> Vec<u8> {
        let mut b = b"MSB \x01\0\0\0\x10\0\0\0\0\0\x01\xff".to_vec();
        let mut previous = None;
        for (index, name) in TABLES.iter().enumerate() {
            let entries = match index {
                2 => vec![region(77)],
                5 => vec![part(2, 10, 11), part(10, 20, 21), part(13, 30, 31)],
                _ => Vec::new(),
            };
            let start = b.len();
            if let Some(previous) = previous {
                put64(&mut b, previous, start);
            }
            b.resize(start + 24 + entries.len() * 8, 0);
            put32(&mut b, start, 73);
            put32(&mut b, start + 4, entries.len() as u32 + 1);
            let name_offset = b.len();
            put64(&mut b, start + 8, name_offset);
            b.extend(name.encode_utf16().chain([0]).flat_map(u16::to_le_bytes));
            b.resize(b.len().next_multiple_of(8), 0);
            previous = Some(start + 16 + entries.len() * 8);
            for (entry_index, entry) in entries.iter().enumerate() {
                let offset = b.len();
                put64(&mut b, start + 16 + entry_index * 8, offset);
                b.extend(entry);
                b.resize(b.len().next_multiple_of(8), 0);
            }
        }
        b
    }

    #[test]
    fn actors_groups_dummy_and_regions_remain_distinct() {
        let scene = parse_msb(&fixture()).unwrap();
        assert_eq!(scene.part_count, 3);
        assert_eq!(scene.actors.len(), 2);
        assert_eq!(scene.actors[0].npc_param_id, 1234);
        assert_eq!(scene.actors[1].kind, ActorKind::DummyEnemy);
        for id in [10, 11, 20, 21] {
            assert!(scene.contains_actor(id));
        }
        assert_eq!(scene.other_part_entities, BTreeSet::from([30, 31]));
        assert_eq!(scene.region_entities, BTreeSet::from([77]));
        for id in [0, u32::MAX, 30, 31, 77, 99] {
            assert!(!scene.contains_actor(id));
        }
    }

    #[test]
    fn validated_dlc_layout_and_unknown_layout_failures() {
        let mut b = fixture();
        let mut next = 16;
        let mut total = 0;
        for name in TABLES {
            let t = table(&b, next, name, &mut total).unwrap();
            put32(&mut b, next, 74);
            next = t.next;
        }
        let scene = parse_msb(&b).unwrap();
        assert_eq!(scene.version, 74);
        assert!(scene.contains_actor(10));
        put32(&mut b, 16, 73);
        assert_eq!(parse_msb(&b).unwrap_err(), AssetError::Unsupported);

        let original = fixture();
        let mut next = 16;
        let mut total = 0;
        for name in TABLES {
            let t = table(&original, next, name, &mut total).unwrap();
            if name == "PARTS_PARAM_ST" {
                let p = t.entries[0].start;
                let mut b = original.clone();
                put32(&mut b, p + 0x0c, 99);
                assert_eq!(parse_msb(&b).unwrap_err(), AssetError::Unsupported);
                for value in [0, 0x20, usize::MAX] {
                    let mut b = original.clone();
                    put64(&mut b, p + 0x60, value);
                    assert!(parse_msb(&b).is_err());
                }
                break;
            }
            next = t.next;
        }
    }

    #[test]
    fn rejects_bad_versions_tables_counts_and_overlapping_offsets() {
        let original = fixture();
        let mut b = original.clone();
        put32(&mut b, 16, 75);
        assert_eq!(parse_msb(&b).unwrap_err(), AssetError::Unsupported);
        b = original.clone();
        put32(&mut b, 20, u32::MAX);
        assert_eq!(parse_msb(&b).unwrap_err(), AssetError::Budget);
        b = original.clone();
        put64(&mut b, 32, 16);
        assert_eq!(parse_msb(&b).unwrap_err(), AssetError::Invalid);
        b = original.clone();
        let name = offset_at(&b, 24).unwrap();
        b[name] = b'X';
        assert_eq!(parse_msb(&b).unwrap_err(), AssetError::Invalid);
        b = original.clone();
        let mut total = 0;
        let mut next = 16;
        for name in TABLES {
            let t = table(&b, next, name, &mut total).unwrap();
            if name == "PARTS_PARAM_ST" {
                let p = t.entries[0].start;
                put64(&mut b, p + 0x60, 0x1b0);
                assert!(parse_msb(&b).is_err());
                break;
            }
            next = t.next;
        }
        for length in [0, 15, 23, original.len() - 0x120] {
            assert!(parse_msb(&original[..length]).is_err());
        }
    }

    /// Explicit optional input: .msb plus same-name .expected.tsv generated
    /// independently from SoulsFormats. Never relies on installed game files.
    #[test]
    fn optional_soulsformats_membership_differential() {
        let Some(root) = std::env::var_os("EROVERLAY_BOSS_SCENE_FIXTURES") else {
            return;
        };
        let mut checked = 0;
        for entry in std::fs::read_dir(root).unwrap() {
            let path = entry.unwrap().path();
            if path.extension().is_none_or(|ext| ext != "msb") {
                continue;
            }
            let expected = path.with_extension("expected.tsv");
            if !expected.exists() {
                continue;
            }
            let scene = parse_msb(&std::fs::read(&path).unwrap())
                .unwrap_or_else(|error| panic!("{}: {error:?}", path.display()));
            let mut actual = Vec::new();
            for actor in &scene.actors {
                actual.push(format!(
                    "{:?}\t{}\t{}\t{}\t{}\t{}\t{}",
                    actor.kind,
                    actor.entity_id,
                    actor.group_ids.map(|id| id.to_string()).join(","),
                    actor.npc_param_id,
                    actor.char_init_id,
                    actor.activation_condition,
                    actor.edition_disable,
                ));
            }
            actual.extend(
                scene
                    .other_part_entities
                    .iter()
                    .map(|id| format!("Other\t{id}")),
            );
            actual.extend(
                scene
                    .region_entities
                    .iter()
                    .map(|id| format!("Region\t{id}")),
            );
            actual.sort();
            let expected = std::fs::read_to_string(expected).unwrap();
            let mut expected: Vec<_> = expected.lines().map(str::to_owned).collect();
            expected.sort();
            assert_eq!(actual, expected, "{}", path.display());
            checked += 1;
        }
        assert!(checked > 0, "no .msb/.expected.tsv differential pairs");
    }
}
