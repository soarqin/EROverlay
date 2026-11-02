//! Initialization-only EMEVD evidence for named boss health bars.
//!
//! No NPC/stat heuristic defines a boss here: the source is enabled 2003[11].
//! Completion flags require a conditional read and a true write in an event
//! which handles a boss defeat, then an exact entity/guard relationship.
use std::collections::{BTreeMap, BTreeSet, VecDeque};

const MAX_BYTES: usize = 64 * 1024 * 1024;
const MAX_EVENTS: usize = 32768;
const MAX_INSTRUCTIONS: usize = 262144;
const MAX_ARGUMENTS: usize = 4096;
const MAX_INSTANCES: usize = 65536;
const MAX_EXPANDED_INSTRUCTIONS: usize = 4 * 1024 * 1024;
const ROOTS: [u32; 6] = [0, 50, 100, 150, 200, 250];

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum EventError {
    Truncated,
    Unsupported,
    Invalid,
    Budget,
    MissingParameters,
}
type Result<T> = std::result::Result<T, EventError>;

fn slice(bytes: &[u8], offset: usize, size: usize) -> Result<&[u8]> {
    bytes
        .get(offset..offset.checked_add(size).ok_or(EventError::Truncated)?)
        .ok_or(EventError::Truncated)
}
fn u32_at(b: &[u8], p: usize) -> Result<u32> {
    Ok(u32::from_le_bytes(slice(b, p, 4)?.try_into().unwrap()))
}
fn u64_at(b: &[u8], p: usize) -> Result<u64> {
    Ok(u64::from_le_bytes(slice(b, p, 8)?.try_into().unwrap()))
}
fn usize_at(b: &[u8], p: usize) -> Result<usize> {
    usize::try_from(u64_at(b, p)?).map_err(|_| EventError::Budget)
}
fn range_in(table: (usize, usize), offset: usize, size: usize, stride: usize) -> Result<usize> {
    let length = table.0.checked_mul(stride).ok_or(EventError::Budget)?;
    if offset.checked_add(size).ok_or(EventError::Budget)? > length {
        return Err(EventError::Invalid);
    }
    table.1.checked_add(offset).ok_or(EventError::Budget)
}

#[derive(Debug, Clone)]
struct Instruction {
    bank: u32,
    opcode: u32,
    arguments: Vec<u8>,
    common_ordinal: Option<u32>,
}
#[derive(Debug, Clone)]
struct Parameter {
    instruction: usize,
    target: usize,
    source: usize,
    size: usize,
}
#[derive(Debug, Clone)]
struct Event {
    instructions: Vec<Instruction>,
    parameters: Vec<Parameter>,
}
#[derive(Debug, Clone)]
pub struct EventScript {
    pub name: String,
    pub map_id: Option<u32>,
    pub linked: Vec<String>,
    events: BTreeMap<u32, Event>,
}

fn normalized_name(name: &str) -> Result<String> {
    let name = name.rsplit(['/', '\\']).next().ok_or(EventError::Invalid)?;
    let name = name.strip_suffix(".dcx").unwrap_or(name);
    if name.len() > 128 || !name.ends_with(".emevd") {
        return Err(EventError::Invalid);
    }
    Ok(name.to_owned())
}

/// Packed mAA_BB_CC_DD bytes; the final byte is the resource map version/scale.
pub fn map_id(name: &str) -> Option<u32> {
    let name = name.strip_suffix(".dcx").unwrap_or(name);
    let name = name.strip_suffix(".emevd")?.strip_prefix('m')?;
    let mut values = name.split('_');
    let mut result = 0u32;
    for _ in 0..4 {
        let component = values.next()?;
        if component.len() != 2 || !component.bytes().all(|b| b.is_ascii_digit()) {
            return None;
        }
        result = result << 8 | component.parse::<u32>().ok()?;
    }
    values.next().is_none().then_some(result)
}

/// Parses decompressed PC EMEVD v205 bytes. Does not read files or decompress.
pub fn parse_emevd(name: &str, bytes: &[u8]) -> Result<EventScript> {
    if bytes.len() > MAX_BYTES {
        return Err(EventError::Budget);
    }
    if slice(bytes, 0, 12)? != b"EVD\0\0\xff\x01\xff\xcd\0\0\0"
        || bytes.len() < 144
        || u32_at(bytes, 12)? as usize != bytes.len()
    {
        return Err(EventError::Unsupported);
    }
    let name = normalized_name(name)?;
    let mut tables = [(0, 0); 8];
    for (i, stride) in [48usize, 32, 1, 32, 32, 8, 1, 1].into_iter().enumerate() {
        let count = usize_at(bytes, 16 + i * 16)?;
        let offset = usize_at(bytes, 24 + i * 16)?;
        slice(
            bytes,
            offset,
            count.checked_mul(stride).ok_or(EventError::Budget)?,
        )?;
        tables[i] = (count, offset);
    }
    if tables[0].0 > MAX_EVENTS || tables[1].0 > MAX_INSTRUCTIONS || tables[5].0 > 32 {
        return Err(EventError::Budget);
    }
    let mut events = BTreeMap::new();
    let mut common_offsets = BTreeMap::new();
    let mut total_instructions = 0usize;
    let mut total_parameters = 0usize;
    for index in 0..tables[0].0 {
        let p = tables[0].1 + index * 48;
        let id = u32::try_from(u64_at(bytes, p)?).map_err(|_| EventError::Unsupported)?;
        let count = usize_at(bytes, p + 8)?;
        let offset = usize_at(bytes, p + 16)?;
        let parameter_count = usize_at(bytes, p + 24)?;
        let parameter_offset = usize_at(bytes, p + 32)?;
        if u32_at(bytes, p + 40)? > 2 || u32_at(bytes, p + 44)? != 0 {
            return Err(EventError::Invalid);
        }
        let instruction_start = range_in(
            tables[1],
            if count == 0 { 0 } else { offset },
            count.checked_mul(32).ok_or(EventError::Budget)?,
            32,
        )?;
        let parameter_start = range_in(
            tables[4],
            if parameter_count == 0 {
                0
            } else {
                parameter_offset
            },
            parameter_count.checked_mul(32).ok_or(EventError::Budget)?,
            32,
        )?;
        total_instructions = total_instructions
            .checked_add(count)
            .ok_or(EventError::Budget)?;
        total_parameters = total_parameters
            .checked_add(parameter_count)
            .ok_or(EventError::Budget)?;
        if total_instructions > MAX_INSTRUCTIONS || total_parameters > MAX_INSTRUCTIONS {
            return Err(EventError::Budget);
        }
        let mut instructions = Vec::with_capacity(count);
        for i in 0..count {
            let q = instruction_start + i * 32;
            let bank = u32_at(bytes, q)?;
            let opcode = u32_at(bytes, q + 4)?;
            let size = usize_at(bytes, q + 8)?;
            if size > MAX_ARGUMENTS {
                return Err(EventError::Budget);
            }
            let argument_offset = usize_at(bytes, q + 16)?;
            let argument_start = if size == 0 {
                0
            } else {
                range_in(tables[6], argument_offset, size, 1)?
            };
            let common_ordinal = if opcode == 6 {
                let next = common_offsets.len() as u32;
                Some(*common_offsets.entry(argument_offset).or_insert(next))
            } else {
                None
            };
            instructions.push(Instruction {
                bank,
                opcode,
                arguments: slice(bytes, argument_start, size)?.to_vec(),
                common_ordinal,
            });
        }
        let mut parameters = Vec::with_capacity(parameter_count);
        for i in 0..parameter_count {
            let q = parameter_start + i * 32;
            let instruction = usize_at(bytes, q)?;
            let target = usize_at(bytes, q + 8)?;
            let source = usize_at(bytes, q + 16)?;
            let size = u32_at(bytes, q + 24)? as usize;
            if size == 0
                || source.checked_add(size).ok_or(EventError::Budget)? > MAX_ARGUMENTS
                || target.checked_add(size).ok_or(EventError::Budget)?
                    > instructions
                        .get(instruction)
                        .ok_or(EventError::Invalid)?
                        .arguments
                        .len()
            {
                return Err(EventError::Invalid);
            }
            parameters.push(Parameter {
                instruction,
                target,
                source,
                size,
            });
        }
        if events
            .insert(
                id,
                Event {
                    instructions,
                    parameters,
                },
            )
            .is_some()
        {
            return Err(EventError::Invalid);
        }
    }
    let mut linked = Vec::new();
    for i in 0..tables[5].0 {
        let start = usize_at(bytes, tables[5].1 + i * 8)?;
        let mut units = Vec::new();
        for j in 0..=4096 {
            let p = range_in(
                tables[7],
                start.checked_add(j * 2).ok_or(EventError::Budget)?,
                2,
                1,
            )?;
            let unit = u16::from_le_bytes(slice(bytes, p, 2)?.try_into().unwrap());
            if unit == 0 {
                break;
            }
            units.push(unit);
            if j == 4096 {
                return Err(EventError::Budget);
            }
        }
        linked.push(normalized_name(
            &String::from_utf16(&units).map_err(|_| EventError::Invalid)?,
        )?);
    }
    Ok(EventScript {
        map_id: map_id(&name),
        name,
        linked,
        events,
    })
}

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord)]
pub struct EventProof {
    pub source: String,
    pub event_id: u32,
    pub instruction: u32,
    pub event_completion_flag: Option<u32>,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BossDisplay {
    pub map_id: u32,
    pub runtime_variant: u8,
    pub entity_id: u32,
    pub name_id: u32,
    pub slot: u16,
    /// Only populated when a single read/write/defeat relationship is proved.
    pub completion_flag: Option<u32>,
    /// Exact conditional flag operands in the displaying event, for Param joins.
    pub guard_flags: Vec<u32>,
    /// Character death/HP/AI operands in the same instantiated display event.
    pub related_entities: Vec<u32>,
    pub proof: EventProof,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct BossDefeat {
    pub map_id: u32,
    pub runtime_variant: u8,
    pub entity_id: u32,
    /// Explicit dead/HP conditions in this same event, including duo members.
    pub death_entities: Vec<u32>,
    /// Flags both conditionally read and enabled by this same defeat event.
    pub flags: Vec<u32>,
    pub proof: EventProof,
}

#[derive(Debug, Default)]
pub struct Discovery {
    pub displays: Vec<BossDisplay>,
    pub defeats: Vec<BossDefeat>,
    pub missing_events: Vec<(String, u32)>,
    pub instances: usize,
}

#[derive(Debug, Clone, PartialEq, Eq, PartialOrd, Ord)]
struct Instance {
    script: usize,
    event: u32,
    slot: i16,
    parameters: Vec<u8>,
    completion: Option<u32>,
    variant: u8,
}

fn common_completion(map: Option<u32>, ordinal: u32, variant: u8) -> Result<u32> {
    let ordinal = (ordinal & 0xffff) % 1000;
    let Some(map) = map else {
        return Ok(5000 + ordinal);
    };
    let [area, block, sub, _] = map.to_be_bytes();
    let prefix = match area {
        60 => {
            (10 + u32::from(variant)) * 100000000
                + u32::from(block) * 1000000
                + u32::from(sub) * 10000
        }
        61 => {
            (20 + u32::from(variant)) * 100000000
                + u32::from(block) * 1000000
                + u32::from(sub) * 10000
        }
        _ => u32::from(area) * 1000000 + u32::from(block) * 10000,
    };
    prefix
        .checked_add(5000 + ordinal)
        .ok_or(EventError::Invalid)
}

fn resolved_flag(kind: u8, id: u32, completion: Option<u32>) -> Option<u32> {
    match kind {
        0 => Some(id),
        2 => id.checked_add(completion?),
        _ => None,
    }
    .filter(|v| *v != 0)
}

fn conditional_flag(i: &Instruction, completion: Option<u32>) -> Option<u32> {
    let kind = match (i.bank, i.opcode) {
        (3, 0) | (1003, 1 | 2 | 101) => *i.arguments.get(2)?,
        (1003, 0) => *i.arguments.get(1)?,
        _ => return None,
    };
    resolved_flag(kind, u32_at(&i.arguments, 4).ok()?, completion)
}

/// Expands the map's native constructor roots and linked event calls.
///
/// `scripts` must include every available linked resource (usually
/// common_func.emevd). Missing links are returned explicitly and never guessed.
/// Call on a worker after loading a bounded map catalogue, never while rendering.
pub fn discover_boss_displays(map: &EventScript, scripts: &[EventScript]) -> Result<Discovery> {
    let map_id = map.map_id.ok_or(EventError::Invalid)?;
    let mut resources: Vec<&EventScript> = Vec::new();
    for name in &map.linked {
        if let Some(script) = scripts.iter().find(|s| &s.name == name) {
            resources.push(script);
        }
    }
    resources.push(map);
    let local = resources.len() - 1;
    let field = matches!(map_id.to_be_bytes()[0], 60 | 61);
    let mut queue = VecDeque::new();
    for id in ROOTS {
        if map.events.contains_key(&id) {
            queue.push_back(Instance {
                script: local,
                event: id,
                slot: -1,
                parameters: Vec::new(),
                completion: None,
                variant: if field { (id / 100) as u8 } else { 0 },
            });
        }
    }
    let mut found = Discovery::default();
    let mut seen = BTreeSet::new();
    let mut expanded = 0usize;
    while let Some(instance) = queue.pop_front() {
        if seen.contains(&instance) {
            continue;
        }
        if seen.len() >= MAX_INSTANCES {
            return Err(EventError::Budget);
        }
        let script = resources[instance.script];
        let event = script
            .events
            .get(&instance.event)
            .ok_or(EventError::Invalid)?;
        expanded = expanded
            .checked_add(event.instructions.len())
            .ok_or(EventError::Budget)?;
        if expanded > MAX_EXPANDED_INSTRUCTIONS {
            return Err(EventError::Budget);
        }
        let mut instructions = event.instructions.clone();
        for p in &event.parameters {
            let bytes = slice(&instance.parameters, p.source, p.size)
                .map_err(|_| EventError::MissingParameters)?;
            instructions[p.instruction].arguments[p.target..p.target + p.size]
                .copy_from_slice(bytes);
        }
        let guards: BTreeSet<u32> = instructions
            .iter()
            .filter_map(|i| conditional_flag(i, instance.completion))
            .collect();
        let writes: BTreeSet<u32> = instructions
            .iter()
            .filter_map(|i| {
                if i.bank != 2003 || !matches!(i.opcode, 66 | 69) || *i.arguments.get(8)? != 1 {
                    return None;
                }
                resolved_flag(
                    *i.arguments.first()?,
                    u32_at(&i.arguments, 4).ok()?,
                    instance.completion,
                )
            })
            .collect();
        let related_entities: BTreeSet<u32> = instructions
            .iter()
            .filter_map(|i| {
                (i.bank == 4 && matches!(i.opcode, 0 | 2 | 9 | 14))
                    .then(|| u32_at(&i.arguments, 4).ok())
                    .flatten()
                    .filter(|id| *id != 0 && *id != 10000)
            })
            .collect();
        let death_entities: BTreeSet<u32> = instructions
            .iter()
            .filter_map(|i| {
                (i.bank == 4 && matches!(i.opcode, 0 | 2 | 14))
                    .then(|| u32_at(&i.arguments, 4).ok())
                    .flatten()
                    .filter(|id| *id != 0 && *id != 10000)
            })
            .collect();
        for (index, i) in instructions.iter().enumerate() {
            let proof = || EventProof {
                source: script.name.clone(),
                event_id: instance.event,
                instruction: index as u32,
                event_completion_flag: instance.completion,
            };
            if i.bank == 2003 && i.opcode == 11 {
                if i.arguments.len() != 16 {
                    return Err(EventError::Invalid);
                }
                let entity_id = u32_at(&i.arguments, 4)?;
                let name_id = u32_at(&i.arguments, 12)?;
                if i.arguments[0] == 1 && entity_id != 0 && name_id != 0 && name_id != u32::MAX {
                    found.displays.push(BossDisplay {
                        map_id,
                        runtime_variant: instance.variant,
                        entity_id,
                        name_id,
                        slot: u16::from_le_bytes(i.arguments[8..10].try_into().unwrap()),
                        completion_flag: None,
                        guard_flags: guards.iter().copied().collect(),
                        related_entities: related_entities.iter().copied().collect(),
                        proof: proof(),
                    });
                }
            }
            if i.bank == 2003 && i.opcode == 12 {
                if i.arguments.len() != 8 {
                    return Err(EventError::Invalid);
                }
                found.defeats.push(BossDefeat {
                    map_id,
                    runtime_variant: instance.variant,
                    entity_id: u32_at(&i.arguments, 0)?,
                    death_entities: death_entities.iter().copied().collect(),
                    flags: guards.intersection(&writes).copied().collect(),
                    proof: proof(),
                });
            }
            if i.bank != 2000 || !matches!(i.opcode, 0 | 6) {
                continue;
            }
            if i.arguments.len() < 8 {
                return Err(EventError::Invalid);
            }
            let mut slot = i16::from_le_bytes(i.arguments[..2].try_into().unwrap());
            let id = u32_at(&i.arguments, 4)?;
            let completion = if i.opcode == 6 {
                slot = -1;
                common_completion(
                    map.map_id,
                    i.common_ordinal.ok_or(EventError::Invalid)?,
                    instance.variant,
                )?
            } else {
                id.checked_add(slot.max(0) as u32)
                    .ok_or(EventError::Invalid)?
            };
            if let Some(target) = resources.iter().position(|s| s.events.contains_key(&id)) {
                if queue.len() >= MAX_INSTANCES {
                    return Err(EventError::Budget);
                }
                queue.push_back(Instance {
                    script: target,
                    event: id,
                    slot,
                    parameters: i.arguments[8..].to_vec(),
                    completion: Some(completion),
                    variant: instance.variant,
                });
            } else if found.missing_events.len() < 1024 {
                found.missing_events.push((script.name.clone(), id));
            }
        }
        seen.insert(instance);
    }
    found.instances = seen.len();
    for display in &mut found.displays {
        let mut candidates = BTreeSet::new();
        let mut guarded_candidates = BTreeSet::new();
        for defeat in &found.defeats {
            if defeat.runtime_variant != display.runtime_variant {
                continue;
            }
            for flag in &defeat.flags {
                if display.guard_flags.binary_search(flag).is_ok() {
                    guarded_candidates.insert(*flag);
                }
                if defeat.entity_id == display.entity_id
                    || defeat
                        .death_entities
                        .binary_search(&display.entity_id)
                        .is_ok()
                {
                    candidates.insert(*flag);
                }
            }
        }
        // A battle's display guard identifies its persistent defeat flag even
        // when its defeat event also repairs earlier quest/boss progression.
        if !guarded_candidates.is_empty() {
            candidates = guarded_candidates;
        }
        if candidates.is_empty() {
            for defeat in &found.defeats {
                if defeat.runtime_variant == display.runtime_variant
                    && display
                        .related_entities
                        .binary_search(&defeat.entity_id)
                        .is_ok()
                {
                    candidates.extend(defeat.flags.iter().copied());
                }
            }
        }
        if candidates.len() == 1 {
            display.completion_flag = candidates.into_iter().next();
        }
    }
    found.missing_events.sort();
    found.missing_events.dedup();
    Ok(found)
}

#[cfg(test)]
mod tests {
    use super::*;

    fn instruction(bank: u32, opcode: u32, arguments: &[u8]) -> Instruction {
        Instruction {
            bank,
            opcode,
            arguments: arguments.to_vec(),
            common_ordinal: (opcode == 6).then_some(0),
        }
    }
    fn args(values: &[u32]) -> Vec<u8> {
        values.iter().flat_map(|v| v.to_le_bytes()).collect()
    }
    fn script(name: &str, events: Vec<(u32, Vec<Instruction>, Vec<Parameter>)>) -> EventScript {
        EventScript {
            name: name.into(),
            map_id: map_id(name),
            linked: Vec::new(),
            events: events
                .into_iter()
                .map(|(id, instructions, parameters)| {
                    (
                        id,
                        Event {
                            instructions,
                            parameters,
                        },
                    )
                })
                .collect(),
        }
    }

    #[test]
    fn parameterized_common_bars_join_exact_defeat_evidence() {
        let mut map = script(
            "m60_42_36_00.emevd",
            vec![
                (
                    0,
                    vec![
                        instruction(2000, 6, &args(&[0, 90005870, 1042360800, 903251600])),
                        instruction(2000, 0, &args(&[0, 8000])),
                    ],
                    vec![],
                ),
                (
                    8000,
                    vec![
                        instruction(1003, 2, &args(&[256, 1042360800])),
                        instruction(2003, 12, &args(&[1042360800, 16])),
                        instruction(2003, 66, &args(&[0, 1042360800, 1])),
                    ],
                    vec![],
                ),
            ],
        );
        map.linked.push("common_func.emevd".into());
        let common = script(
            "common_func.emevd",
            vec![(
                90005870,
                vec![instruction(2003, 11, &args(&[1, 0, 0, 0]))],
                vec![
                    Parameter {
                        instruction: 0,
                        target: 4,
                        source: 0,
                        size: 4,
                    },
                    Parameter {
                        instruction: 0,
                        target: 12,
                        source: 4,
                        size: 4,
                    },
                ],
            )],
        );
        let result = discover_boss_displays(&map, &[common]).unwrap();
        assert_eq!(result.displays.len(), 1);
        let d = &result.displays[0];
        assert_eq!(
            (d.entity_id, d.name_id, d.completion_flag),
            (1042360800, 903251600, Some(1042360800))
        );
        assert_eq!(d.proof.event_completion_flag, Some(1042365000));
        assert_eq!(result.instances, 3);
    }

    #[test]
    fn multi_bars_and_phases_keep_shared_guard_and_variant() {
        let map = script(
            "m61_44_47_00.emevd",
            vec![
                (100, vec![instruction(2000, 0, &args(&[0, 500]))], vec![]),
                (
                    500,
                    vec![
                        instruction(1003, 2, &args(&[256, 77])),
                        instruction(2003, 11, &args(&[1, 120, 0, 900])),
                        instruction(2003, 11, &args(&[1, 121, 1, 901])),
                        instruction(2003, 12, &args(&[120, 16])),
                        instruction(2003, 66, &args(&[0, 77, 1])),
                    ],
                    vec![],
                ),
            ],
        );
        let result = discover_boss_displays(&map, &[]).unwrap();
        assert_eq!(
            result
                .displays
                .iter()
                .map(|d| (d.entity_id, d.name_id, d.completion_flag, d.runtime_variant))
                .collect::<Vec<_>>(),
            vec![(120, 900, Some(77), 1), (121, 901, Some(77), 1)]
        );
        assert_eq!(common_completion(map.map_id, 0, 1).unwrap(), 2144475000);
    }

    #[test]
    fn ambiguity_and_missing_definition_are_explicit() {
        let map = script(
            "m10_00_00_00.emevd",
            vec![(
                0,
                vec![
                    instruction(2000, 0, &args(&[0, 999])),
                    instruction(2003, 11, &args(&[1, 100, 0, 200])),
                    instruction(1003, 2, &args(&[256, 77])),
                    instruction(1003, 2, &args(&[256, 78])),
                    instruction(2003, 12, &args(&[100, 16])),
                    instruction(2003, 66, &args(&[0, 77, 1])),
                    instruction(2003, 66, &args(&[0, 78, 1])),
                ],
                vec![],
            )],
        );
        let result = discover_boss_displays(&map, &[]).unwrap();
        assert_eq!(result.displays[0].completion_flag, None);
        assert_eq!(result.displays[0].guard_flags, vec![77, 78]);
        assert_eq!(
            result.missing_events,
            vec![("m10_00_00_00.emevd".into(), 999)]
        );
    }

    #[test]
    fn binary_reader_rejects_short_tables_and_parameter_overruns() {
        for n in 0..144 {
            assert!(parse_emevd("m10_00_00_00.emevd", &vec![0; n]).is_err());
        }
        let mut b = vec![0u8; 224];
        b[..12].copy_from_slice(b"EVD\0\0\xff\x01\xff\xcd\0\0\0");
        b[12..16].copy_from_slice(&224u32.to_le_bytes());
        b[16..24].copy_from_slice(&1u64.to_le_bytes());
        b[24..32].copy_from_slice(&144u64.to_le_bytes());
        b[32..40].copy_from_slice(&1u64.to_le_bytes());
        b[40..48].copy_from_slice(&192u64.to_le_bytes());
        b[152..160].copy_from_slice(&1u64.to_le_bytes());
        assert!(parse_emevd("m10_00_00_00.emevd", &b).is_ok());
        b[160..168].copy_from_slice(&32u64.to_le_bytes());
        assert_eq!(
            parse_emevd("m10_00_00_00.emevd", &b).unwrap_err(),
            EventError::Invalid
        );
    }
}
