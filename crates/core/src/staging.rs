use eroverlay_abi::{
    ERR_BUSY, ERR_INVALID, MAX_INDICES, MAX_MESHES, MAX_VERTICES, Mesh, OK, RenderOptions, Vertex,
};

struct Command {
    texture: u64,
    vertex_start: usize,
    vertex_count: u32,
    index_start: usize,
    index_count: u32,
    clip: [f32; 4],
}

/// Allocated once per renderer. Raw cross-DLL data never survives a callback.
pub struct Staging {
    vertices: Vec<Vertex>,
    indices: Vec<u32>,
    commands: Vec<Command>,
    meshes: Vec<Mesh>,
    pub options: RenderOptions,
}
// SAFETY: Mesh pointers point only to our own stable preallocated buffers. They
// are rebuilt under the renderer mutex, consumed synchronously, and never shared.
unsafe impl Send for Staging {}
impl Default for Staging {
    fn default() -> Self {
        Self {
            vertices: Vec::with_capacity(8192),
            indices: Vec::with_capacity(16384),
            commands: Vec::with_capacity(256),
            meshes: Vec::with_capacity(256),
            options: RenderOptions::default(),
        }
    }
}
impl Staging {
    pub fn clear(&mut self) {
        self.vertices.clear();
        self.indices.clear();
        self.commands.clear();
        self.meshes.clear();
        self.options = RenderOptions::default();
    }
    /// # Safety
    /// Mesh buffers must be readable for their specified lengths during this call.
    /// Plugins are trusted native code; structural checks cannot validate arbitrary pointers.
    pub unsafe fn submit(&mut self, mesh: &Mesh) -> i32 {
        let vc = mesh.vertex_count as usize;
        let ic = mesh.index_count as usize;
        if vc == 0 || ic == 0 {
            return OK;
        }
        if vc > MAX_VERTICES
            || ic > MAX_INDICES
            || !ic.is_multiple_of(3)
            || mesh.vertices.is_null()
            || mesh.indices.is_null()
            || !(mesh.vertices as usize).is_multiple_of(align_of::<Vertex>())
            || !(mesh.indices as usize).is_multiple_of(align_of::<u32>())
            || mesh.clip.iter().any(|v| !v.is_finite())
            || mesh.clip[0] > mesh.clip[2]
            || mesh.clip[1] > mesh.clip[3]
        {
            return ERR_INVALID;
        }
        if self.commands.len() == MAX_MESHES
            || self.vertices.len() + vc > MAX_VERTICES
            || self.indices.len() + ic > MAX_INDICES
        {
            return ERR_BUSY;
        }
        // SAFETY: pointer validity is the caller's explicit native ABI contract.
        let vertices = unsafe { std::slice::from_raw_parts(mesh.vertices, vc) };
        let indices = unsafe { std::slice::from_raw_parts(mesh.indices, ic) };
        if vertices
            .iter()
            .any(|v| v.pos.iter().chain(v.uv.iter()).any(|f| !f.is_finite()))
            || indices.iter().any(|&i| i >= mesh.vertex_count)
        {
            return ERR_INVALID;
        }
        self.commands.push(Command {
            texture: mesh.texture,
            vertex_start: self.vertices.len(),
            vertex_count: mesh.vertex_count,
            index_start: self.indices.len(),
            index_count: mesh.index_count,
            clip: mesh.clip,
        });
        self.vertices.extend_from_slice(vertices);
        self.indices.extend_from_slice(indices);
        OK
    }
    pub fn meshes(&mut self) -> &[Mesh] {
        self.meshes.clear();
        for c in &self.commands {
            self.meshes.push(Mesh {
                texture: c.texture,
                vertices: self.vertices[c.vertex_start..].as_ptr(),
                vertex_count: c.vertex_count,
                indices: self.indices[c.index_start..].as_ptr(),
                index_count: c.index_count,
                clip: c.clip,
            });
        }
        &self.meshes
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn copies_foreign_memory_and_rejects_invalid_indices() {
        let mut s = Staging::default();
        let mut v = [Vertex::default(); 3];
        let mut i = [0, 1, 2];
        let mesh = Mesh {
            texture: 9,
            vertices: v.as_ptr(),
            vertex_count: 3,
            indices: i.as_ptr(),
            index_count: 3,
            clip: [0., 0., 100., 100.],
        };
        assert_eq!(unsafe { s.submit(&mesh) }, OK);
        v[0].pos = [99., 99.];
        i[0] = 2;
        assert_eq!(v[0].pos, [99., 99.]);
        assert_eq!(unsafe { (*s.meshes()[0].vertices).pos }, [0., 0.]);
        let bad = Mesh {
            index_count: 2,
            ..mesh
        };
        assert_eq!(unsafe { s.submit(&bad) }, ERR_INVALID);
        i[0] = 3;
        let invalid = Mesh {
            vertices: v.as_ptr(),
            indices: i.as_ptr(),
            ..mesh
        };
        assert_eq!(unsafe { s.submit(&invalid) }, ERR_INVALID);
        s.clear();
        assert!(s.meshes().is_empty());
    }
}
