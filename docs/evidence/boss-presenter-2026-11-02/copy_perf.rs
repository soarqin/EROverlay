//! Renderer-only diagnostic. The harness drains the queue outside each timed
//! render call; it is not a game-frame or end-to-end Minimap benchmark.
use eroverlay_abi::{Mesh, RenderOptions, Vertex};
use eroverlay_renderer::{NativeFrame, Renderer};
use std::{
    alloc::{GlobalAlloc, Layout, System},
    sync::atomic::{AtomicBool, AtomicU64, Ordering},
    time::Instant,
};
use windows::{
    Win32::Graphics::{
        Direct3D::D3D_FEATURE_LEVEL_11_0,
        Direct3D12::*,
        Dxgi::{Common::*, *},
    },
    core::{Interface, Result},
};

struct CountingAllocator;
static COUNTING: AtomicBool = AtomicBool::new(false);
static ALLOCS: AtomicU64 = AtomicU64::new(0);
static BYTES: AtomicU64 = AtomicU64::new(0);
unsafe impl GlobalAlloc for CountingAllocator {
    unsafe fn alloc(&self, layout: Layout) -> *mut u8 {
        if COUNTING.load(Ordering::Relaxed) {
            ALLOCS.fetch_add(1, Ordering::Relaxed);
            BYTES.fetch_add(layout.size() as u64, Ordering::Relaxed);
        }
        unsafe { System.alloc(layout) }
    }
    unsafe fn dealloc(&self, pointer: *mut u8, layout: Layout) {
        unsafe { System.dealloc(pointer, layout) }
    }
    unsafe fn realloc(&self, pointer: *mut u8, layout: Layout, new_size: usize) -> *mut u8 {
        if COUNTING.load(Ordering::Relaxed) {
            ALLOCS.fetch_add(1, Ordering::Relaxed);
            BYTES.fetch_add(new_size as u64, Ordering::Relaxed);
        }
        unsafe { System.realloc(pointer, layout, new_size) }
    }
}
#[global_allocator]
static ALLOCATOR: CountingAllocator = CountingAllocator;

fn percentile(values: &mut [u64], p: usize) -> f64 {
    if values.is_empty() {
        return 0.0;
    }
    values.sort_unstable();
    values[((values.len() - 1) * p) / 100] as f64 / 1000.0
}
fn geometry(size: f32, cells: usize, rotate: bool) -> (Vec<Vertex>, Vec<u32>) {
    let mut vertices = Vec::with_capacity(cells * cells * 4);
    let mut indices = Vec::with_capacity(cells * cells * 6);
    for row in 0..cells {
        for col in 0..cells {
            let x = 40.0 + col as f32 * size / cells as f32;
            let y = 40.0 + row as f32 * size / cells as f32;
            let s = size / cells as f32;
            let start = vertices.len() as u32;
            for (dx, dy, u, v) in [
                (0.0, 0.0, 0.0, 0.0),
                (s, 0.0, 1.0, 0.0),
                (s, s, 1.0, 1.0),
                (0.0, s, 0.0, 1.0),
            ] {
                let (mut px, mut py) = (x + dx, y + dy);
                if rotate {
                    let center = 40.0 + size / 2.0;
                    let tx = px - center;
                    let ty = py - center;
                    px = center + (tx - ty) * std::f32::consts::FRAC_1_SQRT_2;
                    py = center + (tx + ty) * std::f32::consts::FRAC_1_SQRT_2;
                }
                vertices.push(Vertex {
                    pos: [px, py],
                    uv: [u, v],
                    color: 0xcc00cc00,
                });
            }
            indices.extend([start, start + 1, start + 2, start, start + 2, start + 3]);
        }
    }
    (vertices, indices)
}

struct Staging {
    vertices: Vec<Vertex>,
    indices: Vec<u32>,
    meshes: Vec<Mesh>,
    free: Vec<u64>,
}
impl Staging {
    fn new() -> Self {
        Self {
            vertices: Vec::with_capacity(eroverlay_abi::MAX_VERTICES),
            indices: Vec::with_capacity(eroverlay_abi::MAX_INDICES),
            meshes: Vec::with_capacity(eroverlay_abi::MAX_MESHES),
            free: Vec::with_capacity(16),
        }
    }
    fn egui_frame(
        &mut self,
        context: &egui::Context,
        renderer: &mut Renderer,
        quad_vertices: &[Vertex],
        hidden: bool,
        batch: Option<&egui::Mesh>,
    ) -> Result<()> {
        self.vertices.clear();
        self.indices.clear();
        self.meshes.clear();
        self.free.clear();
        let input = egui::RawInput {
            screen_rect: Some(egui::Rect::from_min_size(
                egui::Pos2::ZERO,
                egui::vec2(1920.0, 1080.0),
            )),
            ..Default::default()
        };
        let output = context.run(input, |ctx| {
            if !hidden {
                let painter = ctx.layer_painter(egui::LayerId::new(
                    egui::Order::Background,
                    egui::Id::new("fixture"),
                ));
                if let Some(mesh) = batch {
                    painter.add(egui::Shape::mesh(mesh.clone()));
                } else {
                    for quad in quad_vertices.as_chunks::<4>().0 {
                        let points = quad
                            .iter()
                            .map(|v| egui::pos2(v.pos[0], v.pos[1]))
                            .collect();
                        painter.add(egui::Shape::convex_polygon(
                            points,
                            egui::Color32::from_rgba_premultiplied(0, 204, 0, 204),
                            egui::Stroke::NONE,
                        ));
                    }
                }
            }
        });
        for (id, delta) in output.textures_delta.set {
            // This fixture draws no text, so the white atlas texel is a single
            // initial full upload; production Minimap handles font patches.
            assert!(delta.pos.is_none(), "unexpected fixture font patch");
            let egui::ImageData::Color(image) = delta.image;
            let rgba = image.pixels.iter().flat_map(|c| c.to_array()).collect();
            renderer.upload_rgba_owned(
                texture_id(id),
                image.size[0] as u32,
                image.size[1] as u32,
                rgba,
            );
        }
        for primitive in context.tessellate(output.shapes, output.pixels_per_point) {
            let egui::epaint::Primitive::Mesh(mesh) = primitive.primitive else {
                continue;
            };
            assert!(self.vertices.len() + mesh.vertices.len() <= eroverlay_abi::MAX_VERTICES);
            assert!(self.indices.len() + mesh.indices.len() <= eroverlay_abi::MAX_INDICES);
            let vertex_start = self.vertices.len();
            let index_start = self.indices.len();
            self.vertices.extend(mesh.vertices.iter().map(|v| Vertex {
                pos: [v.pos.x, v.pos.y],
                uv: [v.uv.x, v.uv.y],
                color: u32::from_le_bytes(v.color.to_array()),
            }));
            self.indices.extend_from_slice(&mesh.indices);
            self.meshes.push(Mesh {
                texture: texture_id(mesh.texture_id),
                vertices: unsafe { self.vertices.as_ptr().add(vertex_start) },
                vertex_count: mesh.vertices.len() as u32,
                indices: unsafe { self.indices.as_ptr().add(index_start) },
                index_count: mesh.indices.len() as u32,
                clip: [
                    primitive.clip_rect.min.x,
                    primitive.clip_rect.min.y,
                    primitive.clip_rect.max.x,
                    primitive.clip_rect.max.y,
                ],
            });
        }
        for id in output.textures_delta.free {
            self.free.push(texture_id(id));
        }
        Ok(())
    }
}
fn texture_id(id: egui::TextureId) -> u64 {
    match id {
        egui::TextureId::Managed(id) => id + 10_000,
        egui::TextureId::User(id) => id,
    }
}

fn main() -> Result<()> {
    let args: Vec<String> = std::env::args().collect();
    let retain = args.iter().any(|s| s == "--retain");
    let warp = args.iter().any(|s| s == "--warp");
    let hdr = args.iter().any(|s| s == "--hdr");
    let timing = !args.iter().any(|s| s == "--no-gpu-timing");
    let target_format = if hdr {
        DXGI_FORMAT_R10G10B10A2_UNORM
    } else {
        DXGI_FORMAT_R8G8B8A8_UNORM
    };
    let target_name = if hdr {
        "RGB10A2 AboveUI full-viewport"
    } else {
        "RGBA8 native UI transmittance"
    };
    let egui_batched = args.iter().any(|s| s == "--egui-batched");
    let egui_mode = egui_batched || args.iter().any(|s| s == "--egui");
    let frontend = if egui_batched {
        "egui run + cloned batched mesh + tessellation + preallocated ABI staging + renderer"
    } else if egui_mode {
        "egui run + per-quad convex shapes + tessellation + preallocated ABI staging + renderer"
    } else {
        "renderer only"
    };
    let samples = args
        .windows(2)
        .find(|p| p[0] == "--samples")
        .and_then(|p| p[1].parse::<usize>().ok())
        .unwrap_or(600)
        .clamp(10, 100_000);
    unsafe {
        let factory: IDXGIFactory6 = CreateDXGIFactory2(Default::default())?;
        let adapter: IDXGIAdapter1 = if warp {
            factory.EnumWarpAdapter()?
        } else {
            factory.EnumAdapterByGpuPreference(0, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE)?
        };
        let description = adapter.GetDesc1()?;
        let adapter_name = String::from_utf16_lossy(
            &description.Description[..description
                .Description
                .iter()
                .position(|&c| c == 0)
                .unwrap_or(description.Description.len())],
        );
        println!(
            "{{\"kind\":\"metadata\",\"adapter\":{adapter_name:?},\"warp\":{warp},\"resolution\":\"1920x1080\",\"target\":{target_name:?},\"gpu_timing\":{timing},\"samples\":{samples},\"frontend\":{frontend:?},\"scope\":\"CPU scope selected by frontend; GPU queue timestamps; excludes dynamic plugin dispatch, updater and game; explicit queue drain outside timed region; Rust allocator counts only\"}}"
        );
        let mut device = None;
        D3D12CreateDevice(&adapter, D3D_FEATURE_LEVEL_11_0, &mut device)?;
        let device: ID3D12Device = device.unwrap();
        let queue: ID3D12CommandQueue = device.CreateCommandQueue(&D3D12_COMMAND_QUEUE_DESC {
            Type: D3D12_COMMAND_LIST_TYPE_DIRECT,
            ..Default::default()
        })?;
        let rtv: ID3D12DescriptorHeap =
            device.CreateDescriptorHeap(&D3D12_DESCRIPTOR_HEAP_DESC {
                Type: D3D12_DESCRIPTOR_HEAP_TYPE_RTV,
                NumDescriptors: 1,
                ..Default::default()
            })?;
        let mut target = None;
        device.CreateCommittedResource(
            &D3D12_HEAP_PROPERTIES {
                Type: D3D12_HEAP_TYPE_DEFAULT,
                CreationNodeMask: 1,
                VisibleNodeMask: 1,
                ..Default::default()
            },
            D3D12_HEAP_FLAG_NONE,
            &D3D12_RESOURCE_DESC {
                Dimension: D3D12_RESOURCE_DIMENSION_TEXTURE2D,
                Width: 1920,
                Height: 1080,
                DepthOrArraySize: 1,
                MipLevels: 1,
                Format: target_format,
                SampleDesc: DXGI_SAMPLE_DESC {
                    Count: 1,
                    Quality: 0,
                },
                Flags: D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET,
                ..Default::default()
            },
            D3D12_RESOURCE_STATE_RENDER_TARGET,
            None,
            &mut target,
        )?;
        let target: ID3D12Resource = target.unwrap();
        let target_rtv = rtv.GetCPUDescriptorHandleForHeapStart();
        device.CreateRenderTargetView(&target, None, target_rtv);
        let clear_allocator: ID3D12CommandAllocator =
            device.CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT)?;
        let clear_list: ID3D12GraphicsCommandList =
            device.CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, &clear_allocator, None)?;
        clear_list.ClearRenderTargetView(target_rtv, &[0.1, 0.1, 0.1, 1.0], None);
        clear_list.Close()?;
        queue.ExecuteCommandLists(&[Some(clear_list.cast()?)]);
        let mut frame = NativeFrame {
            device: device.as_raw(),
            queue: queue.as_raw(),
            target: target_rtv.ptr,
            format: target_format.0 as u32,
            width: 1920.0,
            height: 1080.0,
            layer: 0,
            sequence: 1,
        };
        let mut renderer = Renderer::new(&frame)?;
        if timing {
            renderer.enable_gpu_timing()?;
        }
        let context = egui::Context::default();
        context.tessellation_options_mut(|options| options.feathering = false);
        let mut staging = Staging::new();
        for scenario in [
            "stable_64_quads",
        ] {
            let large = scenario == "large_rotated_transparent_1024_quads";
            let size = if large { 1000.0 } else { 320.0 };
            let (vertices, indices) = geometry(size, if large { 32 } else { 8 }, large);
            let fixture_mesh = egui::Mesh {
                vertices: vertices
                    .iter()
                    .map(|v| egui::epaint::Vertex {
                        pos: egui::pos2(v.pos[0], v.pos[1]),
                        uv: egui::epaint::WHITE_UV,
                        color: egui::Color32::from_rgba_premultiplied(0, 204, 0, 204),
                    })
                    .collect(),
                indices: indices.clone(),
                texture_id: egui::TextureId::Managed(0),
            };
            let mesh = Mesh {
                texture: 0,
                vertices: vertices.as_ptr(),
                vertex_count: vertices.len() as u32,
                indices: indices.as_ptr(),
                index_count: indices.len() as u32,
                clip: [40.0, 40.0, 40.0 + size, 40.0 + size],
            };
            let mut cpu = Vec::with_capacity(samples);
            let mut gpu = Vec::with_capacity(samples);
            let mut draws = 0u64;
            let mut uploads = 0u64;
            let mut committed_peak = 0u64;
            let mut dropped = 0u64;
            let mut sampled_start = Instant::now();
            ALLOCS.store(0, Ordering::Relaxed);
            BYTES.store(0, Ordering::Relaxed);
            for sample in 0..samples + 32 {
                let mut options = RenderOptions {
                    clip_rect: [0.0, 0.0, 1920.0, 1080.0],
                    circle: 0,
                    opacity: if large { 0.4 } else { 1.0 },
                    ..Default::default()
                };
                if scenario == "resize_region" {
                    options.clip_rect[2] = 360.0 + (sample % 32) as f32 * 4.0;
                    options.clip_rect[3] = options.clip_rect[2];
                }
                if scenario == "upload_burst" && sample % 30 == 0 {
                    renderer.upload_rgba_owned(2, 512, 512, vec![sample as u8; 512 * 512 * 4]);
                }
                frame.sequence += 1;
                let visible = if scenario == "hidden" {
                    &[][..]
                } else {
                    std::slice::from_ref(&mesh)
                };
                if sample == 32 {
                    sampled_start = Instant::now();
                }
                COUNTING.store(sample >= 32, Ordering::Relaxed);
                let start = Instant::now();
                if egui_mode {
                    staging.egui_frame(
                        &context,
                        &mut renderer,
                        &vertices,
                        scenario == "hidden",
                        egui_batched.then_some(&fixture_mesh),
                    )?;
                }
                let visible = if egui_mode {
                    &staging.meshes[..]
                } else {
                    visible
                };
                let result = renderer.render_with_group(&frame, visible, options, usize::from(retain));
                if egui_mode {
                    for id in staging.free.drain(..) {
                        renderer.retire_texture(id);
                    }
                }
                let elapsed = start.elapsed().as_nanos() as u64;
                COUNTING.store(false, Ordering::Relaxed);
                let stats = result?;
                if sample >= 32 {
                    cpu.push(elapsed);
                    draws += stats.draw_calls as u64;
                    uploads += stats.uploaded_bytes;
                    committed_peak = committed_peak.max(stats.resident_bytes);
                    dropped += u64::from(stats.dropped);
                }
                assert!(renderer.wait_idle(10000), "GPU drain timed out");
                let mut times = [0; 4];
                let count = renderer.take_gpu_samples(&mut times)?;
                if sample >= 32 {
                    gpu.extend_from_slice(&times[..count]);
                }
            }
            let sampled_wall_seconds = sampled_start.elapsed().as_secs_f64();
            let cpu50 = percentile(&mut cpu, 50);
            let cpu95 = percentile(&mut cpu, 95);
            let cpu99 = percentile(&mut cpu, 99);
            let gpu50 = if !timing {
                "null".to_owned()
            } else {
                format!("{:.3}", percentile(&mut gpu, 50))
            };
            let gpu95 = if !timing {
                "null".to_owned()
            } else {
                format!("{:.3}", percentile(&mut gpu, 95))
            };
            let gpu99 = if !timing {
                "null".to_owned()
            } else {
                format!("{:.3}", percentile(&mut gpu, 99))
            };
            println!(
                "{{\"kind\":\"scenario\",\"scenario\":{scenario:?},\"cpu_p50_us\":{cpu50:.3},\"cpu_p95_us\":{cpu95:.3},\"cpu_p99_us\":{cpu99:.3},\"gpu_p50_us\":{gpu50},\"gpu_p95_us\":{gpu95},\"gpu_p99_us\":{gpu99},\"gpu_samples\":{},\"rust_allocations\":{},\"rust_allocated_bytes\":{},\"draw_calls_per_frame\":{:.3},\"sampled_wall_seconds\":{sampled_wall_seconds:.6},\"upload_bytes_total\":{uploads},\"committed_resource_bytes_peak\":{committed_peak},\"dropped\":{dropped}}}",
                gpu.len(),
                ALLOCS.load(Ordering::Relaxed),
                BYTES.load(Ordering::Relaxed),
                draws as f64 / samples as f64
            );
        }
        assert!(renderer.wait_idle(10000));
    }
    Ok(())
}
