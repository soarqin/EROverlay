//! Bounded reader for the game's uncompressed Scaleform GFX timeline.
//! Definitions are parsed once on update; rendering only visits resolved recipes.
use std::collections::{BTreeMap, HashMap};

pub type Matrix = [f32; 6];
pub const IDENTITY: Matrix = [1.0, 0.0, 0.0, 1.0, 0.0, 0.0];
pub type Point = [f32; 2];

pub fn transform(m: Matrix, p: Point) -> Point {
    [
        m[0] * p[0] + m[2] * p[1] + m[4],
        m[1] * p[0] + m[3] * p[1] + m[5],
    ]
}
pub fn multiply(a: Matrix, b: Matrix) -> Matrix {
    [
        a[0] * b[0] + a[2] * b[1],
        a[1] * b[0] + a[3] * b[1],
        a[0] * b[2] + a[2] * b[3],
        a[1] * b[2] + a[3] * b[3],
        a[0] * b[4] + a[2] * b[5] + a[4],
        a[1] * b[4] + a[3] * b[5] + a[5],
    ]
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct ColorTransform {
    pub multiply: [f32; 4],
    pub add: [f32; 4],
}
impl Default for ColorTransform {
    fn default() -> Self {
        Self {
            multiply: [1.0; 4],
            add: [0.0; 4],
        }
    }
}
impl ColorTransform {
    pub fn apply(&self, rgba: [u8; 4]) -> [u8; 4] {
        std::array::from_fn(|i| {
            (f32::from(rgba[i]) * self.multiply[i] + self.add[i])
                .round()
                .clamp(0.0, 255.0) as u8
        })
    }
    fn child(self, child: Self) -> Self {
        Self {
            multiply: std::array::from_fn(|i| self.multiply[i] * child.multiply[i]),
            add: std::array::from_fn(|i| self.add[i] + self.multiply[i] * child.add[i]),
        }
    }
}

#[derive(Clone, Default, Debug)]
pub struct VectorStyle {
    pub color: [u8; 4],
    pub width: f32,
}
#[derive(Clone, Default, Debug)]
pub struct VectorCommand {
    pub op: u8,
    pub to: Point,
    pub control: Point,
    pub fill0: u16,
    pub fill1: u16,
    pub stroke: u16,
}
#[derive(Clone, Default, Debug)]
pub struct VectorShape {
    pub bounds: [f32; 4],
    pub fills: Vec<VectorStyle>,
    pub strokes: Vec<VectorStyle>,
    pub commands: Vec<VectorCommand>,
    pub compiled: Vec<crate::geometry::PaintMesh>,
}
#[derive(Clone, Debug)]
pub struct IconLayer {
    pub image: String,
    pub width: u32,
    pub height: u32,
    pub matrix: Matrix,
    pub color: ColorTransform,
    pub shape: VectorShape,
    pub region: Option<usize>,
}
impl Default for IconLayer {
    fn default() -> Self {
        Self {
            image: String::new(),
            width: 0,
            height: 0,
            matrix: IDENTITY,
            color: ColorTransform::default(),
            shape: VectorShape::default(),
            region: None,
        }
    }
}
#[derive(Clone, Default, Debug)]
pub struct Recipe {
    pub layers: Vec<IconLayer>,
}
#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum MapLayer {
    Area,
    Item,
    Warp,
    Memo,
    Marker,
    Dead,
    Player,
}
#[derive(Clone, Debug)]
pub struct TextLayout {
    pub bounds: [f32; 4],
    pub matrix: Matrix,
    pub height: f32,
    pub color: [u8; 4],
    pub align: u8,
    pub left: f32,
    pub right: f32,
    pub indent: f32,
}

pub struct Reader<'a> {
    pub bytes: &'a [u8],
    pub at: usize,
}
impl<'a> Reader<'a> {
    pub fn new(bytes: &'a [u8]) -> Self {
        Self { bytes, at: 0 }
    }
    pub fn take(&mut self, size: usize) -> Option<&'a [u8]> {
        let end = self.at.checked_add(size)?;
        let bytes = self.bytes.get(self.at..end)?;
        self.at = end;
        Some(bytes)
    }
    pub fn u8(&mut self) -> Option<u8> {
        Some(self.take(1)?[0])
    }
    pub fn u16(&mut self) -> Option<u16> {
        Some(u16::from_le_bytes(self.take(2)?.try_into().ok()?))
    }
    pub fn u32(&mut self) -> Option<u32> {
        Some(u32::from_le_bytes(self.take(4)?.try_into().ok()?))
    }
    pub fn text(&mut self) -> Option<String> {
        let len = self.bytes.get(self.at..)?.iter().position(|b| *b == 0)?;
        let text = String::from_utf8(self.take(len)?.to_vec()).ok()?;
        self.at += 1;
        Some(text)
    }
    fn text8(&mut self) -> Option<String> {
        let len = self.u8()? as usize;
        String::from_utf8(self.take(len)?.to_vec()).ok()
    }
    fn done(&self) -> bool {
        self.at == self.bytes.len()
    }
}

struct Bits<'a> {
    bytes: &'a [u8],
    bit: usize,
}
impl<'a> Bits<'a> {
    fn new(reader: &Reader<'a>) -> Self {
        Self {
            bytes: reader.bytes,
            bit: reader.at * 8,
        }
    }
    fn get(&mut self, count: u32) -> Option<u32> {
        if count > 32 || self.bit.checked_add(count as usize)? > self.bytes.len().checked_mul(8)? {
            return None;
        }
        let mut value = 0;
        for _ in 0..count {
            value = (value << 1) | u32::from((self.bytes[self.bit / 8] >> (7 - self.bit % 8)) & 1);
            self.bit += 1;
        }
        Some(value)
    }
    fn signed(&mut self, count: u32, divisor: f32) -> Option<f32> {
        let value = self.get(count)? as i64;
        let value = if count != 0 && value & (1i64 << (count - 1)) != 0 {
            value - (1i64 << count)
        } else {
            value
        };
        Some(value as f32 / divisor)
    }
    fn position(&self) -> usize {
        self.bit.div_ceil(8)
    }
}

fn rectangle(reader: &mut Reader<'_>) -> Option<[f32; 4]> {
    let mut bits = Bits::new(reader);
    let n = bits.get(5)?;
    let left = bits.signed(n, 20.0)?;
    let right = bits.signed(n, 20.0)?;
    let top = bits.signed(n, 20.0)?;
    let bottom = bits.signed(n, 20.0)?;
    reader.at = bits.position();
    Some([left, top, right, bottom])
}
fn matrix(reader: &mut Reader<'_>) -> Option<Matrix> {
    let mut bits = Bits::new(reader);
    let mut m = IDENTITY;
    if bits.get(1)? != 0 {
        let n = bits.get(5)?;
        m[0] = bits.signed(n, 65536.0)?;
        m[3] = bits.signed(n, 65536.0)?;
    }
    if bits.get(1)? != 0 {
        let n = bits.get(5)?;
        m[1] = bits.signed(n, 65536.0)?;
        m[2] = bits.signed(n, 65536.0)?;
    }
    let n = bits.get(5)?;
    m[4] = bits.signed(n, 20.0)?;
    m[5] = bits.signed(n, 20.0)?;
    reader.at = bits.position();
    Some(m)
}
pub fn parse_matrix(bytes: &[u8]) -> Option<Matrix> {
    let mut r = Reader::new(bytes);
    let matrix = matrix(&mut r)?;
    r.done().then_some(matrix)
}
fn color_transform(reader: &mut Reader<'_>) -> Option<ColorTransform> {
    let mut c = ColorTransform::default();
    let mut bits = Bits::new(reader);
    let add = bits.get(1)?;
    let mult = bits.get(1)?;
    let n = bits.get(4)?;
    if mult != 0 {
        for v in &mut c.multiply {
            *v = bits.signed(n, 256.0)?;
        }
    }
    if add != 0 {
        for v in &mut c.add {
            *v = bits.signed(n, 1.0)?;
        }
    }
    reader.at = bits.position();
    Some(c)
}
fn filters(r: &mut Reader<'_>) -> Option<()> {
    for _ in 0..r.u8()? {
        let size = match r.u8()? {
            0 => 23,
            1 => 9,
            2 => 15,
            3 => 27,
            6 => 80,
            4 | 7 => r.u8()? as usize * 5 + 19,
            5 => {
                let x = r.u8()? as usize;
                let y = r.u8()? as usize;
                13 + x * y * 4
            }
            _ => return None,
        };
        r.take(size)?;
    }
    Some(())
}
fn style_count(r: &mut Reader<'_>) -> Option<usize> {
    let first = r.u8()?;
    let count = if first == 255 {
        r.u16()? as usize
    } else {
        first as usize
    };
    (count <= 1024).then_some(count)
}
fn shape(bytes: &[u8], code: u16) -> Option<VectorShape> {
    let mut r = Reader::new(bytes);
    r.take(2)?;
    let mut shape = VectorShape {
        bounds: rectangle(&mut r)?,
        ..Default::default()
    };
    if code == 83 {
        rectangle(&mut r)?;
        r.u8()?;
    }
    for _ in 0..style_count(&mut r)? {
        if r.u8()? != 0 {
            return None;
        }
        shape.fills.push(VectorStyle {
            color: r.take(4)?.try_into().ok()?,
            width: 0.0,
        });
    }
    for _ in 0..style_count(&mut r)? {
        let width = r.u16()? as f32 / 20.0;
        if code == 83 {
            let mut bits = Bits::new(&r);
            let flags = bits.get(16)?;
            if flags & 0x800 != 0 {
                return None;
            }
            r.at = bits.position();
            if (flags >> 12) & 3 == 2 {
                r.u16()?;
            }
        }
        shape.strokes.push(VectorStyle {
            color: r.take(4)?.try_into().ok()?,
            width,
        });
    }
    let mut bits = Bits::new(&r);
    let mut fill_bits = bits.get(4)?;
    let mut line_bits = bits.get(4)?;
    let (mut fill0, mut fill1, mut stroke) = (0, 0, 0);
    let mut pos = [0.0, 0.0];
    for _ in 0..100_000 {
        if bits.get(1)? != 0 {
            let straight = bits.get(1)?;
            let n = bits.get(4)? + 2;
            let mut command = VectorCommand {
                op: if straight != 0 { 1 } else { 2 },
                fill0,
                fill1,
                stroke,
                ..Default::default()
            };
            if straight != 0 {
                let both = bits.get(1)?;
                let vertical = if both == 0 { bits.get(1)? } else { 0 };
                if both != 0 || vertical == 0 {
                    pos[0] += bits.signed(n, 20.0)?;
                }
                if both != 0 || vertical != 0 {
                    pos[1] += bits.signed(n, 20.0)?;
                }
            } else {
                command.control = [
                    pos[0] + bits.signed(n, 20.0)?,
                    pos[1] + bits.signed(n, 20.0)?,
                ];
                pos = [
                    command.control[0] + bits.signed(n, 20.0)?,
                    command.control[1] + bits.signed(n, 20.0)?,
                ];
            }
            command.to = pos;
            shape.commands.push(command);
        } else {
            let flags = bits.get(5)?;
            if flags == 0 {
                return Some(shape);
            }
            if flags & 1 != 0 {
                let n = bits.get(5)?;
                pos = [bits.signed(n, 20.0)?, bits.signed(n, 20.0)?];
                shape.commands.push(VectorCommand {
                    to: pos,
                    ..Default::default()
                });
            }
            if flags & 2 != 0 {
                fill0 = bits.get(fill_bits)? as u16;
            }
            if flags & 4 != 0 {
                fill1 = bits.get(fill_bits)? as u16;
            }
            if flags & 8 != 0 {
                stroke = bits.get(line_bits)? as u16;
            }
            if flags & 16 != 0 {
                return None;
            } // New style arrays are not used by map recipes.
            if usize::from(fill0) > shape.fills.len()
                || usize::from(fill1) > shape.fills.len()
                || usize::from(stroke) > shape.strokes.len()
            {
                return None;
            }
            // Keep the bit widths mutable alongside the SWF state for clear record correspondence.
            let _ = (&mut fill_bits, &mut line_bits);
        }
    }
    None
}

#[derive(Clone)]
struct Placement {
    character: u32,
    matrix: Matrix,
    color: ColorTransform,
    name: String,
    visible: bool,
    effects: bool,
}
impl Default for Placement {
    fn default() -> Self {
        Self {
            character: 0,
            matrix: IDENTITY,
            color: ColorTransform::default(),
            name: String::new(),
            visible: true,
            effects: false,
        }
    }
}
#[derive(Default)]
struct Character {
    image: String,
    width: u32,
    height: u32,
    vector: bool,
    frames: Vec<BTreeMap<u16, Placement>>,
}
#[derive(Default)]
pub struct Movie {
    characters: HashMap<u32, Character>,
    shapes: HashMap<u32, (u16, Vec<u8>)>,
    texts: HashMap<u32, Vec<u8>>,
    icons: u32,
    item: u32,
    placements: usize,
}
impl Movie {
    pub fn parse(bytes: &[u8]) -> Option<Self> {
        if bytes.len() > 64 * 1024 * 1024 {
            return None;
        }
        if bytes.get(..4)? != b"GFX\x0b" {
            return None;
        }
        let size = u32::from_le_bytes(bytes.get(4..8)?.try_into().ok()?) as usize;
        if size < 8 || size > bytes.len() || bytes[size..].iter().any(|b| *b != 0) {
            return None;
        }
        let mut r = Reader::new(&bytes[..size]);
        r.take(8)?;
        rectangle(&mut r)?;
        r.u16()?;
        let count = r.u16()?;
        let mut movie = Self::default();
        let root = movie.timeline(r.take(size - r.at)?, count, 0)?;
        movie.characters.insert(0, root);
        let item = movie.characters.get(&movie.item)?.frames.first()?;
        for p in item.values().filter(|p| p.name == "Icon_0") {
            if movie.icons != 0 {
                return None;
            }
            movie.icons = p.character;
        }
        (movie.icons != 0 && movie.characters.contains_key(&movie.icons)).then_some(movie)
    }
    fn timeline(&mut self, bytes: &[u8], count: u16, nesting: u32) -> Option<Character> {
        if nesting > 32 || count > 10_000 {
            return None;
        }
        let mut character = Character::default();
        let mut display: BTreeMap<u16, Placement> = BTreeMap::new();
        let mut r = Reader::new(bytes);
        while !r.done() {
            if self.characters.len() > 65536 {
                return None;
            }
            let header = r.u16()?;
            let code = header >> 6;
            let mut size = usize::from(header & 63);
            if size == 63 {
                size = r.u32()? as usize;
            }
            let body = r.take(size)?;
            let mut c = Reader::new(body);
            match code {
                0 => {
                    return (size == 0 && r.done() && character.frames.len() == usize::from(count))
                        .then_some(character);
                }
                1009 => {
                    let id = c.u32()?;
                    c.u16()?;
                    let w = c.u16()? as u32;
                    let h = c.u16()? as u32;
                    let image = asset_key(&c.text8()?);
                    c.text8()?;
                    if w == 0 || h == 0 || !c.done() || self.characters.contains_key(&id) {
                        return None;
                    }
                    self.characters.insert(
                        id,
                        Character {
                            image,
                            width: w,
                            height: h,
                            ..Default::default()
                        },
                    );
                }
                39 => {
                    let id = c.u16()? as u32;
                    let frames = c.u16()?;
                    if self.characters.contains_key(&id) {
                        return None;
                    }
                    let child = self.timeline(c.take(body.len() - c.at)?, frames, nesting + 1)?;
                    self.characters.insert(id, child);
                }
                32 | 83 | 37 => {
                    let id = c.u16()? as u32;
                    if self.characters.contains_key(&id) {
                        return None;
                    }
                    if code == 37 {
                        self.texts.insert(id, body.to_vec());
                    } else {
                        self.shapes.insert(id, (code, body.to_vec()));
                    }
                    self.characters.insert(
                        id,
                        Character {
                            vector: code != 37,
                            ..Default::default()
                        },
                    );
                }
                26 | 70 => {
                    let flags = c.u8()?;
                    let extra = if code == 70 { c.u8()? } else { 0 };
                    if extra & 0xc0 != 0 {
                        return None;
                    }
                    let depth = c.u16()?;
                    let moving = flags & 1 != 0;
                    let mut p = if moving {
                        display.get(&depth)?.clone()
                    } else {
                        if flags & 2 == 0 {
                            return None;
                        }
                        Placement::default()
                    };
                    if extra & 8 != 0 {
                        c.text()?;
                    }
                    if flags & 2 != 0 {
                        p.character = c.u16()? as u32;
                    }
                    if flags & 4 != 0 {
                        p.matrix = matrix(&mut c)?;
                    }
                    if flags & 8 != 0 {
                        p.color = color_transform(&mut c)?;
                    }
                    if flags & 16 != 0 {
                        c.u16()?;
                    }
                    if flags & 32 != 0 {
                        p.name = c.text()?;
                    }
                    if flags & 64 != 0 {
                        c.u16()?;
                        p.effects = true;
                    }
                    if extra & 1 != 0 {
                        filters(&mut c)?;
                        p.effects = true;
                    }
                    if extra & 2 != 0 {
                        p.effects |= c.u8()? > 2;
                    }
                    if extra & 4 != 0 {
                        c.u8()?;
                    }
                    if extra & 32 != 0 {
                        p.visible = c.u8()? != 0;
                    }
                    if flags & 128 != 0 {
                        p.effects = true;
                        c.at = body.len();
                    }
                    if !c.done() {
                        return None;
                    }
                    display.insert(depth, p);
                }
                28 => {
                    let depth = c.u16()?;
                    if !c.done() {
                        return None;
                    }
                    display.remove(&depth);
                }
                1 => {
                    if size != 0 || character.frames.len() >= usize::from(count) {
                        return None;
                    }
                    self.placements = self.placements.checked_add(display.len())?;
                    if self.placements > 1_000_000 {
                        return None;
                    }
                    character.frames.push(display.clone());
                }
                76 => {
                    for _ in 0..c.u16()? {
                        let id = c.u16()? as u32;
                        let name = c.text()?;
                        if name == "WorldMapItem" {
                            if self.item != 0 {
                                return None;
                            }
                            self.item = id;
                        }
                    }
                    if !c.done() {
                        return None;
                    }
                }
                _ => {}
            }
        }
        None
    }
    fn named(&self, path: &str) -> Option<(u32, Matrix)> {
        let mut id = 0;
        let mut matrix = IDENTITY;
        for name in path.split('/') {
            let mut matches = self
                .characters
                .get(&id)?
                .frames
                .first()?
                .values()
                .filter(|p| p.name == name);
            let p = matches.next()?;
            if matches.next().is_some() {
                return None;
            }
            id = p.character;
            matrix = p.matrix;
        }
        Some((id, matrix))
    }
    fn flatten(
        &self,
        id: u32,
        frame: u32,
        matrix: Matrix,
        color: ColorTransform,
        parents: &mut Vec<u32>,
        out: &mut Recipe,
    ) -> Option<()> {
        if parents.len() >= 32 || parents.contains(&id) || out.layers.len() > 4096 {
            return None;
        }
        if matrix
            .iter()
            .chain(color.multiply.iter())
            .chain(color.add.iter())
            .any(|v| !v.is_finite())
        {
            return None;
        }
        let c = self.characters.get(&id)?;
        if !c.image.is_empty() {
            out.layers.push(IconLayer {
                image: c.image.clone(),
                width: c.width,
                height: c.height,
                matrix,
                color,
                ..Default::default()
            });
            return Some(());
        }
        if c.vector {
            let (code, bytes) = self.shapes.get(&id)?;
            let mut shape = shape(bytes, *code)?;
            for s in shape.fills.iter_mut().chain(&mut shape.strokes) {
                s.color = color.apply(s.color);
            }
            shape.compiled = crate::geometry::compile(&shape);
            out.layers.push(IconLayer {
                matrix,
                shape,
                ..Default::default()
            });
            return Some(());
        }
        let frame = c.frames.get(frame.checked_sub(1)? as usize)?;
        parents.push(id);
        for p in frame.values() {
            if !p.visible {
                continue;
            }
            if p.effects {
                return None;
            }
            self.flatten(
                p.character,
                1,
                multiply(matrix, p.matrix),
                color.child(p.color),
                parents,
                out,
            )?;
        }
        parents.pop();
        Some(())
    }
    fn recipe(&self, id: u32, frame: u32, matrix: Matrix, color: ColorTransform) -> Option<Recipe> {
        let mut out = Recipe::default();
        self.flatten(id, frame, matrix, color, &mut Vec::new(), &mut out)?;
        (!out.layers.is_empty()).then_some(out)
    }
    pub fn icon(&self, frame: u32) -> Option<Recipe> {
        self.recipe(self.icons, frame, IDENTITY, ColorTransform::default())
    }
    pub fn frame_count(&self) -> usize {
        self.characters
            .get(&self.icons)
            .map_or(0, |c| c.frames.len())
    }
    pub fn frame_layers(&self, frame: usize) -> usize {
        self.characters
            .get(&self.icons)
            .and_then(|c| c.frames.get(frame.saturating_sub(1)))
            .map_or(0, BTreeMap::len)
    }
    pub fn special(&self, path: &str, frame: u32) -> Option<Recipe> {
        self.recipe(
            self.named(path)?.0,
            frame,
            IDENTITY,
            ColorTransform::default(),
        )
    }
    pub fn overlay(&self, name: &str) -> Option<Recipe> {
        let frame = self.characters.get(&self.item)?.frames.first()?;
        let p = frame.values().find(|p| p.name == name)?;
        if p.effects {
            return None;
        }
        self.recipe(p.character, 1, p.matrix, p.color)
    }
    pub fn image(&self, name: &str) -> Option<Recipe> {
        let (id, _) = self.characters.iter().find(|(_, c)| c.image == name)?;
        self.recipe(*id, 1, IDENTITY, ColorTransform::default())
    }
    pub fn marker_layers(&self) -> Option<Vec<MapLayer>> {
        let base = self
            .characters
            .get(&self.named("Body/_/Base")?.0)?
            .frames
            .first()?;
        Some(
            base.values()
                .filter_map(|p| match p.name.as_str() {
                    "AreaList" => Some(MapLayer::Area),
                    "ItemList" => Some(MapLayer::Item),
                    "WarpList" => Some(MapLayer::Warp),
                    "MemoList" => Some(MapLayer::Memo),
                    "MarkerList" => Some(MapLayer::Marker),
                    "Dead" => Some(MapLayer::Dead),
                    "Player" => Some(MapLayer::Player),
                    _ => None,
                })
                .collect(),
        )
    }
    pub fn text(&self, path: &str) -> Option<TextLayout> {
        let (id, matrix) = self.named(path)?;
        let mut r = Reader::new(self.texts.get(&id)?);
        r.u16()?;
        let bounds = rectangle(&mut r)?;
        let flags = (u16::from(r.u8()?) << 8) | u16::from(r.u8()?);
        if flags & 0x100 != 0 {
            r.u16()?;
        }
        if flags & 0x80 != 0 {
            r.text()?;
        }
        let height = if flags & 0x180 != 0 {
            f32::from(r.u16()?) / 20.0
        } else {
            0.0
        };
        let color = if flags & 0x400 != 0 {
            r.take(4)?.try_into().ok()?
        } else {
            [255; 4]
        };
        if flags & 0x200 != 0 {
            r.u16()?;
        }
        let (mut align, mut left, mut right, mut indent) = (0, 0.0, 0.0, 0.0);
        if flags & 0x20 != 0 {
            align = r.u8()?;
            left = f32::from(r.u16()?) / 20.0;
            right = f32::from(r.u16()?) / 20.0;
            indent = f32::from(r.u16()?) / 20.0;
            r.u16()?;
        }
        r.text()?;
        if flags & 0x8000 != 0 {
            r.text()?;
        }
        if !r.done() || height <= 0.0 || height > 1024.0 || align > 3 {
            return None;
        }
        Some(TextLayout {
            bounds,
            matrix,
            height,
            color,
            align,
            left,
            right,
            indent,
        })
    }
}

pub fn asset_key(name: &str) -> String {
    name.rsplit(['\\', '/'])
        .next()
        .unwrap_or(name)
        .split('.')
        .next()
        .unwrap_or("")
        .into()
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn matrix_order_and_color_clamping() {
        assert_eq!(
            transform(
                multiply(
                    [2.0, 0.0, 0.0, 2.0, 1.0, 3.0],
                    [1.0, 0.0, 0.0, 1.0, 5.0, 7.0]
                ),
                [0.0, 0.0]
            ),
            [11.0, 17.0]
        );
        let c = ColorTransform {
            multiply: [2.0; 4],
            add: [-20.0; 4],
        };
        assert_eq!(c.apply([0, 20, 128, 255]), [0, 20, 236, 255]);
    }
    #[test]
    fn malformed_gfx_rejected() {
        for len in 0..64 {
            assert!(Movie::parse(&vec![0u8; len]).is_none());
        }
        assert!(Movie::parse(b"GFX\x0b\xff\xff\xff\xff").is_none());
    }
}
