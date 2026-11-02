//! A bounded egui mesh/texture adapter for plugin-private contexts.
use egui::{TextureId, epaint::Primitive};
use eroverlay_abi as abi;
use std::{
    collections::HashMap,
    time::{Duration, Instant},
};

struct Texture {
    size: [usize; 2],
    pixels: Vec<u8>,
    handle: u64,
    dirty: bool,
    pending: bool,
    retry: Option<Instant>,
}

#[derive(Default)]
pub struct Meshes {
    textures: HashMap<TextureId, Texture>,
    vertices: Vec<abi::Vertex>,
    deferred: Vec<u64>,
}

fn release(host: &abi::HostApi, id: u64) {
    if id != 0
        && let Some(f) = host.texture_release
    {
        // SAFETY: caller retains the host until all plugin resources are released.
        unsafe { f(host.user, id) };
    }
}

impl Meshes {
    pub fn reset(&mut self, host: &abi::HostApi) {
        for (_, t) in self.textures.drain() {
            release(host, t.handle);
        }
        for t in self.deferred.drain(..) {
            release(host, t);
        }
    }

    fn defer_free(&mut self, ids: Vec<TextureId>) {
        for id in ids {
            if let Some(t) = self.textures.remove(&id) {
                self.deferred.push(t.handle);
            }
        }
    }

    /// The context, output and buffers belong to this DLL. The host copies each mesh.
    pub fn submit(
        &mut self,
        host: &abi::HostApi,
        ctx: &egui::Context,
        output: egui::FullOutput,
    ) -> i32 {
        for t in self.deferred.drain(..) {
            release(host, t);
        }
        for (id, delta) in output.textures_delta.set {
            let egui::ImageData::Color(image) = delta.image;
            let bytes = image.size[0]
                .checked_mul(image.size[1])
                .and_then(|n| n.checked_mul(4));
            if bytes.is_none_or(|n| n > abi::MAX_TEXTURE_BYTES) {
                return abi::ERR_INVALID;
            }
            let t = self.textures.entry(id).or_insert(Texture {
                size: [0; 2],
                pixels: Vec::new(),
                handle: 0,
                dirty: false,
                pending: false,
                retry: None,
            });
            let [x, y] = delta.pos.unwrap_or([0, 0]);
            if delta.pos.is_none() {
                t.size = image.size;
                t.pixels.resize(bytes.unwrap(), 0);
            }
            if x.checked_add(image.size[0]).is_none_or(|v| v > t.size[0])
                || y.checked_add(image.size[1]).is_none_or(|v| v > t.size[1])
            {
                return abi::ERR_INVALID;
            }
            for row in 0..image.size[1] {
                for col in 0..image.size[0] {
                    let [r, g, b, a] = image.pixels[row * image.size[0] + col].to_array();
                    let straight = |c: u8| {
                        if a == 0 {
                            0
                        } else {
                            ((u32::from(c) * 255 + u32::from(a) / 2) / u32::from(a)).min(255) as u8
                        }
                    };
                    let dst = ((y + row) * t.size[0] + x + col) * 4;
                    t.pixels[dst..dst + 4].copy_from_slice(&[
                        straight(r),
                        straight(g),
                        straight(b),
                        a,
                    ]);
                }
            }
            t.dirty = true;
        }
        let now = Instant::now();
        for t in self.textures.values_mut() {
            if t.pending
                && let Some(poll) = host.texture_poll
            {
                let mut v = abi::TextureView::default();
                // SAFETY: host writes a POD view into this stack slot.
                if unsafe { poll(host.user, t.handle, &mut v) } == abi::OK {
                    match v.status {
                        abi::TEXTURE_READY => t.pending = false,
                        abi::TEXTURE_FAILED => {
                            release(host, t.handle);
                            t.handle = 0;
                            t.pending = false;
                            t.dirty = true;
                            t.retry = Some(now + Duration::from_millis(250));
                        }
                        _ => {}
                    }
                }
            }
            if t.dirty && t.retry.is_none_or(|retry| now >= retry) {
                let replacement = host.texture_create_rgba.map_or(0, |f| {
                    // SAFETY: host synchronously copies this bounded pixel buffer.
                    unsafe {
                        f(
                            host.user,
                            t.size[0] as u32,
                            t.size[1] as u32,
                            abi::ByteSlice::new(&t.pixels),
                        )
                    }
                });
                if replacement == 0 {
                    t.retry = Some(now + Duration::from_millis(250));
                } else {
                    release(host, t.handle);
                    t.handle = replacement;
                    t.dirty = false;
                    t.pending = true;
                    t.retry = None;
                }
            }
        }
        let primitives = ctx.tessellate(output.shapes, output.pixels_per_point);
        // Changed atlas dimensions also change mesh UVs. A refused upload must
        // never pair those new UVs with the previous atlas or publish empty paint.
        if primitives.iter().any(|clipped| match &clipped.primitive {
            Primitive::Mesh(mesh)
                if !mesh.indices.is_empty() && matches!(mesh.texture_id, TextureId::Managed(_)) =>
            {
                self.textures
                    .get(&mesh.texture_id)
                    .is_none_or(|t| t.dirty || t.handle == 0)
            }
            _ => false,
        }) {
            self.defer_free(output.textures_delta.free);
            return abi::ERR_BUSY;
        }
        let mut vertices = 0usize;
        let mut indices = 0usize;
        let mut meshes = 0usize;
        for clipped in primitives {
            let Primitive::Mesh(mesh) = clipped.primitive else {
                continue;
            };
            vertices = vertices.saturating_add(mesh.vertices.len());
            indices = indices.saturating_add(mesh.indices.len());
            meshes += 1;
            if vertices > abi::MAX_VERTICES
                || indices > abi::MAX_INDICES
                || meshes > abi::MAX_MESHES
            {
                return abi::ERR_BUSY;
            }
            let texture = match mesh.texture_id {
                TextureId::User(id) => id,
                managed => self.textures.get(&managed).map_or(0, |t| t.handle),
            };
            if texture == 0 {
                continue;
            }
            self.vertices.clear();
            self.vertices
                .extend(mesh.vertices.iter().map(|v| abi::Vertex {
                    pos: [v.pos.x, v.pos.y],
                    uv: [v.uv.x, v.uv.y],
                    color: u32::from_le_bytes(v.color.to_array()),
                }));
            let clip = clipped.clip_rect;
            let mesh = abi::Mesh {
                texture,
                vertices: self.vertices.as_ptr(),
                vertex_count: self.vertices.len() as u32,
                indices: mesh.indices.as_ptr(),
                index_count: mesh.indices.len() as u32,
                clip: [clip.min.x, clip.min.y, clip.max.x, clip.max.y],
            };
            if let Some(submit) = host.submit_mesh {
                // SAFETY: host copies both buffers synchronously; neither crosses ownership.
                let result = unsafe { submit(host.user, &mesh) };
                if result != abi::OK {
                    return result;
                }
            }
        }
        self.defer_free(output.textures_delta.free);
        abi::OK
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use egui::{
        Color32, ColorImage, Pos2, Rect, Shape, TextureOptions,
        epaint::{ClippedShape, ImageDelta, Mesh},
    };
    use std::{collections::VecDeque, ffi::c_void};

    #[derive(Default)]
    struct Calls {
        handles: VecDeque<u64>,
        creates: Vec<([u32; 2], Vec<u8>)>,
        released: Vec<u64>,
        meshes: Vec<(u64, Vec<[f32; 2]>)>,
    }
    impl Calls {
        fn host(&mut self) -> abi::HostApi {
            unsafe extern "C" fn create(
                user: *mut c_void,
                width: u32,
                height: u32,
                pixels: abi::ByteSlice,
            ) -> u64 {
                // SAFETY: each test retains this callback state and the adapter
                // borrows readable pixels until the synchronous call returns.
                let calls = unsafe { &mut *user.cast::<Calls>() };
                let pixels = unsafe { std::slice::from_raw_parts(pixels.data, pixels.len) };
                calls.creates.push(([width, height], pixels.to_vec()));
                calls.handles.pop_front().unwrap_or(0)
            }
            unsafe extern "C" fn poll(_: *mut c_void, _: u64, out: *mut abi::TextureView) -> i32 {
                // SAFETY: the adapter supplies one writable complete POD slot.
                unsafe {
                    out.write(abi::TextureView {
                        status: abi::TEXTURE_READY,
                        ..Default::default()
                    })
                };
                abi::OK
            }
            unsafe extern "C" fn release(user: *mut c_void, handle: u64) {
                // SAFETY: tests retain their Calls until all adapter work ends.
                unsafe { &mut *user.cast::<Calls>() }.released.push(handle);
            }
            unsafe extern "C" fn mesh(user: *mut c_void, mesh: *const abi::Mesh) -> i32 {
                // SAFETY: submit synchronously borrows both the complete Mesh
                // and vertex range; copy UVs instead of keeping foreign pointers.
                let calls = unsafe { &mut *user.cast::<Calls>() };
                let mesh = unsafe { &*mesh };
                let vertices = unsafe {
                    std::slice::from_raw_parts(mesh.vertices, mesh.vertex_count as usize)
                };
                calls
                    .meshes
                    .push((mesh.texture, vertices.iter().map(|v| v.uv).collect()));
                abi::OK
            }
            // SAFETY: HostApi consists of nullable pointers and integer fields.
            // These tests only call the populated synchronous callbacks, and do
            // not move Calls after making this raw pointer.
            let mut host: abi::HostApi = unsafe { std::mem::zeroed() };
            host.user = (self as *mut Self).cast();
            host.texture_create_rgba = Some(create);
            host.texture_poll = Some(poll);
            host.texture_release = Some(release);
            host.submit_mesh = Some(mesh);
            host
        }
    }
    fn output(
        textures: &[(TextureId, f32)],
        set: Vec<(TextureId, usize)>,
        free: Vec<TextureId>,
    ) -> egui::FullOutput {
        let shapes = textures
            .iter()
            .map(|&(texture, uv)| {
                let mut mesh = Mesh::with_texture(texture);
                mesh.add_rect_with_uv(
                    Rect::from_min_max(Pos2::ZERO, egui::pos2(20., 20.)),
                    Rect::from_min_max(Pos2::ZERO, egui::pos2(uv, uv)),
                    Color32::WHITE,
                );
                ClippedShape {
                    clip_rect: Rect::from_min_max(Pos2::ZERO, egui::pos2(100., 100.)),
                    shape: Shape::mesh(mesh),
                }
            })
            .collect();
        egui::FullOutput {
            shapes,
            pixels_per_point: 1.,
            textures_delta: egui::TexturesDelta {
                set: set
                    .into_iter()
                    .map(|(id, size)| {
                        (
                            id,
                            ImageDelta::full(
                                ColorImage::filled([size; 2], Color32::WHITE),
                                TextureOptions::LINEAR,
                            ),
                        )
                    })
                    .collect(),
                free,
            },
            ..Default::default()
        }
    }
    fn context() -> egui::Context {
        let context = egui::Context::default();
        let _ = context.run(egui::RawInput::default(), |_| {});
        context
    }

    #[test]
    fn refused_atlas_update_never_pairs_new_uvs_with_old_handle_and_retries_without_a_delta() {
        let managed = TextureId::Managed(0);
        let mut calls = Calls {
            handles: VecDeque::from([11, 0, 22]),
            ..Default::default()
        };
        let host = calls.host();
        let context = context();
        let mut adapter = Meshes::default();
        assert_eq!(
            adapter.submit(
                &host,
                &context,
                output(&[(managed, 1.)], vec![(managed, 2)], vec![])
            ),
            abi::OK
        );
        assert_eq!(calls.meshes[0].0, 11);
        calls.meshes.clear();
        // A preceding user mesh must not escape before the dirty-atlas check.
        assert_eq!(
            adapter.submit(
                &host,
                &context,
                output(
                    &[(TextureId::User(99), 1.), (managed, 0.5)],
                    vec![(managed, 4)],
                    vec![]
                )
            ),
            abi::ERR_BUSY
        );
        assert!(calls.meshes.is_empty());
        assert!(calls.released.is_empty());
        assert_eq!(adapter.textures[&managed].handle, 11);
        assert!(adapter.textures[&managed].dirty);
        assert_eq!(adapter.textures[&managed].size, [4; 2]);
        // Egui need not repeat its texture delta after the refused upload.
        adapter.textures.get_mut(&managed).unwrap().retry =
            Some(Instant::now() + Duration::from_secs(10));
        assert_eq!(
            adapter.submit(&host, &context, output(&[(managed, 0.5)], vec![], vec![])),
            abi::ERR_BUSY
        );
        assert_eq!(calls.creates.len(), 2);
        assert!(calls.meshes.is_empty());
        adapter.textures.get_mut(&managed).unwrap().retry = Some(Instant::now());
        assert_eq!(
            adapter.submit(&host, &context, output(&[(managed, 0.5)], vec![], vec![])),
            abi::OK
        );
        assert_eq!(calls.creates[2].0, [4; 2]);
        assert_eq!(calls.creates[2].1.len(), 64);
        assert_eq!(calls.released, [11]);
        assert_eq!(calls.meshes.len(), 1);
        assert_eq!(calls.meshes[0].0, 22);
        assert!(calls.meshes[0].1.contains(&[0.5, 0.5]));
        assert!(!adapter.textures[&managed].dirty);
    }

    #[test]
    fn first_refused_or_missing_managed_texture_is_busy_instead_of_empty_success() {
        let managed = TextureId::Managed(0);
        let mut calls = Calls::default();
        let host = calls.host();
        let context = context();
        let mut adapter = Meshes::default();
        assert_eq!(
            adapter.submit(
                &host,
                &context,
                output(&[(managed, 1.)], vec![(managed, 2)], vec![])
            ),
            abi::ERR_BUSY
        );
        assert_eq!(adapter.textures[&managed].handle, 0);
        assert!(calls.meshes.is_empty());
        assert!(calls.released.is_empty());
        assert_eq!(
            adapter.submit(
                &host,
                &context,
                output(&[(TextureId::Managed(9), 1.)], vec![], vec![])
            ),
            abi::ERR_BUSY
        );
        assert!(calls.meshes.is_empty());
    }

    #[test]
    fn unused_dirty_atlas_does_not_block_and_free_handles_still_retire_after_submission() {
        let atlas = TextureId::Managed(0);
        let freed = TextureId::Managed(1);
        let mut calls = Calls {
            handles: VecDeque::from([11, 12, 0]),
            ..Default::default()
        };
        let host = calls.host();
        let context = context();
        let mut adapter = Meshes::default();
        assert_eq!(
            adapter.submit(
                &host,
                &context,
                output(&[(atlas, 1.)], vec![(atlas, 2)], vec![])
            ),
            abi::OK
        );
        assert_eq!(
            adapter.submit(
                &host,
                &context,
                output(&[(atlas, 1.)], vec![(freed, 2)], vec![])
            ),
            abi::OK
        );
        calls.meshes.clear();
        assert_eq!(
            adapter.submit(
                &host,
                &context,
                output(&[(atlas, 0.5)], vec![(atlas, 4)], vec![freed])
            ),
            abi::ERR_BUSY
        );
        assert!(calls.meshes.is_empty());
        assert!(calls.released.is_empty());
        assert!(!adapter.textures.contains_key(&freed));
        assert_eq!(adapter.deferred, [12]);
        assert!(adapter.textures[&atlas].dirty);
        assert_eq!(
            adapter.submit(
                &host,
                &context,
                output(&[(TextureId::User(99), 1.)], vec![], vec![])
            ),
            abi::OK
        );
        assert_eq!(calls.meshes[0].0, 99);
        assert_eq!(calls.released, [12]);
        assert_eq!(adapter.textures[&atlas].handle, 11);
        adapter.reset(&host);
        assert_eq!(calls.released, [12, 11]);
        assert!(adapter.textures.is_empty());
    }
}
