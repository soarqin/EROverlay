//! Update-only catalogue preparation. File jobs and EMEVD expansion are bounded;
//! the renderer receives only the final immutable catalogue.
use crate::encounters::Relations;
use crate::host::Host;
use eroverlay_game::{
    boss_events::{BossDisplay, EventScript, discover_boss_displays, parse_emevd},
    boss_params::{
        self as params, BonfireRecord, GameAreaRecord, MapPointRecord, SubcategoryRecord,
        TabRecord, TextChoice,
    },
    boss_scene::{SceneIndex, parse_msb},
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
const MAX_PENDING: usize = 4;
const MAX_SCENE_BYTES: usize = 32 * 1024 * 1024;
const UPDATE_BUDGET: Duration = Duration::from_millis(2);
const MAX_BATCH: usize = 32;
const JOB_TIMEOUT: Duration = Duration::from_secs(10);
const LOADING_TIMEOUT: Duration = Duration::from_secs(60);

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Encounter {
    pub flag: u32,
    pub completion: Vec<u32>,
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
    pub flag_ids: Vec<u32>,
}
impl Catalog {
    pub fn states(&self, values: &[u8]) -> Vec<u8> {
        self.rows
            .iter()
            .map(|row| {
                if row.completion.is_empty() || values.len() != self.flag_ids.len() {
                    return 2;
                }
                let mut result = 0;
                for flag in &row.completion {
                    let value = self
                        .flag_ids
                        .binary_search(flag)
                        .ok()
                        .map_or(2, |i| values[i]);
                    if value == 1 {
                        return 1;
                    }
                    if value != 0 {
                        result = 2;
                    }
                }
                result
            })
            .collect()
    }
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
    Scene {
        map: u32,
    },
}
struct Job {
    path: String,
    kind: Kind,
    attempts: u8,
    retry: Instant,
    token: u64,
    started: Option<Instant>,
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
    scene_started: bool,
    scene_bytes: usize,
    scenes: BTreeMap<u32, Vec<SceneIndex>>,
    scene_untrusted: BTreeSet<u32>,
    excluded: Vec<BossDisplay>,
    started: Instant,
    timeouts: usize,
    first_failure: Option<String>,
}
impl Loading {
    fn queue(&mut self, path: String, kind: Kind) {
        if self.queued.len() >= MAX_FILES && !self.queued.contains(&path) {
            if let Kind::Scene { map } = kind {
                self.scene_untrusted.insert(map);
            }
            if !self.budget_exhausted {
                self.budget_exhausted = true;
                self.failures += 1;
            }
            return;
        }
        if matches!(kind, Kind::Event { required: true }) {
            if self.absent.remove(&path) {
                // A later explicit link upgrades an earlier optional miss.
                // Permit its required attempt/retry once; final required
                // failures never return to absent, preventing a retry loop.
                self.queued.remove(&path);
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
                started: None,
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
            scene_started: false,
            scene_bytes: 0,
            scenes: BTreeMap::new(),
            scene_untrusted: BTreeSet::new(),
            excluded: Vec::new(),
            started: Instant::now(),
            timeouts: 0,
            first_failure: None,
        };
        let language = host
            .config("common.language")
            .filter(|s| !s.trim().is_empty())
            .or_else(eroverlay_game::game_language);
        load.queue_text(language.as_deref(), host);
        load.event("common_func.emevd", true);
        load.event("common.emevd", false);
        // Get the existing Boss-position maps ahead of broad resource probes.
        // The full authoritative map directory is still queued below.
        let priority: BTreeSet<_> = load
            .areas
            .iter()
            .filter(|a| a.flag != 0 && a.map[0] != 0)
            .map(|a| a.map)
            .collect();
        for [area, block, sub] in priority {
            load.event(&format!("m{area:02}_{block:02}_{sub:02}_00.emevd"), false);
            if matches!(area, 60 | 61) {
                for version in [10, 20] {
                    load.event(
                        &format!("m{area:02}_{block:02}_{sub:02}_{version:02}.emevd"),
                        false,
                    );
                }
            }
        }
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
        Some(load)
    }
    fn cancel(&mut self, host: Host) {
        for job in &mut self.jobs {
            host.release(job.token);
            job.token = 0;
        }
    }
    fn failure(&mut self, path: &str, reason: &str) {
        self.failures += 1;
        self.first_failure
            .get_or_insert_with(|| format!("{path}: {reason}"));
    }
    fn unavailable(&mut self, job: Job, timeout: Option<&str>, fallback: bool) {
        if let Some(reason) = timeout {
            self.timeouts += 1;
            self.failure(&job.path, reason);
        }
        if let Kind::Scene { map } = job.kind {
            // A transport failure is unknown, never negative scene evidence.
            self.scene_untrusted.insert(map);
        } else if let Kind::Text {
            needed,
            fallback: Some(path),
        } = job.kind
        {
            if fallback {
                self.queue(
                    path,
                    Kind::Text {
                        needed,
                        fallback: None,
                    },
                );
            } else if timeout.is_none() {
                self.failure(&job.path, "text resource unavailable");
            }
        } else if timeout.is_none() {
            self.failure(&job.path, "resource unavailable or invalid");
        }
    }
    /// One update shares a soft CPU budget with discovery. Poll a pending token
    /// at most once, retain busy work, and bound both rejected and accepted jobs.
    fn files_at(&mut self, host: Host, now: Instant, until: Instant) -> bool {
        let mut polled = BTreeSet::new();
        let mut submissions = 0;
        let mut completed = 0;
        let loading_expired = now.saturating_duration_since(self.started) >= LOADING_TIMEOUT;
        while completed < MAX_BATCH && Instant::now() < until {
            if let Some(index) = self.jobs.iter().position(|job| {
                loading_expired
                    || job
                        .started
                        .is_some_and(|start| now.saturating_duration_since(start) >= JOB_TIMEOUT)
            }) {
                let job = self.jobs.remove(index).unwrap();
                host.release(job.token);
                self.unavailable(
                    job,
                    Some(if loading_expired {
                        "initialization deadline exceeded"
                    } else {
                        "request deadline exceeded"
                    }),
                    !loading_expired,
                );
                completed += 1;
                continue;
            }
            let mut pending = self.jobs.iter().filter(|job| job.token != 0).count();
            for job in &mut self.jobs {
                if pending >= MAX_PENDING || submissions >= MAX_BATCH || Instant::now() >= until {
                    break;
                }
                if job.token == 0 && now >= job.retry {
                    job.started.get_or_insert(now);
                    job.token = host.request(&job.path);
                    submissions += 1;
                    if job.token == 0 {
                        job.retry = now + Duration::from_millis(250);
                    } else {
                        pending += 1;
                    }
                }
            }
            let ready = self.jobs.iter_mut().enumerate().find_map(|(index, job)| {
                if job.token == 0 || !polled.insert(job.token) {
                    return None;
                }
                job.started.get_or_insert(now);
                host.poll(job.token).map(|result| (index, result))
            });
            let Some((index, result)) = ready else {
                break;
            };
            let mut job = self.jobs.remove(index).unwrap();
            host.release(job.token);
            job.token = 0;
            completed += 1;
            let was_read = result.is_ok();
            let parsed = match result {
                Ok(bytes) => match &mut job.kind {
                    Kind::Text { needed, .. } => self
                        .texts
                        .merge_binder_for(&bytes, *needed)
                        .map(|merged| {
                            *needed = needed.missing_from(merged.namespaces);
                            needed.is_empty()
                        })
                        .unwrap_or(false),
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
                    Kind::Scene { map } => self.accept_scene(*map, &bytes),
                },
                Err(()) => false,
            };
            if parsed {
                continue;
            }
            job.attempts += 1;
            let optional = matches!(job.kind, Kind::Event { required: false });
            // Obtained bytes cannot improve by reading them again. Optional
            // misses are probes, not a reason to double the whole map IO plan.
            if !was_read && !optional && job.attempts < 2 {
                job.retry = now + Duration::from_secs(1);
                self.jobs.push_back(job);
            } else if !was_read && optional {
                self.absent.insert(job.path);
            } else {
                self.unavailable(job, None, true);
            }
        }
        self.jobs.is_empty()
    }
    #[cfg(test)]
    fn files(&mut self, host: Host) -> bool {
        self.files_at(host, Instant::now(), Instant::now() + UPDATE_BUDGET)
    }
    fn discover_until(&mut self, until: Instant) -> bool {
        let mut maps = 0;
        while let Some(script) = self.scripts.get(self.discover_at) {
            if maps >= MAX_BATCH || Instant::now() >= until {
                return false;
            }
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
            maps += 1;
        }
        true
    }
    #[cfg(test)]
    fn discover(&mut self) -> bool {
        self.discover_until(Instant::now() + UPDATE_BUDGET)
    }
    fn param_locates(&self, display: &BossDisplay) -> bool {
        self.areas.iter().any(|area| {
            area.flag != 0
                && (display.completion_flag == Some(area.flag)
                    || display.entity_id == area.flag
                    || (display.completion_flag == Some(area.row)
                        && area.map == map3(display.map_id)))
        })
    }
    /// Only unresolved Param relationships need a scene read. No old boss list
    /// or flag-specific exclusion participates in this resource plan.
    fn prepare_scenes(&mut self) -> bool {
        if self.scene_started {
            return !self.jobs.is_empty();
        }
        self.scene_started = true;
        let maps: BTreeSet<_> = self
            .displays
            .iter()
            .filter(|d| !self.param_locates(d))
            .map(|d| d.map_id)
            .collect();
        for map in maps {
            if map.to_be_bytes()[3] != 0 {
                self.scene_untrusted.insert(map);
                continue;
            }
            let name = map_label(map);
            // The native area bitset proves base-only areas. DLC2 candidates
            // also depend on an unread engine setting, so keep both aliases
            // there; any supported actor wins and failures never prove absence.
            for mount in eroverlay_game::boss_scene::scene_mounts(map) {
                self.queue(format!("{mount}:/{name}.msb"), Kind::Scene { map });
            }
        }
        !self.jobs.is_empty()
    }
    fn accept_scene(&mut self, map: u32, bytes: &[u8]) -> bool {
        let parsed = self
            .scene_bytes
            .checked_add(bytes.len())
            .filter(|n| *n <= MAX_SCENE_BYTES)
            .and_then(|n| parse_msb(bytes).ok().map(|scene| (n, scene)));
        if let Some((n, scene)) = parsed {
            self.scene_bytes = n;
            self.scenes.entry(map).or_default().push(scene);
            true
        } else {
            self.scene_untrusted.insert(map);
            false
        }
    }
    fn filter_scenes(&mut self) {
        let mut exclude = BTreeSet::new();
        let mut uncertain = BTreeSet::new();
        let located: BTreeSet<_> = self
            .displays
            .iter()
            .filter(|d| self.param_locates(d))
            .map(|d| (d.map_id, d.runtime_variant, d.entity_id))
            .collect();
        for d in &self.displays {
            if self.param_locates(d) {
                continue;
            }
            let Some(scenes) = self.scenes.get(&d.map_id).filter(|s| !s.is_empty()) else {
                uncertain.insert(d.map_id);
                continue;
            };
            let targets: BTreeSet<_> = std::iter::once(d.entity_id)
                .chain(d.related_entities.iter().copied())
                .collect();
            if scenes
                .iter()
                .any(|s| targets.iter().any(|id| s.contains_actor(*id)))
            {
                continue;
            }
            let aliases_unknown = self.scene_untrusted.contains(&d.map_id);
            let proxy = scenes.iter().any(|s| {
                targets
                    .iter()
                    .any(|id| s.other_part_entities.contains(id) || s.region_entities.contains(id))
            });
            let foreign = self.displays.iter().any(|other| {
                other.entity_id == d.entity_id && map3(other.map_id) != map3(d.map_id)
            });
            let local = !aliases_unknown
                && !proxy
                && !foreign
                && d.runtime_variant == 0
                && d.map_id.to_be_bytes()[3] == 0
                && d.proof.source == "common_func.emevd"
                && !d.related_entities.is_empty()
                && d.related_entities.iter().all(|id| *id == d.entity_id)
                && self
                    .scripts
                    .iter()
                    .find(|s| s.map_id == Some(d.map_id))
                    .is_some_and(|script| {
                        eroverlay_game::boss_events::scene_absence_is_local(
                            d,
                            script,
                            &self.scripts,
                        )
                    });
            if local {
                exclude.insert((d.map_id, d.runtime_variant, d.entity_id));
            } else {
                uncertain.insert(d.map_id);
            }
        }
        // An ambiguous branch keeps all candidates in that map conservative.
        self.failures += uncertain.len();
        let mut retained = Vec::with_capacity(self.displays.len());
        for display in self.displays.drain(..) {
            if exclude.contains(&(display.map_id, display.runtime_variant, display.entity_id))
                && !located.contains(&(display.map_id, display.runtime_variant, display.entity_id))
                && !uncertain.contains(&display.map_id)
            {
                self.excluded.push(display);
            } else {
                retained.push(display);
            }
        }
        self.displays = retained;
    }
    fn catalog(&self, host: Host, relations: &Relations) -> Catalog {
        build_catalog(
            &self.displays,
            &self.areas,
            &self.graces,
            &self.points,
            &self.subcategories,
            &self.tabs,
            &self.texts,
            host,
            relations,
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
    relations: Relations,
    relation_error: bool,
}
impl Data {
    pub fn new(host: Host) -> Self {
        let root = std::path::PathBuf::from(host.config("runtime.directory").unwrap_or_default());
        let relations = crate::encounters::load(&root);
        if let Err(error) = &relations {
            host.log(&format!(
                "boss-encounters: {error}; cross-location links disabled\n"
            ));
        }
        let relation_error = relations.is_err();
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
            relations: relations.unwrap_or_default(),
            relation_error,
        }
    }
    pub fn status(&self) -> &'static str {
        self.status
    }
    pub fn revision(&self) -> u64 {
        self.revision
    }
    fn set_status(&mut self, status: &'static str) {
        if self.status != status {
            self.status = status;
            self.host.log(&format!("boss-loading: {status}\n"));
        }
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
        let until = now + UPDATE_BUDGET;
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
        if !loading.files_at(self.host, now, until) {
            let status = if loading.timeouts != 0 {
                "Loading Boss data; some requests timed out"
            } else if loading.scene_started {
                "Checking Boss scene entities"
            } else {
                "Loading game text and Boss events"
            };
            self.set_status(status);
            return None;
        }
        self.set_status("Discovering named Boss health bars");
        let loading = self.loading.as_mut().unwrap();
        if !loading.discover_until(until) {
            return None;
        }
        if loading.prepare_scenes() {
            self.set_status("Checking Boss scene entities");
            return None;
        }
        // A parser/expander may exhaust the soft budget. Final preparation is a
        // distinct bounded unit, so defer it instead of appending it that tick.
        if Instant::now() >= until {
            return None;
        }
        loading.filter_scenes();
        let loading = self.loading.take().unwrap();
        self.finished = true;
        let catalog = loading.catalog(self.host, &self.relations);
        let status = if catalog.rows.is_empty() {
            if loading.failures != 0 || loading.timeouts != 0 {
                "Boss data unavailable; reload game data"
            } else {
                "No named Boss health bars found"
            }
        } else if loading.failures != 0 || self.relation_error {
            "Ready; some game resources are unavailable"
        } else {
            "Ready"
        };
        self.set_status(status);
        self.host.log(&format!("boss-catalog: {} encounters, {} unknown, {} scripts, {} unavailable game resources, {} absent optional resources, {} unresolved event calls, {} absent scene bars excluded, {} request timeouts, first_failure={:?}\n",catalog.rows.len(),catalog.unknown_count,loading.scripts.len(),loading.failures,loading.absent.len(),loading.missing,loading.excluded.len(),loading.timeouts,loading.first_failure));
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

struct EncounterGroup<'a> {
    flag: u32,
    members: Vec<&'a BossDisplay>,
}

fn encounter_groups<'a>(
    displays: &'a [BossDisplay],
    relations: &Relations,
) -> Vec<EncounterGroup<'a>> {
    let proved: BTreeSet<_> = displays.iter().filter_map(|d| d.completion_flag).collect();
    let mut grouped: BTreeMap<(u32, u32, u8, u32), Vec<&BossDisplay>> = BTreeMap::new();
    for display in displays {
        // An exact scoped entity resolves duplicate slot-switch helpers.
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
        // Never invent a missing Mod completion event from an old relationship.
        let flag = flag.map(|flag| {
            let canonical = relations.canonical(flag);
            if proved.contains(&canonical) {
                canonical
            } else {
                flag
            }
        });
        grouped
            .entry(if let Some(flag) = flag {
                (flag, 0, 0, 0)
            } else {
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
    grouped
        .into_iter()
        .map(|((flag, ..), members)| EncounterGroup { flag, members })
        .collect()
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
    relations: &Relations,
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
    let mut catalog = Catalog::default();
    let mut group_indices = BTreeMap::new();
    let mut group_order = BTreeMap::new();
    for EncounterGroup {
        flag,
        members: evidence,
    } in encounter_groups(displays, relations)
    {
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
            completion: if flag == 0 {
                Vec::new()
            } else {
                relations
                    .completion_flags(flag)
                    .filter(|_| relations.canonical(flag) == flag)
                    .map_or_else(
                        || vec![flag],
                        |ids| {
                            ids.iter()
                                .copied()
                                .filter(|id| {
                                    displays.iter().any(|d| d.completion_flag == Some(*id))
                                })
                                .collect()
                        },
                    )
            },
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
    catalog.flag_ids = catalog
        .rows
        .iter()
        .flat_map(|r| &r.completion)
        .copied()
        .collect::<BTreeSet<_>>()
        .into_iter()
        .collect();
    catalog
}

#[cfg(test)]
#[path = "data_audit.rs"]
mod audit;

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
            scene_started: false,
            scene_bytes: 0,
            scenes: BTreeMap::new(),
            scene_untrusted: BTreeSet::new(),
            excluded: Vec::new(),
            started: Instant::now(),
            timeouts: 0,
            first_failure: None,
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
    #[derive(Clone)]
    enum Reply {
        Ready(Vec<u8>),
        Missing,
        Busy,
        Pending,
        Zero,
        Delayed(Vec<u8>),
    }
    #[derive(Default)]
    struct Transport {
        replies: BTreeMap<String, Reply>,
        requests: Vec<String>,
        active: BTreeMap<u64, String>,
        polls: BTreeMap<u64, usize>,
        released: Vec<u64>,
        next: u64,
        param: usize,
        state: Option<abi::BossState>,
    }
    impl Transport {
        fn host(&mut self) -> Host {
            unsafe extern "C" fn request(user: *mut c_void, r: *const abi::FileRequest) -> u64 {
                // SAFETY: this fixture is retained and callbacks run serially.
                let transport = unsafe { &mut *user.cast::<Transport>() };
                let request = unsafe { &*r };
                let bytes =
                    unsafe { std::slice::from_raw_parts(request.path.data, request.path.len) };
                let path = std::str::from_utf8(bytes).unwrap();
                let path = path.strip_suffix(".dcx").unwrap_or(path).to_owned();
                transport.requests.push(path.clone());
                if matches!(transport.replies.get(&path), None | Some(Reply::Zero)) {
                    return 0;
                }
                transport.next += 1;
                let token = transport.next;
                transport.active.insert(token, path);
                token
            }
            unsafe extern "C" fn poll(
                user: *mut c_void,
                token: u64,
                out: *mut abi::FileView,
            ) -> i32 {
                // SAFETY: the callback receives a valid fixture/output buffer.
                let transport = unsafe { &mut *user.cast::<Transport>() };
                let count = transport.polls.entry(token).or_default();
                *count += 1;
                let path = &transport.active[&token];
                let reply = &transport.replies[path];
                if matches!(reply, Reply::Busy) || matches!(reply, Reply::Delayed(_)) && *count == 1
                {
                    return abi::ERR_BUSY;
                }
                let view = match reply {
                    Reply::Ready(bytes) | Reply::Delayed(bytes)
                        if !matches!(reply, Reply::Delayed(_)) || *count > 2 =>
                    {
                        abi::FileView {
                            status: abi::FILE_READY,
                            bytes: abi::ByteSlice::new(bytes),
                            ..Default::default()
                        }
                    }
                    Reply::Pending | Reply::Delayed(_) => abi::FileView {
                        status: abi::FILE_PENDING,
                        ..Default::default()
                    },
                    _ => abi::FileView {
                        status: abi::FILE_FAILED,
                        ..Default::default()
                    },
                };
                unsafe {
                    out.write(view);
                }
                abi::OK
            }
            unsafe extern "C" fn release(user: *mut c_void, token: u64) {
                // SAFETY: no callback or borrowed bytes remain after Host::poll.
                let transport = unsafe { &mut *user.cast::<Transport>() };
                assert!(transport.active.remove(&token).is_some());
                transport.released.push(token);
            }
            unsafe extern "C" fn param(user: *mut c_void, _: u32) -> usize {
                unsafe { (*user.cast::<Transport>()).param }
            }
            unsafe extern "C" fn state(user: *mut c_void, out: *mut abi::BossState) -> i32 {
                // SAFETY: the Host supplies a complete writable BossState.
                if let Some(state) = unsafe { (*user.cast::<Transport>()).state } {
                    unsafe {
                        out.write(state);
                    }
                    abi::OK
                } else {
                    abi::ERR_UNAVAILABLE
                }
            }
            let mut host = host();
            host.0.user = (self as *mut Self).cast();
            host.0.request_file = Some(request);
            host.0.poll_file = Some(poll);
            host.0.release_file = Some(release);
            host.0.find_param_table = Some(param);
            host.0.read_boss_state = Some(state);
            host
        }
    }
    fn empty_emevd() -> Vec<u8> {
        let mut bytes = vec![0; 144];
        bytes[..12].copy_from_slice(b"EVD\0\0\xff\x01\xff\xcd\0\0\0");
        bytes[12..16].copy_from_slice(&144u32.to_le_bytes());
        bytes
    }
    fn run_files(load: &mut Loading, host: Host, now: Instant) -> bool {
        // Queue creation uses the real clock; align only never-attempted jobs
        // with this fixture's explicit time. Retry deadlines remain untouched.
        for job in &mut load.jobs {
            if job.started.is_none() {
                job.retry = now;
            }
        }
        load.files_at(host, now, Instant::now() + Duration::from_secs(1))
    }
    #[test]
    fn file_batches_bound_work_poll_busy_once_and_do_not_retry_optional_misses() {
        let mut transport = Transport::default();
        let mut load = empty_loading();
        let now = Instant::now();
        let busy = "event:/m11_00_00_00.emevd";
        transport.replies.insert(busy.into(), Reply::Busy);
        load.queue(busy.into(), Kind::Event { required: false });
        for i in 0..70 {
            let path = format!("event:/m10_00_{i:02}_00.emevd");
            transport.replies.insert(
                path.clone(),
                if i == 0 {
                    Reply::Missing
                } else {
                    Reply::Ready(empty_emevd())
                },
            );
            load.queue(path, Kind::Event { required: false });
        }
        let host = transport.host();
        assert!(!run_files(&mut load, host, now));
        assert!(
            load.scripts.len() > 1,
            "ready files must not wait one tick each"
        );
        assert!(transport.released.len() <= MAX_BATCH);
        assert_eq!(
            transport.released.len() + transport.active.len(),
            transport.requests.len()
        );
        assert_eq!(
            transport.polls[&1], 1,
            "pending token must not be polled per completed file"
        );
        while load.jobs.len() > 1 {
            run_files(&mut load, host, now);
        }
        assert_eq!(load.scripts.len(), 69);
        assert_eq!(load.absent.len(), 1);
        assert_eq!(
            transport
                .requests
                .iter()
                .filter(|path| path.as_str() == "event:/m10_00_00_00.emevd")
                .count(),
            1
        );
        assert_eq!(transport.requests.len(), 71);
        assert!(run_files(&mut load, host, now + JOB_TIMEOUT));
        assert_eq!(load.timeouts, 1);
        assert_eq!(transport.released.len(), 71);
        assert!(transport.active.is_empty());
        assert!(run_files(&mut load, host, now + JOB_TIMEOUT));
        assert_eq!(transport.requests.len(), 71);
    }
    #[test]
    fn rejected_busy_and_pending_jobs_expire_without_sleep_and_a_fresh_load_recovers() {
        for reply in [Reply::Zero, Reply::Busy, Reply::Pending] {
            let mut transport = Transport::default();
            let path = "event:/m10_00_00_00.emevd";
            transport.replies.insert(path.into(), reply.clone());
            let host = transport.host();
            let now = Instant::now();
            let mut load = empty_loading();
            load.queue(path.into(), Kind::Event { required: false });
            assert!(!run_files(&mut load, host, now));
            assert!(!run_files(&mut load, host, now + Duration::from_millis(1)));
            assert_eq!(transport.requests.len(), 1);
            assert!(run_files(&mut load, host, now + JOB_TIMEOUT));
            assert_eq!((load.timeouts, load.failures), (1, 1));
            assert!(load.first_failure.as_ref().unwrap().contains(path));
            assert_eq!(
                transport.released.len(),
                usize::from(!matches!(reply, Reply::Zero))
            );
            transport
                .replies
                .insert(path.into(), Reply::Ready(empty_emevd()));
            let mut recovered = empty_loading();
            recovered.queue(path.into(), Kind::Event { required: false });
            assert!(run_files(&mut recovered, host, now + JOB_TIMEOUT));
            assert_eq!(
                (
                    recovered.scripts.len(),
                    recovered.timeouts,
                    recovered.failures
                ),
                (1, 0, 0)
            );
        }
    }
    #[test]
    fn delayed_completion_and_required_retry_are_bounded_and_invalid_bytes_are_not_reloaded() {
        let path = "event:/m10_00_00_00.emevd";
        let now = Instant::now();
        let mut transport = Transport::default();
        transport
            .replies
            .insert(path.into(), Reply::Delayed(empty_emevd()));
        let host = transport.host();
        let mut load = empty_loading();
        load.queue(path.into(), Kind::Event { required: true });
        assert!(!run_files(&mut load, host, now));
        assert!(!run_files(&mut load, host, now));
        assert!(run_files(&mut load, host, now));
        assert_eq!(
            (
                transport.requests.len(),
                transport.released.len(),
                load.scripts.len()
            ),
            (1, 1, 1)
        );
        transport.replies.insert(path.into(), Reply::Missing);
        let mut load = empty_loading();
        load.queue(path.into(), Kind::Event { required: true });
        assert!(!run_files(&mut load, host, now));
        let requests = transport.requests.len();
        assert!(!run_files(
            &mut load,
            host,
            now + Duration::from_millis(999)
        ));
        assert_eq!(transport.requests.len(), requests);
        assert!(run_files(&mut load, host, now + Duration::from_secs(1)));
        assert_eq!((load.failures, transport.requests.len()), (1, requests + 1));
        transport
            .replies
            .insert(path.into(), Reply::Ready(vec![0; 4 * 1024 * 1024]));
        let mut load = empty_loading();
        load.queue(path.into(), Kind::Event { required: true });
        assert!(run_files(&mut load, host, now));
        let requests = transport.requests.len();
        assert!(run_files(&mut load, host, now + Duration::from_secs(1)));
        assert_eq!(
            transport.requests.len(),
            requests,
            "an invalid completed resource must not be read twice"
        );
        assert_eq!(load.failures, 1);
    }
    #[test]
    fn later_explicit_link_upgrades_an_optional_miss_once_but_not_a_known_parse_failure() {
        let path = "event:/m10_00_00_00.emevd";
        for recover in [false, true] {
            let mut transport = Transport::default();
            transport.replies.insert(path.into(), Reply::Missing);
            let host = transport.host();
            let mut load = empty_loading();
            let now = Instant::now();
            load.queue(path.into(), Kind::Event { required: false });
            assert!(run_files(&mut load, host, now));
            assert!(load.absent.contains(path));
            load.event("m10_00_00_00.emevd", true);
            assert!(!load.absent.contains(path));
            assert!(!run_files(&mut load, host, now));
            if recover {
                transport
                    .replies
                    .insert(path.into(), Reply::Ready(empty_emevd()));
            }
            assert!(run_files(&mut load, host, now + Duration::from_secs(1)));
            assert_eq!(transport.requests.len(), 3);
            assert_eq!(load.failures, usize::from(!recover));
            load.event("m10_00_00_00.emevd", true);
            assert!(load.jobs.is_empty());
        }
        let mut transport = Transport::default();
        transport
            .replies
            .insert(path.into(), Reply::Ready(vec![0; 1024]));
        let host = transport.host();
        let mut load = empty_loading();
        load.queue(path.into(), Kind::Event { required: false });
        assert!(run_files(&mut load, host, Instant::now()));
        load.event("m10_00_00_00.emevd", true);
        assert!(load.jobs.is_empty());
        assert_eq!(transport.requests.len(), 1);
        assert_eq!(load.failures, 1);
    }
    #[test]
    fn initialization_deadline_does_not_publish_zero_success_as_ready_and_reload_cancels_old_generations()
     {
        let mut transport = Transport::default();
        transport
            .replies
            .insert("event:/m10_00_00_00.emevd".into(), Reply::Pending);
        let host = transport.host();
        let mut load = empty_loading();
        let now = Instant::now();
        load.started = now - LOADING_TIMEOUT;
        for i in 0..20 {
            load.queue(
                format!("event:/m10_00_{i:02}_00.emevd"),
                Kind::Event { required: false },
            );
        }
        assert!(run_files(&mut load, host, now));
        assert_eq!(
            (load.timeouts, load.failures, transport.requests.len()),
            (20, 20, 0)
        );
        let mut data = Data::new(host);
        data.loading = Some(load);
        assert!(data.update().is_none());
        assert!(data.status().contains("unavailable"));
        assert!(data.finished);
        data.reload();
        assert!(!data.finished);
        assert!(data.loading.is_none());
        let mut load = empty_loading();
        load.queue(
            "event:/m10_00_00_00.emevd".into(),
            Kind::Event { required: false },
        );
        assert!(!run_files(&mut load, host, Instant::now()));
        data.loading = Some(load);
        data.tables = [1; 6];
        transport.param = 1;
        transport.state = Some(abi::BossState {
            size: size_of::<abi::BossState>() as u32,
            valid: 1,
            context: 1,
            raw_map_id: 0x0a00_0000,
            ..Default::default()
        });
        assert!(data.update().is_none());
        transport.state.as_mut().unwrap().context = 2;
        transport.state.as_mut().unwrap().raw_map_id = 0x0b00_0000;
        transport.param = 0;
        data.check = Instant::now();
        assert!(
            data.update().is_none(),
            "temporary unreadable PARAM/context must retain pending work"
        );
        assert!(transport.released.is_empty());
        assert!(data.loading.is_some());
        transport.param = 2;
        transport.state = None;
        data.check = Instant::now();
        data.update();
        assert_eq!(data.tables, [2; 6]);
        assert!(data.loading.is_none());
        assert_eq!(transport.released, [1]);
        assert!(transport.active.is_empty());
    }
    #[test]
    fn discovery_batches_maps_and_respects_an_expired_cpu_budget() {
        let mut load = empty_loading();
        for i in 0..70 {
            load.scripts
                .push(parse_emevd(&format!("m10_00_{i:02}_00.emevd"), &empty_emevd()).unwrap());
        }
        assert!(!load.discover_until(Instant::now()));
        assert_eq!(load.discover_at, 0);
        assert!(!load.discover_until(Instant::now() + Duration::from_secs(1)));
        assert_eq!(load.discover_at, MAX_BATCH);
        while !load.discover() {}
        assert_eq!(load.discover_at, 70);
        assert!(load.displays.is_empty());
    }
    #[test]
    fn failed_or_budget_dropped_scene_alias_is_unknown_not_absent() {
        unsafe extern "C" fn failed(_: *mut c_void, _: u64, out: *mut abi::FileView) -> i32 {
            // SAFETY: the production Host provides a valid output FileView.
            unsafe {
                out.write(abi::FileView {
                    status: abi::FILE_FAILED,
                    ..Default::default()
                });
            }
            abi::OK
        }
        let map = u32::from_be_bytes([60, 38, 54, 0]);
        let mut load = empty_loading();
        load.queue("mapstudio:/scene.msb".into(), Kind::Scene { map });
        load.jobs[0].token = 1;
        load.jobs[0].attempts = 1;
        let mut host = host();
        host.0.poll_file = Some(failed);
        assert!(load.files(host));
        assert!(load.scene_untrusted.contains(&map));
        assert!(load.absent.is_empty());
        load.scene_untrusted.clear();
        load.queued = (0..MAX_FILES).map(|i| i.to_string()).collect();
        load.queue("mapstudio_dlc2:/scene.msb".into(), Kind::Scene { map });
        assert!(load.jobs.is_empty());
        assert!(load.scene_untrusted.contains(&map));
        assert!(load.budget_exhausted);
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
        let catalog = load.catalog(host(), &Relations::default());
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
        let catalog = load.catalog(host(), &Relations::default());
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

    #[test]
    fn shared_bars_and_phases_merge_but_independent_same_name_victories_do_not() {
        let mut load = empty_loading();
        let altus = u32::from_be_bytes([60, 41, 51, 0]);
        let hinterland = u32::from_be_bytes([61, 50, 47, 0]);
        let godfrey = u32::from_be_bytes([11, 5, 0, 0]);
        load.displays = vec![
            // Altus: two entities and identical names, one joint victory.
            display(altus, 0, 1041510800, 903251600, Some(1041510800)),
            display(altus, 0, 1041510801, 903251600, Some(1041510800)),
            // Hinterland: even identical names and shared event operands must
            // preserve its two independently persistent victories.
            display(hinterland, 0, 2050470800, 906251600, Some(2050470800)),
            display(hinterland, 0, 2050480860, 906251600, Some(2050480860)),
            // Godfrey/Hoarah Loux: one bar changes name between phases.
            display(godfrey, 0, 11050800, 904720000, Some(11050800)),
            display(godfrey, 0, 11050800, 904720001, Some(11050800)),
        ];
        for member in &mut load.displays[2..4] {
            member.guard_flags = vec![2050470800, 2050480860];
            member.related_entities = vec![2050470800, 2050480860];
        }
        let catalog = load.catalog(host(), &Relations::default());
        assert_eq!(catalog.rows.len(), 4);
        assert_eq!(catalog.unknown_count, 0);
        let names: BTreeMap<_, _> = catalog
            .rows
            .iter()
            .map(|row| (row.flag, row.name.as_str()))
            .collect();
        assert_eq!(names[&1041510800], "NpcName#903251600");
        assert_eq!(names[&2050470800], "NpcName#906251600");
        assert_eq!(names[&2050480860], names[&2050470800]);
        assert_eq!(names[&11050800], "NpcName#904720000 / NpcName#904720001");
    }

    #[test]
    fn selected_journeys_count_alternative_kills_once_and_ignore_retreat_or_rematch_flags() {
        let relations = crate::encounters::load(
            std::path::Path::new(env!("CARGO_MANIFEST_DIR"))
                .join("../..")
                .as_path(),
        )
        .unwrap();
        let mut load = empty_loading();
        load.displays = vec![
            display(0x3c223200, 0, 1034500800, 1, Some(1034500800)),
            display(0x3c222a00, 0, 1034420800, 2, Some(1034420800)),
            display(0x3c253300, 0, 1037510800, 3, Some(1037510800)),
            display(0x3c293400, 0, 1041520800, 4, Some(1041520800)),
            display(0x1f000000, 0, 31000800, 5, Some(31000800)),
            display(0x1f000000, 0, 31000850, 6, Some(31000850)),
            display(0x3d322f00, 0, 2050470800, 7, Some(2050470800)),
            display(0x3d322f00, 0, 2050480860, 7, Some(2050480860)),
        ];
        let c = load.catalog(host(), &relations);
        assert_eq!(c.rows.len(), 5);
        assert_eq!(c.flag_ids.len(), 6);
        assert!(!c.flag_ids.contains(&1034500800));
        assert!(!c.flag_ids.contains(&1037510810));
        assert!(!c.flag_ids.contains(&31000850));
        let row = |flag| c.rows.iter().position(|r| r.flag == flag).unwrap();
        assert_eq!(c.rows[row(1034420800)].name, "NpcName#1 / NpcName#2");
        let mut values = vec![0; c.flag_ids.len()];
        values[c.flag_ids.binary_search(&1037510800).unwrap()] = 1;
        assert_eq!(c.states(&values).iter().filter(|v| **v == 1).count(), 1);
        assert_eq!(c.states(&values)[row(1041520800)], 1);
        values[c.flag_ids.binary_search(&1041520800).unwrap()] = 2;
        assert_eq!(c.states(&values)[row(1041520800)], 1);
        values[c.flag_ids.binary_search(&1037510800).unwrap()] = 0;
        assert_eq!(c.states(&values)[row(1041520800)], 2);
        values.fill(0);
        values[c.flag_ids.binary_search(&2050470800).unwrap()] = 1;
        let states = c.states(&values);
        assert_eq!(states[row(2050470800)], 1);
        assert_eq!(states[row(2050480860)], 0);
        assert!(c.states(&[]).iter().all(|v| *v == 2));
        // Removing the canonical event in a Mod must not invent its progress.
        load.displays
            .retain(|d| d.completion_flag != Some(1034420800));
        let modified = load.catalog(host(), &relations);
        let early = modified.rows.iter().find(|r| r.flag == 1034500800).unwrap();
        assert_eq!(early.completion, [1034500800]);
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
                completion: vec![7],
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
            flag_ids: vec![7],
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
        let catalog = load.catalog(host(), &Relations::default());
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
