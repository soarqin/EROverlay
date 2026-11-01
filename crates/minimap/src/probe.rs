//! Opt-in CPU probe using archived production recipes and the production egui path.
//! The ABI sink counts submitted meshes; this does not measure D3D12 or game frames.
use crate::{
    abi,
    data::{Decoration, MapData, Source},
    host::{Host, Texture},
    resources::{self, TileView, Visuals},
    settings::{Preset, Settings, Shape},
    ui::{Renderer, Snapshot},
};
use std::{
    alloc::{GlobalAlloc, Layout, System},
    cell::Cell,
    collections::HashMap,
    ffi::c_void,
    sync::{
        Arc,
        atomic::{AtomicU64, Ordering},
    },
    time::Instant,
};

thread_local! {static COUNT:Cell<bool>=const{Cell::new(false)};static ALLOCS:Cell<u64>=const{Cell::new(0)};static BYTES:Cell<u64>=const{Cell::new(0)};}
struct CountingAllocator;
#[global_allocator]
static ALLOCATOR: CountingAllocator = CountingAllocator;
unsafe impl GlobalAlloc for CountingAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        let ptr = unsafe { System.alloc(layout) };
        if COUNT.try_with(Cell::get).unwrap_or(false) && !ptr.is_null() {
            ALLOCS.with(|n| n.set(n.get() + 1));
            BYTES.with(|n| n.set(n.get() + layout.size() as u64));
        }
        ptr
    }
    unsafe fn dealloc(&self, ptr: *mut u8, layout: Layout) {
        unsafe { System.dealloc(ptr, layout) }
    }
    unsafe fn realloc(&self, ptr: *mut u8, layout: Layout, size: usize) -> *mut u8 {
        let ptr = unsafe { System.realloc(ptr, layout, size) };
        if COUNT.try_with(Cell::get).unwrap_or(false) && !ptr.is_null() {
            ALLOCS.with(|n| n.set(n.get() + 1));
            BYTES.with(|n| n.set(n.get() + size as u64));
        }
        ptr
    }
}
struct Sink {
    sizes: HashMap<u64, (u32, u32)>,
    meshes: AtomicU64,
    vertices: AtomicU64,
    indices: AtomicU64,
    uploads: AtomicU64,
    next: AtomicU64,
}
unsafe extern "C" fn display(_user: *mut c_void, out: *mut abi::DisplayState) -> i32 {
    unsafe {
        out.write(abi::DisplayState {
            valid: 1,
            ..Default::default()
        })
    };
    abi::OK
}
unsafe extern "C" fn poll(user: *mut c_void, id: u64, out: *mut abi::TextureView) -> i32 {
    let sink = unsafe { &*user.cast::<Sink>() };
    let (w, h) = sink.sizes.get(&id).copied().unwrap_or((1024, 1024));
    unsafe {
        out.write(abi::TextureView {
            status: abi::TEXTURE_READY,
            width: w,
            height: h,
            memory_bytes: u64::from(w) * u64::from(h) * 4,
            ..Default::default()
        })
    };
    abi::OK
}
unsafe extern "C" fn release(_user: *mut c_void, _id: u64) {}
unsafe extern "C" fn options(_user: *mut c_void, _options: *const abi::RenderOptions) -> i32 {
    abi::OK
}
unsafe extern "C" fn rgba(user: *mut c_void, _w: u32, _h: u32, _bytes: abi::ByteSlice) -> u64 {
    let sink = unsafe { &*user.cast::<Sink>() };
    sink.uploads.fetch_add(1, Ordering::Relaxed);
    sink.next.fetch_add(1, Ordering::Relaxed)
}
unsafe extern "C" fn submit(user: *mut c_void, mesh: *const abi::Mesh) -> i32 {
    let sink = unsafe { &*user.cast::<Sink>() };
    let mesh = unsafe { &*mesh };
    sink.meshes.fetch_add(1, Ordering::Relaxed);
    sink.vertices
        .fetch_add(u64::from(mesh.vertex_count), Ordering::Relaxed);
    sink.indices
        .fetch_add(u64::from(mesh.index_count), Ordering::Relaxed);
    abi::OK
}

#[test]
#[ignore = "requires archived EROVERLAY_GFX/EROVERLAY_LAYOUTS/EROVERLAY_TPF; reports CPU, no GPU"]
fn archived_egui_cpu_probe() {
    let gfx = std::fs::read(std::env::var("EROVERLAY_GFX").expect("EROVERLAY_GFX")).unwrap();
    let layouts =
        std::fs::read(std::env::var("EROVERLAY_LAYOUTS").expect("EROVERLAY_LAYOUTS")).unwrap();
    let tpf = std::fs::read(std::env::var("EROVERLAY_TPF").expect("EROVERLAY_TPF")).unwrap();
    let (defs, atlas_specs) =
        resources::definitions(&gfx, &layouts).expect("production native icon definitions");
    let defs = Arc::new(defs);
    let entries = eroverlay_game::assets::parse_tpf(&tpf).unwrap();
    let count = defs.regions.iter().map(|r| r.atlas).max().unwrap() + 1;
    let mut sink = Sink {
        sizes: HashMap::new(),
        meshes: AtomicU64::new(0),
        vertices: AtomicU64::new(0),
        indices: AtomicU64::new(0),
        uploads: AtomicU64::new(0),
        next: AtomicU64::new(10_000),
    };
    for (i, spec) in atlas_specs.iter().enumerate().take(count) {
        let entry = entries
            .iter()
            .find(|e| crate::gfx::asset_key(&e.name) == spec.source)
            .unwrap();
        let image = eroverlay_game::assets::parse_dds(entry.dds).unwrap();
        let (w, h) = if spec.source_index.is_some() {
            (spec.region.width, spec.region.height)
        } else {
            (image.width, image.height)
        };
        sink.sizes.insert((i + 1) as u64, (w, h));
    }
    // SAFETY: all-zero nullable callbacks/pointers and integer ABI fields are valid.
    let mut api: abi::HostApi = unsafe { std::mem::zeroed() };
    api.user = (&mut sink as *mut Sink).cast();
    api.read_display_state = Some(display);
    api.texture_poll = Some(poll);
    api.texture_release = Some(release);
    api.texture_create_rgba = Some(rgba);
    api.submit_mesh = Some(submit);
    api.set_render_options = Some(options);
    let host = Host(api);
    let atlases: Vec<_> = (0..count)
        .map(|i| Some(Arc::new(Texture::ready(host, (i + 1) as u64).unwrap())))
        .collect();
    let mut ids: Vec<_> = defs.icons.keys().copied().collect();
    ids.sort_unstable();
    let settings = Settings::load(&HashMap::new());
    let mut frame = abi::FrameInfo {
        size: size_of::<abi::FrameInfo>() as u32,
        layer: abi::LAYER_BELOW_UI,
        width: 1920.0,
        height: 1080.0,
        pixels_per_point: 1.0,
        delta_seconds: 1.0 / 60.0,
        ..Default::default()
    };
    println!(
        "CPU egui production paint+tessellation+ABI sink; 1920x1080, 100 warmup + 1000 measured frames. Synthetic marker positions, archived native recipes. GPU/update/assets I/O excluded."
    );
    for (name, markers, rotate, large, shown) in [
        ("hidden", 0, false, false, false),
        ("steady", 117, false, false, true),
        ("large-rotated-alpha", 117, true, true, true),
        ("many-markers", 1000, false, true, true),
    ] {
        let preset = Preset {
            name: name.into(),
            rotate,
            shape: if rotate {
                Shape::Circle
            } else {
                Shape::Rounded
            },
            opacity: 0.6,
            width: if large { 0.9 } else { 0.32 },
            height: if large { 0.9 } else { 0.32 },
            center: true,
            ..Default::default()
        };
        let scale = (preset.zoom * preset.map_scale).clamp(0.01, 16.0);
        let decorations = (0..markers)
            .map(|i| Decoration {
                id: i as u64,
                icon: ids[i % ids.len()],
                maps: 1,
                x: 5000.0 + ((i % 37) as f32 - 18.0) * 8.0 / scale,
                y: 5000.0 + ((i / 37) as f32 - 13.0) * 8.0 / scale,
                angle: 0.0,
                source: Source::Landmark,
                area: false,
                cleared: i % 7 == 0,
                runtime: false,
            })
            .collect();
        let mut settings = settings.clone();
        settings.presets = vec![preset];
        let tile = atlases.iter().find_map(Clone::clone).unwrap();
        let tiles = (0..4)
            .map(|i| {
                let x = 4744.0 + (i % 2) as f32 * 256.0;
                let y = 4744.0 + (i / 2) as f32 * 256.0;
                TileView {
                    texture: tile.clone(),
                    bounds: [x, y, x + 256.0, y + 256.0],
                }
            })
            .collect();
        let snapshot = Snapshot {
            shown,
            preset: 0,
            settings: Arc::new(settings),
            data: MapData {
                valid: true,
                state: abi::MapState {
                    x: 5000.0,
                    y: 5000.0,
                    map_id: 0,
                    camera_yaw_cos: 0.8,
                    camera_yaw_sin: 0.6,
                    ..Default::default()
                },
                decorations: Arc::new(decorations),
                ..Default::default()
            },
            visuals: Visuals {
                definitions: Some(defs.clone()),
                atlases: atlases.clone(),
                tiles,
                status: "",
            },
            graces: true,
            landmarks: true,
        };
        let mut renderer = Renderer::new();
        for i in 0..100 {
            frame.sequence = i;
            assert_eq!(renderer.render(host, &snapshot, &frame), abi::OK);
            assert!(
                !renderer.geometry_budget_exhausted(),
                "benchmark must not truncate its marker workload"
            );
        }
        sink.meshes.store(0, Ordering::Relaxed);
        sink.vertices.store(0, Ordering::Relaxed);
        sink.indices.store(0, Ordering::Relaxed);
        sink.uploads.store(0, Ordering::Relaxed);
        ALLOCS.with(|n| n.set(0));
        BYTES.with(|n| n.set(0));
        let mut samples = Vec::with_capacity(1000);
        for i in 100..1100 {
            frame.sequence = i;
            COUNT.with(|c| c.set(true));
            let start = Instant::now();
            let result = renderer.render(host, &snapshot, &frame);
            let elapsed = start.elapsed().as_nanos();
            COUNT.with(|c| c.set(false));
            assert_eq!(result, abi::OK);
            samples.push(elapsed);
        }
        samples.sort_unstable();
        println!(
            "scenario={name} markers={markers} p50_us={:.3} p95_us={:.3} p99_us={:.3} allocations_per_frame={:.2} allocated_bytes_per_frame={:.2} meshes_per_frame={:.2} vertices_per_frame={:.2} indices_per_frame={:.2} steady_uploads={}",
            samples[500] as f64 / 1000.0,
            samples[950] as f64 / 1000.0,
            samples[990] as f64 / 1000.0,
            ALLOCS.with(Cell::get) as f64 / 1000.0,
            BYTES.with(Cell::get) as f64 / 1000.0,
            sink.meshes.load(Ordering::Relaxed) as f64 / 1000.0,
            sink.vertices.load(Ordering::Relaxed) as f64 / 1000.0,
            sink.indices.load(Ordering::Relaxed) as f64 / 1000.0,
            sink.uploads.load(Ordering::Relaxed)
        );
        if shown {
            assert!(sink.meshes.load(Ordering::Relaxed) > 0);
        } else {
            assert_eq!(ALLOCS.with(Cell::get), 0);
            assert_eq!(sink.meshes.load(Ordering::Relaxed), 0);
        }
        assert_eq!(sink.uploads.load(Ordering::Relaxed), 0);
        renderer.reset(host);
    }
}
