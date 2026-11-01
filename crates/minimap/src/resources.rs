use crate::{
    data::{MapData, Source},
    settings::{Preset, Settings, Shape},
};
use crate::{
    gfx::{self, ColorTransform, MapLayer, Movie, Recipe, TextLayout},
    host::{Host, Texture},
};
use eroverlay_game::assets::{decode_dds_rgba, parse_binder, parse_dds, parse_tpf};
use quick_xml::{Reader, events::Event};
use std::{
    collections::{HashMap, HashSet},
    ops::Range,
    sync::Arc,
    time::{Duration, Instant},
};

const PATHS: [&str; 4] = [
    "menu:/02_120_worldmap.gfx",
    "menu:/hi/01_common.sblytbnd.dcx",
    "menu:/71_maptile.tpfbhd",
    "menu:/71_maptile.mtmskbnd.dcx",
];
const ATLAS_PATH: &str = "menu:/hi/01_common.tpf.dcx";
const TILE_CACHE_BYTES: u64 = 64 * 1024 * 1024;
const TILE_CACHE_ENTRIES: usize = 512;

/// One preparation allowance shared by native and optional provider artwork.
pub struct UploadBudget {
    remaining: usize,
    count: u32,
}
impl UploadBudget {
    pub fn new() -> Self {
        Self {
            remaining: 4 * 1024 * 1024,
            count: 0,
        }
    }
    pub fn accepts(&self, bytes: usize) -> bool {
        self.count < 4 && (bytes <= self.remaining || self.count == 0)
    }
    pub fn record(&mut self, bytes: usize) {
        self.count += 1;
        self.remaining = self.remaining.saturating_sub(bytes);
    }
}

#[derive(Clone, Copy, Debug, Default, PartialEq, Eq, Hash)]
pub struct TileKey {
    pub map: u8,
    pub level: u8,
    pub x: u16,
    pub y: u16,
    pub variant: u32,
}
impl TileKey {
    pub fn parse(name: &str) -> Option<Self> {
        let text = name.strip_prefix("MENU_MapTile_M")?;
        if text.len() != 20
            || !text.is_ascii()
            || &text[2..4] != "_L"
            || &text[5..6] != "_"
            || &text[8..9] != "_"
            || &text[11..12] != "_"
        {
            return None;
        }
        let key = Self {
            map: text[..2].parse().ok()?,
            level: text[4..5].parse().ok()?,
            x: text[6..8].parse().ok()?,
            y: text[9..11].parse().ok()?,
            variant: u32::from_str_radix(&text[12..], 16).ok()?,
        };
        ([0, 1, 10].contains(&key.map) && key.level <= 2).then_some(key)
    }
}
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub struct Region {
    pub atlas: usize,
    pub x: u32,
    pub y: u32,
    pub width: u32,
    pub height: u32,
}
#[derive(Clone)]
pub struct Definitions {
    pub icons: HashMap<u32, Recipe>,
    pub memos: HashMap<u32, Recipe>,
    pub specials: HashMap<String, Recipe>,
    pub text: TextLayout,
    pub layers: Vec<MapLayer>,
    pub regions: Vec<Region>,
}
impl Definitions {
    pub fn recipe(&self, name: &str) -> Option<&Recipe> {
        self.specials.get(name)
    }
}

#[derive(Clone)]
pub struct TileView {
    pub texture: Arc<Texture>,
    pub bounds: [f32; 4],
}
#[derive(Clone, Default)]
pub struct Visuals {
    pub definitions: Option<Arc<Definitions>>,
    pub atlases: Vec<Option<Arc<Texture>>>,
    pub tiles: Vec<TileView>,
    pub status: &'static str,
}

struct FileSlot {
    token: u64,
    retry: Instant,
    bytes: Option<Vec<u8>>,
    error: Option<&'static str>,
}
impl Default for FileSlot {
    fn default() -> Self {
        Self {
            token: 0,
            retry: Instant::now(),
            bytes: None,
            error: None,
        }
    }
}
impl FileSlot {
    fn failed(&mut self, host: Host, path: &str, part: &str, reason: &'static str) {
        // Called only for an actual failed attempt/completion, never just because
        // an error remains visible while its retry timer is running.
        self.error = Some(reason);
        host.log(&format!(
            "minimap-resource phase={reason} path={path} part={part}\n"
        ));
    }
    fn update(&mut self, host: Host, path: &str, part: &str) {
        if self.bytes.is_some() {
            return;
        }
        if self.token == 0 && Instant::now() >= self.retry {
            self.token = host.request(path, part);
            if self.token == 0 {
                self.retry = Instant::now() + Duration::from_millis(250);
            }
        }
        if self.token != 0
            && let Some(result) = host.poll(self.token)
        {
            host.release_file(self.token);
            self.token = 0;
            match result {
                Ok(bytes) => {
                    self.bytes = Some(bytes);
                    self.error = None;
                }
                Err(()) => {
                    self.failed(host, path, part, "file-poll");
                    self.retry = Instant::now() + Duration::from_secs(3);
                }
            }
        }
    }
    fn stop(&mut self, host: Host) {
        host.release_file(self.token);
        self.token = 0;
    }
}
pub(crate) struct Atlas {
    pub(crate) source: String,
    pub(crate) source_index: Option<usize>,
    pub(crate) region: Region,
    color: ColorTransform,
    file: FileSlot,
    dds: Option<Arc<Vec<u8>>>,
    pending: u64,
    texture: Option<Arc<Texture>>,
    retry: Instant,
    touched: Instant,
}
impl Atlas {
    fn source(name: String) -> Self {
        Self {
            source: name,
            source_index: None,
            region: Region::default(),
            color: ColorTransform::default(),
            file: FileSlot::default(),
            dds: None,
            pending: 0,
            texture: None,
            retry: Instant::now(),
            touched: Instant::now(),
        }
    }
}
struct Tile {
    file: FileSlot,
    source: Option<Range<usize>>,
    pending: u64,
    texture: Option<Arc<Texture>>,
    touch: u64,
    bytes: u64,
}
impl Tile {
    fn active(&self) -> bool {
        self.file.token != 0 || self.file.bytes.is_some() || self.pending != 0
    }
}

fn tile_source(bytes: &[u8]) -> Option<Range<usize>> {
    let range = if bytes.starts_with(b"DDS ") {
        0..bytes.len()
    } else {
        let entries = parse_tpf(bytes).ok()?;
        if entries.len() != 1 {
            return None;
        }
        entries[0].data_range.clone()
    };
    (range.len() <= eroverlay_abi::MAX_TEXTURE_BYTES
        && parse_dds(bytes.get(range.clone())?).is_ok())
    .then_some(range)
}

pub struct Resources {
    host: Host,
    metadata: [FileSlot; 4],
    definitions: Option<Arc<Definitions>>,
    atlases: Vec<Atlas>,
    tile_paths: HashMap<TileKey, String>,
    masks: [HashMap<u32, (u32, bool)>; 3],
    tiles: HashMap<TileKey, Tile>,
    generation: u64,
    frame: u64,
    directory_ready: bool,
    parse_retry: Instant,
    reported_wait: &'static str,
}
pub struct ViewRequest<'a> {
    pub data: &'a MapData,
    pub bounds: [f32; 2],
    pub shown: bool,
    pub preset: &'a Preset,
    pub settings: &'a Settings,
    pub graces: bool,
    pub landmarks: bool,
}

fn xml(
    bytes: &[u8],
    mut element: impl FnMut(&str, &HashMap<String, String>) -> Option<()>,
) -> Option<()> {
    if bytes.len() > 64 * 1024 * 1024 {
        return None;
    }
    let mut reader = Reader::from_reader(bytes);
    reader.config_mut().check_end_names = true;
    let mut count = 0;
    loop {
        match reader.read_event().ok()? {
            Event::Start(e) | Event::Empty(e) => {
                count += 1;
                if count > 500_000 {
                    return None;
                }
                let tag = String::from_utf8(e.local_name().as_ref().to_vec()).ok()?;
                let mut attributes = HashMap::new();
                for a in e.attributes() {
                    let a = a.ok()?;
                    let key = String::from_utf8(a.key.local_name().as_ref().to_vec()).ok()?;
                    let value = a
                        .decode_and_unescape_value(reader.decoder())
                        .ok()?
                        .into_owned();
                    if value.len() > 4096 || attributes.insert(key, value).is_some() {
                        return None;
                    }
                }
                element(&tag, &attributes)?;
            }
            Event::DocType(_) => return None,
            Event::Eof => return Some(()),
            _ => {}
        }
    }
}
fn number(a: &HashMap<String, String>, key: &str) -> Option<u32> {
    a.get(key)?.parse().ok()
}

pub(crate) fn definitions(gfx: &[u8], layouts: &[u8]) -> Option<(Definitions, Vec<Atlas>)> {
    let movie = Movie::parse(gfx)?;
    let entries = parse_binder(layouts).ok()?;
    let mut regions: HashMap<String, Region> = HashMap::new();
    let mut atlases: Vec<Atlas> = Vec::new();
    for entry in entries {
        let mut atlas = None;
        xml(entry.bytes, |tag, a| {
            if tag == "TextureAtlas" {
                let name = gfx::asset_key(a.get("imagePath")?);
                let index = atlases
                    .iter()
                    .position(|s| s.source == name)
                    .unwrap_or_else(|| {
                        atlases.push(Atlas::source(name));
                        atlases.len() - 1
                    });
                atlas = Some(index);
            } else if tag == "SubTexture"
                && let Some(atlas) = atlas
            {
                let region = Region {
                    atlas,
                    x: number(a, "x")?,
                    y: number(a, "y")?,
                    width: number(a, "width")?,
                    height: number(a, "height")?,
                };
                if region.width == 0
                    || region.height == 0
                    || region.x > 16384
                    || region.y > 16384
                    || region.width > 16384
                    || region.height > 16384
                {
                    return None;
                }
                if number(a, "half")? == 0 {
                    regions
                        .entry(gfx::asset_key(a.get("name")?))
                        .or_insert(region);
                }
            }
            Some(())
        })?;
    }
    let mut icons = HashMap::new();
    for i in 1..=movie.frame_count().min(10_000) {
        if let Some(recipe) = movie.icon(i as u32) {
            icons.insert(i as u32, recipe);
        } else if movie.frame_layers(i) != 0 {
            return None;
        }
    }
    if icons.is_empty() {
        return None;
    }
    let mut memos = HashMap::new();
    for i in 1..=10 {
        if let Some(recipe) = movie.special("Body/_/Base/MemoList/Item_0/Icon_0", i) {
            memos.insert(i, recipe);
        }
    }
    let mut specials = HashMap::new();
    for (name, path) in [
        ("home", "Body/_/Base/Home"),
        ("arrow", "Body/_/Base/Player/Rotate"),
        ("death", "Body/_/Base/Dead"),
        ("marker", "Body/_/Base/MarkerList/Item_0/Icon_0"),
    ] {
        specials.insert(name.to_owned(), movie.special(path, 1)?);
    }
    if let Some(recipe) = movie.overlay("Cleared") {
        specials.insert("cleared".into(), recipe);
    }
    for (name, image) in [("player", "MENU_MAP_Host"), ("bearing", "MENU_MAP_Bearing")] {
        let mut recipe = movie.image(image)?;
        let layer = recipe.layers.first_mut()?;
        layer.matrix[4] = -(layer.width as f32) * 0.5;
        layer.matrix[5] = -(layer.height as f32) * 0.5;
        specials.insert(name.into(), recipe);
    }
    for layer in &mut specials.get_mut("arrow")?.layers {
        for v in &mut layer.matrix {
            *v *= 2.0;
        }
    }
    let unresolved = |recipe: &Recipe| {
        recipe.layers.iter().any(|l| {
            !l.image.is_empty()
                && !regions
                    .get(&l.image)
                    .is_some_and(|r| r.width == l.width && r.height == l.height)
        })
    };
    icons.retain(|_, r| !unresolved(r));
    memos.retain(|_, r| !unresolved(r));
    specials.retain(|key, r| key != "cleared" || !unresolved(r));
    if specials.values().any(unresolved) {
        return None;
    }
    let needed: HashSet<_> = icons
        .values()
        .chain(memos.values())
        .chain(specials.values())
        .flat_map(|r| {
            r.layers
                .iter()
                .filter(|l| !l.image.is_empty())
                .map(|l| l.image.clone())
        })
        .collect();
    // Retain only the atlas files referenced by resolved recipes, not every UI atlas.
    let mut remap = HashMap::new();
    let mut used = Vec::new();
    for (i, atlas) in atlases.into_iter().enumerate() {
        if regions
            .iter()
            .any(|(name, r)| needed.contains(name) && r.atlas == i)
        {
            remap.insert(i, used.len());
            used.push(atlas);
        }
    }
    for region in regions.values_mut() {
        if let Some(index) = remap.get(&region.atlas) {
            region.atlas = *index;
        }
    }
    let mut resolved = Vec::new();
    let mut atlases = used;
    for recipe in icons
        .values_mut()
        .chain(memos.values_mut())
        .chain(specials.values_mut())
    {
        for layer in &mut recipe.layers {
            if layer.image.is_empty() {
                continue;
            }
            let mut region = *regions.get(&layer.image)?;
            if layer.color != ColorTransform::default() {
                let index = atlases
                    .iter()
                    .position(|a| {
                        a.source_index == Some(region.atlas)
                            && a.region == region
                            && a.color == layer.color
                    })
                    .unwrap_or_else(|| {
                        let mut atlas = Atlas::source(atlases[region.atlas].source.clone());
                        atlas.source_index = Some(region.atlas);
                        atlas.region = region;
                        atlas.color = layer.color;
                        atlases.push(atlas);
                        atlases.len() - 1
                    });
                region = Region {
                    atlas: index,
                    x: 0,
                    y: 0,
                    width: region.width,
                    height: region.height,
                };
            }
            layer.region = Some(resolved.len());
            resolved.push(region);
        }
    }
    Some((
        Definitions {
            icons,
            memos,
            specials,
            text: movie.text("Body/_/Base/MarkerList/Item_0/Text_0")?,
            layers: movie.marker_layers()?,
            regions: resolved,
        },
        atlases,
    ))
}

impl Resources {
    pub fn new(host: Host) -> Self {
        Self {
            host,
            metadata: std::array::from_fn(|_| FileSlot::default()),
            definitions: None,
            atlases: Vec::new(),
            tile_paths: HashMap::new(),
            masks: std::array::from_fn(|_| HashMap::new()),
            tiles: HashMap::new(),
            generation: 0,
            frame: 0,
            directory_ready: false,
            parse_retry: Instant::now(),
            reported_wait: "",
        }
    }
    fn load_directory(&mut self, index: &[u8], masks: &[u8]) -> Option<()> {
        let mut paths = HashMap::new();
        for entry in parse_binder(index).ok()? {
            let name = gfx::asset_key(&entry.name);
            if let Some(key) = TileKey::parse(&name) {
                paths
                    .entry(key)
                    .or_insert_with(|| format!("menutpfbnd:/71_MapTile/{name}.tpf.dcx"));
            }
        }
        let mut map_masks: [HashMap<u32, (u32, bool)>; 3] = std::array::from_fn(|_| HashMap::new());
        for entry in parse_binder(masks).ok()? {
            let map = match gfx::asset_key(&entry.name).as_str() {
                "MENU_MapTile_M00" => 0,
                "MENU_MapTile_M01" => 1,
                "MENU_MapTile_M10" => 2,
                _ => continue,
            };
            xml(entry.bytes, |tag, a| {
                if tag == "MapTileMaskList" {
                    return Some(());
                }
                let id = number(a, "id")?;
                let mask = number(a, "mask")?;
                let exists = number(a, "exists")?;
                if exists > 1 || map_masks[map].insert(id, (mask, exists != 0)).is_some() {
                    return None;
                }
                Some(())
            })?;
        }
        let supported = self.host.layout().map_mask;
        for (i, masks) in map_masks.iter().enumerate() {
            if supported & (1 << i) != 0 && masks.is_empty() {
                return None;
            }
        }
        if paths.is_empty() {
            return None;
        }
        self.tile_paths = paths;
        self.masks = map_masks;
        self.directory_ready = true;
        Some(())
    }
    fn metadata(&mut self, may_start: bool) {
        for (i, slot) in self.metadata.iter_mut().enumerate() {
            if ((i < 2 && self.definitions.is_none()) || (i >= 2 && !self.directory_ready))
                && (may_start || slot.token != 0)
            {
                slot.update(self.host, PATHS[i], "");
            }
        }
        // A parse retry delay must not stop other native requests retiring.
        if Instant::now() < self.parse_retry {
            return;
        }
        if self.definitions.is_none()
            && let (Some(gfx), Some(layouts)) = (&self.metadata[0].bytes, &self.metadata[1].bytes)
        {
            match definitions(gfx, layouts) {
                Some((defs, atlases)) => {
                    self.host.log(&format!(
                        "minimap-definitions recipes={} atlases={}\n",
                        defs.icons.len(),
                        atlases.len()
                    ));
                    self.definitions = Some(Arc::new(defs));
                    self.atlases = atlases;
                    self.metadata[0].bytes = None;
                    self.metadata[1].bytes = None;
                    self.metadata[0].error = None;
                }
                None => {
                    self.host.log(&format!(
                        "minimap-resource phase=metadata-definitions path={} layout={} gfx_bytes={} layout_bytes={}\n",
                        PATHS[0], PATHS[1], gfx.len(), layouts.len()
                    ));
                    self.metadata[0].error = Some("metadata-definitions");
                    self.parse_retry = Instant::now() + Duration::from_secs(3);
                }
            }
        }
        if !self.directory_ready
            && self.metadata[2].bytes.is_some()
            && self.metadata[3].bytes.is_some()
        {
            let index = self.metadata[2].bytes.take().unwrap();
            let masks = self.metadata[3].bytes.take().unwrap();
            if self.load_directory(&index, &masks).is_none() {
                self.host.log(&format!(
                    "minimap-resource phase=metadata-directory path={} masks={} index_bytes={} mask_bytes={}\n",
                    PATHS[2], PATHS[3], index.len(), masks.len()
                ));
                self.metadata[2].error = Some("metadata-directory");
                self.parse_retry = Instant::now() + Duration::from_secs(3);
                self.metadata[2].bytes = Some(index);
                self.metadata[3].bytes = Some(masks);
            } else {
                self.metadata[2].error = None;
            }
        }
    }
    fn demand(&self, request: &ViewRequest<'_>) -> Vec<bool> {
        let mut demand = vec![false; self.atlases.len()];
        if !request.shown || !request.data.valid || request.preset.opacity == 0.0 {
            return demand;
        }
        let Some(defs) = self.definitions.as_deref() else {
            return demand;
        };
        let mut collect = |recipe: Option<&Recipe>| {
            if let Some(recipe) = recipe {
                for layer in &recipe.layers {
                    if let Some(region) = layer.region.and_then(|i| defs.regions.get(i)) {
                        demand[region.atlas] = true;
                    }
                }
            }
        };
        collect(defs.recipe("player"));
        collect(defs.recipe("arrow"));
        if request.preset.rotate {
            collect(defs.recipe("bearing"));
        }
        if request.data.roundtable {
            collect(defs.recipe("home"));
            collect(defs.icons.get(&request.data.home_icon));
        }
        let map = if request.data.state.map_id == 10 {
            2
        } else {
            u32::from(request.data.state.underground & 1 != 0)
        };
        for marker in request.data.decorations.iter() {
            let visible = match marker.source {
                Source::Grace => request.graces,
                Source::Landmark => request.landmarks,
                Source::Other => request.settings.others,
            };
            if !visible || marker.maps & (1 << map) == 0 || marker.runtime {
                continue;
            }
            collect(defs.icons.get(&marker.icon));
            if marker.cleared {
                collect(defs.recipe("cleared"));
            }
        }
        if request.settings.death && request.data.state.death_valid != 0 {
            collect(defs.recipe("death"));
        }
        if request.settings.beacons && !request.data.beacons.is_empty() {
            collect(defs.recipe("marker"));
        }
        if request.settings.others {
            for memo in &request.data.memos {
                collect(defs.memos.get(&u32::from(memo.number)));
            }
        }
        demand
    }
    fn atlases(&mut self, demand: &[bool], budget: &mut UploadBudget) -> bool {
        let mut failed = false;
        let now = Instant::now();
        let mut active = self.atlases.iter().filter(|a| a.file.token != 0).count();
        let mut cpu_bytes: usize = self
            .atlases
            .iter()
            .filter_map(|a| a.dds.as_ref())
            .map(|b| b.len())
            .sum();
        for i in 0..self.atlases.len() {
            let read = demand[i]
                || self
                    .atlases
                    .iter()
                    .enumerate()
                    .any(|(j, a)| demand[j] && a.source_index == Some(i));
            if !read {
                if now.duration_since(self.atlases[i].touched) >= Duration::from_secs(10) {
                    let atlas = &mut self.atlases[i];
                    atlas.texture = None;
                    self.host.release_texture(atlas.pending);
                    atlas.pending = 0;
                    atlas.file.stop(self.host);
                }
                continue;
            }
            self.atlases[i].touched = now;
            if self.atlases[i].texture.is_some() {
                continue;
            }
            let atlas = &mut self.atlases[i];
            if atlas.pending != 0 {
                match Texture::poll(self.host, atlas.pending) {
                    Some(Ok(texture)) => {
                        atlas.texture = Some(Arc::new(texture));
                        atlas.pending = 0;
                        atlas.file.error = None;
                    }
                    Some(Err(())) => {
                        atlas
                            .file
                            .failed(self.host, ATLAS_PATH, &atlas.source, "atlas-upload");
                        self.host.release_texture(atlas.pending);
                        atlas.pending = 0;
                        atlas.retry = now + Duration::from_secs(1);
                    }
                    None => {}
                }
                failed |= atlas.file.error.is_some();
                continue;
            }
            if atlas.dds.is_none()
                && atlas.source_index.is_none()
                && (active < 8 || atlas.file.token != 0)
            {
                let was = atlas.file.token != 0;
                atlas.file.update(self.host, ATLAS_PATH, &atlas.source);
                if !was && atlas.file.token != 0 {
                    active += 1;
                }
                if let Some(bytes) = atlas.file.bytes.take() {
                    let dds = if bytes.starts_with(b"DDS ") {
                        Some(bytes)
                    } else {
                        parse_tpf(&bytes).ok().and_then(|entries| {
                            entries
                                .into_iter()
                                .find(|e| gfx::asset_key(&e.name) == atlas.source)
                                .map(|e| e.dds.to_vec())
                        })
                    };
                    if let Some(dds) = dds.filter(|dds| {
                        dds.len() <= eroverlay_abi::MAX_TEXTURE_BYTES
                            && parse_dds(dds).is_ok()
                            && dds.len() <= 128 * 1024 * 1024 - cpu_bytes.min(128 * 1024 * 1024)
                    }) {
                        cpu_bytes += dds.len();
                        atlas.dds = Some(Arc::new(dds));
                        atlas.file.error = None;
                    } else {
                        atlas.file.failed(
                            self.host,
                            ATLAS_PATH,
                            &atlas.source,
                            "atlas-dds-or-member",
                        );
                        atlas.file.retry = Instant::now() + Duration::from_secs(3);
                    }
                }
            }
            let region = self.atlases[i].region;
            let colored_bytes = (region.width as usize)
                .saturating_mul(region.height as usize)
                .saturating_mul(4)
                .saturating_add(148);
            if self.atlases[i].dds.is_none()
                && budget.accepts(colored_bytes)
                && Instant::now() >= self.atlases[i].retry
                && let Some(source) = self.atlases[i].source_index
                && let Some(dds) = self.atlases[source].dds.clone()
            {
                let color = self.atlases[i].color;
                if u64::from(region.width) * u64::from(region.height) <= 4 * 1024 * 1024
                    && region.width as usize * region.height as usize * 4 + 148
                        <= 128 * 1024 * 1024 - cpu_bytes.min(128 * 1024 * 1024)
                    && let Some(mut pixels) = decode_region(&dds, region, 16 * 1024 * 1024)
                {
                    for pixel in pixels.as_chunks_mut::<4>().0 {
                        let transformed = color.apply(*pixel);
                        pixel.copy_from_slice(&transformed);
                    }
                    let dds = rgba_dds(region.width, region.height, &pixels);
                    cpu_bytes += dds.len();
                    self.atlases[i].dds = Some(Arc::new(dds));
                    self.atlases[i].file.error = None;
                } else {
                    let atlas = &mut self.atlases[i];
                    atlas
                        .file
                        .failed(self.host, ATLAS_PATH, &atlas.source, "atlas-recolor");
                    self.atlases[i].retry = Instant::now() + Duration::from_secs(3);
                }
            }
            let atlas = &mut self.atlases[i];
            if let Some(dds) = &atlas.dds
                && demand[i]
                && budget.accepts(dds.len())
                && Instant::now() >= atlas.retry
            {
                let id = self.host.texture(dds);
                if id != 0 {
                    atlas.pending = id;
                    budget.record(dds.len());
                } else {
                    // A full queue or a contended registry also returns zero.
                    // Keep validated DDS data and retry without reporting a
                    // resource failure before an upload has even been accepted.
                    atlas.retry = Instant::now() + Duration::from_millis(250);
                }
            }
            failed |= self.atlases[i].file.error.is_some();
        }
        failed
    }
    pub fn update(&mut self, request: ViewRequest<'_>, budget: &mut UploadBudget) -> Visuals {
        let state = (request.shown && request.data.valid).then_some(request.data.state);
        let bounds = request.bounds;
        let circle = request.preset.rotate || request.preset.shape == Shape::Circle;
        let scale = (request.preset.zoom * request.preset.map_scale).clamp(0.01, 16.0);
        let full_map = request.settings.full_map;
        self.frame += 1;
        // Mod overrides may mount after the title/Logo screen. Match the native
        // first-read gate instead of permanently publishing early vanilla GFX.
        // Resource readiness is independent of the heavier marker snapshot:
        // markers may fail their final context check or be skipped at opacity0.
        // Probe the live service only until the initial GFX request has begun.
        let first_metadata = self.definitions.is_none()
            && self.metadata[0].token == 0
            && self.metadata[0].bytes.is_none();
        let may_start = request.shown
            && (!first_metadata || (self.host.visible() && self.host.map().is_some()));
        if may_start || self.metadata.iter().any(|slot| slot.token != 0) {
            self.metadata(may_start);
        }
        // All parsing, file polling, DDS transforms and request formatting are update-only.
        let demand = self.demand(&request);
        let atlas_failed = self.atlases(&demand, budget);
        let mut views = Vec::new();
        let mut active_tiles = self.tiles.values().filter(|t| t.active()).count();
        if let Some(state) = state {
            if self.generation != state.generation {
                self.clear_tiles();
                self.generation = state.generation;
            }
            let map = if state.map_id == 10 {
                Some(2usize)
            } else if state.map_id == 0 {
                Some(usize::from(state.underground & 1 != 0))
            } else {
                None
            };
            if let Some(map) = map.filter(|_| self.directory_ready) {
                let half = [
                    (bounds[0] * 0.5 + 1.0) / scale,
                    (bounds[1] * 0.5 + 1.0) / scale,
                ];
                let x0 = ((state.x - half[0]) / 256.0).floor().clamp(0.0, 40.0) as u16;
                let x1 = ((state.x + half[0]) / 256.0).floor().clamp(0.0, 40.0) as u16;
                let y0 = ((state.y - half[1]) / 256.0).floor().clamp(0.0, 40.0) as u16;
                let y1 = ((state.y + half[1]) / 256.0).floor().clamp(0.0, 40.0) as u16;
                for layer in if map == 1 { 0..=1 } else { map..=map } {
                    for y in y0..=y1 {
                        for x in x0..=x1 {
                            if circle {
                                let dx = state
                                    .x
                                    .clamp(f32::from(x) * 256.0, f32::from(x + 1) * 256.0)
                                    - state.x;
                                let dy = state
                                    .y
                                    .clamp(f32::from(y) * 256.0, f32::from(y + 1) * 256.0)
                                    - state.y;
                                if dx * dx + dy * dy > half[0] * half[0] {
                                    continue;
                                }
                            }
                            let file_y = 40 - y;
                            let Some((allowed, true)) = self.masks[layer]
                                .get(&(u32::from(x) * 100 + u32::from(file_y)))
                                .copied()
                            else {
                                continue;
                            };
                            let key = TileKey {
                                map: if layer == 2 { 10 } else { layer as u8 },
                                level: 0,
                                x,
                                y: file_y,
                                variant: (if full_map {
                                    u32::MAX
                                } else {
                                    state.active_masks[layer]
                                }) & allowed,
                            };
                            let Some(path) = self.tile_paths.get(&key) else {
                                continue;
                            };
                            let tile = self.tiles.entry(key).or_insert_with(|| Tile {
                                file: FileSlot::default(),
                                source: None,
                                pending: 0,
                                texture: None,
                                touch: self.frame,
                                bytes: 0,
                            });
                            tile.touch = self.frame;
                            let was_active = tile.active();
                            if tile.texture.is_none() {
                                if tile.pending != 0 {
                                    match Texture::poll(self.host, tile.pending) {
                                        Some(Ok(texture)) => {
                                            tile.bytes = texture.bytes;
                                            tile.texture = Some(Arc::new(texture));
                                            tile.pending = 0;
                                            tile.file.error = None;
                                        }
                                        Some(Err(())) => {
                                            tile.file.failed(self.host, path, "", "tile-upload");
                                            self.host.release_texture(tile.pending);
                                            tile.pending = 0;
                                            tile.file.retry =
                                                Instant::now() + Duration::from_secs(1);
                                        }
                                        None => {}
                                    }
                                } else if (tile.active() || active_tiles < 8)
                                    && Instant::now() >= tile.file.retry
                                {
                                    tile.file.update(self.host, path, "");
                                    if tile.source.is_none()
                                        && let Some(bytes) = &tile.file.bytes
                                    {
                                        tile.source = tile_source(bytes);
                                        if tile.source.is_none() {
                                            tile.file.failed(
                                                self.host,
                                                path,
                                                "",
                                                "tile-tpf-or-dds",
                                            );
                                            tile.file.bytes = None;
                                            tile.file.retry =
                                                Instant::now() + Duration::from_secs(3);
                                        }
                                    }
                                    if let (Some(bytes), Some(source)) =
                                        (&tile.file.bytes, &tile.source)
                                        && budget.accepts(source.len())
                                    {
                                        let dds = &bytes[source.clone()];
                                        tile.pending = self.host.texture(dds);
                                        if tile.pending != 0 {
                                            budget.record(dds.len());
                                            tile.file.bytes = None;
                                            tile.source = None;
                                            tile.file.error = None;
                                        } else {
                                            // Zero can mean temporary queue
                                            // pressure; retain the file for
                                            // a later admission attempt.
                                            tile.file.retry =
                                                Instant::now() + Duration::from_millis(250);
                                        }
                                    }
                                }
                            }
                            match (was_active, tile.active()) {
                                (true, false) => active_tiles = active_tiles.saturating_sub(1),
                                (false, true) => active_tiles += 1,
                                _ => {}
                            }
                            if let Some(texture) = &tile.texture {
                                views.push(TileView {
                                    texture: texture.clone(),
                                    bounds: [
                                        f32::from(x) * 256.0,
                                        f32::from(y) * 256.0,
                                        f32::from(x + 1) * 256.0,
                                        f32::from(y + 1) * 256.0,
                                    ],
                                });
                            }
                        }
                    }
                }
            }
        }
        let mut bytes: u64 = self.tiles.values().map(|t| t.bytes).sum();
        for tile in self
            .tiles
            .values_mut()
            .filter(|t| t.touch + 1 < self.frame && t.active())
        {
            tile.file.stop(self.host);
            tile.file.bytes = None;
            tile.source = None;
            self.host.release_texture(tile.pending);
            tile.pending = 0;
        }
        while self.tiles.len() > TILE_CACHE_ENTRIES || bytes > TILE_CACHE_BYTES {
            let oldest = self
                .tiles
                .iter()
                .filter(|(_, t)| t.touch != self.frame)
                .min_by_key(|(_, t)| t.touch)
                .map(|(key, _)| *key);
            let Some(key) = oldest else { break };
            if let Some(mut tile) = self.tiles.remove(&key) {
                bytes = bytes.saturating_sub(tile.bytes);
                tile.file.stop(self.host);
                self.host.release_texture(tile.pending);
            }
        }
        if self.frame.is_multiple_of(60) {
            let old: Vec<_> = self
                .tiles
                .iter()
                .filter(|(_, t)| self.frame.saturating_sub(t.touch) > 600)
                .map(|(k, _)| *k)
                .collect();
            for key in old {
                if let Some(mut tile) = self.tiles.remove(&key) {
                    tile.file.stop(self.host);
                    self.host.release_texture(tile.pending);
                }
            }
        }
        let status = if self.metadata[0].error == Some("metadata-definitions") {
            "游戏 GFX 或图集布局无法解析"
        } else if self.metadata[2].error == Some("metadata-directory") {
            "地图瓦片目录无法解析"
        } else if self.metadata.iter().any(|slot| slot.error.is_some()) {
            "游戏资源读取失败，正在重试"
        } else if atlas_failed {
            "图集读取或处理失败，正在重试"
        } else {
            ""
        };
        let waiting = if !request.shown {
            ""
        } else if first_metadata && !may_start {
            "minimap-wait phase=initial-live-map\n"
        } else if self.metadata.iter().any(|slot| slot.error.is_some()) {
            ""
        } else if self.definitions.is_none() && self.metadata[0].bytes.is_none() {
            "minimap-wait phase=gfx-file\n"
        } else if self.definitions.is_none() && self.metadata[1].bytes.is_none() {
            "minimap-wait phase=atlas-layout-file\n"
        } else if !self.directory_ready && self.metadata[2].bytes.is_none() {
            "minimap-wait phase=tile-directory-file\n"
        } else if !self.directory_ready && self.metadata[3].bytes.is_none() {
            "minimap-wait phase=tile-masks-file\n"
        } else {
            ""
        };
        if waiting != self.reported_wait {
            self.reported_wait = waiting;
            if !waiting.is_empty() {
                self.host.log(waiting);
            }
        }
        Visuals {
            definitions: self.definitions.clone(),
            atlases: self.atlases.iter().map(|a| a.texture.clone()).collect(),
            tiles: views,
            status,
        }
    }
    fn clear_tiles(&mut self) {
        for (_, mut tile) in self.tiles.drain() {
            tile.file.stop(self.host);
            self.host.release_texture(tile.pending);
        }
    }
    pub fn reset(&mut self) {
        self.clear_tiles();
        for atlas in &mut self.atlases {
            atlas.texture = None;
            self.host.release_texture(atlas.pending);
            atlas.pending = 0;
        }
    }
}
impl Drop for Resources {
    fn drop(&mut self) {
        self.clear_tiles();
        for slot in &mut self.metadata {
            slot.stop(self.host);
        }
        for atlas in &mut self.atlases {
            atlas.file.stop(self.host);
            self.host.release_texture(atlas.pending);
        }
    }
}

/// Decode just the sprite's intersecting BC blocks. Large mod atlases must not
/// allocate their full RGBA image merely to tint one small icon.
fn decode_region(bytes: &[u8], region: Region, max_bytes: usize) -> Option<Vec<u8>> {
    let image = parse_dds(bytes).ok()?;
    if region.width == 0
        || region.height == 0
        || region.x > image.width
        || region.y > image.height
        || region.width > image.width - region.x
        || region.height > image.height - region.y
        || (region.width as usize)
            .checked_mul(region.height as usize)?
            .checked_mul(4)?
            > max_bytes
    {
        return None;
    }
    let level = &image.levels[0];
    if image.format == 28 || image.format == 29 {
        let mut pixels = Vec::with_capacity(region.width as usize * region.height as usize * 4);
        for y in region.y..region.y + region.height {
            let start =
                level.offset + y as usize * level.row_bytes as usize + region.x as usize * 4;
            pixels.extend_from_slice(bytes.get(start..start + region.width as usize * 4)?);
        }
        return Some(pixels);
    }
    let block_bytes = if image.format <= 72 { 8 } else { 16 };
    let x = region.x / 4 * 4;
    let y = region.y / 4 * 4;
    let width = ((region.x + region.width).div_ceil(4) * 4).min(image.width) - x;
    let height = ((region.y + region.height).div_ceil(4) * 4).min(image.height) - y;
    let row_bytes = width.div_ceil(4) as usize * block_bytes;
    let mut cropped = single_level_dds(width, height, image.format);
    for by in y / 4..(y + height).div_ceil(4) {
        let start =
            level.offset + by as usize * level.row_bytes as usize + x as usize / 4 * block_bytes;
        cropped.extend_from_slice(bytes.get(start..start + row_bytes)?);
    }
    let (_, _, rgba) = decode_dds_rgba(&cropped, width as usize * height as usize * 4).ok()?;
    let mut pixels = Vec::with_capacity(region.width as usize * region.height as usize * 4);
    for row in region.y - y..region.y - y + region.height {
        let start = (row as usize * width as usize + (region.x - x) as usize) * 4;
        pixels.extend_from_slice(rgba.get(start..start + region.width as usize * 4)?);
    }
    Some(pixels)
}

fn single_level_dds(width: u32, height: u32, format: u32) -> Vec<u8> {
    let mut dds = vec![0; 148];
    dds[..4].copy_from_slice(b"DDS ");
    let uncompressed = format == 28 || format == 29;
    let pitch = if uncompressed {
        width * 4
    } else {
        width.div_ceil(4) * if format <= 72 { 8 } else { 16 } * height.div_ceil(4)
    };
    for (offset, value) in [
        (4, 124u32),
        (8, if uncompressed { 0x100f } else { 0x81007 }),
        (12, height),
        (16, width),
        (20, pitch),
        (28, 1),
        (76, 32),
        (80, 4),
        (84, 0x30315844),
        (108, 0x1000),
        (128, format),
        (132, 3),
        (140, 1),
    ] {
        dds[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
    }
    dds
}

pub fn rgba_dds(width: u32, height: u32, pixels: &[u8]) -> Vec<u8> {
    let mut dds = single_level_dds(width, height, 28);
    dds.extend_from_slice(pixels);
    dds
}

#[cfg(test)]
mod tests {
    use super::*;
    use eroverlay_abi as abi;
    use std::{
        cell::{Cell, RefCell},
        ffi::c_void,
    };

    #[derive(Default)]
    struct NativeFiles {
        map_ready: Cell<bool>,
        hidden: Cell<bool>,
        map_reads: Cell<usize>,
        mounted: Cell<u8>,
        complete: Cell<bool>,
        fail_request: Cell<bool>,
        busy_poll: Cell<bool>,
        fail_poll: Cell<bool>,
        requests: RefCell<Vec<(String, u8)>>,
        files: RefCell<HashMap<u64, Vec<u8>>>,
        released: RefCell<Vec<u64>>,
        polls: Cell<usize>,
        logs: RefCell<Vec<String>>,
        texture_accept: Cell<bool>,
        texture_calls: Cell<u64>,
        texture_attempts: Cell<u64>,
        texture_lengths: RefCell<Vec<usize>>,
        texture_pending: Cell<bool>,
        texture_polls: Cell<usize>,
        texture_released: RefCell<Vec<u64>>,
    }
    impl NativeFiles {
        fn host(&self) -> Host {
            unsafe extern "C" fn display(user: *mut c_void, out: *mut abi::DisplayState) -> i32 {
                let files = unsafe { &*user.cast::<NativeFiles>() };
                unsafe {
                    out.write(abi::DisplayState {
                        valid: 1,
                        loading_or_logo: u32::from(files.hidden.get()),
                        full_screen_map: 0,
                    })
                };
                abi::OK
            }
            unsafe extern "C" fn map(user: *mut c_void, out: *mut abi::MapState) -> i32 {
                let files = unsafe { &*user.cast::<NativeFiles>() };
                files.map_reads.set(files.map_reads.get() + 1);
                if !files.map_ready.get() {
                    return abi::ERR_UNAVAILABLE;
                }
                unsafe {
                    out.write(abi::MapState {
                        view_model: 1,
                        generation: 1,
                        ..Default::default()
                    });
                }
                abi::OK
            }
            unsafe extern "C" fn request(
                user: *mut c_void,
                request: *const abi::FileRequest,
            ) -> u64 {
                let files = unsafe { &*user.cast::<NativeFiles>() };
                let request = unsafe { &*request };
                let path =
                    unsafe { std::slice::from_raw_parts(request.path.data, request.path.len) };
                files.requests.borrow_mut().push((
                    String::from_utf8(path.to_vec()).unwrap(),
                    files.mounted.get(),
                ));
                let id = files.requests.borrow().len() as u64;
                if files.fail_request.get() {
                    return 0;
                }
                files
                    .files
                    .borrow_mut()
                    .insert(id, vec![files.mounted.get(); 4]);
                id
            }
            unsafe extern "C" fn poll(
                user: *mut c_void,
                token: u64,
                out: *mut abi::FileView,
            ) -> i32 {
                let files = unsafe { &*user.cast::<NativeFiles>() };
                files.polls.set(files.polls.get() + 1);
                if files.busy_poll.get() {
                    return abi::ERR_BUSY;
                }
                let stored = files.files.borrow();
                let bytes = stored.get(&token).unwrap();
                unsafe {
                    out.write(abi::FileView {
                        status: if files.fail_poll.get() {
                            abi::FILE_FAILED
                        } else if files.complete.get() {
                            abi::FILE_READY
                        } else {
                            abi::FILE_PENDING
                        },
                        bytes: abi::ByteSlice::new(bytes),
                        ..Default::default()
                    });
                }
                abi::OK
            }
            unsafe extern "C" fn release(user: *mut c_void, token: u64) {
                let files = unsafe { &*user.cast::<NativeFiles>() };
                files.files.borrow_mut().remove(&token);
                files.released.borrow_mut().push(token);
            }
            unsafe extern "C" fn log(user: *mut c_void, _: u32, text: abi::ByteSlice) {
                let files = unsafe { &*user.cast::<NativeFiles>() };
                let text = unsafe { std::slice::from_raw_parts(text.data, text.len) };
                files
                    .logs
                    .borrow_mut()
                    .push(String::from_utf8(text.to_vec()).unwrap());
            }
            unsafe extern "C" fn texture(user: *mut c_void, bytes: abi::ByteSlice) -> u64 {
                let files = unsafe { &*user.cast::<NativeFiles>() };
                files.texture_attempts.set(files.texture_attempts.get() + 1);
                files.texture_lengths.borrow_mut().push(bytes.len);
                if !files.texture_accept.get() {
                    return 0;
                }
                files.texture_calls.set(files.texture_calls.get() + 1);
                1000 + files.texture_calls.get()
            }
            unsafe extern "C" fn texture_poll(
                user: *mut c_void,
                _: u64,
                out: *mut abi::TextureView,
            ) -> i32 {
                let files = unsafe { &*user.cast::<NativeFiles>() };
                files.texture_polls.set(files.texture_polls.get() + 1);
                unsafe {
                    out.write(abi::TextureView {
                        status: if files.texture_pending.get() {
                            abi::TEXTURE_PENDING
                        } else {
                            abi::TEXTURE_READY
                        },
                        width: 1,
                        height: 1,
                        memory_bytes: 4,
                        reserved: 0,
                    });
                }
                abi::OK
            }
            unsafe extern "C" fn texture_release(user: *mut c_void, id: u64) {
                let files = unsafe { &*user.cast::<NativeFiles>() };
                files.texture_released.borrow_mut().push(id);
            }
            // SAFETY: HostApi has integer/pointer fields and nullable callbacks.
            let mut api: abi::HostApi = unsafe { std::mem::zeroed() };
            api.user = (self as *const Self).cast_mut().cast();
            api.read_map_state = Some(map);
            api.read_display_state = Some(display);
            api.request_file = Some(request);
            api.poll_file = Some(poll);
            api.release_file = Some(release);
            api.log = Some(log);
            api.texture_create = Some(texture);
            api.texture_poll = Some(texture_poll);
            api.texture_release = Some(texture_release);
            Host(api)
        }
    }
    fn update_resources(resources: &mut Resources, data: &MapData, shown: bool) -> Visuals {
        let settings = Settings::load(&HashMap::new());
        resources.update(
            ViewRequest {
                data,
                bounds: [100.0; 2],
                shown,
                preset: &settings.presets[0],
                settings: &settings,
                graces: true,
                landmarks: true,
            },
            &mut UploadBudget::new(),
        )
    }
    fn empty_definitions() -> Definitions {
        Definitions {
            icons: HashMap::new(),
            memos: HashMap::new(),
            specials: HashMap::new(),
            regions: Vec::new(),
            layers: Vec::new(),
            text: TextLayout {
                bounds: [0.0; 4],
                matrix: gfx::IDENTITY,
                height: 12.0,
                color: [255; 4],
                align: 0,
                left: 0.0,
                right: 0.0,
                indent: 0.0,
            },
        }
    }
    #[test]
    fn tile_retry_reuses_validated_member_without_reparsing_or_copying_archive() {
        let files = NativeFiles::default();
        let dds = rgba_dds(1, 1, &[255; 4]);
        let mut tpf = vec![0u8; 64];
        tpf[..4].copy_from_slice(b"TPF\0");
        tpf[8..12].copy_from_slice(&1u32.to_le_bytes());
        tpf[13] = 3;
        tpf[16..20].copy_from_slice(&64u32.to_le_bytes());
        tpf[20..24].copy_from_slice(&(dds.len() as u32).to_le_bytes());
        tpf[28..32].copy_from_slice(&36u32.to_le_bytes());
        tpf[36] = b'a';
        tpf.extend_from_slice(&dds);
        let address = tpf.as_ptr();
        let mut resources = Resources::new(files.host());
        resources.definitions = Some(Arc::new(empty_definitions()));
        resources.directory_ready = true;
        resources.generation = 1;
        let key = TileKey {
            y: 40,
            ..Default::default()
        };
        resources.tile_paths.insert(key, "fixture:/tile".into());
        resources.masks[0].insert(40, (0, true));
        resources.tiles.insert(
            key,
            Tile {
                file: FileSlot {
                    bytes: Some(tpf),
                    ..Default::default()
                },
                source: None,
                pending: 0,
                texture: None,
                touch: 0,
                bytes: 0,
            },
        );
        let data = MapData {
            valid: true,
            state: abi::MapState {
                generation: 1,
                x: 128.0,
                y: 128.0,
                ..Default::default()
            },
            ..Default::default()
        };
        update_resources(&mut resources, &data, true);
        let tile = resources.tiles.get_mut(&key).unwrap();
        assert_eq!(tile.source, Some(64..64 + dds.len()));
        let bytes = tile.file.bytes.as_mut().unwrap();
        assert_eq!(bytes.as_ptr(), address);
        // The source and range are immutable in production. Poison only the
        // test archive header to detect unnecessary TPF re-parsing on retry.
        bytes[..4].fill(0);
        tile.file.retry = Instant::now();
        files.texture_accept.set(true);
        update_resources(&mut resources, &data, true);
        assert_eq!(*files.texture_lengths.borrow(), [dds.len(), dds.len()]);
        assert_eq!(files.texture_calls.get(), 1);
        assert!(files.logs.borrow().is_empty());
        let tile = &resources.tiles[&key];
        assert!(tile.file.bytes.is_none() && tile.source.is_none());
    }
    #[test]
    fn oversized_atlas_and_tile_are_errors_instead_of_permanent_admission_waits() {
        let mut dds = single_level_dds(8192, 8192, 71);
        dds.resize(148 + 32 * 1024 * 1024, 0);
        assert!(parse_dds(&dds).is_ok());
        for atlas in [false, true] {
            let files = NativeFiles::default();
            files.complete.set(true);
            let mut resources = Resources::new(files.host());
            let mut defs = empty_definitions();
            let key = TileKey {
                y: 40,
                ..Default::default()
            };
            if atlas {
                defs.specials.insert(
                    "player".into(),
                    Recipe {
                        layers: vec![gfx::IconLayer {
                            region: Some(0),
                            ..Default::default()
                        }],
                    },
                );
                defs.regions.push(Region::default());
                let mut source = Atlas::source("oversized".into());
                source.file.bytes = Some(dds.clone());
                resources.atlases.push(source);
            } else {
                resources
                    .tile_paths
                    .insert(key, "fixture:/oversized-tile".into());
                resources.masks[0].insert(40, (0, true));
                resources.tiles.insert(
                    key,
                    Tile {
                        file: FileSlot {
                            bytes: Some(dds.clone()),
                            ..Default::default()
                        },
                        source: None,
                        pending: 0,
                        texture: None,
                        touch: 0,
                        bytes: 0,
                    },
                );
            }
            resources.definitions = Some(Arc::new(defs));
            resources.directory_ready = true;
            resources.generation = 1;
            let data = MapData {
                valid: true,
                state: abi::MapState {
                    generation: 1,
                    x: 128.0,
                    y: 128.0,
                    ..Default::default()
                },
                ..Default::default()
            };
            let visuals = update_resources(&mut resources, &data, true);
            assert_eq!(visuals.status.contains("失败"), atlas);
            assert_eq!(files.texture_attempts.get(), 0);
            assert_eq!(files.logs.borrow().len(), 1);
            for _ in 0..10 {
                update_resources(&mut resources, &data, true);
            }
            assert_eq!(files.logs.borrow().len(), 1);
        }
    }
    #[test]
    fn file_admission_pressure_is_silent_and_retries_without_a_failed_token() {
        let files = NativeFiles::default();
        files.map_ready.set(true);
        files.fail_request.set(true);
        let mut resources = Resources::new(files.host());
        let data = MapData::default();
        assert!(
            update_resources(&mut resources, &data, true)
                .status
                .is_empty()
        );
        assert!(resources.metadata.iter().all(|slot| slot.error.is_none()));
        assert_eq!(files.requests.borrow().len(), 4);
        for _ in 0..10 {
            update_resources(&mut resources, &data, true);
        }
        assert_eq!(files.requests.borrow().len(), 4);
        files.fail_request.set(false);
        for slot in &mut resources.metadata {
            slot.retry = Instant::now();
        }
        assert!(
            update_resources(&mut resources, &data, true)
                .status
                .is_empty()
        );
        assert!(resources.metadata.iter().all(|slot| slot.token != 0));
        assert!(files.released.borrow().is_empty());
        files.fail_poll.set(true);
        assert!(
            update_resources(&mut resources, &data, true)
                .status
                .contains("失败")
        );
        assert_eq!(files.released.borrow().len(), 4);
        assert_eq!(
            files
                .logs
                .borrow()
                .iter()
                .filter(|s| s.contains("phase=file-poll"))
                .count(),
            4
        );
    }
    #[test]
    fn offscreen_admission_sources_release_tile_job_capacity() {
        let files = NativeFiles::default();
        let mut resources = Resources::new(files.host());
        resources.definitions = Some(Arc::new(empty_definitions()));
        resources.directory_ready = true;
        resources.generation = 1;
        resources.frame = 1;
        let key = TileKey {
            y: 40,
            ..Default::default()
        };
        resources.tile_paths.insert(key, "fixture:/new-tile".into());
        resources.masks[0].insert(40, (0, true));
        for x in 10..18 {
            resources.tiles.insert(
                TileKey {
                    x,
                    y: 40,
                    ..Default::default()
                },
                Tile {
                    file: FileSlot {
                        bytes: Some(rgba_dds(1, 1, &[255; 4])),
                        ..Default::default()
                    },
                    source: None,
                    pending: 0,
                    texture: None,
                    touch: 1,
                    bytes: 0,
                },
            );
        }
        let data = MapData {
            valid: true,
            state: abi::MapState {
                generation: 1,
                x: 128.0,
                y: 128.0,
                ..Default::default()
            },
            ..Default::default()
        };
        for _ in 0..3 {
            update_resources(&mut resources, &data, true);
        }
        assert_eq!(
            files.requests.borrow().len(),
            1,
            "old rejected uploads must not occupy all eight tile jobs for ten seconds"
        );
        assert!(
            resources
                .tiles
                .iter()
                .filter(|(k, _)| **k != key)
                .all(|(_, tile)| !tile.active())
        );
    }
    #[test]
    fn normal_initial_and_metadata_loading_is_silent() {
        let files = NativeFiles::default();
        let mut resources = Resources::new(files.host());
        let data = MapData::default();
        assert!(
            update_resources(&mut resources, &data, true)
                .status
                .is_empty()
        );
        assert!(files.requests.borrow().is_empty());
        files.map_ready.set(true);
        assert!(
            update_resources(&mut resources, &data, true)
                .status
                .is_empty()
        );
        assert_eq!(files.requests.borrow().len(), 4);
        assert!(resources.metadata.iter().all(|slot| slot.token != 0));
    }
    #[test]
    fn busy_file_poll_keeps_token_until_success_without_error_or_reread() {
        let files = NativeFiles::default();
        files.busy_poll.set(true);
        let host = files.host();
        let mut slot = FileSlot::default();
        slot.update(host, PATHS[0], "");
        let token = slot.token;
        assert_ne!(token, 0);
        for _ in 0..10 {
            slot.update(host, PATHS[0], "");
        }
        assert_eq!(slot.token, token);
        assert!(slot.error.is_none());
        assert!(files.logs.borrow().is_empty());
        assert!(files.released.borrow().is_empty());
        assert_eq!(files.requests.borrow().len(), 1);
        files.busy_poll.set(false);
        files.complete.set(true);
        slot.update(host, PATHS[0], "");
        assert_eq!(slot.token, 0);
        assert_eq!(slot.bytes.as_deref(), Some(&[0; 4][..]));
        assert_eq!(*files.released.borrow(), [token]);
        assert_eq!(files.requests.borrow().len(), 1);
        assert!(files.logs.borrow().is_empty());
    }
    #[test]
    fn texture_admission_and_pending_uploads_stay_silent_and_reuse_source() {
        for atlas in [false, true] {
            let files = NativeFiles::default();
            let mut resources = Resources::new(files.host());
            let mut defs = empty_definitions();
            let key = TileKey {
                y: 40,
                ..Default::default()
            };
            let dds = rgba_dds(1, 1, &[255; 4]);
            if atlas {
                defs.specials.insert(
                    "player".into(),
                    Recipe {
                        layers: vec![gfx::IconLayer {
                            region: Some(0),
                            ..Default::default()
                        }],
                    },
                );
                defs.regions.push(Region::default());
                let mut source = Atlas::source("validated-atlas".into());
                source.dds = Some(Arc::new(dds.clone()));
                resources.atlases.push(source);
            } else {
                resources.tile_paths.insert(key, "fixture:/tile".into());
                resources.masks[0].insert(40, (0, true));
                resources.tiles.insert(
                    key,
                    Tile {
                        file: FileSlot {
                            bytes: Some(dds.clone()),
                            ..Default::default()
                        },
                        source: None,
                        pending: 0,
                        texture: None,
                        touch: 0,
                        bytes: 0,
                    },
                );
            }
            resources.definitions = Some(Arc::new(defs));
            resources.directory_ready = true;
            resources.generation = 1;
            let data = MapData {
                valid: true,
                state: abi::MapState {
                    generation: 1,
                    x: 128.0,
                    y: 128.0,
                    ..Default::default()
                },
                ..Default::default()
            };
            assert!(
                update_resources(&mut resources, &data, true)
                    .status
                    .is_empty()
            );
            assert_eq!(files.texture_attempts.get(), 1);
            let retry = Instant::now() + Duration::from_secs(60);
            if atlas {
                assert_eq!(resources.atlases[0].dds.as_deref().unwrap().as_slice(), dds);
                assert!(resources.atlases[0].file.error.is_none());
                resources.atlases[0].retry = retry;
            } else {
                let tile = resources.tiles.get_mut(&key).unwrap();
                assert_eq!(tile.file.bytes.as_ref().unwrap(), &dds);
                assert!(tile.file.error.is_none());
                tile.file.retry = retry;
            }
            for _ in 0..10 {
                assert!(
                    update_resources(&mut resources, &data, true)
                        .status
                        .is_empty()
                );
            }
            assert_eq!(files.texture_attempts.get(), 1);
            if atlas {
                resources.atlases[0].retry = Instant::now();
            } else {
                resources.tiles.get_mut(&key).unwrap().file.retry = Instant::now();
            }
            files.texture_accept.set(true);
            files.texture_pending.set(true);
            for _ in 0..3 {
                let polls = files.texture_polls.get();
                assert!(
                    update_resources(&mut resources, &data, true)
                        .status
                        .is_empty()
                );
                assert!(files.texture_polls.get() - polls <= 1);
            }
            assert_eq!(files.texture_attempts.get(), 2);
            assert_eq!(files.texture_calls.get(), 1);
            files.texture_pending.set(false);
            let visuals = update_resources(&mut resources, &data, true);
            assert!(visuals.status.is_empty());
            if atlas {
                assert_eq!(visuals.atlases[0].as_ref().unwrap().id, 1001);
            } else {
                assert_eq!(visuals.tiles[0].texture.id, 1001);
            }
            assert!(files.logs.borrow().is_empty());
            assert!(files.requests.borrow().is_empty());
        }
    }
    #[test]
    fn first_metadata_starts_at_zero_opacity_without_a_marker_snapshot() {
        let files = NativeFiles::default();
        files.map_ready.set(true);
        let mut resources = Resources::new(files.host());
        let mut settings = Settings::load(&HashMap::new());
        settings.presets[0].opacity = 0.0;
        resources.update(
            ViewRequest {
                data: &MapData::default(),
                bounds: [100.0; 2],
                shown: true,
                preset: &settings.presets[0],
                settings: &settings,
                graces: true,
                landmarks: true,
            },
            &mut UploadBudget::new(),
        );
        assert_eq!(files.requests.borrow().len(), 4);
    }

    #[test]
    fn first_metadata_waits_for_live_map_and_reads_late_mounted_override() {
        let files = NativeFiles::default();
        files.mounted.set(1); // Vanilla content before the mod loader mounts.
        files.map_ready.set(true);
        files.hidden.set(true);
        let mut resources = Resources::new(files.host());
        for _ in 0..10 {
            // A stale/live map alone must not start reads on Logo/loading.
            update_resources(&mut resources, &MapData::default(), true);
        }
        assert!(files.requests.borrow().is_empty());
        files.hidden.set(false);
        files.map_ready.set(false);
        let valid = MapData {
            valid: true,
            ..Default::default()
        };
        // A stale valid snapshot alone is insufficient if the live service failed.
        update_resources(&mut resources, &valid, true);
        assert!(files.requests.borrow().is_empty());
        files.mounted.set(2);
        files.map_ready.set(true);
        update_resources(&mut resources, &valid, true);
        assert_eq!(files.requests.borrow().len(), 4);
        assert!(files.requests.borrow().iter().all(|(_, mount)| *mount == 2));
        // Loading/hiding after submission must still poll and retire that batch.
        files.map_ready.set(false);
        files.complete.set(true);
        update_resources(&mut resources, &MapData::default(), false);
        assert_eq!(files.requests.borrow().len(), 4);
        assert_eq!(files.released.borrow().len(), 4);
        assert!(resources.metadata.iter().all(|slot| slot.token == 0));
        assert_eq!(resources.metadata[0].bytes.as_deref(), Some(&[2; 4][..]));
    }
    #[test]
    fn first_read_gate_preserves_cleanup_and_published_definitions_on_reset() {
        let files = NativeFiles::default();
        let mut resources = Resources::new(files.host());
        resources.tiles.insert(
            TileKey::default(),
            Tile {
                file: FileSlot {
                    token: 77,
                    ..Default::default()
                },
                source: None,
                pending: 0,
                texture: None,
                touch: 0,
                bytes: 0,
            },
        );
        update_resources(&mut resources, &MapData::default(), true);
        update_resources(&mut resources, &MapData::default(), true);
        assert_eq!(*files.released.borrow(), [77]);
        assert!(files.requests.borrow().is_empty());
        let probes = files.map_reads.get();
        let definitions = Arc::new(empty_definitions());
        resources.definitions = Some(definitions.clone());
        resources.directory_ready = true;
        resources.reset();
        update_resources(&mut resources, &MapData::default(), true);
        assert!(Arc::ptr_eq(
            resources.definitions.as_ref().unwrap(),
            &definitions
        ));
        assert!(files.requests.borrow().is_empty());
        assert_eq!(files.map_reads.get(), probes); // No readiness probe after publication.
    }
    #[test]
    fn renderer_reset_rebuilds_textures_without_reloading_validated_metadata() {
        let files = NativeFiles::default();
        files.texture_accept.set(true);
        let mut resources = Resources::new(files.host());
        let mut defs = empty_definitions();
        defs.specials.insert(
            "player".into(),
            Recipe {
                layers: vec![gfx::IconLayer {
                    region: Some(0),
                    ..Default::default()
                }],
            },
        );
        defs.regions.push(Region::default());
        resources.definitions = Some(Arc::new(defs));
        resources.directory_ready = true;
        let mut atlas = Atlas::source("validated-atlas".into());
        atlas.dds = Some(Arc::new(rgba_dds(1, 1, &[255; 4])));
        resources.atlases.push(atlas);
        let valid = MapData {
            valid: true,
            ..Default::default()
        };
        update_resources(&mut resources, &valid, true);
        let snapshot = update_resources(&mut resources, &valid, true);
        assert_eq!(snapshot.atlases[0].as_ref().unwrap().id, 1001);
        resources.reset();
        assert!(files.texture_released.borrow().is_empty()); // Published snapshot retains ownership.
        drop(snapshot);
        assert_eq!(*files.texture_released.borrow(), [1001]);
        update_resources(&mut resources, &valid, true);
        let rebuilt = update_resources(&mut resources, &valid, true);
        assert_eq!(rebuilt.atlases[0].as_ref().unwrap().id, 1002);
        assert_eq!(files.texture_calls.get(), 2);
        assert!(files.requests.borrow().is_empty());
        assert_eq!(files.map_reads.get(), 0);

        // A new plugin/resource instance must not inherit the old ready gate.
        let mut fresh = Resources::new(files.host());
        update_resources(&mut fresh, &MapData::default(), true);
        update_resources(&mut fresh, &valid, true); // Live map still unavailable.
        assert!(files.requests.borrow().is_empty());
        files.mounted.set(3);
        files.map_ready.set(true);
        update_resources(&mut fresh, &valid, true);
        assert_eq!(files.requests.borrow().len(), 4);
        assert!(files.requests.borrow().iter().all(|(_, mount)| *mount == 3));
    }
    #[test]
    fn file_and_metadata_failures_log_only_failed_attempts_and_keep_ui_status() {
        let files = NativeFiles::default();
        files.map_ready.set(true);
        files.fail_poll.set(true);
        let mut resources = Resources::new(files.host());
        let data = MapData {
            valid: true,
            ..Default::default()
        };
        let status = update_resources(&mut resources, &data, true).status;
        assert!(status.contains("失败"));
        assert_eq!(files.logs.borrow().len(), 4);
        for _ in 0..10 {
            update_resources(&mut resources, &data, true);
        }
        assert_eq!(files.logs.borrow().len(), 4);
        assert!(files.logs.borrow()[0].contains(PATHS[0]));
        assert!(files.logs.borrow()[0].contains("phase=file-poll"));
        files.fail_poll.set(false);
        files.complete.set(true);
        for slot in &mut resources.metadata {
            slot.retry = Instant::now();
        }
        update_resources(&mut resources, &data, true);
        assert_eq!(files.logs.borrow().len(), 6);
        assert!(files.logs.borrow()[4].contains("phase=metadata-definitions"));
        assert!(files.logs.borrow()[5].contains("phase=metadata-directory"));
        for _ in 0..10 {
            update_resources(&mut resources, &data, true);
        }
        assert_eq!(files.logs.borrow().len(), 6);
        resources.parse_retry = Instant::now();
        update_resources(&mut resources, &data, true);
        assert_eq!(files.logs.borrow().len(), 8); // Two actual failed parse retries.
    }
    #[test]
    fn atlas_errors_are_visible_and_tile_errors_only_log_with_retry_limits() {
        let files = NativeFiles::default();
        files.complete.set(true);
        let data = MapData {
            valid: true,
            state: abi::MapState {
                generation: 1,
                x: 128.0,
                y: 128.0,
                ..Default::default()
            },
            ..Default::default()
        };
        for recolor in [false, true] {
            let mut resources = Resources::new(files.host());
            let mut defs = empty_definitions();
            defs.specials.insert(
                "player".into(),
                Recipe {
                    layers: vec![gfx::IconLayer {
                        region: Some(0),
                        ..Default::default()
                    }],
                },
            );
            defs.regions.push(Region {
                atlas: usize::from(recolor),
                ..Default::default()
            });
            resources.definitions = Some(Arc::new(defs));
            resources.directory_ready = true;
            resources.atlases.push(Atlas::source("bad-atlas".into()));
            if recolor {
                resources.atlases[0].dds = Some(Arc::new(rgba_dds(1, 1, &[255; 4])));
                let mut colored = Atlas::source("bad-atlas".into());
                colored.source_index = Some(0);
                colored.region = Region {
                    x: 2,
                    width: 1,
                    height: 1,
                    ..Default::default()
                };
                resources.atlases.push(colored);
            }
            let before = files.logs.borrow().len();
            assert!(
                update_resources(&mut resources, &data, true)
                    .status
                    .contains("失败")
            );
            assert_eq!(files.logs.borrow().len(), before + 1);
            let phase = if recolor {
                "atlas-recolor"
            } else {
                "atlas-dds-or-member"
            };
            assert!(files.logs.borrow().last().unwrap().contains(phase));
            assert!(
                files
                    .logs
                    .borrow()
                    .last()
                    .unwrap()
                    .contains("part=bad-atlas")
            );
            for _ in 0..10 {
                update_resources(&mut resources, &data, true);
            }
            assert_eq!(files.logs.borrow().len(), before + 1);
        }
        let mut resources = Resources::new(files.host());
        resources.definitions = Some(Arc::new(empty_definitions()));
        resources.directory_ready = true;
        resources.generation = 1;
        let key = TileKey {
            y: 40,
            ..Default::default()
        };
        resources.tile_paths.insert(key, "fixture:/bad-tile".into());
        resources.masks[0].insert(40, (0, true));
        let before = files.logs.borrow().len();
        assert!(
            update_resources(&mut resources, &data, true)
                .status
                .is_empty()
        );
        assert_eq!(files.logs.borrow().len(), before + 1);
        assert!(
            files
                .logs
                .borrow()
                .last()
                .unwrap()
                .contains("phase=tile-tpf-or-dds path=fixture:/bad-tile")
        );
        for _ in 0..10 {
            update_resources(&mut resources, &data, true);
        }
        assert_eq!(files.logs.borrow().len(), before + 1);
        let tile = resources.tiles.get_mut(&key).unwrap();
        tile.file.bytes = Some(rgba_dds(1, 1, &[255; 4]));
        tile.file.retry = Instant::now();
        assert!(
            update_resources(&mut resources, &data, true)
                .status
                .is_empty()
        );
        assert_eq!(files.logs.borrow().len(), before + 1);
        let after = files.logs.borrow().len();
        for _ in 0..10 {
            update_resources(&mut resources, &data, true);
        }
        assert_eq!(files.logs.borrow().len(), after);
    }
    #[test]
    fn native_and_provider_preparation_share_one_count_and_byte_budget() {
        use crate::goblins::{Goblins, RuntimeIcon};
        use std::{cell::Cell, ffi::c_void};
        struct Sink {
            count: Cell<usize>,
            bytes: Cell<usize>,
        }
        fn count(user: *mut c_void, bytes: usize) -> u64 {
            let sink = unsafe { &*user.cast::<Sink>() };
            sink.count.set(sink.count.get() + 1);
            sink.bytes.set(sink.bytes.get() + bytes);
            sink.count.get() as u64
        }
        unsafe extern "C" fn dds(user: *mut c_void, bytes: eroverlay_abi::ByteSlice) -> u64 {
            count(user, bytes.len)
        }
        unsafe extern "C" fn rgba(
            user: *mut c_void,
            _: u32,
            _: u32,
            bytes: eroverlay_abi::ByteSlice,
        ) -> u64 {
            count(user, bytes.len)
        }
        let sink = Sink {
            count: Cell::new(0),
            bytes: Cell::new(0),
        };
        // SAFETY: HostApi contains integers, raw pointers and nullable callbacks.
        let mut api: eroverlay_abi::HostApi = unsafe { std::mem::zeroed() };
        api.user = (&sink as *const Sink).cast_mut().cast();
        api.texture_create = Some(dds);
        api.texture_create_rgba = Some(rgba);
        let host = Host(api);
        let native = |dds| {
            let mut resources = Resources::new(host);
            let mut atlas = Atlas::source("fixture".into());
            atlas.dds = Some(Arc::new(dds));
            resources.atlases.push(atlas);
            resources
        };
        let provider = |n| {
            let mut goblins = Goblins::new(host);
            for id in 0..n {
                goblins.icons.insert(
                    id,
                    RuntimeIcon {
                        recipe: Recipe { layers: Vec::new() },
                        region: Region {
                            atlas: 0,
                            x: 0,
                            y: 0,
                            width: 1,
                            height: 1,
                        },
                        texture: None,
                        pending: 0,
                        rgba: vec![255; 4],
                        touched: Instant::now(),
                        retry: Instant::now(),
                    },
                );
            }
            goblins
        };
        let demand = HashSet::from([0, 1, 2, 3]);
        let mut budget = UploadBudget::new();
        let mut goblins = provider(4);
        goblins.update(&demand, &mut budget);
        native(rgba_dds(1, 1, &[255; 4])).atlases(&[true], &mut budget);
        assert_eq!(sink.count.get(), 4);
        assert_eq!(sink.bytes.get(), 16);

        // Exactly 4MiB from the provider leaves no allowance for a native image.
        let mut budget = UploadBudget::new();
        let mut goblins = provider(1);
        let icon = goblins.icons.get_mut(&0).unwrap();
        icon.region.width = 1024;
        icon.region.height = 1024;
        icon.rgba = vec![255; 4 * 1024 * 1024];
        goblins.update(&demand, &mut budget);
        native(rgba_dds(1, 1, &[255; 4])).atlases(&[true], &mut budget);
        assert_eq!(sink.count.get(), 5);
        assert_eq!(budget.count, 1);
        assert_eq!(budget.remaining, 0);

        // A single oversized atlas is admitted only into an empty shared batch.
        let mut budget = UploadBudget::new();
        native(rgba_dds(1024, 1024, &vec![0; 4 * 1024 * 1024])).atlases(&[true], &mut budget);
        provider(1).update(&demand, &mut budget);
        assert_eq!(sink.count.get(), 6);
        assert_eq!(budget.count, 1);
        let mut budget = UploadBudget::new();
        provider(1).update(&demand, &mut budget);
        native(rgba_dds(1024, 1024, &vec![0; 4 * 1024 * 1024])).atlases(&[true], &mut budget);
        assert_eq!(sink.count.get(), 7);
        assert_eq!(budget.count, 1);
    }
    #[test]
    fn strict_tile_names() {
        let k = TileKey::parse("MENU_MapTile_M10_L0_12_34_01aB0000").unwrap();
        assert_eq!(k.map, 10);
        assert_eq!(k.x, 12);
        assert_eq!(k.y, 34);
        assert_eq!(k.variant, 0x01ab0000);
        assert!(TileKey::parse("MENU_MapTile_M02_L0_12_34_00000000").is_none());
        assert!(TileKey::parse("MENU_MapTile_M00_L3_12_34_00000000").is_none());
    }
    #[test]
    fn xml_dtd_rejected() {
        assert!(xml(b"<!DOCTYPE x [<!ENTITY a 'bad'>]><x/>", |_, _| Some(())).is_none());
    }
    #[test]
    fn rgba_dds_matches_decoder() {
        let dds = rgba_dds(1, 1, &[1, 2, 3, 4]);
        assert_eq!(decode_dds_rgba(&dds, 4).unwrap().2, [1, 2, 3, 4]);
    }

    #[test]
    fn sprite_region_decode_is_bounded_and_preserves_non_aligned_pixels() {
        let pixels: Vec<u8> = (0..24).collect();
        let dds = rgba_dds(3, 2, &pixels);
        let region = Region {
            atlas: 0,
            x: 1,
            y: 0,
            width: 2,
            height: 2,
        };
        assert_eq!(
            decode_region(&dds, region, 16).unwrap(),
            [&pixels[4..12], &pixels[16..24]].concat()
        );
        assert!(decode_region(&dds, region, 15).is_none());
        assert!(decode_region(&dds, Region { x: 2, ..region }, 16).is_none());

        // A 256MiB decoded atlas needs only one 4x4 block for this 3x3 sprite.
        let mut bc1 = single_level_dds(8192, 8192, 71);
        bc1.resize(148 + 2048 * 2048 * 8, 0);
        let block = 148 + (2046 * 2048 + 2046) * 8;
        bc1[block..block + 2].copy_from_slice(&0xf800u16.to_le_bytes());
        let region = Region {
            atlas: 0,
            x: 8185,
            y: 8185,
            width: 3,
            height: 3,
        };
        assert!(decode_dds_rgba(&bc1, 36).is_err());
        assert_eq!(
            decode_region(&bc1, region, 36).unwrap(),
            [255, 0, 0, 255].repeat(9)
        );
    }

    #[test]
    #[ignore = "requires EROVERLAY_GFX and EROVERLAY_LAYOUTS archived game assets"]
    fn archived_game_gfx_and_atlas_layouts() {
        let gfx = std::fs::read(std::env::var("EROVERLAY_GFX").expect("EROVERLAY_GFX")).unwrap();
        let layouts =
            std::fs::read(std::env::var("EROVERLAY_LAYOUTS").expect("EROVERLAY_LAYOUTS")).unwrap();
        let (defs, atlases) =
            definitions(&gfx, &layouts).expect("complete archived GFX/atlas recipes");
        let files = NativeFiles::default();
        let mut resources = Resources::new(files.host());
        resources.metadata[0].bytes = Some(vec![0; 4]);
        resources.metadata[1].bytes = Some(layouts.clone());
        resources.metadata(false);
        assert!(resources.metadata[0].error.is_some());
        resources.metadata[0].bytes = Some(gfx.clone());
        resources.parse_retry = Instant::now();
        resources.metadata(false);
        assert!(resources.definitions.is_some());
        assert!(!resources.directory_ready);
        assert!(
            update_resources(&mut resources, &MapData::default(), true)
                .status
                .is_empty(),
            "a recovered definition parse must not leave a stale error while the directory is pending"
        );
        assert!(defs.icons.len() >= 50);
        assert_eq!(defs.layers.len(), 7);
        for name in ["player", "arrow", "home", "death", "marker", "bearing"] {
            assert!(defs.recipe(name).is_some());
        }
        println!(
            "archived GFX: {} recipes, {} resolved regions, {} atlases, {} stamp frames",
            defs.icons.len(),
            defs.regions.len(),
            atlases.len(),
            defs.memos.len()
        );
        if let Ok(path) = std::env::var("EROVERLAY_TPF") {
            let tpf = std::fs::read(path).unwrap();
            let entries = parse_tpf(&tpf).expect("archived common TPF");
            for atlas in atlases.iter().filter(|a| a.source_index.is_none()) {
                let entry = entries
                    .iter()
                    .find(|e| gfx::asset_key(&e.name) == atlas.source)
                    .unwrap_or_else(|| panic!("missing {}", atlas.source));
                let dds = parse_dds(entry.dds).expect("supported DDS");
                println!(
                    "atlas {} {}x{} format={} bytes={}",
                    atlas.source,
                    dds.width,
                    dds.height,
                    dds.format,
                    entry.dds.len()
                );
            }
        }
    }
}
