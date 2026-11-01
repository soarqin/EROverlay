//! Maps archived game EXEs as data only; never loads or executes them.
use eroverlay_game::{
    Bindings,
    scan::{map_pe, relocate},
    scan_image,
};
use std::{
    path::{Path, PathBuf},
    time::Instant,
};
fn scan(path: &Path) -> Result<Bindings, String> {
    let raw = std::fs::read(path).map_err(|e| e.to_string())?;
    let (mapped, base) = map_pe(&raw).ok_or("invalid archived PE")?;
    scan_image(&mapped, base)
}
fn find_exe(directory: &Path) -> Option<PathBuf> {
    if directory.is_file() {
        return Some(directory.to_owned());
    }
    for name in ["eldenring.exe", "Game/eldenring.exe", "Game/ELDENRING.exe"] {
        let p = directory.join(name);
        if p.is_file() {
            return Some(p);
        }
    }
    None
}
fn contract(b: &Bindings) -> [u32; 23] {
    let l = &b.layout;
    [
        b.submit,
        b.flush,
        b.enqueue,
        b.cancel,
        b.manager,
        b.allocator,
        b.retire_pool_owner,
        b.menu,
        b.game_data,
        b.event_flags,
        b.field,
        b.repository,
        b.reveal,
        l.size,
        l.menu_owner_offset,
        l.owner_view_offset,
        l.menu_info_offset,
        l.screen_state_offset,
        l.grace_stride,
        l.grace_normal_offset,
        l.alternate_icons,
        l.map_mask,
        l.repository_groups,
    ]
}
fn verify(path: &Path) -> Result<(), String> {
    let raw = std::fs::read(path).map_err(|e| e.to_string())?;
    let (mut image, base) = map_pe(&raw).ok_or("invalid archived PE")?;
    let bindings = scan_image(&image, base)?;
    image[0x30] ^= 1;
    let hash = scan_image(&image, base)?;
    image[0x30] ^= 1;
    if contract(&hash) != contract(&bindings) {
        return Err("unknown file hash changed resolved contract".into());
    }
    println!("unknown executable hash: PASS");
    image[bindings.submit as usize] ^= 1;
    if scan_image(&image, base).is_ok() {
        return Err("changed request ABI was accepted".into());
    }
    image[bindings.submit as usize] ^= 1;
    println!("changed request ABI rejected: PASS");
    if scan_image(&image[..64], base).is_ok() {
        return Err("truncated PE was accepted".into());
    }
    println!("truncated PE rejected: PASS");
    let delta = 0x2_0000_0000u64;
    relocate(&mut image, delta).ok_or("invalid relocation directory")?;
    let relocated = scan_image(&image, base + delta as usize)?;
    if contract(&relocated) != contract(&bindings) {
        return Err("ASLR changed resolved RVAs/layout".into());
    }
    println!("ASLR/relocated RTTI: PASS");
    Ok(())
}
fn main() {
    let mut args = std::env::args().skip(1);
    let mode = args.next().unwrap_or_default();
    let path = PathBuf::from(args.next().unwrap_or_default());
    if mode == "verify" {
        let result = find_exe(&path)
            .ok_or_else(|| "no archived executable found".to_owned())
            .and_then(|exe| verify(&exe));
        if let Err(error) = result {
            eprintln!("{error}");
            std::process::exit(1);
        }
        return;
    }
    if mode != "scan" && mode != "archive" {
        eprintln!(
            "usage: game_probe scan <eldenring.exe> | archive <versions-directory> | verify <eldenring.exe>"
        );
        std::process::exit(2);
    }
    let mut inputs = Vec::new();
    if mode == "archive" {
        if let Ok(entries) = std::fs::read_dir(&path) {
            for entry in entries.flatten() {
                if let Some(exe) = find_exe(&entry.path()) {
                    inputs.push((entry.file_name().to_string_lossy().into_owned(), exe));
                }
            }
        }
        inputs.sort_by(|a, b| a.0.cmp(&b.0));
    } else if let Some(exe) = find_exe(&path) {
        inputs.push((path.display().to_string(), exe));
    }
    if inputs.is_empty() {
        eprintln!("no archived executable found");
        std::process::exit(2);
    }
    println!(
        "version,result,scan_ms,submit_rva,menu_rva,menu_owner_offset,view_offset,grace_stride,map_mask,reason"
    );
    for (name, exe) in inputs {
        let begin = Instant::now();
        let result = scan(&exe);
        let ms = begin.elapsed().as_secs_f64() * 1000.;
        match result {
            Ok(b) => println!(
                "{name},PASS,{ms:.3},{:x},{:x},{:x},{:x},{:x},{:x},",
                b.submit,
                b.menu,
                b.layout.menu_owner_offset,
                b.layout.owner_view_offset,
                b.layout.grace_stride,
                b.layout.map_mask
            ),
            Err(error) => println!(
                "{name},UNSUPPORTED,{ms:.3},,,,,,,{}",
                error.replace(',', ";")
            ),
        }
    }
}
#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn rejects_truncated_pe() {
        for n in 0..256 {
            assert!(map_pe(&vec![0; n]).is_none());
        }
    }
}
