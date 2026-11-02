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
        let mut vertices = 0usize;
        let mut indices = 0usize;
        let mut meshes = 0usize;
        for clipped in ctx.tessellate(output.shapes, output.pixels_per_point) {
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
        for id in output.textures_delta.free {
            if let Some(t) = self.textures.remove(&id) {
                self.deferred.push(t.handle);
            }
        }
        abi::OK
    }
}
