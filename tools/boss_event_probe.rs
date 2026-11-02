//! Optional original-resource evidence; never needed by a production build.
//! rustc --edition 2024 tools/boss_event_probe.rs -O -o target/boss-event-probe.exe
//! target/boss-event-probe.exe <directory-of-decompressed-original-emevds>
#[path = "../crates/game/src/boss_events.rs"]
mod boss_events;
use boss_events::{discover_boss_displays, parse_emevd};
use std::collections::BTreeSet;

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let directory = std::env::args_os()
        .nth(1)
        .ok_or("supply original EMEVD directory")?;
    let mut scripts = Vec::new();
    let mut paths = std::fs::read_dir(directory)?
        .map(|e| e.map(|e| e.path()))
        .collect::<std::io::Result<Vec<_>>>()?;
    paths.sort();
    for path in paths {
        if path.extension().and_then(|e| e.to_str()) != Some("emevd") {
            continue;
        }
        let name = path
            .file_name()
            .and_then(|n| n.to_str())
            .ok_or("invalid filename")?;
        scripts
            .push(parse_emevd(name, &std::fs::read(&path)?).map_err(|e| format!("{name}: {e:?}"))?);
    }
    let mut instances = 0;
    let mut displays = 0;
    let mut associated = 0;
    let mut flags = BTreeSet::new();
    let mut unresolved = BTreeSet::new();
    let mut missing = BTreeSet::new();
    for map in &scripts {
        if map.map_id.is_none() {
            continue;
        }
        let result =
            discover_boss_displays(map, &scripts).map_err(|e| format!("{}: {e:?}", map.name))?;
        instances += result.instances;
        for display in result.displays {
            displays += 1;
            if let Some(flag) = display.completion_flag {
                associated += 1;
                flags.insert(flag);
            } else {
                unresolved.insert((
                    map.name.clone(),
                    display.entity_id,
                    display.name_id,
                    display.proof.event_id,
                ));
            }
            println!(
                "bar,{},{},{},{},{},{},{},{}",
                map.name,
                display.runtime_variant,
                display.entity_id,
                display.name_id,
                display.slot,
                display.completion_flag.unwrap_or(0),
                display.proof.event_id,
                display.proof.instruction
            );
        }
        missing.extend(result.missing_events);
    }
    eprintln!(
        "scripts={}, instances={}, enabled_bars={}, associated_bars={}, completion_flags={}, unresolved_displays={}, missing_events={}",
        scripts.len(),
        instances,
        displays,
        associated,
        flags.len(),
        unresolved.len(),
        missing.len()
    );
    for item in unresolved {
        eprintln!("unresolved,{},{},{},{}", item.0, item.1, item.2, item.3);
    }
    Ok(())
}
