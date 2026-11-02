//! Update-only catalogue preparation. File jobs and EMEVD expansion are bounded;
//! the renderer receives only the final immutable catalogue.
use crate::host::Host;
use eroverlay_game::{
    boss_events::{BossDisplay, EventScript, discover_boss_displays, parse_emevd},
    boss_params::{
        self as params, BonfireRecord, GameAreaRecord, MapPointRecord, SubcategoryRecord,
        TabRecord, TextChoice,
    },
    text::{self, TextCatalog, TextNamespaces},
};
use std::{
    collections::{BTreeMap, BTreeSet, VecDeque},
    sync::Arc,
    time::{Duration, Instant},
};

const PARAM_GROUPS: [u32; 6] = [
    params::GAME_AREA_GROUP,
    params::BONFIRE_GROUP,
    params::WORLD_MAP_POINT_GROUP,
    params::BONFIRE_SUBCATEGORY_GROUP,
    params::BONFIRE_TAB_GROUP,
    params::MAP_DEFAULT_INFO_GROUP,
];
const MAX_FILES: usize = 4096;
const MAX_SCRIPT_BYTES: usize = 32 * 1024 * 1024;
const MAX_PENDING: usize = 2;

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Encounter {
    pub flag: u32,
    pub name: String,
    pub place: String,
    pub group: usize,
    pub map_id: u32,
    pub search: String,
}
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Group {
    pub id: u32,
    pub name: String,
}
#[derive(Clone, Debug, Default, PartialEq, Eq)]
pub struct Catalog {
    pub rows: Vec<Encounter>,
    pub groups: Vec<Group>,
    pub unknown_count: usize,
}

#[derive(Clone)]
enum Kind {
    Text {
        needed: TextNamespaces,
        fallback: Option<String>,
    },
    Event {
        required: bool,
    },
}
struct Job {
    path: String,
    kind: Kind,
    attempts: u8,
    retry: Instant,
    token: u64,
}

struct Loading {
    areas: Vec<GameAreaRecord>,
    graces: Vec<BonfireRecord>,
    points: Vec<MapPointRecord>,
    subcategories: Vec<SubcategoryRecord>,
    tabs: Vec<TabRecord>,
    texts: TextCatalog,
    scripts: Vec<EventScript>,
    script_bytes: usize,
    jobs: VecDeque<Job>,
    queued: BTreeSet<String>,
    discover_at: usize,
    displays: Vec<BossDisplay>,
    missing: usize,
    failures: usize,
    absent: BTreeSet<String>,
    budget_exhausted: bool,
}
impl Loading {
    fn queue(&mut self, path: String, kind: Kind) {
        if self.queued.len() >= MAX_FILES && !self.queued.contains(&path) {
            if !self.budget_exhausted {
                self.budget_exhausted = true;
                self.failures += 1;
            }
            return;
        }
        if matches!(kind, Kind::Event { required: true }) {
            if self.absent.remove(&path) {
                self.failures += 1;
            }
            if let Some(job) = self.jobs.iter_mut().find(|job| job.path == path) {
                job.kind = kind;
                return;
            }
        }
        if self.queued.len() < MAX_FILES && self.queued.insert(path.clone()) {
            self.jobs.push_back(Job {
                path,
                kind,
                attempts: 0,
                retry: Instant::now(),
                token: 0,
            });
        }
    }
    fn event(&mut self, name: &str, required: bool) {
        self.queue(format!("event:/{name}"), Kind::Event { required });
    }
    fn queue_text(&mut self, language: Option<&str>, host: Host) {
        if let Some(paths) = language.and_then(text::binder_paths) {
            self.queue(
                paths[0].clone(),
                Kind::Text {
                    needed: TextNamespaces::ITEM,
                    fallback: Some(paths[2].clone()),
                },
            );
            self.queue(
                paths[1].clone(),
                Kind::Text {
                    needed: TextNamespaces::MENU,
                    fallback: Some(paths[3].clone()),
                },
            );
        } else {
            self.failures += 1;
            host.log("boss-text: current game language is unavailable; using numeric labels\n");
        }
    }
    fn new(host: Host, tables: [usize; 6]) -> Option<Self> {
        let mut load = Self {
            areas: params::game_areas(tables[0])?,
            graces: params::bonfires(tables[1])?,
            points: params::map_points(tables[2])?,
            subcategories: params::subcategories(tables[3])?,
            tabs: params::tabs(tables[4])?,
            texts: TextCatalog::default(),
            scripts: Vec::new(),
            script_bytes: 0,
            jobs: VecDeque::new(),
            queued: BTreeSet::new(),
            discover_at: 0,
            displays: Vec::new(),
            missing: 0,
            failures: 0,
            absent: BTreeSet::new(),
            budget_exhausted: false,
        };
        let language = host
            .config("common.language")
            .filter(|s| !s.trim().is_empty())
            .or_else(eroverlay_game::game_language);
        load.queue_text(language.as_deref(), host);
        load.event("common_func.emevd", true);
        load.event("common.emevd", false);
        for [area, block, sub, version] in params::event_maps(tables[5])? {
            load.event(
                &format!("m{area:02}_{block:02}_{sub:02}_{version:02}.emevd"),
                false,
            );
        }
        let maps: BTreeSet<[u8; 3]> = load
            .areas
            .iter()
            .filter(|a| a.flag != 0)
            .map(|a| a.map)
            .chain(load.graces.iter().map(|g| g.map))
            .chain(load.points.iter().map(|p| p.map))
            .filter(|m| m[0] != 0)
            .collect();
        for [area, block, sub] in maps {
            load.event(&format!("m{area:02}_{block:02}_{sub:02}_00.emevd"), false);
        }
        let field_maps: BTreeSet<_> = load
            .areas
            .iter()
            .filter(|a| a.flag != 0 && matches!(a.map[0], 60 | 61))
            .map(|a| a.map)
            .collect();
        for [area, block, sub] in field_maps {
            for version in [10, 20] {
                load.event(
                    &format!("m{area:02}_{block:02}_{sub:02}_{version:02}.emevd"),
                    false,
                );
            }
        }
        Some(load)
    }
    fn cancel(&mut self, host: Host) {
        for job in &mut self.jobs {
            host.release(job.token);
            job.token = 0;
        }
    }
    /// Completes at most one resource parse per update and retains busy tokens.
    fn files(&mut self, host: Host) -> bool {
        let now = Instant::now();
        let mut pending = self.jobs.iter().filter(|j| j.token != 0).count();
        for job in &mut self.jobs {
            if pending >= MAX_PENDING {
                break;
            }
            if job.token == 0 && now >= job.retry {
                job.token = host.request(&job.path);
                if job.token == 0 {
                    job.retry = now + Duration::from_millis(250);
                } else {
                    pending += 1;
                }
            }
        }
        let ready = self.jobs.iter().enumerate().find_map(|(index, job)| {
            (job.token != 0)
                .then(|| host.poll(job.token))
                .flatten()
                .map(|result| (index, result))
        });
        let Some((index, result)) = ready else {
            return self.jobs.is_empty();
        };
        let mut job = self.jobs.remove(index).unwrap();
        host.release(job.token);
        job.token = 0;
        let was_read = result.is_ok();
        let mut retryable = true;
        let parsed = match result {
            Ok(bytes) => match &mut job.kind {
                Kind::Text { needed, .. } => {
                    match self.texts.merge_binder_for(&bytes, *needed) {
                        Ok(merged) => {
                            *needed = needed.missing_from(merged.namespaces);
                            // A valid binder cannot gain a missing member on retry.
                            retryable = false;
                            needed.is_empty()
                        }
                        Err(_) => false,
                    }
                }
                Kind::Event { .. } => {
                    if self.script_bytes + bytes.len() > MAX_SCRIPT_BYTES {
                        false
                    } else if let Ok(script) = parse_emevd(&job.path, &bytes) {
                        self.script_bytes += bytes.len();
                        for link in &script.linked {
                            self.event(link, true);
                        }
                        self.scripts.push(script);
                        true
                    } else {
                        false
                    }
                }
            },
            Err(()) => false,
        };
        if !parsed {
            job.attempts += 1;
            if retryable && job.attempts < 2 {
                job.retry = now + Duration::from_secs(1);
                self.jobs.push_back(job);
            } else if let Kind::Text {
                needed,
                fallback: Some(path),
            } = job.kind
            {
                self.queue(
                    path,
                    Kind::Text {
                        needed,
                        fallback: None,
                    },
                );
            } else if !was_read && matches!(job.kind, Kind::Event { required: false }) {
                self.absent.insert(job.path);
            } else {
                self.failures += 1;
            }
        }
        self.jobs.is_empty()
    }
    fn discover(&mut self) -> bool {
        while let Some(script) = self.scripts.get(self.discover_at) {
            self.discover_at += 1;
            if script.map_id.is_none() {
                continue;
            }
            match discover_boss_displays(script, &self.scripts) {
                Ok(result) => {
                    self.missing += result.missing_events.len();
                    self.displays.extend(result.displays);
                }
                Err(_) => self.failures += 1,
            }
            return false;
        }
        true
    }
    fn catalog(&self, host: Host) -> Catalog {
        build_catalog(
            &self.displays,
            &self.areas,
            &self.graces,
            &self.points,
            &self.subcategories,
            &self.tabs,
            &self.texts,
            host,
        )
    }
}

pub struct Data {
    host: Host,
    tables: [usize; 6],
    check: Instant,
    loading: Option<Loading>,
    catalog: Arc<Catalog>,
    revision: u64,
    status: &'static str,
    finished: bool,
    publish_empty: bool,
}
impl Data {
    pub fn new(host: Host) -> Self {
        Self {
            host,
            tables: [0; 6],
            check: Instant::now(),
            loading: None,
            catalog: Arc::new(Catalog::default()),
            revision: 0,
            status: "Waiting for game parameters",
            finished: false,
            publish_empty: false,
        }
    }
    pub fn status(&self) -> &'static str {
        self.status
    }
    pub fn revision(&self) -> u64 {
        self.revision
    }
    /// Explicit Mod/resource refresh; transient loading/menu states do not
    /// discard the catalogue or cancel in-flight native file generations.
    pub fn reload(&mut self) {
        if let Some(mut load) = self.loading.take() {
            load.cancel(self.host);
        }
        self.tables = [0; 6];
        self.check = Instant::now();
        self.finished = false;
        self.status = "Waiting for game parameters";
        self.publish_empty = self.publish(Catalog::default()).is_some();
    }
    fn publish(&mut self, catalog: Catalog) -> Option<Arc<Catalog>> {
        if *self.catalog == catalog {
            return None;
        }
        self.catalog = Arc::new(catalog);
        self.revision = self.revision.wrapping_add(1);
        Some(self.catalog.clone())
    }
    pub fn update(&mut self) -> Option<Arc<Catalog>> {
        if self.publish_empty {
            self.publish_empty = false;
            return Some(self.catalog.clone());
        }
        let now = Instant::now();
        if now >= self.check {
            self.check = now + Duration::from_millis(500);
            let tables = PARAM_GROUPS.map(|group| self.host.param(group));
            if tables.iter().all(|t| *t != 0) && tables != self.tables {
                if let Some(mut loading) = self.loading.take() {
                    loading.cancel(self.host);
                }
                self.tables = tables;
                self.finished = false;
                self.status = "Waiting for game parameters";
                if self.publish(Catalog::default()).is_some() {
                    return Some(self.catalog.clone());
                }
            }
            if self.loading.is_none()
                && !self.finished
                && tables.iter().all(|t| *t != 0)
                && self.host.state().is_some_and(|s| s.valid != 0)
            {
                self.loading = Loading::new(self.host, tables);
                if self.loading.is_some() {
                    self.status = "Loading game text and Boss events";
                }
            }
        }
        let loading = self.loading.as_mut()?;
        if !loading.files(self.host) {
            return None;
        }
        self.status = "Discovering named Boss health bars";
        if !loading.discover() {
            return None;
        }
        let loading = self.loading.take().unwrap();
        self.finished = true;
        let catalog = loading.catalog(self.host);
        self.status = if catalog.rows.is_empty() {
            "No named Boss health bars found"
        } else if loading.failures != 0 {
            "Ready; some game resources are unavailable"
        } else {
            "Ready"
        };
        self.host.log(&format!("boss-catalog: {} encounters, {} unknown, {} scripts, {} unavailable game resources, {} absent optional maps, {} unresolved event calls\n",catalog.rows.len(),catalog.unknown_count,loading.scripts.len(),loading.failures,loading.absent.len(),loading.missing));
        self.publish(catalog)
    }
}
impl Drop for Data {
    fn drop(&mut self) {
        if let Some(load) = &mut self.loading {
            load.cancel(self.host);
        }
    }
}

fn map3(map: u32) -> [u8; 3] {
    let [a, b, c, _] = map.to_be_bytes();
    [a, b, c]
}
fn map_label(map: u32) -> String {
    let [a, b, c, d] = map.to_be_bytes();
    format!("m{a:02}_{b:02}_{c:02}_{d:02}")
}
fn distance(a: [f32; 3], b: [f32; 3]) -> f32 {
    a.into_iter().zip(b).map(|(x, y)| (x - y) * (x - y)).sum()
}
fn place_text<'a>(
    choices: &[TextChoice; 8],
    text: &'a TextCatalog,
    flags: &BTreeMap<u32, bool>,
) -> Option<&'a str> {
    choices
        .iter()
        .filter(|t| t.kind == 0 && t.id > 0)
        .filter(|t| t.enable_flag == 0 || flags.get(&t.enable_flag) == Some(&true))
        .filter(|t| t.disable_flag == 0 || flags.get(&t.disable_flag) != Some(&true))
        .find_map(|t| text.place(t.id))
}

#[allow(clippy::too_many_arguments)]
fn build_catalog(
    displays: &[BossDisplay],
    areas: &[GameAreaRecord],
    graces: &[BonfireRecord],
    points: &[MapPointRecord],
    subcategories: &[SubcategoryRecord],
    tabs: &[TabRecord],
    texts: &TextCatalog,
    host: Host,
) -> Catalog {
    let ids: BTreeSet<u32> = graces
        .iter()
        .flat_map(|g| &g.texts)
        .chain(points.iter().flat_map(|p| &p.texts))
        .flat_map(|t| [t.enable_flag, t.disable_flag])
        .filter(|f| *f != 0)
        .collect();
    let ids: Vec<_> = ids.into_iter().collect();
    let mut values = vec![2; ids.len()];
    host.flags(&ids, &mut values);
    let flags: BTreeMap<_, _> = ids
        .into_iter()
        .zip(values)
        .filter(|(_, v)| *v < 2)
        .map(|(id, v)| (id, v != 0))
        .collect();
    let mut grouped: BTreeMap<(u32, u32, u8, u32), Vec<&BossDisplay>> = BTreeMap::new();
    for display in displays {
        // An exact entity match resolves duplicate slot-switch helpers. It
        // never joins distinct entities merely because their IDs are close.
        let resolved: BTreeSet<_> = displays
            .iter()
            .filter(|d| {
                d.map_id == display.map_id
                    && d.runtime_variant == display.runtime_variant
                    && d.entity_id == display.entity_id
            })
            .filter_map(|d| d.completion_flag)
            .collect();
        let flag = display
            .completion_flag
            .or_else(|| (resolved.len() == 1).then(|| *resolved.first().unwrap()));
        grouped
            .entry(if let Some(flag) = flag {
                (flag, 0, 0, 0)
            } else {
                // Entity numbers may be reused in another map or ceremony.
                (
                    0,
                    display.map_id,
                    display.runtime_variant,
                    display.entity_id,
                )
            })
            .or_default()
            .push(display);
    }
    let mut catalog = Catalog::default();
    let mut group_indices = BTreeMap::new();
    let mut group_order = BTreeMap::new();
    for ((flag, ..), evidence) in grouped {
        let first = evidence[0];
        let area = areas
            .iter()
            .find(|a| a.flag == flag && a.map == map3(first.map_id))
            .or_else(|| areas.iter().find(|a| flag != 0 && a.flag == flag));
        let map = area.map_or(map3(first.map_id), |a| a.map);
        let grace = graces.iter().filter(|g| g.map == map).min_by(|a, b| {
            let origin = area.map_or([0.; 3], |a| a.pos);
            distance(a.pos, origin)
                .total_cmp(&distance(b.pos, origin))
                .then_with(|| a.row.cmp(&b.row))
        });
        let sub = grace.and_then(|g| subcategories.iter().find(|s| s.row == g.subcategory as u32));
        let tab = sub.and_then(|s| tabs.iter().find(|t| t.row == u32::from(s.tab)));
        let packed = u32::from_be_bytes([map[0], map[1], map[2], 0]);
        let group_id = sub.map_or(packed | 0x8000_0000, |s| s.row);
        group_order.insert(
            group_id,
            (
                tab.map_or(i32::MAX, |t| t.sort),
                sub.map_or(u16::MAX, |s| s.sort),
                group_id,
            ),
        );
        let group = *group_indices.entry(group_id).or_insert_with(|| {
            let region = sub.and_then(|s| texts.menu(s.text_id));
            let heading = tab.and_then(|t| texts.menu(t.text_id));
            let name = match (heading, region) {
                (Some(a), Some(b)) if a != b => format!("{a} / {b}"),
                (_, Some(b)) => b.into(),
                (Some(a), _) => a.into(),
                _ => map_label(packed),
            };
            let index = catalog.groups.len();
            catalog.groups.push(Group { id: group_id, name });
            index
        });
        let mut name_ids = Vec::new();
        for d in &evidence {
            if !name_ids.contains(&d.name_id) {
                name_ids.push(d.name_id);
            }
        }
        let mut names = Vec::new();
        for id in name_ids {
            let name = texts
                .npc(id as i32)
                .map(str::to_owned)
                .unwrap_or_else(|| format!("NpcName#{id}"));
            if !names.contains(&name) {
                names.push(name);
            }
        }
        let name = names.join(" / ");
        let place = points
            .iter()
            .filter(|p| {
                flag != 0
                    && (p.cleared_flag == flag || p.texts.iter().any(|t| t.disable_flag == flag))
            })
            .find_map(|p| place_text(&p.texts, texts, &flags))
            .or_else(|| grace.and_then(|g| place_text(&g.texts, texts, &flags)))
            .map(str::to_owned)
            .unwrap_or_else(|| map_label(packed));
        let search = format!("{} {} {}", name, place, catalog.groups[group].name).to_lowercase();
        if flag == 0 {
            catalog.unknown_count += 1;
        }
        catalog.rows.push(Encounter {
            flag,
            name,
            place,
            group,
            map_id: packed,
            search,
        });
    }
    let mut order: Vec<_> = (0..catalog.groups.len()).collect();
    order.sort_by_key(|i| group_order[&catalog.groups[*i].id]);
    let mut remap = vec![0; order.len()];
    for (new, old) in order.iter().copied().enumerate() {
        remap[old] = new;
    }
    catalog.groups = order
        .into_iter()
        .map(|i| catalog.groups[i].clone())
        .collect();
    for row in &mut catalog.rows {
        row.group = remap[row.group];
    }
    catalog.rows.sort_by(|a, b| {
        a.group
            .cmp(&b.group)
            .then_with(|| a.flag.cmp(&b.flag))
            .then_with(|| a.name.cmp(&b.name))
    });
    catalog
}

#[cfg(test)]
mod tests {
    use super::*;
    use eroverlay_abi as abi;
    use std::{ffi::c_void, path::PathBuf};

    fn host() -> Host {
        // SAFETY: HostApi has only integers, raw pointers and nullable function
        // pointers; the production Host checks absent optional callbacks.
        Host(unsafe { std::mem::zeroed() })
    }

    fn empty_loading() -> Loading {
        Loading {
            areas: Vec::new(),
            graces: Vec::new(),
            points: Vec::new(),
            subcategories: Vec::new(),
            tabs: Vec::new(),
            texts: TextCatalog::default(),
            scripts: Vec::new(),
            script_bytes: 0,
            jobs: VecDeque::new(),
            queued: BTreeSet::new(),
            discover_at: 0,
            displays: Vec::new(),
            missing: 0,
            failures: 0,
            absent: BTreeSet::new(),
            budget_exhausted: false,
        }
    }
    fn display(map: u32, variant: u8, entity: u32, name: u32, flag: Option<u32>) -> BossDisplay {
        BossDisplay {
            map_id: map,
            runtime_variant: variant,
            entity_id: entity,
            name_id: name,
            slot: 0,
            completion_flag: flag,
            guard_flags: Vec::new(),
            related_entities: Vec::new(),
            proof: eroverlay_game::boss_events::EventProof {
                source: "test.emevd".into(),
                event_id: 0,
                instruction: 0,
                event_completion_flag: None,
            },
        }
    }
    #[test]
    fn unknown_entities_merge_only_within_the_same_map_and_variant() {
        let mut load = empty_loading();
        load.displays = vec![
            display(0x0a000000, 0, 7, 1, None),
            display(0x0a000000, 0, 7, 2, None),
            display(0x14000000, 0, 7, 3, None),
            display(0x0a000000, 1, 7, 4, None),
            display(0x0a000000, 1, 8, 5, None),
        ];
        let catalog = load.catalog(host());
        assert_eq!(catalog.rows.len(), 4);
        assert_eq!(catalog.unknown_count, 4);
        assert!(catalog.rows.iter().all(|r| r.flag == 0));
        assert!(
            catalog
                .rows
                .iter()
                .any(|r| r.name == "NpcName#1 / NpcName#2")
        );
    }
    #[test]
    fn known_flags_merge_across_entities_and_maps_without_leaking_to_another_variant() {
        let mut load = empty_loading();
        load.displays = vec![
            display(0x0a000000, 0, 7, 1, Some(100)),
            display(0x0a000000, 0, 8, 2, Some(100)),
            display(0x14000000, 0, 7, 3, Some(100)),
            display(0x0a000000, 0, 7, 4, None),
            display(0x0a000000, 1, 7, 5, None),
        ];
        let catalog = load.catalog(host());
        assert_eq!(catalog.rows.len(), 2);
        assert_eq!(catalog.unknown_count, 1);
        let known = catalog.rows.iter().find(|r| r.flag == 100).unwrap();
        assert_eq!(known.name, "NpcName#1 / NpcName#2 / NpcName#3 / NpcName#4");
        assert!(
            catalog
                .rows
                .iter()
                .any(|r| r.flag == 0 && r.name == "NpcName#5")
        );
    }

    // Small valid BND4/FMG files drive the production parser and file-job state
    // machine; a missing member is different from an empty FMG or empty string.
    fn text_binder(entries: &[(&str, Option<&str>)]) -> Vec<u8> {
        let mut b = vec![0; 64 + entries.len() * 36];
        b[..4].copy_from_slice(b"BND4");
        b[10] = 1;
        b[12..16].copy_from_slice(&(entries.len() as u32).to_le_bytes());
        b[16..24].copy_from_slice(&64u64.to_le_bytes());
        b[32..40].copy_from_slice(&36u64.to_le_bytes());
        for (i, (name, text)) in entries.iter().enumerate() {
            let mut fmg = vec![0u8; if text.is_some() { 64 } else { 40 }];
            fmg[..4].copy_from_slice(&[0, 0, 2, 0]);
            fmg[8] = 1;
            fmg[12..16].copy_from_slice(&u32::from(text.is_some()).to_le_bytes());
            fmg[16..20].copy_from_slice(&u32::from(text.is_some()).to_le_bytes());
            fmg[20] = 0xff;
            let offsets = if text.is_some() { 56u64 } else { 40 };
            fmg[24..32].copy_from_slice(&offsets.to_le_bytes());
            if let Some(text) = text {
                fmg[44..48].copy_from_slice(&100i32.to_le_bytes());
                fmg[48..52].copy_from_slice(&100i32.to_le_bytes());
                fmg[56..64].copy_from_slice(&64u64.to_le_bytes());
                fmg.extend(text.encode_utf16().chain([0]).flat_map(u16::to_le_bytes));
            }
            let len = fmg.len() as u32;
            fmg[4..8].copy_from_slice(&len.to_le_bytes());
            let p = 64 + i * 36;
            let name_offset = b.len() as u32;
            b[p + 32..p + 36].copy_from_slice(&name_offset.to_le_bytes());
            b.extend(name.encode_utf16().chain([0]).flat_map(u16::to_le_bytes));
            let data_offset = b.len() as u32;
            b[p + 24..p + 28].copy_from_slice(&data_offset.to_le_bytes());
            for at in [p + 8, p + 16] {
                b[at..at + 8].copy_from_slice(&(fmg.len() as u64).to_le_bytes());
            }
            b.extend(fmg);
        }
        b
    }
    #[test]
    fn text_jobs_fallback_only_for_missing_target_namespaces() {
        struct Files(Vec<Vec<u8>>);
        unsafe extern "C" fn poll(user: *mut c_void, token: u64, out: *mut abi::FileView) -> i32 {
            // SAFETY: the test retains Files and its buffers for the entire job.
            let files = unsafe { &*user.cast::<Files>() };
            let bytes = &files.0[token as usize - 1];
            unsafe {
                out.write(abi::FileView {
                    status: abi::FILE_READY,
                    bytes: abi::ByteSlice::new(bytes),
                    ..Default::default()
                });
            }
            abi::OK
        }
        let base = text_binder(&[
            ("NpcName.fmg", Some("base NPC")),
            ("PlaceName.fmg", Some("base place")),
            ("GR_MenuText.fmg", Some("base menu")),
        ]);
        let cases = [
            // A valid unrelated FMG and a valid wrong target namespace both
            // require fallback; unrelated namespaces must not pollute the cache.
            (
                TextNamespaces::ITEM,
                vec![],
                Some(TextNamespaces::ITEM),
                Some("base NPC"),
                Some("base place"),
                None,
            ),
            (
                TextNamespaces::ITEM,
                vec![("GoodsName.fmg", Some("goods"))],
                Some(TextNamespaces::ITEM),
                Some("base NPC"),
                Some("base place"),
                None,
            ),
            (
                TextNamespaces::ITEM,
                vec![("GR_MenuText.fmg", Some("wrong menu"))],
                Some(TextNamespaces::ITEM),
                Some("base NPC"),
                Some("base place"),
                None,
            ),
            (
                TextNamespaces::MENU,
                vec![("NpcName.fmg", Some("wrong NPC"))],
                Some(TextNamespaces::MENU),
                None,
                None,
                Some("base menu"),
            ),
            // Either half of an item binder can be absent. Fallback must not
            // replace the already present base-layer Mod namespace.
            (
                TextNamespaces::ITEM,
                vec![("NpcName.fmg", Some("mod NPC"))],
                Some(TextNamespaces::PLACE),
                Some("mod NPC"),
                Some("base place"),
                None,
            ),
            (
                TextNamespaces::ITEM,
                vec![("PlaceName.fmg", Some("mod place"))],
                Some(TextNamespaces::NPC),
                Some("base NPC"),
                Some("mod place"),
                None,
            ),
            // Empty FMGs and empty strings are intentional, valid namespaces.
            (
                TextNamespaces::ITEM,
                vec![("NpcName.fmg", None), ("PlaceName.fmg", None)],
                None,
                None,
                None,
                None,
            ),
            (
                TextNamespaces::ITEM,
                vec![("NpcName.fmg", Some("")), ("PlaceName.fmg", Some(""))],
                None,
                Some(""),
                Some(""),
                None,
            ),
            (
                TextNamespaces::MENU,
                vec![("GR_MenuText.fmg", None)],
                None,
                None,
                None,
                None,
            ),
        ];
        for (needed, entries, fallback, npc, place, menu) in cases {
            let files = Files(vec![text_binder(&entries), base.clone()]);
            let mut host = host();
            host.0.user = (&files as *const Files).cast_mut().cast();
            host.0.poll_file = Some(poll);
            let mut load = empty_loading();
            load.queue(
                "primary".into(),
                Kind::Text {
                    needed,
                    fallback: Some("base".into()),
                },
            );
            load.jobs[0].token = 1;
            assert_eq!(load.files(host), fallback.is_none(), "{entries:?}");
            if let Some(expected) = fallback {
                assert_eq!(load.jobs.len(), 1);
                assert_eq!(load.jobs[0].path, "base");
                assert!(
                    matches!(load.jobs[0].kind, Kind::Text { needed, fallback: None } if needed == expected)
                );
                load.jobs[0].token = 2;
                assert!(load.files(host));
            }
            assert_eq!(load.failures, 0, "{entries:?}");
            assert_eq!(load.texts.npc(100), npc, "{entries:?}");
            assert_eq!(load.texts.place(100), place, "{entries:?}");
            assert_eq!(load.texts.menu(100), menu, "{entries:?}");
        }

        // A valid base binder still missing the requested namespace is a real
        // unavailable-resource result, even if it contains another FMG.
        let files = Files(vec![text_binder(&[("GR_MenuText.fmg", Some("menu"))])]);
        let mut host = host();
        host.0.user = (&files as *const Files).cast_mut().cast();
        host.0.poll_file = Some(poll);
        let mut load = empty_loading();
        load.queue(
            "base".into(),
            Kind::Text {
                needed: TextNamespaces::ITEM,
                fallback: None,
            },
        );
        load.jobs[0].token = 1;
        assert!(load.files(host));
        assert_eq!(load.failures, 1);
        assert!(load.texts.is_empty());
    }

    #[test]
    fn unknown_or_missing_language_is_an_unavailable_text_resource() {
        for language in [None, Some("unsupported-language")] {
            let mut load = empty_loading();
            load.queue_text(language, host());
            assert!(load.jobs.is_empty());
            assert_eq!(load.failures, 1);
        }
    }

    #[test]
    fn transient_context_and_param_unavailability_keep_catalog_and_pending() {
        unsafe extern "C" fn busy(_: *mut c_void, _: u64, _: *mut abi::FileView) -> i32 {
            abi::ERR_BUSY
        }
        let mut host = host();
        host.0.poll_file = Some(busy);
        let mut data = Data::new(host);
        let old = Arc::new(Catalog {
            rows: vec![Encounter {
                flag: 7,
                name: "Boss".into(),
                place: "Place".into(),
                group: 0,
                map_id: 0x0a000000,
                search: "boss place".into(),
            }],
            groups: vec![Group {
                id: 1,
                name: "Region".into(),
            }],
            unknown_count: 0,
        });
        data.catalog = old.clone();
        data.tables = [1; 6];
        data.finished = true;
        assert!(data.update().is_none());
        assert!(Arc::ptr_eq(&old, &data.catalog));
        assert_eq!(data.tables, [1; 6]);
        assert!(data.finished);
        data.reload();
        assert!(data.update().unwrap().rows.is_empty());
        assert!(!data.finished);
    }

    #[test]
    #[ignore = "requires original PARAM/EMEVD/FMG exports; see docs/boss-events.md"]
    fn archived_catalog_uses_param_map_plan_and_current_game_text() {
        let root = PathBuf::from(std::env::var_os("EROVERLAY_BOSS_PARAM_DIR").unwrap());
        let events = PathBuf::from(std::env::var_os("EROVERLAY_BOSS_EVENTS_DIR").unwrap());
        let names = [
            "GameAreaParam",
            "BonfireWarpParam",
            "WorldMapPointParam",
            "BonfireWarpSubCategoryParam",
            "BonfireWarpTabParam",
            "MapDefaultInfoParam",
        ];
        let native: Vec<_> = names
            .iter()
            .map(|name| {
                let bytes = std::fs::read(root.join(format!("{name}.param"))).unwrap();
                let mut native = vec![0; 16];
                native[..4].copy_from_slice(&(bytes.len() as u32).to_le_bytes());
                native.extend(bytes);
                native
            })
            .collect();
        let tables = std::array::from_fn(|i| native[i].as_ptr() as usize + 16);
        let mut load = Loading::new(host(), tables).unwrap();
        // The same production plan selects resources; the original files only
        // replace the native asynchronous transport in this optional check.
        while let Some(job) = load.jobs.pop_front() {
            let Kind::Event { .. } = job.kind else {
                continue;
            };
            let name = job.path.strip_prefix("event:/").unwrap();
            if let Ok(bytes) = std::fs::read(events.join(name)) {
                let script = parse_emevd(name, &bytes).unwrap();
                for link in &script.linked {
                    load.event(link, true);
                }
                load.scripts.push(script);
            }
        }
        load.texts
            .merge_binder(&std::fs::read(root.join("item_dlc02.bnd")).unwrap())
            .unwrap();
        load.texts
            .merge_binder(&std::fs::read(root.join("menu_dlc02.bnd")).unwrap())
            .unwrap();
        while !load.discover() {}
        let catalog = load.catalog(host());
        let flags: BTreeSet<_> = catalog
            .rows
            .iter()
            .map(|r| r.flag)
            .filter(|f| *f != 0)
            .collect();
        assert_eq!(flags.len(), 211);
        for flag in [
            10000800, 11000800, 1252520800, 1052560800, 2048380850, 2052430800, 31000850,
            1034500800, 1037510800, 1038540800,
        ] {
            assert!(flags.contains(&flag), "missing {flag}");
        }
        assert!(
            catalog
                .rows
                .iter()
                .any(|r| r.flag == 10000800 && r.name == "Godrick the Grafted")
        );
        assert!(
            catalog
                .rows
                .iter()
                .any(|r| r.flag == 31150800 && r.name.contains("Demi-Human"))
        );
        let missing_names = catalog
            .rows
            .iter()
            .filter(|r| r.name.contains("NpcName#"))
            .count();
        assert_eq!(missing_names, 0);
        println!(
            "catalog encounters={}, known={}, unknown={}, groups={}, scripts={}, requested_paths={}, missing_names={}",
            catalog.rows.len(),
            flags.len(),
            catalog.unknown_count,
            catalog.groups.len(),
            load.scripts.len(),
            load.queued.len(),
            missing_names
        );
        for row in &catalog.rows {
            println!(
                "boss,{},{},{},{},{}",
                row.flag,
                row.name,
                row.place,
                catalog.groups[row.group].name,
                map_label(row.map_id)
            );
        }
    }
}
