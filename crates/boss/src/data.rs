//! Update-only catalogue preparation. File jobs and EMEVD expansion are bounded;
//! the renderer receives only the final immutable catalogue.
use crate::host::Host;
use eroverlay_game::{
    boss_events::{BossDisplay, EventScript, discover_boss_displays, parse_emevd},
    boss_params::{
        self as params, BonfireRecord, GameAreaRecord, MapPointRecord, SubcategoryRecord,
        TabRecord, TextChoice,
    },
    text::{self, TextCatalog},
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
    Text { fallback: Option<String> },
    Event { required: bool },
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
        if let Some(paths) = language.as_deref().and_then(text::binder_paths) {
            load.queue(
                paths[0].clone(),
                Kind::Text {
                    fallback: Some(paths[2].clone()),
                },
            );
            load.queue(
                paths[1].clone(),
                Kind::Text {
                    fallback: Some(paths[3].clone()),
                },
            );
        }
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
        let parsed = match result {
            Ok(bytes) => match &job.kind {
                Kind::Text { .. } => self.texts.merge_binder(&bytes).is_ok(),
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
            if job.attempts < 2 {
                job.retry = now + Duration::from_secs(1);
                self.jobs.push_back(job);
            } else if let Kind::Text {
                fallback: Some(path),
            } = job.kind
            {
                self.queue(path, Kind::Text { fallback: None });
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
            "Ready; some event resources are unavailable"
        } else {
            "Ready"
        };
        self.host.log(&format!("boss-catalog: {} encounters, {} unknown, {} scripts, {} unavailable required resources, {} absent optional maps, {} unresolved event calls\n",catalog.rows.len(),catalog.unknown_count,loading.scripts.len(),loading.failures,loading.absent.len(),loading.missing));
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
    let mut grouped: BTreeMap<(u32, u32), Vec<&BossDisplay>> = BTreeMap::new();
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
            .entry((
                flag.unwrap_or(0),
                if flag.is_some() { 0 } else { display.entity_id },
            ))
            .or_default()
            .push(display);
    }
    let mut catalog = Catalog::default();
    let mut group_indices = BTreeMap::new();
    let mut group_order = BTreeMap::new();
    for ((flag, _), evidence) in grouped {
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
