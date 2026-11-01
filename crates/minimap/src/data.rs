//! Game state is copied on update; no game-owned memory reaches the renderer.
use crate::goblins::{Goblins, RuntimeVisuals};
use crate::host::Host;
use eroverlay_abi::{GameLayout, MapState};
use std::{
    collections::{HashMap, HashSet},
    sync::Arc,
    time::{Duration, Instant},
};

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Source {
    Grace,
    Landmark,
    Other,
}
#[derive(Clone, Debug)]
pub struct Decoration {
    pub id: u64,
    pub icon: u32,
    pub maps: u32,
    pub x: f32,
    pub y: f32,
    pub angle: f32,
    pub source: Source,
    pub area: bool,
    pub cleared: bool,
    pub runtime: bool,
}
#[derive(Clone, Copy, Debug)]
pub struct SavedMarker {
    pub x: f32,
    pub y: f32,
    pub number: u8,
    pub map: u8,
}
#[derive(Clone, Default)]
pub struct MapData {
    pub state: MapState,
    pub valid: bool,
    pub roundtable: bool,
    pub home_icon: u32,
    pub decorations: Arc<Vec<Decoration>>,
    pub beacons: Vec<SavedMarker>,
    pub memos: Vec<SavedMarker>,
    pub runtime: Arc<RuntimeVisuals>,
}

fn u16_at(bytes: &[u8], offset: usize) -> Option<u16> {
    Some(u16::from_le_bytes(
        bytes.get(offset..offset + 2)?.try_into().ok()?,
    ))
}
fn u32_at(bytes: &[u8], offset: usize) -> Option<u32> {
    Some(u32::from_le_bytes(
        bytes.get(offset..offset + 4)?.try_into().ok()?,
    ))
}
fn u64_at(bytes: &[u8], offset: usize) -> Option<u64> {
    Some(u64::from_le_bytes(
        bytes.get(offset..offset + 8)?.try_into().ok()?,
    ))
}
fn f32_at(bytes: &[u8], offset: usize) -> Option<f32> {
    Some(f32::from_bits(u32_at(bytes, offset)?))
}
struct Row {
    id: u32,
    bytes: Vec<u8>,
}

fn rows(
    host: Host,
    group: u32,
    minimum: usize,
    maximum: usize,
    wanted: Option<u32>,
) -> Option<Vec<Row>> {
    let table = host.param(group);
    let header = host.bytes(table, 48)?;
    if header[0x2c] != 0 {
        return None;
    }
    let count = u16_at(&header, 0xa)? as usize;
    let format = header[0x2d] & 0x7f;
    if count == 0 || count > 20_000 || !(2..=5).contains(&format) {
        return None;
    }
    let start = if format == 2 { 0x30 } else { 0x40 };
    let wide = format >= 4 && header[0x2e] & 2 != 0 && (format == 4 || header[0x2d] & 0x80 != 0);
    let stride = if wide { 24 } else { 12 };
    let end = start + count * stride;
    let size = table
        .checked_sub(16)
        .and_then(|p| host.u32(p))
        .map(|v| v as usize)
        .filter(|n| *n >= end && *n <= 0x1000_0000);
    let snapshot = size
        .filter(|_| wanted.is_none())
        .filter(|n| *n <= 4 * 1024 * 1024)
        .and_then(|n| host.bytes(table, n));
    let directory = if let Some(bytes) = &snapshot {
        bytes.get(start..end)?.to_vec()
    } else {
        host.bytes(table.checked_add(start)?, count * stride)?
    };
    let mut entries = Vec::with_capacity(count);
    let mut boundaries = Vec::with_capacity(count * 2 + 2);
    if let Some(size) = size {
        boundaries.push(size)
    }
    let strings = u32_at(&header, 0)? as usize;
    if strings >= end && strings <= 0x1000_0000 {
        boundaries.push(strings)
    }
    for i in 0..count {
        let p = i * stride;
        let id = u32_at(&directory, p)?;
        let (offset, name) = if wide {
            (
                u64_at(&directory, p + 8)? as usize,
                u64_at(&directory, p + 16)? as usize,
            )
        } else {
            (
                u32_at(&directory, p + 4)? as usize,
                u32_at(&directory, p + 8)? as usize,
            )
        };
        if offset < end || offset > 0x1000_0000 || name > 0x1000_0000 || (name != 0 && name < end) {
            return None;
        }
        entries.push((id, offset));
        boundaries.push(offset);
        if name != 0 {
            boundaries.push(name)
        }
    }
    boundaries.sort_unstable();
    boundaries.dedup();
    let mut selected = Vec::with_capacity(if wanted.is_some() { 1 } else { count });
    for (id, offset) in entries {
        if wanted.is_some_and(|wanted| id != wanted) {
            continue;
        }
        let next = boundaries.partition_point(|b| *b <= offset);
        let Some(end) = boundaries.get(next) else {
            continue;
        };
        let size = end - offset;
        if size < minimum {
            continue;
        }
        let n = size.min(maximum);
        selected.push((id, offset, n));
    }
    // Large/unsized tables cannot use the whole-file snapshot. Copy nearby row
    // prefixes together instead of crossing the host/OS boundary once per row.
    // A gap no larger than one row prefix bounds over-copying; every block stays
    // within Host::bytes' existing 4MiB limit. Original directory order survives.
    let mut spans: Vec<(usize, usize, Option<Vec<u8>>)> = Vec::new();
    if snapshot.is_none() && wanted.is_none() {
        let mut sorted = selected
            .iter()
            .map(|(_, offset, n)| (*offset, *n))
            .collect::<Vec<_>>();
        sorted.sort_unstable();
        for (offset, n) in sorted {
            if let Some((start, end, _)) = spans.last_mut()
                && offset <= end.saturating_add(maximum)
                && offset + n - *start <= 4 * 1024 * 1024
            {
                *end = (*end).max(offset + n);
            } else {
                spans.push((offset, offset + n, None));
            }
        }
        for (start, end, bytes) in &mut spans {
            *bytes = host.bytes(table.checked_add(*start)?, *end - *start);
        }
    }
    let mut rows = Vec::with_capacity(selected.len());
    for (id, offset, n) in selected {
        let source = snapshot
            .as_ref()
            .and_then(|bytes| bytes.get(offset..offset + n))
            .or_else(|| {
                let index = spans
                    .partition_point(|(start, _, _)| *start <= offset)
                    .checked_sub(1)?;
                let (start, _, bytes) = &spans[index];
                bytes.as_ref()?.get(offset - *start..offset - *start + n)
            });
        // A partial snapshot must not discard other readable rows (C++ load()).
        let bytes = source
            .map(<[u8]>::to_vec)
            .or_else(|| host.bytes(table.checked_add(offset)?, n));
        if let Some(bytes) = bytes {
            rows.push(Row { id, bytes });
        }
    }
    Some(rows)
}

fn saved_markers(
    host: Host,
    view: usize,
    offset: usize,
    capacity: usize,
    beacons: bool,
) -> Vec<SavedMarker> {
    let sample = || -> Option<Vec<SavedMarker>> {
        let save = host.pointer(view.checked_add(offset)?)?;
        let slots = host.pointer(save + 8)?;
        let count = host.u64(save + 0x40)?;
        let cap = host.u64(save + 16)? as usize;
        if cap == 0 || cap > capacity || count > cap as u64 {
            return None;
        }
        let first = host.bytes(slots, cap * 16)?;
        let second = host.bytes(slots, cap * 16)?;
        if first != second
            || host.pointer(view + offset)? != save
            || host.pointer(save + 8)? != slots
            || host.u64(save + 16)? as usize != cap
            || host.u64(save + 0x40)? != count
        {
            return None;
        }
        if first
            .as_chunks::<16>()
            .0
            .iter()
            .filter(|b| u32_at(b.as_slice(), 0).is_some_and(|id| id as i32 >= 0))
            .count() as u64
            != count
        {
            return None;
        }
        let mut result = Vec::with_capacity(if beacons { 5 } else { 100 });
        for (slot, record) in first
            .as_chunks::<16>()
            .0
            .iter()
            .enumerate()
            .take(if beacons { 5 } else { 100 })
        {
            let id = u32_at(record, 0)? as i32;
            let x = f32_at(record, 4)?;
            let y = f32_at(record, 8)?;
            let map = record[12];
            let icon = record[13];
            if id >= 0
                && x.is_finite()
                && y.is_finite()
                && [0, 1, 10].contains(&map)
                && if beacons {
                    icon == 1
                } else {
                    (1..=10).contains(&icon)
                }
            {
                result.push(SavedMarker {
                    x,
                    y,
                    map,
                    number: if beacons { (slot + 1) as u8 } else { icon },
                });
            }
        }
        Some(result)
    };
    sample().unwrap_or_default()
}

#[derive(Clone, Copy)]
struct Conversion {
    target: u32,
    offset: [f32; 3],
}
struct Converter {
    origin: [u8; 4],
    settings: [f32; 6],
    head: usize,
}
type Conversions = HashMap<(usize, u32), Option<Conversion>>;
fn converters(host: Host, view: usize, map_mask: u32) -> Vec<Converter> {
    let mut result = Vec::new();
    let count = host.u64(view + 0x280).unwrap_or(0);
    if count > 8 {
        return result;
    }
    for i in 0..count as usize {
        let address = view + 0xf8 + i * 48;
        let Some(origin) = host.bytes(address + 8, 4) else {
            continue;
        };
        if origin[3] != 60 && (origin[3] != 61 || map_mask & 4 == 0) {
            continue;
        }
        let Some(settings) = host.bytes(address + 12, 24) else {
            continue;
        };
        let mut values = [0.0; 6];
        for (i, v) in values.iter_mut().enumerate() {
            *v = f32_at(&settings, i * 4).unwrap_or(f32::NAN);
        }
        let head = host
            .pointer(address + 40)
            .and_then(|p| host.pointer(p + 16))
            .unwrap_or(0);
        result.push(Converter {
            origin: origin.try_into().unwrap(),
            settings: values,
            head,
        });
    }
    result
}
fn lookup(host: Host, head: usize, raw: u32) -> Option<Option<Conversion>> {
    let mut node = host.pointer(head + 8)?;
    let mut candidate = head;
    for _ in 0..64 {
        if host.u8(node + 25)? != 0 {
            if candidate == head {
                return Some(None);
            }
            if host.u32(candidate + 28)? != raw {
                return Some(None);
            }
            let target = host.u32(candidate + 32)?;
            let bytes = host.bytes(candidate + 36, 12)?;
            return Some(Some(Conversion {
                target,
                offset: [f32_at(&bytes, 0)?, f32_at(&bytes, 4)?, f32_at(&bytes, 8)?],
            }));
        }
        let key = host.u32(node + 28)?;
        if key >= raw {
            candidate = node;
            node = host.pointer(node)?;
        } else {
            node = host.pointer(node + 16)?;
        }
    }
    None
}
fn convert(
    host: Host,
    converters: &[Converter],
    cache: &mut Conversions,
    raw: u32,
    x: f32,
    z: f32,
) -> Option<[f32; 2]> {
    for converter in converters {
        let (mut target, mut px, mut pz) = (raw, x, z);
        if ![60, 61].contains(&(raw >> 24)) {
            if converter.head == 0 {
                continue;
            }
            let value = if let Some(value) = cache.get(&(converter.head, raw)) {
                *value
            } else {
                let Some(value) = lookup(host, converter.head, raw) else {
                    continue;
                };
                cache.insert((converter.head, raw), value);
                value
            };
            let Some(value) = value else { continue };
            target = value.target;
            px += value.offset[0];
            pz += value.offset[2];
        }
        let origin = converter.origin;
        if target >> 24 != u32::from(origin[3]) {
            continue;
        }
        let s = converter.settings;
        let x = (px + (((target >> 16) & 255) as i32 - i32::from(origin[2])) as f32 * 256.0 - s[0])
            * s[5]
            + s[3];
        let y = -(pz + (((target >> 8) & 255) as i32 - i32::from(origin[1])) as f32 * 256.0 - s[2])
            * s[5]
            + s[4];
        if x.is_finite() && y.is_finite() {
            return Some([x, y]);
        }
    }
    None
}
fn raw_map(row: &[u8], offset: usize) -> u32 {
    (u32::from(row[offset]) << 24)
        | (u32::from(row[offset + 1]) << 16)
        | (u32::from(row[offset + 2]) << 8)
}
struct Flags {
    host: Host,
    values: HashMap<u32, bool>,
}
impl Flags {
    fn new(host: Host) -> Self {
        Self {
            host,
            values: HashMap::new(),
        }
    }
    fn flag(&mut self, id: u32) -> bool {
        if id == 0 || id == u32::MAX {
            return false;
        }
        if let Some(value) = self.values.get(&id) {
            return *value;
        }
        let value = self.host.flag(id);
        if self.values.len() < 16_384 {
            self.values.insert(id, value);
        }
        value
    }
    fn enabled(&mut self, id: u32) -> bool {
        id == 0 || id == u32::MAX || self.flag(id)
    }
    fn cleared(&mut self, id: u32) -> bool {
        id != 0 && id != u32::MAX && self.flag(id)
    }
}
fn text_active(
    flags: &mut Flags,
    row: &[u8],
    index: usize,
    second_enable: usize,
    second_disable: usize,
) -> bool {
    if u32_at(row, 48 + index * 12).is_none_or(|v| (v as i32) < 0) {
        return false;
    }
    let first = u32_at(row, 52 + index * 12).unwrap_or(0);
    let second = u32_at(row, second_enable + index * 4).unwrap_or(0);
    if !flags.enabled(first) || !flags.enabled(second) {
        return false;
    }
    let mut disables = 0;
    for id in [
        u32_at(row, 56 + index * 12).unwrap_or(0),
        u32_at(row, second_disable + index * 4).unwrap_or(0),
    ] {
        if id == 0 || id == u32::MAX {
            continue;
        }
        if !flags.flag(id) {
            return true;
        }
        disables += 1;
    }
    disables == 0
}
fn alternate(flags: &mut Flags, row: &[u8], enable: usize, disable: usize) -> bool {
    (0..8).any(|i| row.get(144 + i) == Some(&1) && text_active(flags, row, i, enable, disable))
}
fn grace_icons(host: Host, view: usize, layout: GameLayout) -> HashMap<u64, (u32, u32)> {
    let mut result = HashMap::new();
    let (Some(start), Some(end)) = (host.pointer(view + 0x2e8), host.pointer(view + 0x2f0)) else {
        return result;
    };
    let stride = layout.grace_stride as usize;
    if end < start || stride == 0 || (end - start) % stride != 0 || (end - start) / stride >= 2000 {
        return result;
    }
    let Some(bytes) = host.bytes(start, end - start) else {
        return result;
    };
    for entry in bytes.chunks_exact(stride) {
        let Some(id) = u32_at(entry, 0x238) else {
            continue;
        };
        let normal = entry
            .get(layout.grace_normal_offset as usize)
            .copied()
            .unwrap_or(1)
            != 0;
        let offset = if normal { 0x248 } else { 0x288 };
        if let Some(base) = u32_at(entry, offset) {
            let alt = if layout.alternate_icons != 0 {
                u32_at(entry, offset + 0x80).unwrap_or(0)
            } else {
                0
            };
            result.insert(u64::from(id), (base, alt));
        }
    }
    result
}

pub struct Data {
    host: Host,
    previous: MapData,
    refresh: Instant,
    others: bool,
    conversions: Conversions,
    generation: u64,
    goblins: Goblins,
    failed_until: Option<Instant>,
    reported: &'static str,
}
impl Data {
    pub fn reset(&mut self) {
        self.goblins.reset();
        self.previous = MapData::default();
        self.refresh = Instant::now();
        self.failed_until = None;
        self.reported = "";
    }
    pub fn new(host: Host) -> Self {
        Self {
            host,
            previous: MapData::default(),
            refresh: Instant::now(),
            others: false,
            conversions: HashMap::new(),
            generation: 0,
            goblins: Goblins::new(host),
            failed_until: None,
            reported: "",
        }
    }
    pub fn update(&mut self, show_others: bool) -> MapData {
        let host = self.host;
        if !host.visible() {
            self.report("minimap-data phase=display-hidden\n");
            return MapData::default();
        }
        if self
            .failed_until
            .is_some_and(|until| Instant::now() < until)
        {
            self.report("minimap-data phase=marker-build-backoff\n");
            return MapData::default();
        }
        let Some(state) = host.map() else {
            self.report("minimap-data phase=initial-map-unavailable\n");
            return MapData::default();
        };
        self.failed_until = None;
        let mut next = MapData {
            state,
            valid: true,
            home_icon: 48,
            runtime: self.goblins.visuals.clone(),
            ..Default::default()
        };
        let reused = Instant::now() < self.refresh
            && self.others == show_others
            && self.previous.valid
            && self.previous.state.generation == state.generation
            && self.previous.state.raw_map_id == state.raw_map_id
            && self.previous.state.view_model == state.view_model;
        if reused {
            next.decorations = self.previous.decorations.clone();
            next.roundtable = self.previous.roundtable;
            next.home_icon = self.previous.home_icon;
        } else {
            self.refresh = Instant::now() + Duration::from_millis(200);
            self.others = show_others;
            if state.generation != self.generation || self.conversions.len() > 4096 {
                self.conversions.clear();
                self.generation = state.generation;
            }
            let layout = host.layout();
            let converters = converters(host, state.view_model, layout.map_mask);
            let icons = grace_icons(host, state.view_model, layout);
            let mut events = Flags::new(host);
            let home = rows(host, 141, 0x27c, 0x27c, Some(0))
                .and_then(|rows| rows.into_iter().find(|r| r.id == 0))
                .and_then(|r| u32_at(&r.bytes, 0x278))
                .unwrap_or(0);
            let mut decorations = Vec::with_capacity(self.previous.decorations.len());
            for row in rows(host, 43, 48, 236, None).unwrap_or_default() {
                let r = &row.bytes;
                let is_home = row.id == home;
                let discovered = events.flag(u32_at(r, 4).unwrap_or(0));
                if !is_home && !discovered {
                    continue;
                }
                let raw = raw_map(r, 32);
                let mut selected = u32::from(u16_at(r, 28).unwrap_or(0));
                let alt = layout.alternate_icons != 0
                    && r.len() >= 236
                    && alternate(&mut events, r, 168, 200);
                if let Some((normal, alternate)) = icons.get(&u64::from(row.id)) {
                    selected = if alt { *alternate } else { *normal };
                } else if alt {
                    let alternate = u32::from(u16_at(r, 232).unwrap_or(0));
                    if alternate != 0 {
                        selected = alternate;
                    }
                }
                if is_home {
                    next.roundtable = state.raw_map_id == raw;
                    next.home_icon = selected;
                }
                if !discovered || is_home {
                    continue;
                }
                let maps = u32::from(r[30] & 7) & layout.map_mask;
                if maps == 0 || selected == 0 {
                    continue;
                }
                let Some([x, y]) = convert(
                    host,
                    &converters,
                    &mut self.conversions,
                    raw,
                    f32_at(r, 36).unwrap_or(f32::NAN),
                    f32_at(r, 44).unwrap_or(f32::NAN),
                ) else {
                    continue;
                };
                decorations.push(Decoration {
                    id: u64::from(row.id),
                    icon: selected,
                    maps,
                    x,
                    y,
                    angle: 0.0,
                    source: Source::Grace,
                    area: false,
                    cleared: events.cleared(u32_at(r, 24).unwrap_or(0)),
                    runtime: false,
                });
            }
            let start = decorations.len();
            for row in rows(host, 87, 168, 256, None).unwrap_or_default() {
                let r = &row.bytes;
                let base = u32::from(u16_at(r, 12).unwrap_or(0));
                let distant_icon = u32::from(u16_at(r, 26).unwrap_or(0));
                let alt_icon = u32::from(u16_at(r, 184).unwrap_or(0));
                let other = row.id < 78500 || base == 80;
                if other && !show_others {
                    continue;
                }
                if base == 0 && distant_icon == 0 && (layout.alternate_icons == 0 || alt_icon == 0)
                {
                    continue;
                }
                let maps = u32::from(r[24] & 7) & layout.map_mask;
                if maps == 0 {
                    continue;
                }
                let opened = events.enabled(u32_at(r, 4).unwrap_or(0));
                let distant = !opened && events.flag(u32_at(r, 8).unwrap_or(0));
                if !opened && !distant {
                    continue;
                }
                let flags = r[16];
                let area = flags & 1 != 0;
                if !area
                    && flags & 4 == 0
                    && !(0..8).any(|i| text_active(&mut events, r, i, 192, 224))
                {
                    continue;
                }
                let selected = if !opened && distant && distant_icon != 0 {
                    distant_icon
                } else if layout.alternate_icons != 0
                    && r.len() >= 256
                    && alternate(&mut events, r, 192, 224)
                    && alt_icon != 0
                {
                    alt_icon
                } else {
                    base
                };
                let (raw, x, z) = if distant && flags & 2 != 0 {
                    (
                        raw_map(r, 17),
                        f32_at(r, 156).unwrap_or(f32::NAN),
                        f32_at(r, 164).unwrap_or(f32::NAN),
                    )
                } else {
                    (
                        raw_map(r, 32),
                        f32_at(r, 36).unwrap_or(f32::NAN),
                        f32_at(r, 44).unwrap_or(f32::NAN),
                    )
                };
                if selected == 0 {
                    continue;
                }
                let Some([x, y]) = convert(host, &converters, &mut self.conversions, raw, x, z)
                else {
                    continue;
                };
                decorations.push(Decoration {
                    id: u64::from(row.id),
                    icon: selected,
                    maps,
                    x,
                    y,
                    angle: f32_at(r, 28).unwrap_or(0.0).to_radians(),
                    source: if other {
                        Source::Other
                    } else {
                        Source::Landmark
                    },
                    area,
                    cleared: events.cleared(u32_at(r, 20).unwrap_or(0)),
                    runtime: false,
                });
            }
            decorations[start..].sort_unstable_by_key(|m| std::cmp::Reverse(m.id));
            if show_others && !next.roundtable && (state.map_id == 0 || state.map_id == 10) {
                let layer = if state.map_id == 10 {
                    2
                } else {
                    u32::from(state.underground & 1 != 0)
                };
                for point in self.goblins.points(layer) {
                    if let Some([x, y]) = convert(
                        host,
                        &converters,
                        &mut self.conversions,
                        point.raw_map_id,
                        point.x,
                        point.z,
                    ) {
                        decorations.push(Decoration {
                            id: point.id,
                            icon: point.icon_id,
                            maps: (1 << layer) & layout.map_mask,
                            x,
                            y,
                            angle: 0.0,
                            source: Source::Other,
                            area: false,
                            cleared: false,
                            runtime: true,
                        });
                    }
                }
            }
            next.decorations = Arc::new(decorations);
        }
        next.beacons = saved_markers(host, state.view_model, 0x338, 10, true);
        if show_others {
            next.memos = saved_markers(host, state.view_model, 0x378, 100, false);
        }
        let after = host.map();
        let failure = if after.is_none() {
            Some("minimap-data phase=final-map-unavailable\n")
        } else if after.is_some_and(|after| {
            after.generation != state.generation
                || after.view_model != state.view_model
                || after.raw_map_id != state.raw_map_id
                || after.map_id != state.map_id
                || after.underground != state.underground
        }) {
            Some("minimap-data phase=final-context-changed\n")
        } else {
            None
        };
        if let Some(reason) = failure {
            if !reused {
                self.failed_until = Some(Instant::now() + Duration::from_millis(200));
            }
            self.report(reason);
            return MapData::default();
        }
        self.previous.state = next.state;
        self.previous.valid = next.valid;
        self.previous.roundtable = next.roundtable;
        self.previous.home_icon = next.home_icon;
        self.previous.decorations = next.decorations.clone();
        self.report("minimap-data phase=ready\n");
        next
    }

    fn report(&mut self, reason: &'static str) {
        if self.reported != reason {
            self.host.log(reason);
            self.reported = reason;
        }
    }

    pub fn prepare_runtime(
        &mut self,
        visible: &mut MapData,
        budget: &mut crate::resources::UploadBudget,
    ) {
        let demand: HashSet<_> = visible
            .decorations
            .iter()
            .filter(|point| point.runtime)
            .map(|point| point.icon)
            .collect();
        self.goblins.update(&demand, budget);
        visible.runtime = self.goblins.visuals.clone();
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use eroverlay_abi as abi;
    use std::{
        cell::{Cell, RefCell},
        ffi::c_void,
    };

    struct NativeFixture {
        view: Vec<u8>,
        common: Vec<u8>,
        points: Vec<u8>,
        available: Cell<bool>,
        fail_final: Cell<bool>,
        change_context: Cell<bool>,
        map_reads: Cell<usize>,
        requests: Cell<usize>,
        reads: Cell<usize>,
        flag_reads: Cell<usize>,
        params: Cell<usize>,
        logs: RefCell<Vec<String>>,
    }
    fn table(count: usize, width: usize, first_id: u32, sized: bool) -> Vec<u8> {
        let begin = (0x30 + count * 12 + 15) & !15;
        let size = begin + count * width;
        let mut bytes = vec![0u8; 16 + size];
        if sized {
            bytes[..4].copy_from_slice(&(size as u32).to_le_bytes());
        }
        let body = &mut bytes[16..];
        body[..4].copy_from_slice(&(size as u32).to_le_bytes());
        body[10..12].copy_from_slice(&(count as u16).to_le_bytes());
        body[0x2d] = 2;
        for i in 0..count {
            let at = 0x30 + i * 12;
            body[at..at + 4].copy_from_slice(&(first_id + i as u32).to_le_bytes());
            body[at + 4..at + 8].copy_from_slice(&((begin + i * width) as u32).to_le_bytes());
        }
        bytes
    }
    impl NativeFixture {
        fn new(sized: bool) -> Self {
            let mut view = vec![0; 0x500];
            view[0x280..0x288].copy_from_slice(&1u64.to_le_bytes());
            view[0xf8 + 11] = 60;
            view[0xf8 + 32..0xf8 + 36].copy_from_slice(&1.0f32.to_le_bytes());
            let mut common = table(2000, 0x27c, 0, true);
            let common_row = u32_at(&common[16..], 0x34).unwrap() as usize + 16;
            common[common_row + 0x278..common_row + 0x27c].copy_from_slice(&u32::MAX.to_le_bytes());
            let mut points = table(1000, 256, 78500, sized);
            for i in 0..1000 {
                let row = u32_at(&points[16..], 0x34 + i * 12).unwrap() as usize + 16;
                points[row + 4..row + 8].copy_from_slice(&999u32.to_le_bytes());
                points[row + 12..row + 14].copy_from_slice(&5u16.to_le_bytes());
                points[row + 16] = 4; // Native no-text point, not an area icon.
                points[row + 20..row + 24].copy_from_slice(&999u32.to_le_bytes());
                points[row + 24] = 1;
                points[row + 32] = 60;
                points[row + 36..row + 40].copy_from_slice(&(i as f32).to_le_bytes());
            }
            Self {
                view,
                common,
                points,
                available: Cell::new(false),
                fail_final: Cell::new(false),
                change_context: Cell::new(false),
                map_reads: Cell::new(0),
                requests: Cell::new(0),
                reads: Cell::new(0),
                flag_reads: Cell::new(0),
                params: Cell::new(0),
                logs: RefCell::new(Vec::new()),
            }
        }
        fn host(&self) -> Host {
            unsafe extern "C" fn display(_: *mut c_void, out: *mut abi::DisplayState) -> i32 {
                unsafe {
                    out.write(abi::DisplayState {
                        valid: 1,
                        ..Default::default()
                    })
                };
                abi::OK
            }
            unsafe extern "C" fn map(user: *mut c_void, out: *mut abi::MapState) -> i32 {
                let f = unsafe { &*user.cast::<NativeFixture>() };
                f.map_reads.set(f.map_reads.get() + 1);
                if !f.available.get() || (f.fail_final.get() && f.map_reads.get().is_multiple_of(2))
                {
                    return abi::ERR_UNAVAILABLE;
                }
                let generation = if f.change_context.get() && f.map_reads.get() > 1 {
                    2
                } else {
                    1
                };
                unsafe {
                    out.write(MapState {
                        generation,
                        view_model: f.view.as_ptr() as usize,
                        raw_map_id: 0x3c00_0000,
                        map_id: 0,
                        ..Default::default()
                    })
                };
                abi::OK
            }
            unsafe extern "C" fn read(
                user: *mut c_void,
                address: usize,
                out: *mut u8,
                len: usize,
            ) -> i32 {
                let f = unsafe { &*user.cast::<NativeFixture>() };
                f.reads.set(f.reads.get() + 1);
                let out = unsafe { std::slice::from_raw_parts_mut(out, len) };
                if eroverlay_game::memory::read_memory(address, out) {
                    abi::OK
                } else {
                    abi::ERR_UNAVAILABLE
                }
            }
            unsafe extern "C" fn param(user: *mut c_void, group: u32) -> usize {
                let f = unsafe { &*user.cast::<NativeFixture>() };
                f.params.set(f.params.get() + 1);
                match group {
                    141 => f.common.as_ptr() as usize + 16,
                    87 => f.points.as_ptr() as usize + 16,
                    _ => 0,
                }
            }
            unsafe extern "C" fn flag(user: *mut c_void, _: u32) -> u32 {
                let f = unsafe { &*user.cast::<NativeFixture>() };
                f.flag_reads.set(f.flag_reads.get() + 1);
                1
            }
            unsafe extern "C" fn request(user: *mut c_void, _: *const abi::FileRequest) -> u64 {
                let f = unsafe { &*user.cast::<NativeFixture>() };
                f.requests.set(f.requests.get() + 1);
                f.requests.get() as u64
            }
            unsafe extern "C" fn log(user: *mut c_void, _: u32, value: abi::ByteSlice) {
                let f = unsafe { &*user.cast::<NativeFixture>() };
                f.logs.borrow_mut().push(
                    String::from_utf8_lossy(unsafe {
                        std::slice::from_raw_parts(value.data, value.len)
                    })
                    .into_owned(),
                );
            }
            // SAFETY: all-zero nullable callbacks/pointers and integers are valid.
            let mut api: abi::HostApi = unsafe { std::mem::zeroed() };
            api.user = (self as *const Self).cast_mut().cast();
            api.read_display_state = Some(display);
            api.read_map_state = Some(map);
            api.read_memory = Some(read);
            api.find_param_table = Some(param);
            api.read_event_flag = Some(flag);
            api.log = Some(log);
            api.request_file = Some(request);
            Host(api)
        }
    }
    #[test]
    fn large_unsized_param_rows_use_bounded_bulk_reads_in_directory_order() {
        let mut fixture = NativeFixture::new(false);
        fixture.points = table(20_000, 256, 78500, false);
        let result = rows(fixture.host(), 87, 168, 256, None).unwrap();
        assert_eq!(result.len(), 20_000);
        assert_eq!(result.first().unwrap().id, 78500);
        assert_eq!(result.last().unwrap().id, 98499);
        assert!(
            fixture.reads.get() <= 6,
            "header, size, directory and bounded row spans"
        );
    }

    #[test]
    fn first_metadata_uses_independent_live_map_after_marker_context_changes() {
        use crate::{
            resources::{Resources, UploadBudget, ViewRequest},
            settings::Settings,
        };
        let fixture = NativeFixture::new(true);
        let host = fixture.host();
        let mut data = Data::new(host);
        let mut resources = Resources::new(host);
        let settings = Settings::load(&HashMap::new());
        fixture.available.set(true);
        fixture.change_context.set(true);
        // Real production chain: A at marker start, B at its final validation,
        // B at the independent first-resource gate. Do not publish A's markers.
        let snapshot = data.update(false);
        assert!(!snapshot.valid);
        assert_eq!(fixture.map_reads.get(), 2);
        resources.update(
            ViewRequest {
                data: &snapshot,
                bounds: [100.0; 2],
                shown: true,
                preset: &settings.presets[0],
                settings: &settings,
                graces: true,
                landmarks: true,
            },
            &mut UploadBudget::new(),
        );
        assert_eq!(fixture.map_reads.get(), 3);
        assert_eq!(fixture.requests.get(), 4);
    }

    #[test]
    fn native_wait_to_thousand_markers_batches_reads_and_failed_first_build_backs_off() {
        use eroverlay_game::memory;
        for sized in [false, true] {
            let fixture = NativeFixture::new(sized);
            let mut data = Data::new(fixture.host());
            for _ in 0..100 {
                assert!(!data.update(false).valid);
            }
            assert_eq!(fixture.reads.get(), 0);
            assert_eq!(fixture.params.get(), 0);
            assert_eq!(fixture.logs.borrow().len(), 1);
            fixture.available.set(true);
            fixture.map_reads.set(0);
            fixture.fail_final.set(true);
            memory::set_tracking(true);
            let before = memory::syscall_counters();
            let start = Instant::now();
            assert!(!data.update(false).valid);
            let stats = memory::syscall_counters().since(before);
            println!(
                "minimap1000 sized={sized} build_us={} host_reads={} rpm={} queries={} bytes={} logical_flags={}",
                start.elapsed().as_micros(),
                fixture.reads.get(),
                stats.rpm_calls,
                stats.virtual_queries,
                stats.bytes,
                fixture.flag_reads.get()
            );
            assert!(fixture.reads.get() < 40);
            assert!(stats.rpm_calls < 40);
            assert!(
                stats.bytes < 400_000,
                "home lookup must not copy 2000 unrelated rows"
            );
            assert_eq!(fixture.flag_reads.get(), 1);
            let reads = fixture.reads.get();
            let params = fixture.params.get();
            let maps = fixture.map_reads.get();
            for _ in 0..100 {
                assert!(!data.update(false).valid);
            }
            assert_eq!(fixture.reads.get(), reads);
            assert_eq!(fixture.params.get(), params);
            assert_eq!(fixture.map_reads.get(), maps);
            assert_eq!(fixture.flag_reads.get(), 1);
            assert!(
                !data.previous.valid,
                "an unverified catalog must never become a valid snapshot"
            );
            fixture.fail_final.set(false);
            data.failed_until = Some(Instant::now() - Duration::from_millis(1));
            let ready = data.update(false);
            assert!(ready.valid);
            assert_eq!(ready.decorations.len(), 1000);
            assert_eq!(
                fixture.flag_reads.get(),
                2,
                "flags refresh instead of staying frozen after retry"
            );
            let params = fixture.params.get();
            let reused = data.update(false);
            assert!(Arc::ptr_eq(&ready.decorations, &reused.decorations));
            assert_eq!(fixture.params.get(), params);
            memory::set_tracking(false);
        }
    }
    #[test]
    fn param_field_readers_enforce_bounds() {
        assert_eq!(u32_at(&[1, 0, 0, 0], 0), Some(1));
        assert_eq!(u32_at(&[1, 2, 3], 0), None);
        assert_eq!(raw_map(&[60, 10, 20], 0), 0x3c0a1400);
    }
}
