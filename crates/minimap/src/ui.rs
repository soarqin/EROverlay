use crate::{
    data::{MapData, Source},
    gfx::{self, MapLayer, Recipe},
    host::Host,
    resources::{Definitions, Visuals},
    settings::{Preset, Settings, Shape},
};
use egui::{
    Color32, Pos2, Rect, TextureId, Vec2,
    epaint::{self, Mesh as PaintMesh, Primitive, Vertex as PaintVertex},
};
use eroverlay_abi as abi;
#[cfg(test)]
use eroverlay_ui::fonts::{STATUS_GLYPHS, custom_font_candidates};
use eroverlay_ui::fonts::{
    font_candidates, font_charset, load_font_definitions, minimal_font_definitions,
    supports_status_glyphs,
};
use std::time::{Duration, Instant};
use std::{cell::RefCell, collections::HashMap, sync::Arc};

// Batch adjacent native layers without reordering them. Avoid one Vec/Arc mesh
// allocation per bitmap layer while retaining Scaleform child draw order.
#[derive(Default)]
struct MeshPool {
    meshes: Vec<Arc<PaintMesh>>,
    cursor: usize,
    ready: bool,
    vertices: usize,
    indices: usize,
    groups: usize,
    full: bool,
}
impl MeshPool {
    fn begin(&mut self) {
        self.cursor = 0;
        self.ready = false;
        self.vertices = 0;
        self.indices = 0;
        self.groups = 0;
        self.full = false;
    }
    fn reserve(&mut self, vertices: usize, indices: usize, new_group: bool) -> bool {
        if self.full
            || vertices > abi::MAX_VERTICES.saturating_sub(self.vertices)
            || indices > abi::MAX_INDICES.saturating_sub(self.indices)
            || (new_group && self.groups == abi::MAX_MESHES)
        {
            self.full = true;
            return false;
        }
        self.vertices += vertices;
        self.indices += indices;
        self.groups += usize::from(new_group);
        true
    }
    fn active(&mut self) -> &mut PaintMesh {
        if self.cursor == self.meshes.len() {
            self.meshes.push(Arc::new(PaintMesh::default()));
        }
        if !self.ready {
            if let Some(mesh) = Arc::get_mut(&mut self.meshes[self.cursor]) {
                mesh.vertices.clear();
                mesh.indices.clear();
            } else {
                self.meshes[self.cursor] = Arc::new(PaintMesh::default());
            }
            self.ready = true;
        }
        Arc::get_mut(&mut self.meshes[self.cursor])
            .expect("active mesh is not published before flush")
    }
}
struct Drawing<'a> {
    painter: &'a egui::Painter,
    pool: RefCell<&'a mut MeshPool>,
}
impl<'a> Drawing<'a> {
    fn new(painter: &'a egui::Painter, pool: &'a mut MeshPool) -> Self {
        pool.begin();
        Self {
            painter,
            pool: RefCell::new(pool),
        }
    }
    fn flush(&self) {
        let mut pool = self.pool.borrow_mut();
        if !pool.ready || pool.meshes[pool.cursor].indices.is_empty() {
            return;
        }
        let shape = pool.meshes[pool.cursor].clone();
        pool.cursor += 1;
        pool.ready = false;
        drop(pool);
        self.painter.add(epaint::Shape::Mesh(shape));
    }
    fn append(
        &self,
        texture: TextureId,
        vertices: impl ExactSizeIterator<Item = PaintVertex>,
        indices: &[u32],
    ) {
        if self.full() {
            return;
        }
        let changed = {
            let pool = self.pool.borrow();
            pool.ready && pool.meshes[pool.cursor].texture_id != texture
        };
        if changed {
            self.flush();
        }
        let mut pool = self.pool.borrow_mut();
        let new_group = !pool.ready;
        if !pool.reserve(vertices.len(), indices.len(), new_group) {
            return;
        }
        let batch = pool.active();
        batch.texture_id = texture;
        let base = batch.vertices.len() as u32;
        batch.vertices.extend(vertices);
        batch.indices.extend(indices.iter().map(|i| i + base));
    }
    fn full(&self) -> bool {
        self.pool.borrow().full
    }
    fn line_segment(&self, points: [Pos2; 2], stroke: impl Into<egui::Stroke>) {
        // epaint line antialiasing fits within this conservative primitive budget.
        if !self.pool.borrow_mut().reserve(16, 48, true) {
            return;
        }
        self.flush();
        self.painter.line_segment(points, stroke);
    }
    fn quadratic(&self, from: Pos2, control: Pos2, to: Pos2, stroke: impl Into<egui::Stroke>) {
        // A quadratic chord deviates by at most |P0-2P1+P2|/(4*n*n).
        // Reserve the 0.25-pixel-error polyline budget before any allocation.
        let curvature = Vec2::new(
            from.x - 2.0 * control.x + to.x,
            from.y - 2.0 * control.y + to.y,
        )
        .length();
        if !curvature.is_finite() {
            self.pool.borrow_mut().full = true;
            return;
        }
        let segments = (curvature.sqrt().ceil() as usize).max(1);
        let points = segments.saturating_add(1);
        if !self.pool.borrow_mut().reserve(
            points.saturating_mul(4).saturating_add(8),
            points.saturating_mul(18).saturating_add(24),
            true,
        ) {
            return;
        }
        let points: Vec<_> = (0..=segments)
            .map(|step| {
                let t = step as f32 / segments as f32;
                let u = 1.0 - t;
                Pos2::new(
                    from.x * u * u + control.x * 2.0 * u * t + to.x * t * t,
                    from.y * u * u + control.y * 2.0 * u * t + to.y * t * t,
                )
            })
            .collect();
        self.flush();
        self.painter.add(epaint::Shape::line(points, stroke.into()));
    }
    fn galley(&self, pos: Pos2, galley: Arc<egui::Galley>, color: Color32) {
        if !self
            .pool
            .borrow_mut()
            .reserve(galley.num_vertices, galley.num_indices, true)
        {
            return;
        }
        self.flush();
        self.painter.galley(pos, galley, color);
    }
    fn galley_with_override_text_color(
        &self,
        pos: Pos2,
        galley: Arc<egui::Galley>,
        color: Color32,
    ) {
        if !self
            .pool
            .borrow_mut()
            .reserve(galley.num_vertices, galley.num_indices, true)
        {
            return;
        }
        self.flush();
        self.painter
            .galley_with_override_text_color(pos, galley, color);
    }
}
impl std::ops::Deref for Drawing<'_> {
    type Target = egui::Painter;
    fn deref(&self) -> &Self::Target {
        self.painter
    }
}
impl Drop for Drawing<'_> {
    fn drop(&mut self) {
        self.flush();
    }
}

#[derive(Clone)]
pub struct Snapshot {
    pub shown: bool,
    pub preset: usize,
    pub settings: Arc<Settings>,
    pub data: MapData,
    pub visuals: Visuals,
    pub graces: bool,
    pub landmarks: bool,
}
struct FontTexture {
    size: [usize; 2],
    pixels: Vec<u8>,
    handle: u64,
    dirty: bool,
    pending: bool,
    retry_at: Option<Instant>,
}
pub struct Renderer {
    context: egui::Context,
    fonts: HashMap<TextureId, FontTexture>,
    vertices: Vec<abi::Vertex>,
    definitions: egui::FontDefinitions,
    status_size: f32,
    status_chinese: bool,
    deferred: Vec<u64>,
    pool: MeshPool,
    budget_reported: bool,
    last_sequence: Option<u64>,
}
impl Renderer {
    pub fn new() -> Self {
        let context = egui::Context::default();
        context.set_pixels_per_point(1.0);
        let definitions = minimal_font_definitions();
        context.set_fonts(definitions.clone());
        Self {
            context,
            fonts: HashMap::new(),
            vertices: Vec::with_capacity(8192),
            definitions,
            status_size: 20.0,
            status_chinese: false,
            deferred: Vec::new(),
            pool: MeshPool::default(),
            budget_reported: false,
            last_sequence: None,
        }
    }
    pub fn load_fonts(&mut self, host: Host) {
        use std::path::PathBuf;
        self.status_size = host
            .config_raw("common.font_size")
            .and_then(|v| v.parse::<f32>().ok())
            .filter(|v| v.is_finite() && *v > 0.0 && *v <= 512.0)
            .unwrap_or(20.0);
        let custom = host.config_raw("common.font").unwrap_or_default();
        let directory =
            PathBuf::from(host.config_raw("runtime.directory").unwrap_or_default()).join("data");
        let windows =
            PathBuf::from(std::env::var_os("WINDIR").unwrap_or_else(|| "C:/Windows".into()))
                .join("Fonts");
        let charset = font_charset(
            &host.config_raw("common.charset").unwrap_or_default(),
            &host.config_raw("runtime.game_language").unwrap_or_default(),
        );
        let paths = font_candidates(&directory, &custom, &windows, charset);
        let (definitions, loaded) =
            load_font_definitions(&paths, &windows.join("msyh.ttc"), |path| {
                std::fs::metadata(path)
                    .is_ok_and(|m| m.len() <= 64 * 1024 * 1024)
                    .then(|| std::fs::read(path).ok())
                    .flatten()
            });
        self.use_font_definitions(definitions);
        for path in loaded {
            host.log(&format!("minimap-font {}\n", path.display()));
        }
        self.pool = MeshPool::default();
    }
    fn use_font_definitions(&mut self, definitions: egui::FontDefinitions) {
        self.status_chinese = supports_status_glyphs(&definitions);
        self.definitions = definitions;
        self.context.set_fonts(self.definitions.clone());
    }
    pub fn reset(&mut self, host: Host) {
        self.last_sequence = None;
        for (_, font) in self.fonts.drain() {
            host.release_texture(font.handle);
        }
        for handle in self.deferred.drain(..) {
            host.release_texture(handle);
        }
        self.context = egui::Context::default();
        self.context.set_pixels_per_point(1.0);
        self.context.set_fonts(self.definitions.clone());
    }
    pub fn render(&mut self, host: Host, snapshot: &Snapshot, frame: &abi::FrameInfo) -> i32 {
        // One egui frame per native game/UI frame. A queue may be observed more
        // than once; repeated callbacks must not consume deltas or advance UI.
        if self.last_sequence == Some(frame.sequence) {
            return abi::OK;
        }
        self.last_sequence = Some(frame.sequence);
        for handle in self.deferred.drain(..) {
            host.release_texture(handle);
        }
        if !snapshot.shown || !host.visible() {
            return abi::OK;
        }
        let preset = &snapshot.settings.presets[snapshot.preset];
        if preset.zoom < 0.0001 {
            return abi::OK;
        }
        let Some(rect) = map_rect(preset, [frame.width, frame.height]) else {
            return abi::OK;
        };
        let can_paint_map =
            snapshot.data.valid && snapshot.visuals.definitions.is_some() && preset.opacity > 0.0;
        let has_status = !snapshot.visuals.status.is_empty();
        let circle = preset.rotate || preset.shape == Shape::Circle;
        let radius = if circle {
            rect.width() * 0.5
        } else if preset.shape == Shape::Rounded {
            if preset.rounding.percent {
                preset.rounding.value * rect.size().min_elem() * 0.5
            } else {
                preset.rounding.value
            }
        } else {
            0.0
        }
        .clamp(0.0, rect.size().min_elem() * 0.5);
        let options = abi::RenderOptions {
            clip_rect: [rect.min.x, rect.min.y, rect.max.x, rect.max.y],
            circle: u32::from(circle),
            corner_radius: radius,
            // Resource diagnostics remain visible when there is no map to
            // composite, including an explicitly transparent preset.
            opacity: if has_status && !can_paint_map {
                1.0
            } else {
                preset.opacity
            },
            border_width: snapshot.settings.border_width,
            border_color: Color32::from_rgba_unmultiplied(
                snapshot.settings.border[0],
                snapshot.settings.border[1],
                snapshot.settings.border[2],
                snapshot.settings.border[3],
            )
            .to_array()
            .into_iter()
            .enumerate()
            .fold(0, |v, (i, b)| v | (u32::from(b) << (i * 8))),
            ..Default::default()
        };
        if let Some(set) = host.0.set_render_options {
            let result = unsafe { set(host.0.user, &options) };
            if result != abi::OK {
                return result;
            }
        }
        let raw = egui::RawInput {
            screen_rect: Some(Rect::from_min_size(
                Pos2::ZERO,
                Vec2::new(frame.width, frame.height),
            )),
            time: Some(frame.sequence as f64 * f64::from(frame.delta_seconds)),
            predicted_dt: frame.delta_seconds.max(0.001),
            ..Default::default()
        };
        if !can_paint_map {
            self.pool.begin();
        }
        let status = readable_status(snapshot.visuals.status, self.status_chinese);
        let output = self.context.run(raw, |ctx| {
            let painter = ctx
                .layer_painter(egui::LayerId::new(
                    egui::Order::Background,
                    egui::Id::new("Minimap"),
                ))
                .with_clip_rect(rect);
            if can_paint_map {
                paint(&painter, snapshot, preset, rect, &mut self.pool);
            }
            // Draw actionable resource errors
            // after map content; valid gameplay data does not imply assets exist.
            if has_status {
                painter.text(
                    rect.center(),
                    egui::Align2::CENTER_CENTER,
                    status,
                    egui::FontId::proportional(self.status_size),
                    Color32::WHITE,
                );
            }
        });
        if self.pool.full && !self.budget_reported {
            host.log("minimap-geometry: ABI frame budget reached; remaining native-order geometry omitted\n");
        }
        self.budget_reported = self.pool.full;
        for (id, delta) in output.textures_delta.set {
            let egui::ImageData::Color(image) = delta.image;
            let mut pixels = Vec::with_capacity(image.pixels.len() * 4);
            for pixel in &image.pixels {
                let [r, g, b, a] = pixel.to_array();
                // Host RGBA textures are straight-alpha; epaint's Color32 is premultiplied.
                let straight = |c: u8| {
                    if a == 0 {
                        0
                    } else {
                        ((u32::from(c) * 255 + u32::from(a) / 2) / u32::from(a)).min(255) as u8
                    }
                };
                pixels.extend_from_slice(&[straight(r), straight(g), straight(b), a]);
            }
            let font = self.fonts.entry(id).or_insert(FontTexture {
                size: [0, 0],
                pixels: Vec::new(),
                handle: 0,
                dirty: true,
                pending: false,
                retry_at: None,
            });
            if let Some([x, y]) = delta.pos {
                if x + image.size[0] > font.size[0] || y + image.size[1] > font.size[1] {
                    return abi::ERR_INVALID;
                }
                for row in 0..image.size[1] {
                    let dst = ((y + row) * font.size[0] + x) * 4;
                    let src = row * image.size[0] * 4;
                    font.pixels[dst..dst + image.size[0] * 4]
                        .copy_from_slice(&pixels[src..src + image.size[0] * 4]);
                }
            } else {
                font.size = image.size;
                font.pixels = pixels;
            }
            font.dirty = true;
        }
        let now = Instant::now();
        for font in self.fonts.values_mut() {
            if font.pending
                && let Some(view) = host.texture_status(font.handle)
            {
                match view.status {
                    abi::TEXTURE_READY => font.pending = false,
                    abi::TEXTURE_FAILED => {
                        host.release_texture(font.handle);
                        font.handle = 0;
                        font.pending = false;
                        font.dirty = true;
                        font.retry_at = Some(now + Duration::from_millis(250));
                    }
                    _ => {}
                }
            }
            if !font.dirty || font.retry_at.is_some_and(|retry| now < retry) {
                continue;
            }
            let replacement = host.rgba(font.size[0] as u32, font.size[1] as u32, &font.pixels);
            if replacement != 0 {
                host.release_texture(font.handle);
                font.handle = replacement;
                font.dirty = false;
                font.pending = true;
                font.retry_at = None;
            } else {
                font.retry_at = Some(now + Duration::from_millis(250));
            }
        }
        let mut pending = Vec::new();
        for clipped in output.shapes {
            match clipped.shape {
                epaint::Shape::Mesh(mesh) => {
                    let result = self.flush_non_mesh(host, &mut pending, output.pixels_per_point);
                    if result != abi::OK {
                        return result;
                    }
                    // Custom epaint meshes are already triangles. Preserve the Arc pool
                    // instead of copying them through egui's generic tessellator again.
                    let result = self.submit_paint_mesh(host, &mesh, clipped.clip_rect);
                    if result != abi::OK {
                        return result;
                    }
                }
                shape => pending.push(epaint::ClippedShape {
                    clip_rect: clipped.clip_rect,
                    shape,
                }),
            }
        }
        let result = self.flush_non_mesh(host, &mut pending, output.pixels_per_point);
        if result != abi::OK {
            return result;
        }
        for id in output.textures_delta.free {
            if let Some(font) = self.fonts.remove(&id) {
                self.deferred.push(font.handle)
            }
        }
        abi::OK
    }
    fn flush_non_mesh(
        &mut self,
        host: Host,
        pending: &mut Vec<epaint::ClippedShape>,
        pixels_per_point: f32,
    ) -> i32 {
        if pending.is_empty() {
            return abi::OK;
        }
        for clipped in self
            .context
            .tessellate(std::mem::take(pending), pixels_per_point)
        {
            if let Primitive::Mesh(mesh) = clipped.primitive {
                let result = self.submit_paint_mesh(host, &mesh, clipped.clip_rect);
                if result != abi::OK {
                    return result;
                }
            }
        }
        abi::OK
    }
    #[cfg(test)]
    pub(crate) fn geometry_budget_exhausted(&self) -> bool {
        self.pool.full
    }
    fn submit_paint_mesh(&mut self, host: Host, mesh: &PaintMesh, clip: Rect) -> i32 {
        let texture = match mesh.texture_id {
            TextureId::User(id) => id,
            managed => self.fonts.get(&managed).map_or(0, |font| font.handle),
        };
        if texture == 0 {
            return abi::OK;
        }
        if mesh.vertices.len() > abi::MAX_VERTICES || mesh.indices.len() > abi::MAX_INDICES {
            return abi::ERR_BUSY;
        }
        self.vertices.clear();
        self.vertices
            .extend(mesh.vertices.iter().map(|v| abi::Vertex {
                pos: [v.pos.x, v.pos.y],
                uv: [v.uv.x, v.uv.y],
                color: u32::from_le_bytes(v.color.to_array()),
            }));
        let submit = abi::Mesh {
            texture,
            vertices: self.vertices.as_ptr(),
            vertex_count: self.vertices.len() as u32,
            indices: mesh.indices.as_ptr(),
            index_count: mesh.indices.len() as u32,
            clip: [clip.min.x, clip.min.y, clip.max.x, clip.max.y],
        };
        host.0
            .submit_mesh
            .map_or(abi::OK, |f| unsafe { f(host.0.user, &submit) })
    }
}

pub fn map_rect(preset: &Preset, screen: [f32; 2]) -> Option<Rect> {
    let reference = screen[0].min(screen[1]);
    let (mut w, mut h) = (
        (reference * preset.width).floor(),
        (reference * preset.height).floor(),
    );
    if preset.rotate || preset.shape == Shape::Circle {
        w = w.min(h);
        h = w;
    }
    if !w.is_finite() || !h.is_finite() || w <= 0.0 || h <= 0.0 || w > 16384.0 || h > 16384.0 {
        return None;
    }
    let (x, y) = if preset.center {
        ((screen[0] - w) * 0.5, (screen[1] - h) * 0.5)
    } else {
        let x = if preset.x.percent {
            preset.x.value * reference
        } else {
            preset.x.value
        };
        let y = if preset.y.percent {
            preset.y.value * reference
        } else {
            preset.y.value
        };
        (
            if preset.left { x } else { screen[0] - w - x },
            if preset.bottom { screen[1] - h - y } else { y },
        )
    };
    if !x.is_finite() || !y.is_finite() {
        return None;
    }
    Some(Rect::from_min_size(
        Pos2::new(x.floor(), y.floor()),
        Vec2::new(w, h),
    ))
}

fn quad(painter: &Drawing<'_>, texture: u64, points: [Pos2; 4], uv: [Pos2; 4], color: Color32) {
    painter.append(
        TextureId::User(texture),
        (0..4).map(|i| PaintVertex {
            pos: points[i],
            uv: uv[i],
            color,
        }),
        &[0, 1, 2, 0, 2, 3],
    );
}
#[allow(clippy::too_many_arguments)] // Explicit immutable drawing inputs, no shared mutable painter state.
fn draw_recipe(
    painter: &Drawing<'_>,
    recipe: Option<&Recipe>,
    visuals: &Visuals,
    defs: &Definitions,
    center: Pos2,
    scale: f32,
    angle: f32,
    runtime: Option<&crate::host::Texture>,
) {
    let Some(recipe) = recipe else { return };
    if !scale.is_finite() || scale <= 0.0 || !angle.is_finite() {
        return;
    }
    let (c, s) = (angle.cos(), angle.sin());
    for layer in &recipe.layers {
        if painter.full() {
            return;
        }
        let project = |point| {
            let p = gfx::transform(layer.matrix, point);
            center + Vec2::new(p[0] * c - p[1] * s, p[0] * s + p[1] * c) * scale
        };
        if !layer.image.is_empty() {
            let (region, texture) = if let Some(texture) = runtime {
                (
                    crate::resources::Region {
                        atlas: 0,
                        x: 0,
                        y: 0,
                        width: texture.width,
                        height: texture.height,
                    },
                    texture,
                )
            } else {
                let Some(region) = layer.region.and_then(|r| defs.regions.get(r)) else {
                    continue;
                };
                let Some(texture) = visuals.atlases.get(region.atlas).and_then(Option::as_deref)
                else {
                    continue;
                };
                (*region, texture)
            };
            if region.x + region.width > texture.width || region.y + region.height > texture.height
            {
                continue;
            }
            let points = [
                project([0.0, 0.0]),
                project([layer.width as f32, 0.0]),
                project([layer.width as f32, layer.height as f32]),
                project([0.0, layer.height as f32]),
            ];
            if !painter.clip_rect().intersects(Rect::from_points(&points)) {
                continue;
            }
            let u0 = region.x as f32 / texture.width as f32;
            let v0 = region.y as f32 / texture.height as f32;
            let u1 = (region.x + region.width) as f32 / texture.width as f32;
            let v1 = (region.y + region.height) as f32 / texture.height as f32;
            quad(
                painter,
                texture.id,
                points,
                [
                    Pos2::new(u0, v0),
                    Pos2::new(u1, v0),
                    Pos2::new(u1, v1),
                    Pos2::new(u0, v1),
                ],
                Color32::WHITE,
            );
        } else {
            let b = layer.shape.bounds;
            if !painter.clip_rect().intersects(Rect::from_points(&[
                project([b[0], b[1]]),
                project([b[2], b[1]]),
                project([b[2], b[3]]),
                project([b[0], b[3]]),
            ])) {
                continue;
            }
            for compiled in &layer.shape.compiled {
                if painter.full() {
                    break;
                }
                let color = Color32::from_rgba_unmultiplied(
                    compiled.color[0],
                    compiled.color[1],
                    compiled.color[2],
                    compiled.color[3],
                );
                painter.append(
                    TextureId::Managed(0),
                    compiled.vertices.iter().map(|p| PaintVertex {
                        pos: project(*p),
                        uv: epaint::WHITE_UV,
                        color,
                    }),
                    &compiled.indices,
                );
            }
            let mut previous = [0.0, 0.0];
            for command in &layer.shape.commands {
                if painter.full() {
                    break;
                }
                if command.op != 0
                    && command.stroke != 0
                    && let Some(style) = layer.shape.strokes.get(usize::from(command.stroke) - 1)
                {
                    let color = Color32::from_rgba_unmultiplied(
                        style.color[0],
                        style.color[1],
                        style.color[2],
                        style.color[3],
                    );
                    let width = (style.width
                        * scale
                        * (layer.matrix[0] * layer.matrix[3] - layer.matrix[1] * layer.matrix[2])
                            .abs()
                            .sqrt())
                    .max(0.5);
                    if command.op == 1 {
                        painter
                            .line_segment([project(previous), project(command.to)], (width, color));
                    } else {
                        let from = project(previous);
                        let control = project(command.control);
                        let to = project(command.to);
                        painter.quadratic(from, control, to, (width, color));
                    }
                }
                previous = command.to;
            }
        }
    }
}

fn paint(
    painter: &egui::Painter,
    snapshot: &Snapshot,
    preset: &Preset,
    rect: Rect,
    pool: &mut MeshPool,
) {
    let drawing = Drawing::new(painter, pool);
    let painter = &drawing;
    let Some(defs) = snapshot.visuals.definitions.as_deref() else {
        return;
    };
    let data = &snapshot.data;
    let state = &data.state;
    let scale = (preset.zoom * preset.map_scale).clamp(0.01, 16.0);
    let center = rect.center();
    let angle =
        if preset.rotate && state.camera_yaw_cos.is_finite() && state.camera_yaw_sin.is_finite() {
            state.camera_yaw_sin.atan2(state.camera_yaw_cos)
        } else {
            0.0
        };
    let (c, s) = (angle.cos(), angle.sin());
    let point = |x: f32, y: f32| {
        let x = (x - state.x) * scale;
        let y = (y - state.y) * scale;
        center + Vec2::new(x * c - y * s, x * s + y * c)
    };
    let outside = |p: Pos2| !rect.expand(200.0).contains(p);
    let recipe = |name: &str, p: Pos2, size: f32, rotation: f32| {
        draw_recipe(
            painter,
            defs.recipe(name),
            &snapshot.visuals,
            defs,
            p,
            size,
            rotation,
            None,
        )
    };
    let player = || {
        recipe("player", center, scale * 0.45 * preset.player_scale, 0.0);
        recipe(
            "arrow",
            center,
            scale * 0.45 * preset.player_scale,
            state.ori_deg.to_radians() + angle,
        );
    };
    if data.roundtable {
        recipe("home", center, preset.zoom * preset.map_scale, angle);
        draw_recipe(
            painter,
            defs.icons.get(&data.home_icon),
            &snapshot.visuals,
            defs,
            center,
            0.5 * preset.decoration_scale * scale,
            angle,
            None,
        );
        player();
    } else if state.map_id == 0 || state.map_id == 10 {
        let map = if state.map_id == 10 {
            2
        } else {
            u32::from(state.underground & 1 != 0)
        };
        let marker_map = if map == 2 { 10 } else { map as u8 };
        for tile in &snapshot.visuals.tiles {
            let b = tile.bounds;
            quad(
                painter,
                tile.texture.id,
                [
                    point(b[0], b[1]),
                    point(b[2], b[1]),
                    point(b[2], b[3]),
                    point(b[0], b[3]),
                ],
                [
                    Pos2::ZERO,
                    Pos2::new(1.0, 0.0),
                    Pos2::new(1.0, 1.0),
                    Pos2::new(0.0, 1.0),
                ],
                Color32::WHITE,
            );
        }
        for layer in &defs.layers {
            match layer {
                MapLayer::Area | MapLayer::Item | MapLayer::Warp => {
                    for marker in data.decorations.iter() {
                        if painter.full() {
                            break;
                        }
                        let own = if marker.source == Source::Grace {
                            MapLayer::Warp
                        } else if marker.area {
                            MapLayer::Area
                        } else {
                            MapLayer::Item
                        };
                        if own != *layer || marker.maps & (1 << map) == 0 {
                            continue;
                        }
                        let show = match marker.source {
                            Source::Grace => snapshot.graces,
                            Source::Landmark => snapshot.landmarks,
                            Source::Other => snapshot.settings.others,
                        };
                        if !show {
                            continue;
                        }
                        let p = point(marker.x, marker.y);
                        if outside(p) {
                            continue;
                        }
                        let size = if marker.area {
                            scale
                        } else {
                            0.5 * preset.decoration_scale * scale
                        };
                        if marker.runtime {
                            if let Some((recipe, texture)) = data.runtime.get(&marker.icon) {
                                draw_recipe(
                                    painter,
                                    Some(recipe),
                                    &snapshot.visuals,
                                    defs,
                                    p,
                                    size,
                                    marker.angle + angle,
                                    Some(texture),
                                );
                            }
                        } else {
                            draw_recipe(
                                painter,
                                defs.icons.get(&marker.icon),
                                &snapshot.visuals,
                                defs,
                                p,
                                size,
                                marker.angle + angle,
                                None,
                            );
                        }
                        if marker.cleared {
                            recipe("cleared", p, size, marker.angle + angle);
                        }
                    }
                }
                MapLayer::Memo => {
                    if snapshot.settings.others {
                        for marker in &data.memos {
                            let p = point(marker.x, marker.y);
                            if marker.map == marker_map && !outside(p) {
                                draw_recipe(
                                    painter,
                                    defs.memos.get(&u32::from(marker.number)),
                                    &snapshot.visuals,
                                    defs,
                                    p,
                                    0.5 * preset.decoration_scale * scale,
                                    0.0,
                                    None,
                                );
                            }
                        }
                    }
                }
                MapLayer::Marker => {
                    if snapshot.settings.beacons {
                        for marker in &data.beacons {
                            let p = point(marker.x, marker.y);
                            if marker.map == marker_map && !outside(p) {
                                let size = scale * 2.0 * 0.45 * preset.player_scale;
                                recipe("marker", p, size, 0.0);
                                if size > 0.0 {
                                    let text = char::from(b'0' + marker.number).to_string();
                                    let layout = &defs.text;
                                    let font = egui::FontId::proportional(layout.height * size);
                                    let color = Color32::from_rgba_unmultiplied(
                                        layout.color[0],
                                        layout.color[1],
                                        layout.color[2],
                                        layout.color[3],
                                    );
                                    let galley = painter.layout_no_wrap(text, font, color);
                                    let left = layout.bounds[0] + layout.left;
                                    let right = layout.bounds[2] - layout.right;
                                    let x = if layout.align == 2 {
                                        (left + right - galley.size().x / size) * 0.5
                                    } else if layout.align == 1 {
                                        right - galley.size().x / size
                                    } else {
                                        left + layout.indent
                                    };
                                    let local =
                                        gfx::transform(layout.matrix, [x, layout.bounds[1] + 2.0]);
                                    let pos = p + Vec2::new(local[0], local[1]) * size;
                                    let offset = size.max(0.5);
                                    for d in [
                                        Vec2::new(-offset, 0.0),
                                        Vec2::new(offset, 0.0),
                                        Vec2::new(0.0, -offset),
                                        Vec2::new(0.0, offset),
                                    ] {
                                        painter.galley_with_override_text_color(
                                            pos + d,
                                            galley.clone(),
                                            Color32::from_rgb(26, 14, 0),
                                        );
                                    }
                                    painter.galley(pos, galley, color);
                                }
                            }
                        }
                    }
                }
                MapLayer::Dead => {
                    if snapshot.settings.death
                        && state.death_valid != 0
                        && state.death_map_id == i32::from(marker_map)
                    {
                        recipe(
                            "death",
                            point(state.death_x, state.death_y),
                            scale * 0.9 * preset.player_scale,
                            angle,
                        );
                    }
                }
                MapLayer::Player => player(),
            }
        }
    } else {
        player();
    }
    if preset.rotate
        && let Some(bearing) = defs.recipe("bearing")
        && let Some(layer) = bearing.layers.first()
    {
        let size = rect.size().min_elem() * 0.4 * preset.compass_scale;
        if layer.width != 0 {
            draw_recipe(
                painter,
                Some(bearing),
                &snapshot.visuals,
                defs,
                Pos2::new(rect.max.x - size * 0.5, rect.min.y + size * 0.5),
                size / layer.width as f32,
                angle,
                None,
            );
        }
    }
}

// Production text is limited to the resource-status painter and beacon digits;
// both request FontId::proportional. Map icons are native bitmap/vector recipes,
// not emoji or monospace text. Keep a full Latin/Cyrillic fallback for failed
// custom/system reads without linking the other three egui default font files.
fn readable_status(status: &str, chinese: bool) -> &str {
    if chinese {
        return status;
    }
    match status {
        "游戏 GFX 或图集布局无法解析" => "GFX / ATLAS LAYOUT PARSE FAILED",
        "地图瓦片目录无法解析" => "MAP TILE DIRECTORY PARSE FAILED",
        "游戏资源读取失败，正在重试" => "GAME RESOURCE READ FAILED - RETRYING",
        "图集读取或处理失败，正在重试" => "ATLAS READ / PROCESS FAILED - RETRYING",
        _ => status,
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn waiting_frames_back_off_rejected_font_uploads_and_stop_polling_ready_fonts() {
        use std::{cell::Cell, ffi::c_void};
        struct Sink {
            accepted: Cell<bool>,
            uploads: Cell<usize>,
            status: Cell<u32>,
            polls: Cell<usize>,
            releases: Cell<usize>,
        }
        unsafe extern "C" fn display(_: *mut c_void, out: *mut abi::DisplayState) -> i32 {
            unsafe {
                out.write(abi::DisplayState {
                    valid: 1,
                    ..Default::default()
                })
            };
            abi::OK
        }
        unsafe extern "C" fn rgba(user: *mut c_void, _: u32, _: u32, _: abi::ByteSlice) -> u64 {
            let sink = unsafe { &*user.cast::<Sink>() };
            sink.uploads.set(sink.uploads.get() + 1);
            if sink.accepted.get() {
                sink.uploads.get() as u64
            } else {
                0
            }
        }
        unsafe extern "C" fn poll(user: *mut c_void, _: u64, out: *mut abi::TextureView) -> i32 {
            let sink = unsafe { &*user.cast::<Sink>() };
            sink.polls.set(sink.polls.get() + 1);
            unsafe {
                out.write(abi::TextureView {
                    status: sink.status.get(),
                    ..Default::default()
                })
            };
            abi::OK
        }
        unsafe extern "C" fn release(user: *mut c_void, _: u64) {
            let sink = unsafe { &*user.cast::<Sink>() };
            sink.releases.set(sink.releases.get() + 1);
        }
        let sink = Sink {
            accepted: Cell::new(false),
            uploads: Cell::new(0),
            status: Cell::new(abi::TEXTURE_PENDING),
            polls: Cell::new(0),
            releases: Cell::new(0),
        };
        // SAFETY: zero nullable callbacks/pointers and integer ABI members are valid.
        let mut api: abi::HostApi = unsafe { std::mem::zeroed() };
        api.user = (&sink as *const Sink).cast_mut().cast();
        api.read_display_state = Some(display);
        api.texture_create_rgba = Some(rgba);
        api.texture_poll = Some(poll);
        api.texture_release = Some(release);
        let host = Host(api);
        let mut renderer = Renderer::new();
        let snapshot = Snapshot {
            shown: true,
            preset: 0,
            settings: Arc::new(Settings::load(&HashMap::new())),
            data: MapData::default(),
            visuals: Visuals {
                status: "RESOURCE READ FAILED",
                ..Default::default()
            },
            graces: true,
            landmarks: true,
        };
        let mut frame = abi::FrameInfo {
            width: 1920.0,
            height: 1080.0,
            delta_seconds: 1.0 / 60.0,
            ..Default::default()
        };
        assert_eq!(renderer.render(host, &snapshot, &frame), abi::OK);
        assert_eq!(sink.uploads.get(), 1);
        assert!(
            renderer
                .fonts
                .values()
                .all(|font| font.dirty && font.retry_at.is_some())
        );
        // Control only the deadline; calls still run real Context::run, glyph
        // deltas, font pixels and the production retry/submission path.
        for font in renderer.fonts.values_mut() {
            font.retry_at = Some(Instant::now() + Duration::from_secs(60));
        }
        for sequence in 1..101 {
            frame.sequence = sequence;
            assert_eq!(renderer.render(host, &snapshot, &frame), abi::OK);
        }
        assert_eq!(
            sink.uploads.get(),
            1,
            "rejected font queues must not be retried each frame"
        );
        sink.accepted.set(true);
        for font in renderer.fonts.values_mut() {
            font.retry_at = None;
        }
        frame.sequence += 1;
        assert_eq!(renderer.render(host, &snapshot, &frame), abi::OK);
        assert_eq!(sink.uploads.get(), 2);
        for _ in 0..10 {
            frame.sequence += 1;
            assert_eq!(renderer.render(host, &snapshot, &frame), abi::OK);
        }
        assert_eq!(
            sink.uploads.get(),
            2,
            "a pending font upload must not be resubmitted"
        );
        sink.status.set(abi::TEXTURE_FAILED);
        frame.sequence += 1;
        assert_eq!(renderer.render(host, &snapshot, &frame), abi::OK);
        assert_eq!(sink.releases.get(), 1);
        assert_eq!(sink.uploads.get(), 2);
        for font in renderer.fonts.values_mut() {
            font.retry_at = None;
        }
        sink.status.set(abi::TEXTURE_READY);
        frame.sequence += 1;
        assert_eq!(renderer.render(host, &snapshot, &frame), abi::OK);
        frame.sequence += 1;
        assert_eq!(renderer.render(host, &snapshot, &frame), abi::OK);
        let polls = sink.polls.get();
        for _ in 0..100 {
            frame.sequence += 1;
            assert_eq!(renderer.render(host, &snapshot, &frame), abi::OK);
        }
        assert_eq!(sink.uploads.get(), 3);
        assert_eq!(
            sink.polls.get(),
            polls,
            "ready fonts do not scan/poll the texture service per frame"
        );
    }
    #[test]
    fn resource_status_survives_valid_data_and_preserves_hiding_opacity_and_border() {
        use crate::{host::Texture, resources::TileView};
        use std::ffi::c_void;
        struct Sink {
            display: abi::DisplayState,
            options: Vec<abi::RenderOptions>,
            meshes: Vec<(u64, u32, u32)>,
        }
        impl Sink {
            fn clear(&mut self) {
                self.options.clear();
                self.meshes.clear();
            }
        }
        unsafe extern "C" fn display(user: *mut c_void, out: *mut abi::DisplayState) -> i32 {
            unsafe { out.write((&*user.cast::<Sink>()).display) };
            abi::OK
        }
        unsafe extern "C" fn options(user: *mut c_void, options: *const abi::RenderOptions) -> i32 {
            unsafe { &mut *user.cast::<Sink>() }
                .options
                .push(unsafe { *options });
            abi::OK
        }
        unsafe extern "C" fn rgba(_: *mut c_void, _: u32, _: u32, _: abi::ByteSlice) -> u64 {
            901
        }
        unsafe extern "C" fn texture(_: *mut c_void, _: u64, out: *mut abi::TextureView) -> i32 {
            unsafe {
                out.write(abi::TextureView {
                    status: abi::TEXTURE_READY,
                    width: 256,
                    height: 256,
                    memory_bytes: 256 * 256 * 4,
                    reserved: 0,
                })
            };
            abi::OK
        }
        unsafe extern "C" fn submit(user: *mut c_void, mesh: *const abi::Mesh) -> i32 {
            let mesh = unsafe { &*mesh };
            assert!(mesh.vertex_count > 0 && mesh.index_count > 0);
            assert!(!mesh.vertices.is_null() && !mesh.indices.is_null());
            assert!(
                unsafe { std::slice::from_raw_parts(mesh.vertices, mesh.vertex_count as usize) }
                    .iter()
                    .all(|v| v.pos.iter().all(|v| v.is_finite()))
            );
            unsafe { &mut *user.cast::<Sink>() }.meshes.push((
                mesh.texture,
                mesh.vertex_count,
                mesh.index_count,
            ));
            abi::OK
        }
        let mut sink = Sink {
            display: abi::DisplayState {
                valid: 1,
                ..Default::default()
            },
            options: Vec::new(),
            meshes: Vec::new(),
        };
        // SAFETY: HostApi contains integers, raw pointers and nullable callbacks.
        let mut api: abi::HostApi = unsafe { std::mem::zeroed() };
        api.user = (&mut sink as *mut Sink).cast();
        api.read_display_state = Some(display);
        api.set_render_options = Some(options);
        api.texture_create_rgba = Some(rgba);
        api.texture_poll = Some(texture);
        api.submit_mesh = Some(submit);
        let host = Host(api);
        let mut settings = Settings::load(&HashMap::new());
        settings.presets = vec![Preset {
            opacity: 0.35,
            ..Default::default()
        }];
        settings.border = [120, 160, 200, 128];
        settings.border_width = 3.5;
        let border =
            u32::from_le_bytes(Color32::from_rgba_unmultiplied(120, 160, 200, 128).to_array());
        let mut snapshot = Snapshot {
            shown: true,
            preset: 0,
            settings: Arc::new(settings),
            data: MapData {
                valid: true,
                ..Default::default()
            },
            visuals: Visuals::default(),
            graces: true,
            landmarks: true,
        };
        let mut frame = abi::FrameInfo {
            size: size_of::<abi::FrameInfo>() as u32,
            layer: abi::LAYER_BELOW_UI,
            width: 1920.0,
            height: 1080.0,
            pixels_per_point: 1.0,
            delta_seconds: 1.0 / 60.0,
            ..Default::default()
        };
        let mut renderer = Renderer::new();
        let paths = font_candidates(
            std::path::Path::new("data"),
            "missing",
            std::path::Path::new("Fonts"),
            "zh",
        );
        let (definitions, loaded) =
            load_font_definitions(&paths, std::path::Path::new("Fonts/msyh.ttc"), |_| None);
        assert!(loaded.is_empty());
        renderer.use_font_definitions(definitions);
        assert!(!renderer.status_chinese);
        // Actual font deltas, egui tessellation and ABI submission must produce
        // text even though gameplay data is valid and definitions are absent.
        for (status, fallback) in [
            ("UNSUPPORTED RESOURCE", "UNSUPPORTED RESOURCE"),
            ("RESOURCE READ FAILED", "RESOURCE READ FAILED"),
            (
                "游戏 GFX 或图集布局无法解析",
                "GFX / ATLAS LAYOUT PARSE FAILED",
            ),
            ("地图瓦片目录无法解析", "MAP TILE DIRECTORY PARSE FAILED"),
            (
                "游戏资源读取失败，正在重试",
                "GAME RESOURCE READ FAILED - RETRYING",
            ),
            (
                "图集读取或处理失败，正在重试",
                "ATLAS READ / PROCESS FAILED - RETRYING",
            ),
        ] {
            snapshot.visuals.status = status;
            frame.sequence += 1;
            sink.clear();
            assert_eq!(renderer.render(host, &snapshot, &frame), abi::OK);
            assert_eq!(readable_status(status, renderer.status_chinese), fallback);
            assert!(fallback.is_ascii());
            assert!(renderer.context.fonts_mut(|fonts| {
                fonts.has_glyphs(&egui::FontId::proportional(renderer.status_size), fallback)
            }));
            assert!(sink.meshes.iter().any(|m| m.0 == 901));
            assert_eq!(sink.options[0].opacity, 1.0);
            assert_eq!(sink.options[0].border_width, 3.5);
            assert_eq!(sink.options[0].border_color, border);
        }
        snapshot.data.valid = false;
        frame.sequence += 1;
        sink.clear();
        assert_eq!(renderer.render(host, &snapshot, &frame), abi::OK);
        assert!(sink.meshes.iter().any(|m| m.0 == 901));
        assert_eq!(sink.options[0].opacity, 1.0);
        snapshot.data.valid = true;
        snapshot.visuals.definitions = Some(Arc::new(Definitions {
            icons: HashMap::new(),
            memos: HashMap::new(),
            specials: HashMap::new(),
            text: gfx::TextLayout {
                bounds: [0.0; 4],
                matrix: gfx::IDENTITY,
                height: 20.0,
                color: [255; 4],
                align: 0,
                left: 0.0,
                right: 0.0,
                indent: 0.0,
            },
            layers: Vec::new(),
            regions: Vec::new(),
        }));
        snapshot.visuals.tiles.push(TileView {
            texture: Arc::new(Texture::ready(host, 42).unwrap()),
            bounds: [-64.0, -64.0, 64.0, 64.0],
        });
        for status in ["RESOURCE READ FAILED", ""] {
            snapshot.visuals.status = status;
            frame.sequence += 1;
            sink.clear();
            assert_eq!(renderer.render(host, &snapshot, &frame), abi::OK);
            assert!(sink.meshes.iter().any(|m| m.0 == 42));
            assert_eq!(
                sink.meshes.iter().any(|m| m.0 == 901),
                !status.is_empty(),
                "partial resource failures must overlay the available map"
            );
            assert_eq!(sink.options[0].opacity, 0.35);
            assert_eq!(sink.options[0].border_color, border);
        }

        // An opacity-zero map never submits its terrain, while its diagnostic
        // remains opaque. With no diagnostic only the independent border stays.
        Arc::make_mut(&mut snapshot.settings).presets[0].opacity = 0.0;
        for (status, opacity) in [("RESOURCE READ FAILED", 1.0), ("", 0.0)] {
            snapshot.visuals.status = status;
            frame.sequence += 1;
            sink.clear();
            assert_eq!(renderer.render(host, &snapshot, &frame), abi::OK);
            assert_eq!(sink.meshes.iter().any(|m| m.0 == 901), !status.is_empty());
            assert!(!sink.meshes.iter().any(|m| m.0 == 42));
            assert_eq!(sink.options[0].opacity, opacity);
            assert_eq!(sink.options[0].border_width, 3.5);
            assert_eq!(sink.options[0].border_color, border);
        }
        snapshot.visuals.status = "RESOURCE READ FAILED";
        for display in [
            abi::DisplayState {
                valid: 1,
                loading_or_logo: 1,
                full_screen_map: 0,
            },
            abi::DisplayState {
                valid: 1,
                loading_or_logo: 0,
                full_screen_map: 1,
            },
            abi::DisplayState::default(),
        ] {
            frame.sequence += 1;
            sink.clear();
            sink.display = display;
            let before = renderer.context.cumulative_frame_nr();
            assert_eq!(renderer.render(host, &snapshot, &frame), abi::OK);
            assert!(sink.meshes.is_empty() && sink.options.is_empty());
            assert_eq!(renderer.context.cumulative_frame_nr(), before);
        }
        sink.display = abi::DisplayState {
            valid: 1,
            ..Default::default()
        };
        snapshot.shown = false;
        frame.sequence += 1;
        sink.clear();
        assert_eq!(renderer.render(host, &snapshot, &frame), abi::OK);
        assert!(sink.meshes.is_empty() && sink.options.is_empty());
    }
    #[test]
    fn duplicate_native_frames_advance_context_once_and_reset_allows_same_sequence() {
        use std::sync::atomic::{AtomicUsize, Ordering};
        unsafe extern "C" fn display(
            user: *mut std::ffi::c_void,
            out: *mut abi::DisplayState,
        ) -> i32 {
            unsafe { &*user.cast::<AtomicUsize>() }.fetch_add(1, Ordering::Relaxed);
            unsafe {
                out.write(abi::DisplayState {
                    valid: 1,
                    ..Default::default()
                })
            };
            abi::OK
        }
        let calls = AtomicUsize::new(0);
        // SAFETY: HostApi has only nullable callbacks/pointers and integers.
        let mut api: abi::HostApi = unsafe { std::mem::zeroed() };
        api.user = (&calls as *const AtomicUsize).cast_mut().cast();
        api.read_display_state = Some(display);
        let host = Host(api);
        let snapshot = Snapshot {
            shown: true,
            preset: 0,
            settings: Arc::new(Settings::load(&HashMap::new())),
            data: MapData::default(),
            visuals: Visuals::default(),
            graces: true,
            landmarks: true,
        };
        let mut frame = abi::FrameInfo {
            width: 1920.0,
            height: 1080.0,
            delta_seconds: 1.0 / 60.0,
            ..Default::default()
        };
        let mut renderer = Renderer::new();
        let initial = renderer.context.cumulative_frame_nr();
        assert_eq!(renderer.render(host, &snapshot, &frame), abi::OK);
        assert_eq!(renderer.context.cumulative_frame_nr(), initial + 1);
        assert_eq!(renderer.render(host, &snapshot, &frame), abi::OK);
        assert_eq!(renderer.context.cumulative_frame_nr(), initial + 1);
        assert_eq!(calls.load(Ordering::Relaxed), 1);
        frame.sequence = 1;
        assert_eq!(renderer.render(host, &snapshot, &frame), abi::OK);
        assert_eq!(renderer.context.cumulative_frame_nr(), initial + 2);
        renderer.reset(host);
        assert_eq!(renderer.render(host, &snapshot, &frame), abi::OK);
        assert_eq!(renderer.context.cumulative_frame_nr(), 1);
        assert_eq!(calls.load(Ordering::Relaxed), 3);
    }
    #[test]
    fn custom_fonts_try_existing_extensionless_paths_and_charset_prefixes() {
        let paths = custom_font_candidates(std::path::Path::new("data"), "overlay");
        assert_eq!(
            paths,
            [
                "data/overlay",
                "data/overlay.ttf",
                "data/overlay.ttc",
                "data/overlay.otf"
            ]
            .map(std::path::PathBuf::from)
        );
        assert_eq!(font_charset("ja", "english"), "ja");
        assert_eq!(font_charset("koKR", "english"), "ko");
        assert_eq!(font_charset("", "jpnJP"), "ja");
        assert_eq!(font_charset("invalid", "koreana"), "ko");
        assert_eq!(font_charset("", "thaTH"), "th");
    }

    fn assert_text_glyphs_and_mesh(definitions: egui::FontDefinitions, text: &str) {
        let context = egui::Context::default();
        context.set_fonts(definitions);
        let output = context.run(
            egui::RawInput {
                screen_rect: Some(Rect::from_min_size(Pos2::ZERO, Vec2::new(1920.0, 1080.0))),
                ..Default::default()
            },
            |ctx| {
                let font = egui::FontId::proportional(20.0);
                assert!(ctx.fonts_mut(|fonts| fonts.has_glyphs(&font, text)));
                ctx.layer_painter(egui::LayerId::background()).text(
                    Pos2::new(8.0, 8.0),
                    egui::Align2::LEFT_TOP,
                    text,
                    font,
                    Color32::WHITE,
                );
            },
        );
        assert!(!output.textures_delta.set.is_empty());
        assert!(
            context
                .tessellate(output.shapes, output.pixels_per_point)
                .iter()
                .any(|p| matches!(&p.primitive, Primitive::Mesh(m) if !m.indices.is_empty()))
        );
    }

    // Keep a real outline font, but replace its cmap with controlled aliases.
    // The aliases use Ubuntu's A outline; they test glyph fallback routing and
    // actual egui rasterization, not the visual correctness of CJK typefaces.
    fn test_font_with_glyphs(extra: &str) -> Vec<u8> {
        let source = eroverlay_ui::fonts::FALLBACK_FONT;
        let face = ttf_parser::Face::parse(source, 0).unwrap();
        let mut mappings = std::collections::BTreeMap::new();
        for code in 32..=126 {
            let glyph = face.glyph_index(char::from_u32(code).unwrap()).unwrap();
            mappings.insert(code, u32::from(glyph.0));
        }
        let alias = u32::from(face.glyph_index('A').unwrap().0);
        for c in extra.chars().filter(|c| !c.is_ascii()) {
            mappings.insert(c as u32, alias);
        }
        let mut cmap = Vec::new();
        cmap.extend_from_slice(&0u16.to_be_bytes());
        cmap.extend_from_slice(&1u16.to_be_bytes());
        cmap.extend_from_slice(&3u16.to_be_bytes());
        cmap.extend_from_slice(&10u16.to_be_bytes());
        cmap.extend_from_slice(&12u32.to_be_bytes());
        cmap.extend_from_slice(&12u16.to_be_bytes());
        cmap.extend_from_slice(&0u16.to_be_bytes());
        cmap.extend_from_slice(&(16u32 + mappings.len() as u32 * 12).to_be_bytes());
        cmap.extend_from_slice(&0u32.to_be_bytes());
        cmap.extend_from_slice(&(mappings.len() as u32).to_be_bytes());
        for (code, glyph) in mappings {
            cmap.extend_from_slice(&code.to_be_bytes());
            cmap.extend_from_slice(&code.to_be_bytes());
            cmap.extend_from_slice(&glyph.to_be_bytes());
        }
        let count = u16::from_be_bytes(source[4..6].try_into().unwrap()) as usize;
        let record = (0..count)
            .map(|i| 12 + i * 16)
            .find(|at| &source[*at..*at + 4] == b"cmap")
            .unwrap();
        let mut bytes = source.to_vec();
        let offset = bytes.len().next_multiple_of(4);
        bytes.resize(offset, 0);
        bytes.extend_from_slice(&cmap);
        bytes[record + 8..record + 12].copy_from_slice(&(offset as u32).to_be_bytes());
        bytes[record + 12..record + 16].copy_from_slice(&(cmap.len() as u32).to_be_bytes());
        bytes
    }

    #[test]
    fn font_definitions_keep_readable_embedded_fallback_when_all_reads_fail() {
        use std::path::Path;
        let paths = font_candidates(Path::new("data"), "broken", Path::new("Fonts"), "en");
        let mut reads = Vec::new();
        let (definitions, loaded) =
            load_font_definitions(&paths, Path::new("Fonts/msyh.ttc"), |path| {
                reads.push(path.to_owned());
                Some(b"invalid-font".to_vec())
            });
        assert!(loaded.is_empty());
        assert!(!supports_status_glyphs(&definitions));
        assert_eq!(
            reads, paths,
            "MSYH must not be read twice after a failed attempt"
        );
        assert_eq!(
            definitions.font_data.keys().collect::<Vec<_>>(),
            ["Ubuntu-Light"]
        );
        for family in definitions.families.values() {
            assert_eq!(family, &["Ubuntu-Light"]);
        }
        assert_text_glyphs_and_mesh(
            definitions,
            "RESOURCE READ FAILED 0123456789 Zażółć gęślą jaźń Русская карта",
        );
    }

    #[test]
    fn font_definitions_keep_language_primary_and_add_chinese_status_fallback() {
        use std::path::Path;
        for (charset, filename, sample) in [
            ("ja", "meiryo.ttc", "日本語あいうアイウ"),
            ("ko", "malgun.ttf", "한국지도"),
            ("th", "LeelawUI.ttf", "ภาษาไทย"),
        ] {
            let language = test_font_with_glyphs(sample);
            let chinese = test_font_with_glyphs(STATUS_GLYPHS);
            let paths = font_candidates(Path::new("data"), "", Path::new("Fonts"), charset);
            assert_eq!(paths, [Path::new("Fonts").join(filename)]);
            let mut reads = Vec::new();
            let (definitions, loaded) =
                load_font_definitions(&paths, Path::new("Fonts/msyh.ttc"), |path| {
                    reads.push(path.to_owned());
                    Some(if path == Path::new("Fonts/msyh.ttc") {
                        chinese.clone()
                    } else {
                        language.clone()
                    })
                });
            assert_eq!(reads.len(), 2);
            assert_eq!(loaded, reads);
            assert_eq!(definitions.font_data.len(), 3);
            assert!(supports_status_glyphs(&definitions));
            assert_eq!(readable_status(STATUS_GLYPHS, true), STATUS_GLYPHS);
            assert_eq!(
                definitions.families[&egui::FontFamily::Proportional],
                ["overlay-font", "overlay-cjk", "Ubuntu-Light"]
            );
            assert_text_glyphs_and_mesh(
                definitions,
                &format!("{sample} {STATUS_GLYPHS} 0123456789"),
            );
        }
    }

    #[test]
    fn font_definitions_use_complete_custom_font_without_redundant_system_reads() {
        use std::path::Path;
        let paths = font_candidates(Path::new("data"), "custom", Path::new("Fonts"), "ja");
        let custom = test_font_with_glyphs(STATUS_GLYPHS);
        let mut reads = Vec::new();
        let (definitions, loaded) =
            load_font_definitions(&paths, Path::new("Fonts/msyh.ttc"), |path| {
                reads.push(path.to_owned());
                match path.to_str().unwrap().replace('\\', "/").as_str() {
                    "data/custom" => None,
                    "data/custom.ttf" => Some(custom.clone()),
                    other => panic!("unnecessary fallback read: {other}"),
                }
            });
        assert_eq!(reads.len(), 2);
        assert_eq!(loaded, [Path::new("data/custom.ttf")]);
        assert_eq!(definitions.font_data.len(), 2);
        assert!(supports_status_glyphs(&definitions));
        assert_eq!(
            definitions.families[&egui::FontFamily::Proportional],
            ["overlay-font", "Ubuntu-Light"]
        );
        assert_text_glyphs_and_mesh(definitions, STATUS_GLYPHS);
    }

    #[test]
    #[ignore = "reads installed Windows language fonts and checks real cmap/egui glyphs"]
    fn installed_language_fonts_cover_status_and_produce_text_meshes() {
        use std::path::Path;
        let windows = std::path::PathBuf::from(
            std::env::var_os("WINDIR").unwrap_or_else(|| "C:/Windows".into()),
        )
        .join("Fonts");
        for (charset, sample) in [
            ("en", "RESOURCE READ FAILED"),
            ("pl", "Zażółć gęślą jaźń"),
            ("ru", "Русская карта"),
            ("ja", "日本語あいうアイウ"),
            ("ko", "한국지도"),
            ("th", "ภาษาไทย"),
            ("zh", "中文地图"),
        ] {
            let paths = font_candidates(Path::new("data"), "", &windows, charset);
            let (definitions, loaded) =
                load_font_definitions(&paths, &windows.join("msyh.ttc"), |path| {
                    std::fs::read(path).ok()
                });
            println!("charset={charset} loaded={loaded:?}");
            assert!(supports_status_glyphs(&definitions));
            assert_text_glyphs_and_mesh(
                definitions,
                &format!("{sample} {STATUS_GLYPHS} 0123456789"),
            );
        }
    }
    #[test]
    fn anchored_circle_and_percent_reference() {
        let preset = Preset {
            width: 0.5,
            height: 0.3,
            shape: Shape::Circle,
            x: crate::settings::Margin {
                value: 0.05,
                percent: true,
            },
            ..Default::default()
        };
        let rect = map_rect(&preset, [1920.0, 1080.0]).unwrap();
        assert_eq!(rect.size(), Vec2::splat(324.0));
        assert_eq!(rect.min.x, 1542.0);
        let mut preset = preset;
        preset.center = true;
        assert_eq!(
            map_rect(&preset, [1920.0, 1080.0]).unwrap().center(),
            Pos2::new(960.0, 540.0)
        );
    }
    #[test]
    fn mesh_budget_accepts_exact_limits_and_latches_overflow() {
        let mut pool = MeshPool::default();
        assert!(pool.reserve(abi::MAX_VERTICES, abi::MAX_INDICES, true));
        assert!(!pool.reserve(1, 0, false));
        assert!(pool.full);
        assert!(!pool.reserve(0, 0, false));
        assert_eq!(pool.meshes.len(), 0);
        pool.begin();
        for _ in 0..abi::MAX_MESHES {
            assert!(pool.reserve(0, 0, true));
        }
        assert!(!pool.reserve(0, 0, true));
        assert!(pool.full);
        pool.begin();
        assert!(pool.reserve(0, 0, true));
        assert!(!pool.full);
    }
    #[test]
    fn pooled_direct_meshes_match_generic_egui_triangles() {
        let context = egui::Context::default();
        let mut pool = MeshPool::default();
        let screen = Rect::from_min_size(Pos2::ZERO, Vec2::splat(100.0));
        let output = context.run(
            egui::RawInput {
                screen_rect: Some(screen),
                ..Default::default()
            },
            |ctx| {
                let painter = ctx
                    .layer_painter(egui::LayerId::background())
                    .with_clip_rect(screen);
                let drawing = Drawing::new(&painter, &mut pool);
                for (i, texture) in [1, 1, 2].into_iter().enumerate() {
                    let x = 10.0 + i as f32 * 20.0;
                    quad(
                        &drawing,
                        texture,
                        [
                            Pos2::new(x, 10.0),
                            Pos2::new(x + 10.0, 10.0),
                            Pos2::new(x + 10.0, 20.0),
                            Pos2::new(x, 20.0),
                        ],
                        [
                            Pos2::ZERO,
                            Pos2::new(1.0, 0.0),
                            Pos2::new(1.0, 1.0),
                            Pos2::new(0.0, 1.0),
                        ],
                        Color32::WHITE,
                    );
                }
            },
        );
        assert_eq!(output.shapes.len(), 2);
        let direct: Vec<_> = output
            .shapes
            .iter()
            .flat_map(|shape| {
                let epaint::Shape::Mesh(mesh) = &shape.shape else {
                    panic!("mesh");
                };
                mesh.indices
                    .iter()
                    .map(|index| (mesh.texture_id, mesh.vertices[*index as usize]))
                    .collect::<Vec<_>>()
            })
            .collect();
        let generic: Vec<_> = context
            .tessellate(output.shapes, 1.0)
            .into_iter()
            .flat_map(|primitive| {
                let Primitive::Mesh(mesh) = primitive.primitive else {
                    panic!("mesh");
                };
                mesh.indices
                    .iter()
                    .map(|index| (mesh.texture_id, mesh.vertices[*index as usize]))
                    .collect::<Vec<_>>()
            })
            .collect();
        assert_eq!(direct, generic);
    }
}
