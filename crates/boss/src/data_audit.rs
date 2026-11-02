//! Explicit offline audit. Compiled only into tests, never a production DLL.
//! Uses the runtime resource plan, event discovery and encounter grouping.
use super::*;
use eroverlay_game::boss_events::{BossDefeat, EventProof};
use serde::Deserialize;
use serde_json::{Value, json};
use std::{
    error::Error,
    path::{Path, PathBuf},
};

type Result<T> = std::result::Result<T, Box<dyn Error>>;

#[derive(Deserialize)]
struct OldRegion {
    region_name: String,
    bosses: Vec<OldBoss>,
}
#[derive(Deserialize)]
struct OldBoss {
    boss: String,
    place: String,
    flag_id: u32,
}
struct OldEntry {
    region: String,
    boss: OldBoss,
}
struct ArchivedRecord {
    multi_label: bool,
    members: Vec<ArchivedMember>,
    display_evidence: Vec<ArchivedDisplay>,
}
struct ArchivedMember {
    map: String,
    variant: u8,
    entity: u32,
    name_id: u32,
    text: Option<String>,
}
struct ArchivedDisplay {
    map: String,
    variant: u8,
    entity: u32,
    name: u32,
    slot: u16,
    source: String,
    event: u32,
    instruction: u32,
    event_completion: Option<u32>,
    flag: Option<u32>,
    guards: Vec<u32>,
    related: Vec<u32>,
}
fn canonical_map(map: u32) -> u32 {
    let mut bytes = map.to_be_bytes();
    if matches!(bytes[0], 60 | 61) && matches!(bytes[3], 10 | 20) {
        bytes[3] = 0;
    }
    u32::from_be_bytes(bytes)
}
type DisplayKey = (
    u32,
    u8,
    u32,
    u32,
    u16,
    String,
    u32,
    u32,
    Option<u32>,
    Option<u32>,
    Vec<u32>,
    Vec<u32>,
);
fn canonical_source(source: &str) -> String {
    eroverlay_game::boss_events::map_id(source)
        .map(|map| format!("{}.emevd", map_label(canonical_map(map))))
        .unwrap_or_else(|| source.to_owned())
}
fn display_key(d: &BossDisplay, canonical: bool) -> DisplayKey {
    let source = if canonical {
        canonical_source(&d.proof.source)
    } else {
        d.proof.source.clone()
    };
    (
        if canonical {
            canonical_map(d.map_id)
        } else {
            d.map_id
        },
        d.runtime_variant,
        d.entity_id,
        d.name_id,
        d.slot,
        source,
        d.proof.event_id,
        d.proof.instruction,
        d.proof.event_completion_flag,
        d.completion_flag,
        d.guard_flags.clone(),
        d.related_entities.clone(),
    )
}
fn member_coverage(archive: &ArchivedRecord, members: &[&BossDisplay]) -> Result<Value> {
    let current: BTreeSet<_> = members
        .iter()
        .map(|d| (d.map_id, d.runtime_variant, d.entity_id, d.name_id))
        .collect();
    let canonical: BTreeSet<_> = current
        .iter()
        .map(|&(map, v, e, n)| (canonical_map(map), v, e, n))
        .collect();
    let mut expected = BTreeSet::new();
    let mut evidence = Vec::new();
    let mut missing = 0;
    let mut aliases = 0;
    for m in &archive.members {
        let map = eroverlay_game::boss_events::map_id(&m.map).ok_or("invalid inventory map")?;
        let key = (map, m.variant, m.entity, m.name_id);
        expected.insert((canonical_map(map), m.variant, m.entity, m.name_id));
        let status = if current.contains(&key) {
            "exact"
        } else if canonical.contains(&(canonical_map(map), m.variant, m.entity, m.name_id)) {
            aliases += 1;
            "field_lod_resource_alias"
        } else {
            missing += 1;
            "missing"
        };
        evidence.push(
            json!({"map":map_label(map),"runtime_variant":m.variant,"entity":m.entity,
            "name_id":m.name_id,"archived_game_name":m.text,"coverage":status}),
        );
    }
    let unexpected: Vec<_> = canonical
        .difference(&expected)
        .map(|&(map, v, e, n)| {
            json!({"map":map_label(map),
        "runtime_variant":v,"entity":e,"name_id":n})
        })
        .collect();
    let exact_displays: BTreeSet<_> = members.iter().map(|m| display_key(m, false)).collect();
    let canonical_displays: BTreeSet<_> = members.iter().map(|m| display_key(m, true)).collect();
    let mut raw_missing = Vec::new();
    let mut raw_aliases = 0;
    let mut raw_alias_evidence = Vec::new();
    for d in &archive.display_evidence {
        let map =
            eroverlay_game::boss_events::map_id(&d.map).ok_or("invalid display inventory map")?;
        let key = (
            map,
            d.variant,
            d.entity,
            d.name,
            d.slot,
            d.source.clone(),
            d.event,
            d.instruction,
            d.event_completion,
            d.flag,
            d.guards.clone(),
            d.related.clone(),
        );
        if exact_displays.contains(&key) {
            continue;
        }
        let source = eroverlay_game::boss_events::map_id(&d.source)
            .map(|m| format!("{}.emevd", map_label(canonical_map(m))))
            .unwrap_or_else(|| d.source.clone());
        if canonical_displays.contains(&(
            canonical_map(map),
            d.variant,
            d.entity,
            d.name,
            d.slot,
            source,
            d.event,
            d.instruction,
            d.event_completion,
            d.flag,
            d.guards.clone(),
            d.related.clone(),
        )) {
            raw_aliases += 1;
            let matched = members
                .iter()
                .find(|m| {
                    canonical_map(m.map_id) == canonical_map(map)
                        && m.runtime_variant == d.variant
                        && m.entity_id == d.entity
                        && m.name_id == d.name
                        && m.slot == d.slot
                        && m.proof.event_id == d.event
                        && m.proof.instruction == d.instruction
                        && m.proof.event_completion_flag == d.event_completion
                        && m.completion_flag == d.flag
                        && m.guard_flags == d.guards
                        && m.related_entities == d.related
                        && canonical_source(&m.proof.source) == canonical_source(&d.source)
                })
                .unwrap();
            raw_alias_evidence.push(json!({"archive_map":map_label(map),"production_map":map_label(matched.map_id),
                "runtime_variant":d.variant,"entity":d.entity,"name_id":d.name,"slot":d.slot,
                "archive_proof":{"source":d.source,"event":d.event,"instruction":d.instruction,"instance_completion_flag":d.event_completion},
                "production_proof":proof(&matched.proof),"completion_flag":d.flag,"guards":d.guards,"related_entities":d.related,
                "basis":"same_field_tile_and_runtime_variant; identical_expanded_bar_operands_proof_guards_related_entities; resource_scale_10_or_20_to_00"}));
        } else {
            raw_missing.push(
                json!({"map":d.map,"runtime_variant":d.variant,"entity":d.entity,"name_id":d.name,
            "slot":d.slot,"source":d.source,"event":d.event,"instruction":d.instruction,
            "instance_completion_flag":d.event_completion}),
            );
        }
    }
    Ok(
        json!({"old_label_has_multiple_members":archive.multi_label,"expected_named_members":evidence,
        "current_unique_named_members":current.len(),"missing_named_members":missing,"lod_alias_members":aliases,
        "unexpected_current_members":unexpected,"expected_display_instructions":archive.display_evidence.len(),
        "missing_display_instructions":raw_missing,"lod_alias_display_instructions":raw_aliases,
        "lod_alias_display_evidence":raw_alias_evidence,
        "all_archived_named_members_present":missing==0 && !archive.members.is_empty()}),
    )
}

fn archived_record(
    entry: &OldEntry,
    groups: &[EncounterGroup<'_>],
    text: &TextCatalog,
) -> ArchivedRecord {
    let displays: Vec<_> = groups
        .iter()
        .filter(|g| g.flag == entry.boss.flag_id)
        .flat_map(|g| g.members.iter().copied())
        .collect();
    let mut members = BTreeMap::new();
    for d in &displays {
        members
            .entry((d.map_id, d.runtime_variant, d.entity_id, d.name_id))
            .or_insert_with(|| ArchivedMember {
                map: format!("{}.emevd", map_label(d.map_id)),
                variant: d.runtime_variant,
                entity: d.entity_id,
                name_id: d.name_id,
                text: text.npc(d.name_id as i32).map(str::to_owned),
            });
    }
    ArchivedRecord {
        multi_label: entry.boss.boss.contains('&')
            || entry.boss.boss.contains("x2")
            || entry.boss.boss.contains("Duo"),
        members: members.into_values().collect(),
        display_evidence: displays
            .iter()
            .map(|d| ArchivedDisplay {
                map: format!("{}.emevd", map_label(d.map_id)),
                variant: d.runtime_variant,
                entity: d.entity_id,
                name: d.name_id,
                slot: d.slot,
                source: d.proof.source.clone(),
                event: d.proof.event_id,
                instruction: d.proof.instruction,
                event_completion: d.proof.event_completion_flag,
                flag: d.completion_flag,
                guards: d.guard_flags.clone(),
                related: d.related_entities.clone(),
            })
            .collect(),
    }
}

fn host() -> Host {
    // SAFETY: the table contains only POD/nullable callbacks. This audit uses
    // no native transport or save; place-selection flags remain unknown.
    let mut api: eroverlay_abi::HostApi = unsafe { std::mem::zeroed() };
    unsafe extern "C" fn config(
        _: *mut std::ffi::c_void,
        _: eroverlay_abi::ByteSlice,
        out: *mut u8,
        capacity: usize,
    ) -> usize {
        if capacity < 5 {
            return 5;
        }
        // SAFETY: Host::config supplies a writable buffer of this capacity.
        unsafe { std::ptr::copy_nonoverlapping(b"engUS".as_ptr(), out, 5) };
        5
    }
    api.config_get = Some(config);
    Host(api)
}

fn proof(p: &EventProof) -> Value {
    json!({"source":p.source,"event":p.event_id,"instruction":p.instruction,
        "instance_completion_flag":p.event_completion_flag})
}
fn scope(p: &EventProof) -> (&str, u32, Option<u32>) {
    (&p.source, p.event_id, p.event_completion_flag)
}
fn same_entity(a: &BossDisplay, b: &BossDisplay) -> bool {
    (a.map_id, a.runtime_variant, a.entity_id) == (b.map_id, b.runtime_variant, b.entity_id)
}
fn same_scope(a: &BossDisplay, b: &BossDisplay) -> bool {
    a.map_id == b.map_id
        && a.runtime_variant == b.runtime_variant
        && scope(&a.proof) == scope(&b.proof)
}
fn explicitly_defeated(d: &BossDefeat, b: &BossDisplay) -> bool {
    d.entity_id == b.entity_id || d.death_entities.contains(&b.entity_id)
}
fn defeat_matches(d: &BossDefeat, b: &BossDisplay, flag: u32) -> Vec<&'static str> {
    if flag == 0
        || d.map_id != b.map_id
        || d.runtime_variant != b.runtime_variant
        || !d.flags.contains(&flag)
    {
        return Vec::new();
    }
    let mut reasons = Vec::new();
    if b.guard_flags.contains(&flag) {
        reasons.push("display_guard_reads_defeat_read_and_true_write_flag");
    }
    if explicitly_defeated(d, b) {
        reasons.push("exact_display_entity_in_defeat_event");
    }
    if b.related_entities.contains(&d.entity_id) {
        reasons.push("display_event_explicit_related_entity");
    }
    reasons
}
fn defeat(d: &BossDefeat) -> Value {
    json!({"map":map_label(d.map_id),"runtime_variant":d.runtime_variant,
        "defeat_notification_entity":d.entity_id,"death_condition_entities":d.death_entities,
        "conditional_read_and_true_write_flags":d.flags,"proof":proof(&d.proof)})
}
fn components(
    members: &[&BossDisplay],
    defeats: &[BossDefeat],
    flag: u32,
) -> (Vec<Vec<usize>>, Value) {
    let mut edges = Vec::new();
    let mut adjacent = vec![Vec::new(); members.len()];
    for i in 0..members.len() {
        for j in i + 1..members.len() {
            let (a, b) = (members[i], members[j]);
            let mut reasons = Vec::new();
            if same_entity(a, b) {
                reasons.push("same_exact_map_variant_entity");
            }
            if same_scope(a, b) {
                reasons.push("same_instantiated_display_event_scope");
            }
            if a.map_id == b.map_id
                && a.runtime_variant == b.runtime_variant
                && defeats.iter().any(|d| {
                    d.map_id == a.map_id
                        && d.runtime_variant == a.runtime_variant
                        && d.flags.contains(&flag)
                        && explicitly_defeated(d, a)
                        && explicitly_defeated(d, b)
                })
            {
                reasons.push("same_defeat_event_explicitly_contains_both_entities");
            }
            if !reasons.is_empty() {
                adjacent[i].push(j);
                adjacent[j].push(i);
                edges.push(json!({"members":[i,j],"reasons":reasons}));
            }
        }
    }
    let mut visited = BTreeSet::new();
    let mut groups = Vec::new();
    for i in 0..members.len() {
        if !visited.insert(i) {
            continue;
        }
        let mut group = vec![i];
        let mut pending = vec![i];
        while let Some(current) = pending.pop() {
            for &next in &adjacent[current] {
                if visited.insert(next) {
                    group.push(next);
                    pending.push(next);
                }
            }
        }
        group.sort_unstable();
        groups.push(group);
    }
    (groups, json!(edges))
}

/// Correlation evidence is deliberately separate from encounter_groups. A
/// shared global guard/AI/death target never unions two production battles.
fn correlations(groups: &[EncounterGroup<'_>], defeats: &[BossDefeat]) -> Value {
    let mut guards = BTreeMap::<u32, BTreeSet<usize>>::new();
    let mut targets = BTreeMap::<(u32, u8, u32), BTreeMap<usize, BTreeSet<&str>>>::new();
    let mut scopes = BTreeMap::<(u32, u8, &str, u32, Option<u32>), BTreeSet<usize>>::new();
    let known: BTreeSet<_> = groups.iter().map(|g| g.flag).filter(|f| *f != 0).collect();
    for (index, g) in groups.iter().enumerate() {
        for d in &g.members {
            for &flag in &d.guard_flags {
                guards.entry(flag).or_default().insert(index);
            }
            scopes
                .entry((
                    d.map_id,
                    d.runtime_variant,
                    &d.proof.source,
                    d.proof.event_id,
                    d.proof.event_completion_flag,
                ))
                .or_default()
                .insert(index);
            let mut add = |entity, role| {
                targets
                    .entry((d.map_id, d.runtime_variant, entity))
                    .or_default()
                    .entry(index)
                    .or_default()
                    .insert(role);
            };
            add(d.entity_id, "display_entity");
            for &entity in &d.related_entities {
                add(entity, "display_event_ai_or_death_operand");
            }
            for defeat in defeats
                .iter()
                .filter(|defeat| !defeat_matches(defeat, d, g.flag).is_empty())
            {
                add(defeat.entity_id, "HandleBossDefeat_target");
                for &entity in &defeat.death_entities {
                    add(entity, "defeat_event_dead_or_hp_operand");
                }
            }
        }
    }
    let different = |indices: &BTreeSet<usize>| {
        indices
            .iter()
            .map(|&i| groups[i].flag)
            .filter(|f| *f != 0)
            .collect::<BTreeSet<_>>()
            .len()
            > 1
    };
    let pair_set = |indices: &BTreeSet<usize>| {
        indices
            .iter()
            .flat_map(|&a| {
                indices
                    .range(a + 1..)
                    .filter(move |&&b| {
                        groups[a].flag != 0
                            && groups[b].flag != 0
                            && groups[a].flag != groups[b].flag
                    })
                    .map(move |&b| (a, b))
            })
            .collect::<BTreeSet<_>>()
    };
    let mut guard_pairs = BTreeSet::new();
    let mut other_pairs = BTreeSet::new();
    let shared_guards: Vec<_> = guards
        .into_iter()
        .filter(|(_, indices)| different(indices))
        .map(|(flag, indices)| {
            guard_pairs.extend(pair_set(&indices));
            json!({"guard":flag,"is_known_battle_completion_flag":known.contains(&flag),
            "battles":indices.iter().map(|i|format!("R{:03}",i+1)).collect::<Vec<_>>(),
            "meaning":"shared_conditional_guard_is_not_battle_union_evidence"})
        })
        .collect();
    let shared_targets:Vec<_>=targets.into_iter().filter(|(_,by_battle)|different(&by_battle.keys().copied().collect()))
        .map(|((map,variant,entity),by_battle)|{
            other_pairs.extend(pair_set(&by_battle.keys().copied().collect()));
            json!({"map":map_label(map),"runtime_variant":variant,"entity":entity,
                "battles":by_battle.into_iter().map(|(i,roles)|json!({"battle":format!("R{:03}",i+1),"roles":roles})).collect::<Vec<_>>(),
                "meaning":"shared_scoped_actor_operand_is_correlation_not_battle_union_evidence"})
        }).collect();
    let shared_scopes:Vec<_>=scopes.into_iter().filter(|(_,indices)|different(indices))
        .map(|((map,variant,source,event,completion),indices)|{
            other_pairs.extend(pair_set(&indices));
            json!({"map":map_label(map),"runtime_variant":variant,"source":source,"event":event,
                "instance_completion_flag":completion,"battles":indices.iter().map(|i|format!("R{:03}",i+1)).collect::<Vec<_>>(),
                "meaning":"shared_instantiated_event_scope_is_correlation_not_battle_union_evidence"})
        }).collect();
    json!({"shared_guard_hyperedges":shared_guards,"shared_actor_hyperedges":shared_targets,
        "shared_display_scope_hyperedges":shared_scopes,
        "only_shared_guards_pairs":guard_pairs.difference(&other_pairs).map(|(a,b)|
            [format!("R{:03}",a+1),format!("R{:03}",b+1)]).collect::<Vec<_>>(),
        "union_performed":false})
}

fn assert_archive_member_cases(groups: &[EncounterGroup<'_>]) {
    let entities = |flag| {
        let found: Vec<_> = groups.iter().filter(|g| g.flag == flag).collect();
        assert_eq!(
            found.len(),
            1,
            "case {flag} must remain one production candidate"
        );
        found[0]
            .members
            .iter()
            .map(|m| m.entity_id)
            .collect::<BTreeSet<_>>()
    };
    for (flag, wanted) in [
        (31150800, vec![31150800, 31150801]),
        (31110800, vec![31110800, 31110801, 31110802]),
        (12020800, vec![12020800, 12020801]),
        (2050480800, vec![2050480810, 2050480811, 2050480812]),
        // Godskin Duo intentionally exposes one shared named health bar.
        (13000850, vec![13000850]),
        // Separate persistent completions must survive shared guards/AI.
        (2050470800, vec![2050470800]),
        (2050480860, vec![2050480860]),
    ] {
        assert_eq!(entities(flag), wanted.into_iter().collect(), "case {flag}");
    }
}

fn csv_cell(value: impl AsRef<str>) -> String {
    format!("\"{}\"", value.as_ref().replace('"', "\"\""))
}
fn md_cell(value: &str) -> String {
    value.replace('|', "\\|").replace(['\r', '\n'], " ")
}
fn required_env(name: &str) -> Result<PathBuf> {
    std::env::var_os(name)
        .map(PathBuf::from)
        .ok_or_else(|| format!("set {name}").into())
}
fn write_json(path: &Path, value: &Value) -> Result<()> {
    // The full indexed evidence stays compact; CSV/Markdown provide the human
    // views without repeating every defeat proof for each display instruction.
    let mut bytes = serde_json::to_vec(value)?;
    bytes.push(b'\n');
    std::fs::write(path, bytes)?;
    Ok(())
}

#[test]
#[ignore = "explicit C++/PARAM/EMEVD/FMG exports; writes a complete optional evidence audit"]
fn archived_old_207_to_rust_member_audit() -> Result<()> {
    let selected_scope = match std::env::var("EROVERLAY_BOSS_AUDIT_SCOPE").as_deref() {
        Ok("selected") => true,
        Ok("static") | Err(_) => false,
        Ok(value) => return Err(format!("unknown audit scope {value:?}").into()),
    };
    let root = Path::new(env!("CARGO_MANIFEST_DIR"))
        .parent()
        .and_then(Path::parent)
        .ok_or("workspace root unavailable")?;
    let relations = if selected_scope {
        crate::encounters::load(root)?
    } else {
        Relations::default()
    };
    let relationship_config = if selected_scope {
        serde_json::from_slice::<Value>(&std::fs::read(root.join("configs/boss-encounters.json"))?)?
    } else {
        Value::Null
    };
    let reference = required_env("EROVERLAY_CPP_REFERENCE")?;
    let metadata: Value =
        serde_json::from_slice(&std::fs::read(reference.join("reference.json"))?)?;
    let commit = "afab7e14db2b62ae0bede980d5d0c96634321350";
    assert_eq!(metadata["commit"], commit);
    let old_regions: Vec<OldRegion> = serde_json::from_slice(&std::fs::read(
        reference.join("src/boss/data/engUS/bosses.json"),
    )?)?;
    let old: Vec<_> = old_regions
        .into_iter()
        .flat_map(|region| {
            region.bosses.into_iter().map(move |boss| OldEntry {
                region: region.region_name.clone(),
                boss,
            })
        })
        .collect();
    assert_eq!(old.len(), 207);
    let params = required_env("EROVERLAY_BOSS_PARAM_DIR")?;
    let events = required_env("EROVERLAY_BOSS_EVENTS_DIR")?;
    let output = required_env("EROVERLAY_BOSS_AUDIT_OUT")?;
    std::fs::create_dir_all(&output)?;
    let names = [
        "GameAreaParam",
        "BonfireWarpParam",
        "WorldMapPointParam",
        "BonfireWarpSubCategoryParam",
        "BonfireWarpTabParam",
        "MapDefaultInfoParam",
        "PlayRegionParam",
    ];
    let native: Vec<_> = names
        .iter()
        .map(|name| -> Result<Vec<u8>> {
            let body = std::fs::read(params.join(format!("{name}.param")))?;
            let mut bytes = vec![0; 16];
            bytes[..4].copy_from_slice(&(body.len() as u32).to_le_bytes());
            bytes.extend(body);
            Ok(bytes)
        })
        .collect::<Result<_>>()?;
    let tables = std::array::from_fn(|i| native[i].as_ptr() as usize + 16);
    let mut load =
        Loading::new(host(), tables).ok_or("production Param/resource plan rejected assets")?;
    let mut absent = Vec::new();
    while let Some(job) = load.jobs.pop_front() {
        let Kind::Event { required } = job.kind else {
            continue;
        };
        let name = job
            .path
            .strip_prefix("event:/")
            .ok_or("invalid event mount")?;
        let path = events.join(name);
        let bytes = match std::fs::read(&path) {
            Ok(bytes) => bytes,
            Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
                absent.push(json!({"path":job.path,"required":required}));
                continue;
            }
            Err(error) => return Err(error.into()),
        };
        load.script_bytes = load
            .script_bytes
            .checked_add(bytes.len())
            .ok_or("script budget overflow")?;
        if load.script_bytes > MAX_SCRIPT_BYTES {
            return Err("production script budget exceeded".into());
        }
        let script = parse_emevd(name, &bytes).map_err(|e| format!("{name}: {e:?}"))?;
        for link in &script.linked {
            load.event(link, true);
        }
        load.scripts.push(script);
    }
    for name in ["item_dlc02.bnd", "menu_dlc02.bnd"] {
        load.texts
            .merge_binder(&std::fs::read(params.join(name))?)
            .map_err(|e| format!("{name}: {e:?}"))?;
    }
    while !load.discover() {}
    let static_display_count = load.displays.len();
    let mut scene_inputs = Vec::new();
    let mut scene_requests = Vec::new();
    let mut scene_checks = Vec::new();
    if selected_scope {
        let scene_root = std::env::var_os("EROVERLAY_BOSS_SCENE_DIR").map(PathBuf::from);
        load.prepare_scenes();
        let mut maps = BTreeMap::<u32, Vec<String>>::new();
        while let Some(job) = load.jobs.pop_front() {
            let Kind::Scene { map } = job.kind else {
                return Err("unexpected non-scene job after discovery".into());
            };
            maps.entry(map).or_default().push(job.path);
        }
        for (map, aliases) in maps {
            let name = format!("{}.msb", map_label(map));
            if matches!(name.as_str(), "m31_00_00_00.msb" | "m60_38_54_00.msb") {
                assert_eq!(aliases, [format!("mapstudio:/{name}")]);
            } else if name == "m61_49_44_00.msb" {
                assert_eq!(
                    aliases,
                    [
                        format!("mapstudio:/{name}"),
                        format!("mapstudio_dlc2:/{name}")
                    ]
                );
            }
            let bytes = scene_root
                .as_ref()
                .map(|root| std::fs::read(root.join(&name)));
            let accepted = match bytes {
                Some(Ok(bytes)) => {
                    scene_inputs.push(name.clone());
                    Some(load.accept_scene(map, &bytes))
                }
                None => None,
                Some(Err(error)) if error.kind() == std::io::ErrorKind::NotFound => None,
                Some(Err(error)) => return Err(error.into()),
            };
            scene_requests.push(
                json!({"map":map_label(map),"requested_runtime_aliases":aliases,
                "explicit_input":accepted.map(|_|name),"accepted":accepted}),
            );
        }
        for d in &load.displays {
            if !scene_requests
                .iter()
                .any(|request| request["map"] == map_label(d.map_id))
            {
                continue;
            }
            let targets: BTreeSet<_> = std::iter::once(d.entity_id)
                .chain(d.related_entities.iter().copied())
                .collect();
            let scenes = load.scenes.get(&d.map_id);
            let script = load.scripts.iter().find(|s| s.map_id == Some(d.map_id));
            let linked_only: Vec<_> = load
                .scripts
                .iter()
                .filter(|candidate| {
                    candidate.map_id == Some(d.map_id)
                        || script.is_some_and(|map| {
                            map.linked.iter().any(|name| name == &candidate.name)
                        })
                })
                .cloned()
                .collect();
            let local = |scripts: &[EventScript]| {
                script.is_some_and(|script| {
                    eroverlay_game::boss_events::scene_absence_is_local(d, script, scripts)
                })
            };
            scene_checks.push(json!({"map":map_label(d.map_id),"variant":d.runtime_variant,
                "entity":d.entity_id,"name_id":d.name_id,"source":d.proof.source,"related":d.related_entities,
                "param_locates":load.param_locates(d),
                "contains_actor":scenes.is_some_and(|scenes|scenes.iter().any(|s|targets.iter().any(|id|s.contains_actor(*id)))),
                "other_part_collisions":scenes.into_iter().flatten().flat_map(|s|targets.intersection(&s.other_part_entities)).copied().collect::<BTreeSet<_>>(),
                "region_collisions":scenes.into_iter().flatten().flat_map(|s|targets.intersection(&s.region_entities)).copied().collect::<BTreeSet<_>>(),
                "foreign_actor":load.displays.iter().any(|other|other.entity_id==d.entity_id && map3(other.map_id)!=map3(d.map_id)),
                "untrusted_scene":load.scene_untrusted.contains(&d.map_id),
                "local_event_operations_all_scripts":local(&load.scripts),
                "local_event_operations_linked_only":local(&linked_only)}));
        }
        if scene_inputs.iter().any(|name| name == "m60_38_54_00.msb") {
            // Real absent-actor template: a failed alternate mount cannot
            // supply negative evidence, and another bar with an exact PARAM
            // relationship protects the whole scoped entity from deletion.
            let original = load.displays.clone();
            let original_failures = load.failures;
            let map = u32::from_be_bytes([60, 38, 54, 0]);
            let ghost = original
                .iter()
                .find(|d| d.completion_flag == Some(1038540800))
                .expect("archived residual template");
            load.scene_untrusted.insert(map);
            load.filter_scenes();
            assert!(load.excluded.is_empty());
            assert_eq!(load.displays.len(), original.len());
            load.scene_untrusted.remove(&map);
            load.displays = original.clone();
            load.failures = original_failures;
            let mut located = ghost.clone();
            located.completion_flag = Some(load.areas.iter().find(|a| a.flag != 0).unwrap().flag);
            assert!(load.param_locates(&located));
            load.displays.push(located);
            load.filter_scenes();
            assert!(load.excluded.is_empty());
            assert_eq!(load.displays.len(), original.len() + 1);
            load.displays = original;
            load.failures = original_failures;
        }
        load.filter_scenes();
    }
    // A full directory superset exposes omissions in the production resource
    // plan. Both sides use the actual parser/discovery/grouping, not a frozen
    // reader copy or an ignored pre-generated inventory.
    let mut inventory_paths = std::fs::read_dir(&events)?
        .map(|entry| entry.map(|e| e.path()))
        .collect::<std::io::Result<Vec<_>>>()?;
    inventory_paths.retain(|path| path.extension().is_some_and(|e| e == "emevd"));
    inventory_paths.sort();
    assert!(
        inventory_paths.len() <= MAX_FILES,
        "archive enumeration exceeds audit budget"
    );
    let mut inventory_scripts = Vec::new();
    let mut inventory_bytes = 0usize;
    for path in inventory_paths {
        let name = path
            .file_name()
            .and_then(|n| n.to_str())
            .ok_or("invalid inventory filename")?;
        let bytes = std::fs::read(&path)?;
        inventory_bytes = inventory_bytes
            .checked_add(bytes.len())
            .ok_or("inventory bytes overflow")?;
        if inventory_bytes > 64 * 1024 * 1024 {
            return Err("audit archive byte budget exceeded".into());
        }
        inventory_scripts.push(parse_emevd(name, &bytes).map_err(|e| format!("{name}: {e:?}"))?);
    }
    let mut inventory_displays = Vec::new();
    for script in &inventory_scripts {
        if script.map_id.is_none() {
            continue;
        }
        inventory_displays.extend(
            discover_boss_displays(script, &inventory_scripts)
                .map_err(|e| format!("{}: {e:?}", script.name))?
                .displays,
        );
    }
    let inventory_groups = encounter_groups(&inventory_displays, &Relations::default());
    let inventory: BTreeMap<_, _> = old
        .iter()
        .map(|entry| {
            (
                entry.boss.flag_id,
                archived_record(entry, &inventory_groups, &load.texts),
            )
        })
        .collect();
    let mut defeats = Vec::new();
    let mut missing = BTreeSet::new();
    for script in &load.scripts {
        if script.map_id.is_none() {
            continue;
        }
        let found = discover_boss_displays(script, &load.scripts)
            .map_err(|e| format!("{}: {e:?}", script.name))?;
        defeats.extend(found.defeats);
        missing.extend(found.missing_events);
    }
    let catalog = load.catalog(host(), &relations);
    let groups = encounter_groups(&load.displays, &relations);
    assert_archive_member_cases(&groups);
    if selected_scope {
        for (primary, completion) in [
            (1034420800, vec![1034420800]),
            (1041520800, vec![1041520800, 1037510800]),
            (31000800, vec![31000800]),
        ] {
            let row = catalog
                .rows
                .iter()
                .find(|row| row.flag == primary)
                .ok_or("configured primary absent from production catalogue")?;
            assert_eq!(row.completion, completion);
        }
    }
    assert_eq!(
        groups.len(),
        catalog.rows.len(),
        "audit must consume the production grouping"
    );
    let mut battles = Vec::new();
    let mut review = Vec::new();
    let mut used_defeats = BTreeSet::new();
    for (index, group) in groups.iter().enumerate() {
        let mut names = Vec::new();
        for d in &group.members {
            let name = load
                .texts
                .npc(d.name_id as i32)
                .map(str::to_owned)
                .unwrap_or_else(|| format!("NpcName#{}", d.name_id));
            if !names.contains(&name) {
                names.push(name);
            }
        }
        let title = names.join(" / ");
        let catalog_rows: Vec<_> = catalog
            .rows
            .iter()
            .enumerate()
            .filter(|(_, r)| r.flag == group.flag && r.name == title)
            .map(|(i, _)| i)
            .collect();
        assert!(
            !catalog_rows.is_empty(),
            "production battle absent from catalogue: {title}"
        );
        let (parts, edges) = components(&group.members, &defeats, group.flag);
        if parts.len() > 1 {
            review.push(index);
        }
        let mut members = Vec::new();
        for (member_index, d) in group.members.iter().enumerate() {
            let inherited: Vec<_> = group
                .members
                .iter()
                .enumerate()
                .filter(|(_, other)| {
                    same_entity(d, other) && other.completion_flag == Some(group.flag)
                })
                .map(|(i, _)| i)
                .collect();
            let matches: Vec<_> = defeats
                .iter()
                .enumerate()
                .filter_map(|(defeat_index, defeat)| {
                    let reasons = defeat_matches(defeat, d, group.flag);
                    if reasons.is_empty() {
                        return None;
                    }
                    used_defeats.insert(defeat_index);
                    Some(json!({"reasons":reasons,"defeat":format!("D{:05}",defeat_index+1)}))
                })
                .collect();
            members.push(json!({"member":member_index,"map":map_label(d.map_id),
                "runtime_variant":d.runtime_variant,"entity":d.entity_id,"name_id":d.name_id,
                "game_name":load.texts.npc(d.name_id as i32),"healthbar_slot":d.slot,
                "original_completion_flag":d.completion_flag,"group_completion_flag":group.flag,
                "completion_basis":if d.completion_flag.is_some_and(|flag| flag != group.flag && relations.canonical(flag)==group.flag){"external_relationship_canonical_primary_with_original_event_defeat_resolution"}
                    else if d.completion_flag.is_some(){"production_event_read_write_defeat_resolution"}
                    else if group.flag!=0{"same_exact_map_variant_entity_unique_resolved_flag"}else{"unresolved"},
                "inherited_from_members":if d.completion_flag.is_none(){inherited}else{Vec::new()},
                "display_guard_flags":d.guard_flags,"display_related_entities":d.related_entities,
                "display_proof":proof(&d.proof),"matching_defeat_evidence":matches}));
        }
        let maps: BTreeSet<_> = group.members.iter().map(|m| map_label(m.map_id)).collect();
        let variants: BTreeSet<_> = group.members.iter().map(|m| m.runtime_variant).collect();
        let scoped_entities: BTreeSet<_> = group
            .members
            .iter()
            .map(|m| (m.map_id, m.runtime_variant, m.entity_id))
            .collect();
        let name_ids: BTreeSet<_> = group.members.iter().map(|m| m.name_id).collect();
        battles.push(json!({"battle":format!("R{:03}",index+1),"completion_flag":group.flag,
            "name":title,"catalogue_rows":catalog_rows,
            "raw_member_count":group.members.len(),"scoped_entity_count":scoped_entities.len(),
            "name_id_count":name_ids.len(),"maps":maps,"runtime_variants":variants,
            "cross_map_or_variant":maps.len()>1 || variants.len()>1,
            "production_merge_basis":if group.members.iter().any(|m|m.completion_flag.is_some_and(|flag|flag!=group.flag && relations.canonical(flag)==group.flag)){"external_relationship_and_resolved_completion_flags"}
                else if group.flag!=0{"same_resolved_completion_flag"}else{"same_exact_map_variant_entity"},
            "structural_components":parts,"structural_edges":edges,
            "structural_review_required":parts.len()>1,
            "old_entries":old.iter().enumerate().filter(|(_,o)|group.flag!=0 && o.boss.flag_id==group.flag)
                .map(|(i,_)|format!("O{:03}",i+1)).collect::<Vec<_>>(),"members":members}));
    }
    let mut rows = Vec::new();
    let mut csv = String::from(
        "old_record,old_region,old_boss,old_place,old_flag,rust_battles,rust_names,members,structural_components,name_review,expected_named_members,missing_named_members,lod_member_aliases,missing_display_instructions,lod_display_aliases\r\n",
    );
    for (i, entry) in old.iter().enumerate() {
        let matches: Vec<_> = groups
            .iter()
            .enumerate()
            .filter(|(_, g)| g.flag != 0 && g.flag == entry.boss.flag_id)
            .map(|(i, _)| i)
            .collect();
        let ids: Vec<_> = matches
            .iter()
            .map(|&i| battles[i]["battle"].as_str().unwrap())
            .collect();
        let names: Vec<_> = matches
            .iter()
            .map(|&i| battles[i]["name"].as_str().unwrap())
            .collect();
        let entities: Vec<_> = matches
            .iter()
            .flat_map(|&i| {
                groups[i].members.iter().map(|m| {
                    format!(
                        "{}/{}/{}/{}",
                        map_label(m.map_id),
                        m.runtime_variant,
                        m.entity_id,
                        m.name_id
                    )
                })
            })
            .collect();
        let parts: usize = matches
            .iter()
            .map(|&i| {
                battles[i]["structural_components"]
                    .as_array()
                    .unwrap()
                    .len()
            })
            .sum();
        let name_review = names.len() != 1 || names[0] != entry.boss.boss;
        let archived = inventory
            .get(&entry.boss.flag_id)
            .ok_or("old record absent from full member inventory")?;
        let actual: Vec<_> = matches
            .iter()
            .flat_map(|&i| groups[i].members.iter().copied())
            .collect();
        let coverage = member_coverage(archived, &actual)?;
        let id = format!("O{:03}", i + 1);
        rows.push(json!({"old_record":id,"old_region":entry.region,"old_name":entry.boss.boss,
            "old_place":entry.boss.place,"old_completion_flag":entry.boss.flag_id,
            "rust_battles":ids,"name_review":name_review,
            "member_coverage":coverage,
            "mapping_basis":"old_primary_completion_flag_locates_group; event_members_and_structural_evidence_expanded_separately"}));
        let fields = [
            id,
            entry.region.clone(),
            entry.boss.boss.clone(),
            entry.boss.place.clone(),
            entry.boss.flag_id.to_string(),
            ids.join(";"),
            names.join(";"),
            entities.join(";"),
            parts.to_string(),
            name_review.to_string(),
            coverage["expected_named_members"]
                .as_array()
                .unwrap()
                .len()
                .to_string(),
            coverage["missing_named_members"].to_string(),
            coverage["lod_alias_members"].to_string(),
            coverage["missing_display_instructions"]
                .as_array()
                .unwrap()
                .len()
                .to_string(),
            coverage["lod_alias_display_instructions"].to_string(),
        ];
        csv.push_str(&fields.iter().map(csv_cell).collect::<Vec<_>>().join(","));
        csv.push_str("\r\n");
    }
    let missing_old: Vec<_> = rows
        .iter()
        .filter(|r| r["rust_battles"].as_array().unwrap().is_empty())
        .map(|r| r["old_record"].clone())
        .collect();
    let one_to_many: Vec<_> = rows
        .iter()
        .filter(|r| r["rust_battles"].as_array().unwrap().len() > 1)
        .map(|r| r["old_record"].clone())
        .collect();
    let many_to_one: Vec<_> = battles
        .iter()
        .filter(|b| b["old_entries"].as_array().unwrap().len() > 1)
        .map(|b| b["battle"].clone())
        .collect();
    let extras: Vec<_> = battles
        .iter()
        .filter(|b| b["old_entries"].as_array().unwrap().is_empty())
        .map(|b| b["battle"].clone())
        .collect();
    let summary = json!({"old_records":old.len(),"rust_battles":battles.len(),"raw_bar_members":load.displays.len(),
        "static_raw_bar_members":static_display_count,"scene_excluded_bar_members":load.excluded.len(),
        "scene_inputs":scene_inputs.len(),"scene_untrusted_maps":load.scene_untrusted.len(),
        "catalogue_rows":catalog.rows.len(),"old_one_to_one":rows.len()-missing_old.len()-one_to_many.len(),
        "missing_old":missing_old,"one_old_to_many_battles":one_to_many,"many_old_to_one_battle":many_to_one,
        "extra_battles":extras,"structurally_disconnected_battles":review.iter().map(|&i|battles[i]["battle"].clone()).collect::<Vec<_>>(),
        "name_review_rows":rows.iter().filter(|r|r["name_review"]==true).count(),"unknown_battles":catalog.unknown_count,
        "old_records_with_complete_named_members":rows.iter().filter(|r|r["member_coverage"]["all_archived_named_members_present"]==true).count(),
        "old_records_with_missing_raw_displays":rows.iter().filter(|r|!r["member_coverage"]["missing_display_instructions"].as_array().unwrap().is_empty()).count(),
        "old_multi_label_records":inventory.values().filter(|r|r.multi_label).count(),
        "inventory_scripts":inventory_scripts.len(),"inventory_raw_bar_members":inventory_displays.len(),
        "multi_member_battles":battles.iter().filter(|b|b["raw_member_count"].as_u64().unwrap()>1).map(|b|b["battle"].clone()).collect::<Vec<_>>(),
        "known_cross_map_or_variant_battles":battles.iter().filter(|b|b["completion_flag"]!=0 && b["cross_map_or_variant"]==true).map(|b|b["battle"].clone()).collect::<Vec<_>>(),
        "known_cross_map_count":battles.iter().filter(|b|b["completion_flag"]!=0 && b["maps"].as_array().unwrap().len()>1).count(),
        "known_cross_variant_count":battles.iter().filter(|b|b["completion_flag"]!=0 && b["runtime_variants"].as_array().unwrap().len()>1).count(),
        "scripts":load.scripts.len(),"requested_paths":load.queued.len(),"unresolved_event_calls":missing.len()});
    let scope_name = if selected_scope { "selected" } else { "static" };
    let record_scope = if selected_scope {
        "production selected scope using external cross-location relationships and available scene evidence; no live-game observation"
    } else {
        "static EMEVD candidates; not a count of confirmed live battles or MSB-present entities"
    };
    let artifact = json!({"schema":1,"cpp_commit":commit,"summary":summary,
        "audit_scope":scope_name,"record_scope":record_scope,
        "relationship_configuration":relationship_config,
        "configuration_inputs":if selected_scope{vec!["configs/boss-encounters.json"]}else{Vec::<&str>::new()},
        "scene_inputs":scene_inputs,
        "scene_evidence":{"transport":"explicit decompressed map-name MSB exports, fed once per requested map; native VFS alias availability or priority is not inferred",
            "requests":scene_requests,"untrusted_maps":load.scene_untrusted.iter().map(|&map|map_label(map)).collect::<Vec<_>>(),
            "candidate_checks":scene_checks,"loading_failures":load.failures,
            "excluded_displays":load.excluded.iter().map(|d|json!({"map":map_label(d.map_id),"runtime_variant":d.runtime_variant,
                "entity":d.entity_id,"name_id":d.name_id,"completion_flag":d.completion_flag,
                "guard_flags":d.guard_flags,"related_entities":d.related_entities,"proof":proof(&d.proof)})).collect::<Vec<_>>()},
        "member_comparison_scope":"same production parser/grouping over the full explicit directory versus the production Param-derived resource plan; not an independent instruction decoder oracle",
        "limits":["Old JSON contains no entity/event identity. Its primary flag locates candidate groups, not semantic proof of the battle boundary.",
            "Disconnected structural components require review; they are not automatically declared separate battles.",
            "FMG names are presentation evidence, not a grouping criterion. English title differences are review items, not missing-boss proofs.",
            "Reachable enabled EMEVD bar instructions do not by themselves prove that an entity exists in MSB or that a live-game bar was observed; selected scope additionally uses only the explicitly available scene inputs.",
            "Place conditional flags are unknown in this offline audit; locality is not checked against the manually authored old places."],
        "old_to_rust":rows,"rust_battles":battles,"catalogue":catalog.rows.iter().enumerate().map(|(i,r)|json!({"row":i,
            "flag":r.flag,"completion_flags":r.completion,"name":r.name,"place":r.place,"group":catalog.groups[r.group].name,
            "group_id":catalog.groups[r.group].id,"map":map_label(r.map_id)})).collect::<Vec<_>>(),
        "defeat_events":used_defeats.iter().map(|&i|{
            let mut value=defeat(&defeats[i]); value["defeat"]=json!(format!("D{:05}",i+1)); value
        }).collect::<Vec<_>>(),"different_flag_correlations":correlations(&groups,&defeats),
        "resource_plan":load.queued,"scripts":load.scripts.iter().map(|s|&s.name).collect::<Vec<_>>(),
        "inventory_scripts":inventory_scripts.iter().map(|s|&s.name).collect::<Vec<_>>(),
        "absent_resources":absent,"unresolved_event_calls":missing});
    write_json(&output.join("audit.json"), &artifact)?;
    std::fs::write(output.join("old-to-rust.csv"), csv)?;
    let mut md = format!(
        "# 旧 Boss 207 项到当前目录记录的逐项审计\n\n本次审计范围：`{scope_name}`。选定范围从实际 `configs/boss-encounters.json` 读取跨场关系，完整配置收录于 JSON 元数据；原始静态范围不应用外部关系。完成旗标列表见各目录行的 `completion_flags`，不代表复活写入清单。\n\n固定 C++ 提交：`{commit}`。本产物复用生产 Param 资源计划、事件发现和 `encounter_groups`；另直接遍历显式归档目录形成完整资源上集，不依赖临时库存或冻结读取器。\n\n| 核对项 | 结果 |\n|---|---:|\n| 旧记录 | {} |\n| 当前目录记录 | {} |\n| 生产计划脚本／全目录脚本 | {} / {} |\n| 生产具名条指令／全目录具名条指令 | {} / {} |\n| 旧项主旗标定位一对一 | {} |\n| 旧项具名成员完整 | {} |\n| 缺少原归档显示指令的旧项 | {} |\n| 多成员候选组 | {} |\n| 同旗标跨资源地图／跨 runtime variant | {} / {} |\n\n静态候选不等于已观察到的游戏战斗，场景实体证据的可用情况另列于 JSON；本工具未观察实时生命条，不能把候选总数宣称为真实 Boss 总数。\n\n旧 JSON 没有实体／事件身份。完成旗标仅用于定位候选组；各组完整成员和死亡事件证明见 `audit.json`，逐项映射见 `old-to-rust.csv`。名称差异只标记待核对，不作为合并或缺失判定。成员完整性比较 map、variant、entity、name ID、slot、原事件／指令／实例旗标、guards 和 related entities；两个输入都使用同一生产解析器，因此这是资源计划覆盖证明，不是独立解码器差分。\n\n## 缺少直接结构连接的组\n\n分量之间没有本工具列出的直接事件连接时需要人工核对，不自动认定为不同战斗。\n\n| 当前组 | 完成旗标 | 名称 | 分量 | 旧项 |\n|---|---:|---|---:|---|\n",
        old.len(),
        groups.len(),
        load.scripts.len(),
        inventory_scripts.len(),
        load.displays.len(),
        inventory_displays.len(),
        summary["old_one_to_one"],
        summary["old_records_with_complete_named_members"],
        summary["old_records_with_missing_raw_displays"],
        summary["multi_member_battles"].as_array().unwrap().len(),
        summary["known_cross_map_count"],
        summary["known_cross_variant_count"]
    );
    for &i in &review {
        let b = &battles[i];
        md.push_str(&format!(
            "| {} | {} | {} | {} | {} |\n",
            b["battle"].as_str().unwrap(),
            b["completion_flag"],
            md_cell(b["name"].as_str().unwrap()),
            b["structural_components"].as_array().unwrap().len(),
            b["old_entries"]
                .as_array()
                .unwrap()
                .iter()
                .map(|v| v.as_str().unwrap())
                .collect::<Vec<_>>()
                .join(", ")
        ));
    }
    md.push_str("\n## 全部多成员候选组\n\n原始指令数包含重新显示、换名及 LOD 资源分支，不能作为物理角色数。名称字符串去重只影响显示，不参与成员身份或归并。\n\n| 当前组 | 完成旗标 | 名称 | 指令数 | scoped entity 数 | name ID 数 |\n|---|---:|---|---:|---:|---:|\n");
    for b in battles
        .iter()
        .filter(|b| b["raw_member_count"].as_u64().unwrap() > 1)
    {
        md.push_str(&format!(
            "| {} | {} | {} | {} | {} | {} |\n",
            b["battle"].as_str().unwrap(),
            b["completion_flag"],
            md_cell(b["name"].as_str().unwrap()),
            b["raw_member_count"],
            b["scoped_entity_count"],
            b["name_id_count"]
        ));
    }
    md.push_str("\nGodskin Duo 使用一个公共具名条 `13000850`；两名实际角色并不各有独立具名条，因此不能用标题的 Duo 或当前名称字符串数判断漏成员。回归直接核对 chiefs 的两个实体、Crystalian 三个实体、Gargoyles 两个实体、Scadutree Avatar 三阶段实体，以及 Godskin 的单公共条。\n\n## 同完成旗标跨资源地图／variant\n\n| 当前组 | 完成旗标 | 名称 | 资源地图 | runtime variants |\n|---|---:|---|---|---|\n");
    for b in battles
        .iter()
        .filter(|b| b["completion_flag"] != 0 && b["cross_map_or_variant"] == true)
    {
        md.push_str(&format!(
            "| {} | {} | {} | {} | {} |\n",
            b["battle"].as_str().unwrap(),
            b["completion_flag"],
            md_cell(b["name"].as_str().unwrap()),
            b["maps"]
                .as_array()
                .unwrap()
                .iter()
                .map(|v| v.as_str().unwrap())
                .collect::<Vec<_>>()
                .join(", "),
            b["runtime_variants"]
        ));
    }
    md.push_str("\n## 全目录上集与生产资源计划的 LOD 差异\n\n只对大地图 60／61 的同 tile `_10`／`_20` 资源与 `_00` 做别名核对；runtime variant 不折叠。以下差异还要求展开后的 bar 操作数、事件／指令／实例旗标、guards 和 related entities 相同。\n\n| 旧项 | 原归档地图 | 生产地图 | entity / name ID | 原指令 | 生产指令 |\n|---|---|---|---|---|---|\n");
    for r in &rows {
        for a in r["member_coverage"]["lod_alias_display_evidence"]
            .as_array()
            .unwrap()
        {
            md.push_str(&format!(
                "| {} | {} | {} | {} / {} | {}#{} | {}#{} |\n",
                r["old_record"].as_str().unwrap(),
                a["archive_map"].as_str().unwrap(),
                a["production_map"].as_str().unwrap(),
                a["entity"],
                a["name_id"],
                a["archive_proof"]["event"],
                a["archive_proof"]["instruction"],
                a["production_proof"]["event"],
                a["production_proof"]["instruction"]
            ));
        }
    }
    let graph = &artifact["different_flag_correlations"];
    md.push_str("\n## 不同完成旗标之间的相关关系\n\n这些超边仅报告相关性，未参与生产归并。共同全局／任务 guard、同一个 AI／死亡对象，或共用收尾函数均不能独立证明是一场战斗。JSON 保留完整 guard 超边、scoped actor 角色及仅共享 guard 的候选对。\n\n| 共享 guard | 是否当前候选主完成旗标 | 涉及候选数 |\n|---|---|---:|\n");
    for edge in graph["shared_guard_hyperedges"].as_array().unwrap() {
        md.push_str(&format!(
            "| {} | {} | {} |\n",
            edge["guard"],
            edge["is_known_battle_completion_flag"],
            edge["battles"].as_array().unwrap().len()
        ));
    }
    md.push_str(&format!("\n仅共享 guard、没有本工具列出的 actor／显示事件 scope 关联的候选对为 {} 对。它们不会被合并。\n\n| 地图／variant | 共享对象 | 各候选中的角色 |\n|---|---:|---|\n",graph["only_shared_guards_pairs"].as_array().unwrap().len()));
    for edge in graph["shared_actor_hyperedges"].as_array().unwrap() {
        let roles: Vec<_> = edge["battles"]
            .as_array()
            .unwrap()
            .iter()
            .map(|b| {
                format!(
                    "{}: {}",
                    b["battle"].as_str().unwrap(),
                    b["roles"]
                        .as_array()
                        .unwrap()
                        .iter()
                        .map(|v| v.as_str().unwrap())
                        .collect::<Vec<_>>()
                        .join(", ")
                )
            })
            .collect();
        md.push_str(&format!(
            "| {} / {} | {} | {} |\n",
            edge["map"].as_str().unwrap(),
            edge["runtime_variant"],
            edge["entity"],
            md_cell(&roles.join("; "))
        ));
    }
    md.push_str("\nHinterland 的 `2050470800` 与 `2050480860` 保持两条独立持久完成记录。回归同时核对两个 group 及各自显示实体；共享 guards／相关对象没有触发 union。更强的阶段、伤害桥和独立奖励语义需结合原指令核对，不能从这张相关图自动推断。\n");
    md.push_str("\n## 旧名单之外的组\n\n| 当前组 | 完成旗标 | 名称 |\n|---|---:|---|\n");
    for b in &battles {
        if b["old_entries"].as_array().unwrap().is_empty() {
            md.push_str(&format!(
                "| {} | {} | {} |\n",
                b["battle"].as_str().unwrap(),
                b["completion_flag"],
                md_cell(b["name"].as_str().unwrap())
            ));
        }
    }
    md.push_str("\n## 旧标题与当前游戏名称不同的项\n\n| 旧项 | 旧标题 | 当前组 | 当前游戏名称 |\n|---|---|---|---|\n");
    for (i, r) in rows
        .iter()
        .enumerate()
        .filter(|(_, r)| r["name_review"] == true)
    {
        let ids = r["rust_battles"].as_array().unwrap();
        let titles: Vec<_> = ids
            .iter()
            .filter_map(|id| battles.iter().find(|b| b["battle"] == *id))
            .map(|b| b["name"].as_str().unwrap())
            .collect();
        md.push_str(&format!(
            "| O{:03} | {} | {} | {} |\n",
            i + 1,
            md_cell(&old[i].boss.boss),
            ids.iter()
                .map(|id| id.as_str().unwrap())
                .collect::<Vec<_>>()
                .join(", "),
            md_cell(&titles.join("; "))
        ));
    }
    std::fs::write(output.join("audit.md"), md)?;
    println!("boss_catalog_audit {}", serde_json::to_string(&summary)?);
    println!("audit_output={}", output.display());
    Ok(())
}
