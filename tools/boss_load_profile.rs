//! Reproducible event-only CPU profile; no native IO, GPU or game/FPS claim.
//! rustc --edition 2024 -O tools/boss_load_profile.rs -o target/boss-load-profile.exe
//! boss-load-profile <raw-EMEVD-dir> <ordered-names.txt> <output-dir> [rounds]
#[path = "../crates/game/src/boss_events.rs"]
#[allow(dead_code)]
mod boss_events;
use boss_events::{discover_boss_displays, parse_emevd};
use std::{path::PathBuf, time::Instant};

fn main() -> Result<(), Box<dyn std::error::Error>> {
    let mut args = std::env::args_os().skip(1);
    let root = PathBuf::from(args.next().ok_or("missing EMEVD directory")?);
    let names = std::fs::read_to_string(args.next().ok_or("missing ordered names file")?)?;
    let out = PathBuf::from(args.next().ok_or("missing output directory")?);
    let rounds: usize = args
        .next()
        .and_then(|s| s.to_str().and_then(|s| s.parse().ok()))
        .unwrap_or(3);
    std::fs::create_dir_all(&out)?;
    // Preload once. File IO and snapshot formatting do not enter CPU samples.
    let bytes: Vec<_> = names
        .lines()
        .filter(|s| !s.is_empty())
        .map(|name| std::fs::read(root.join(name)).map(|bytes| (name.to_owned(), bytes)))
        .collect::<std::io::Result<_>>()?;
    for round in 0..rounds {
        let start = Instant::now();
        let scripts: Vec<_> = bytes
            .iter()
            .map(|(name, bytes)| parse_emevd(name, bytes).map_err(|e| format!("{name}: {e:?}")))
            .collect::<Result<_, _>>()?;
        let parse_us = start.elapsed().as_micros();
        let mut snapshots = String::new();
        let mut samples = Vec::new();
        let mut instances = 0;
        let mut displays = 0;
        let mut defeats = 0;
        let mut missing = 0;
        for map in scripts.iter().filter(|s| s.map_id.is_some()) {
            let start = Instant::now();
            let result = discover_boss_displays(map, &scripts)
                .map_err(|e| format!("{}: {e:?}", map.name))?;
            samples.push((start.elapsed().as_micros(), map.name.clone()));
            instances += result.instances;
            displays += result.displays.len();
            defeats += result.defeats.len();
            missing += result.missing_events.len();
            snapshots.push_str(&format!("{} {result:?}\n", map.name));
        }
        let discovery_us: u128 = samples.iter().map(|s| s.0).sum();
        samples.sort_by(|a, b| b.cmp(a));
        println!(
            "round={round} scripts={} maps={} parse_us={parse_us} discovery_us={discovery_us} instances={instances} displays={displays} defeats={defeats} missing={missing} slowest={:?}",
            scripts.len(),
            samples.len(),
            &samples[..samples.len().min(8)]
        );
        std::fs::write(out.join(format!("round-{round}.txt")), snapshots)?;
    }
    Ok(())
}
