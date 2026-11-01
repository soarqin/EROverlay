//! Maps archived EXEs as data, without loading or executing game code.
use eroverlay_game::scan::{PeImage, map_pe};
use eroverlay_renderer::scene_scan::resolve;
use std::path::Path;

fn verify(path: &Path) -> Result<(), String> {
    let raw = std::fs::read(path).map_err(|error| error.to_string())?;
    let (image, base) = map_pe(&raw).ok_or("invalid archived PE")?;
    let version = path
        .parent()
        .and_then(Path::parent)
        .and_then(Path::file_name)
        .ok_or("missing version directory")?
        .to_string_lossy();
    match resolve(&PeImage::new(&image, base)) {
        Ok(a) => println!(
            "{version}\tPASS\tsubmit=0x{:x} enqueue=0x{:x} consume=0x{:x} format=0x{:x} view=0x{:x} texture=0x{:x} offsets={:x},{:x},{:x},{:x},{:x} composite=0x{:x} render-post={:x} queues={:x},{:x}",
            a.submit,
            a.enqueue,
            a.consume,
            a.pixel_format,
            a.render_view_vtable,
            a.texture_vtable,
            a.swap_chains_offset,
            a.viewport_offset,
            a.context_environment_offset,
            a.backend_offset,
            a.queue_offset,
            a.composite,
            a.render_post_offset,
            a.queue_index_offset,
            a.queues_offset,
        ),
        Err(error) => println!("{version}\tUNSUPPORTED\t{error}"),
    }
    Ok(())
}

fn run() -> Result<(), String> {
    let path = std::env::args_os()
        .nth(1)
        .ok_or("usage: scene_probe <archived-exe-or-root-directory>")?;
    let path = Path::new(&path);
    if path.is_file() {
        return verify(path);
    }
    let mut paths: Vec<_> = std::fs::read_dir(path)
        .map_err(|error| error.to_string())?
        .map(|entry| entry.map(|entry| entry.path().join("Game/eldenring.exe")))
        .collect::<std::io::Result<_>>()
        .map_err(|error| error.to_string())?;
    paths.retain(|path| path.is_file());
    paths.sort();
    for path in &paths {
        verify(path)?;
    }
    if paths.is_empty() {
        return Err("no archived EXEs found".into());
    }
    println!("images={}", paths.len());
    Ok(())
}

fn main() {
    if let Err(error) = run() {
        eprintln!("{error}");
        std::process::exit(1);
    }
}
