//! Native game ABI discovery, preserving afab7e1's scanner contracts.
use super::{
    PeImage,
    rules::{ROLE_NAMES, ROLES, RULES, Role},
};
use crate::Bindings;

#[derive(Clone, Copy)]
struct Range {
    start: u32,
    end: u32,
    root: u32,
    unwind: u32,
}
struct Scanner<'a, 'b> {
    image: &'a PeImage<'b>,
    text_rva: u32,
    text_len: usize,
    data_rva: u32,
    data_len: usize,
    ranges: Vec<Range>,
    functions: [u32; Role::Count as usize],
}
type Result<T> = std::result::Result<T, String>;
impl<'a, 'b> Scanner<'a, 'b> {
    fn new(image: &'a PeImage<'b>) -> Self {
        let (text_rva, text) = image.section(".text").unwrap_or((0, &[]));
        let (data_rva, data) = image.section(".data").unwrap_or((0, &[]));
        Self {
            image,
            text_rva,
            text_len: text.len(),
            data_rva,
            data_len: data.len(),
            ranges: Vec::new(),
            functions: [0; Role::Count as usize],
        }
    }
    fn function(&self, role: Role) -> u32 {
        self.functions[role as usize]
    }
    fn data_contains(&self, address: u32, size: usize) -> bool {
        address >= self.data_rva
            && (address - self.data_rva) as u64 + size as u64 <= self.data_len as u64
    }
    fn unique(&self, mut values: Vec<u32>, name: &str) -> Result<u32> {
        values.sort_unstable();
        values.dedup();
        if values.len() == 1 && values[0] != 0 {
            return Ok(values[0]);
        }
        let mut message = format!(
            "minimap signature missing or ambiguous: {name} (candidates={})",
            values.len()
        );
        for value in values.iter().take(4) {
            message.push_str(&format!(" {value}"));
        }
        Err(message)
    }
    fn load_ranges(&mut self) -> Result<()> {
        let pdata = self
            .image
            .section(".pdata")
            .map(|(_, bytes)| bytes)
            .unwrap_or(&[]);
        if self.text_len == 0 || pdata.is_empty() {
            return Err("minimap PE code/unwind sections are missing".into());
        }
        for entry in pdata.as_chunks::<12>().0 {
            let word = |i: usize| u32::from_le_bytes(entry[i * 4..i * 4 + 4].try_into().unwrap());
            let (start, end, unwind) = (word(0), word(1), word(2));
            if start >= end || !self.image.executable(start) || !self.image.executable(end - 1) {
                continue;
            }
            self.ranges.push(Range {
                start,
                end,
                root: start,
                unwind,
            });
        }
        self.ranges.sort_unstable_by_key(|r| r.start);
        if self.ranges.windows(2).any(|r| r[0].end > r[1].start) {
            return Err("minimap unwind ranges overlap".into());
        }
        for i in 0..self.ranges.len() {
            let mut entry = self.ranges[i].start;
            for depth in 0..=16 {
                let current = self
                    .ranges
                    .binary_search_by_key(&entry, |r| r.start)
                    .ok()
                    .map(|n| self.ranges[n]);
                let header = current.and_then(|r| self.image.bytes(r.unwind, 4));
                if depth == 16 || header.is_none() {
                    entry = 0;
                    break;
                }
                let header = header.unwrap();
                if header[0] & (4 << 3) == 0 {
                    break;
                }
                let chain = current.unwrap().unwind as u64 + 4 + ((header[2] as u64 + 1) & !1) * 2;
                let parent = if chain <= u32::MAX as u64 {
                    self.image.bytes(chain as u32, 12)
                } else {
                    None
                };
                let parent = parent
                    .map(|p| u32::from_le_bytes(p[..4].try_into().unwrap()))
                    .unwrap_or(0);
                if parent == 0 || !self.image.executable(parent) {
                    entry = 0;
                    break;
                }
                if parent == entry {
                    break;
                }
                entry = parent;
            }
            self.ranges[i].root = entry;
        }
        if self.ranges.is_empty() {
            Err("minimap unwind ranges are missing".into())
        } else {
            Ok(())
        }
    }
    fn root(&self, address: u32) -> u32 {
        let n = self.ranges.partition_point(|r| r.start <= address);
        n.checked_sub(1)
            .and_then(|i| self.ranges.get(i))
            .filter(|r| address < r.end)
            .map_or(0, |r| r.root)
    }
    fn scan(&self, pattern: &str) -> Vec<u32> {
        self.image.scan_all(
            pattern,
            self.text_rva,
            self.text_rva.saturating_add(self.text_len as u32),
        )
    }
    fn local(&self, role: Role, patterns: &[&str]) -> Vec<u32> {
        let mut hits = Vec::new();
        for range in self.ranges.iter().filter(|r| r.root == self.function(role)) {
            for pattern in patterns {
                hits.extend(self.image.scan_all(pattern, range.start, range.end));
            }
        }
        hits
    }
    fn required(&self, name: &str, role: Role, patterns: &[&str]) -> Result<Vec<u32>> {
        let hits = self.local(role, patterns);
        if hits.is_empty() {
            Err(format!("minimap field contract changed: {name}"))
        } else {
            Ok(hits)
        }
    }
    fn value(&self, address: u32, offset: u32, byte: bool) -> Result<u32> {
        let at = address
            .checked_add(offset)
            .ok_or("minimap instruction operand is truncated")?;
        (if byte {
            self.image.read_u8(at).map(u32::from)
        } else {
            self.image.read_u32(at)
        })
        .ok_or_else(|| "minimap instruction operand is truncated".into())
    }
    fn operand(&self, hits: Vec<u32>, offset: u32, name: &str, byte: bool) -> Result<u32> {
        self.unique(
            hits.into_iter()
                .map(|hit| self.value(hit, offset, byte))
                .collect::<Result<Vec<_>>>()?,
            name,
        )
    }
    fn find_functions(&mut self) -> Result<()> {
        for &role in ROLES {
            let mut candidates = Vec::new();
            for rule in RULES.iter().filter(|r| r.role == role) {
                let first = self.scan(rule.anchors[0].0);
                let second = self.scan(rule.anchors[1].0);
                for hit in first {
                    let entry = if role == Role::EventStorage {
                        if rule.anchors[0].1 != 0
                            || !second.contains(&hit.saturating_add(rule.anchors[1].1))
                        {
                            continue;
                        }
                        hit
                    } else {
                        let entry = self.root(hit);
                        if entry == 0 || !second.iter().any(|&address| self.root(address) == entry)
                        {
                            continue;
                        }
                        entry
                    };
                    if rule
                        .body
                        .is_some_and(|body| !self.image.matches(entry, body))
                    {
                        continue;
                    }
                    candidates.push(entry);
                }
            }
            if matches!(role, Role::CommonParam | Role::PieceParam) {
                let hits=self.scan(if role==Role::CommonParam {"48 8B 0D ?? ?? ?? ?? 48 85 C9 0F 84 ?? ?? ?? ?? 45 33 C0 BA 8D 00 00 00 E8 ?? ?? ?? ??"}else{"48 8B 0D ?? ?? ?? ?? 45 33 C0 41 8D 50 58 E8 ?? ?? ?? ??"});
                candidates.retain(|&entry| hits.iter().any(|&address| self.root(address) == entry));
            }
            self.functions[role as usize] = self.unique(candidates, ROLE_NAMES[role as usize])?;
        }
        Ok(())
    }
    fn call(&self, caller: Role, callee: Role) -> Result<()> {
        for rule in RULES.iter().filter(|r| r.role == caller) {
            if !rule
                .body
                .is_some_and(|body| self.image.matches(self.function(caller), body))
            {
                continue;
            }
            if rule.calls.iter().any(|offset| {
                self.image
                    .relative(self.function(caller).saturating_add(*offset), 1, 5)
                    == self.function(callee)
            }) {
                return Ok(());
            }
        }
        Err(format!(
            "minimap call contract changed: {} -> {}",
            ROLE_NAMES[caller as usize], ROLE_NAMES[callee as usize]
        ))
    }
    fn references_vtable(&self, entry: u32, name: &str) -> bool {
        let table = self.image.vtable(name);
        table != 0
            && self.ranges.iter().filter(|r| r.root == entry).any(|range| {
                ["48 8D ?? ?? ?? ?? ??", "4C 8D ?? ?? ?? ?? ??"]
                    .iter()
                    .any(|pattern| {
                        self.image
                            .scan_all(pattern, range.start, range.end)
                            .iter()
                            .any(|&hit| {
                                self.image
                                    .read_u8(hit + 2)
                                    .is_some_and(|b| b & 0xc7 == 0x05)
                                    && self.image.relative_address(hit, 3, 7) == table
                            })
                    })
            })
    }
    fn extract(&self) -> Result<Bindings> {
        let mut result = Bindings {
            submit: self.function(Role::Submit),
            flush: self.function(Role::Flush),
            enqueue: self.function(Role::Enqueue),
            cancel: self.function(Role::Cancel),
            ..Default::default()
        };
        let hooks = [
            "48 8B 0D ?? ?? ?? ?? 48 8B 49 08 E8 ?? ?? ?? ?? 48 8B D0 48 8B CE E8",
            "48 8B 05 ?? ?? ?? ?? 48 85 C0 74 05 48 8B 40 58 C3 C3",
            "48 8B 3D ?? ?? ?? ?? 48 85 FF ?? ?? 32 C0 E9",
            "48 8B 0D ?? ?? ?? ?? 48 ?? ?? ?? 44 0F B6 61 ?? E8 ?? ?? ?? ?? 48 63 87 ?? ?? ?? ?? 48 ?? ?? ?? 48 85 C0",
        ];
        let mut globals = [0; 4];
        for (i, hook) in hooks.iter().enumerate() {
            let hit = self.unique(self.scan(hook), "game global")?;
            globals[i] = self.image.relative_address(hit, 3, 7);
            if globals[i] == 0 {
                return Err("minimap game global reference is invalid".into());
            }
        }
        [
            result.menu,
            result.game_data,
            result.event_flags,
            result.field,
        ] = globals;
        let mut hit = self.unique(
            self.local(
                Role::Cancel,
                &["48 8B 15 ?? ?? ?? ?? 48 85 D2 74 ?? 48 8B 82 40 01 00 00"],
            ),
            "file manager",
        )?;
        result.manager = self.image.relative_address(hit, 3, 7);
        hit = self.unique(
            self.local(
                Role::Complete,
                &["48 8B 05 ?? ?? ?? ?? 0F B7 D1 4C 8B 00 49 8B 40 50 48 C1 E9 10"],
            ),
            "pool owner completion",
        )?;
        result.retire_pool_owner = self.image.relative_address(hit, 3, 7);
        hit=self.unique(self.local(Role::Cleanup,&["48 8B 1D ?? ?? ?? ?? 48 8B D7 48 8B CB E8 ?? ?? ?? ?? 48 8B 57 28 48 8B 0B E8 ?? ?? ?? ??"]),"pool owner cleanup")?;
        if result.retire_pool_owner == 0
            || self.image.relative_address(hit, 3, 7) != result.retire_pool_owner
            || self.image.relative(hit + 25, 1, 5) != self.function(Role::Retire)
        {
            return Err("minimap request retirement contract changed".into());
        }
        hit = self.unique(
            self.local(
                Role::AllocatorAnchor,
                &["4C 8B 05 ?? ?? ?? ?? 4C 89 45 B7 41 8D 54 24 08 B9 B0 00 00 00"],
            ),
            "allocator first load",
        )?;
        result.allocator = self.image.relative_address(hit, 3, 7);
        hit = self.unique(
            self.local(
                Role::AllocatorAnchor,
                &["48 8B 0D ?? ?? ?? ?? 48 89 4D 97 4C 89 6D 8F"],
            ),
            "allocator second load",
        )?;
        if result.allocator == 0 || self.image.relative_address(hit, 3, 7) != result.allocator {
            return Err("minimap allocator references disagree".into());
        }
        hit = self.unique(
            self.local(Role::Allocate, &["49 8B CF E8 ?? ?? ?? ?? 48 85 C0 75 12"]),
            "request factory call",
        )?;
        let factory = self.image.relative(hit + 3, 1, 5);
        const FACTORY: &str = concat!(
            "40 57 48 83 EC 30 48 C7 44 24 20 FE FF FF FF 48 89 5C 24 40 48 89 74 24 58 48 8B F1 48 8B 3D ?? ?? ?? ?? 48 89 7C 24 48 4C 8B C7 BA 08 00 00 00 B9 10 01 00 00 ",
            "E8 ?? ?? ?? ?? 48 89 44 24 50 48 85 C0 74 0D 48 8B C8 E8 ?? ?? ?? ?? 48 8B D8 EB 02 33 DB 48 8B D6 48 8B CB E8 ?? ?? ?? ?? 84 C0 75 22 48 85 DB 74 19 4C 8B 03 ",
            "33 D2 48 8B CB 41 FF 50 30 4C 8B 07 48 8B D3 48 8B CF 41 FF 50 68 33 C0 EB 03 48 8B C3 48 8B 5C 24 40 48 8B 74 24 58 48 83 C4 30 5F C3"
        );
        if factory == 0
            || !self.image.matches(factory, FACTORY)
            || self.image.relative(factory + 0x59, 1, 5) != self.function(Role::RequestInit)
        {
            return Err("minimap request factory/initializer contract changed".into());
        }
        if !self.data_contains(
            self.image.relative_address(factory + 0x1c, 3, 7),
            size_of::<usize>(),
        ) {
            return Err("minimap request factory allocator is outside the data section".into());
        }
        let common=self.unique(self.local(Role::CommonParam,&["48 8B 0D ?? ?? ?? ?? 48 85 C9 0F 84 ?? ?? ?? ?? 45 33 C0 BA 8D 00 00 00 E8 ?? ?? ?? ??"]),"common param")?;
        let piece = self.unique(
            self.local(
                Role::PieceParam,
                &["48 8B 0D ?? ?? ?? ?? 45 33 C0 41 8D 50 58 E8 ?? ?? ?? ??"],
            ),
            "piece param",
        )?;
        result.repository = self.image.relative_address(common, 3, 7);
        let getter = self.image.relative(common + 24, 1, 5);
        if result.repository==0 || result.repository!=self.image.relative_address(piece,3,7) || getter==0 || getter!=self.image.relative(piece+14,1,5) || !self.image.matches(getter,"81 FA ?? ?? ?? ?? 7D 24 48 63 D2 48 8D 04 D2 44 3B 84 C1 80 00 00 00 73 13 41 8B C0 48 8D 14 D2 48 03 D0 48 8B 84 D1 88 00 00 00 C3 33 C0 C3"){return Err("minimap parameter repository getter contract changed".into());}
        let layout = &mut result.layout;
        layout.size = size_of::<crate::GameLayout>() as u32;
        layout.repository_groups = self.value(getter, 2, false)?;
        if layout.repository_groups <= 141
            || layout.repository_groups > 4096
            || self
                .local(
                    Role::CommonParam,
                    &["48 8B 80 80 00 00 00 48 8B 90 80 00 00 00"],
                )
                .is_empty()
            || self
                .local(
                    Role::PieceParam,
                    &["48 8B 80 80 00 00 00 48 8B 90 80 00 00 00"],
                )
                .is_empty()
        {
            return Err("minimap parameter entry layout changed".into());
        }
        if !self.references_vtable(self.function(Role::ViewCtor), ".?AVWorldMapViewModel@CS@@")
            || !self.references_vtable(
                self.function(Role::GraceCtor),
                ".?AVWorldMapWarpPinData@CS@@",
            )
        {
            return Err("minimap constructor RTTI disagrees".into());
        }
        let mut creates=self.scan(concat!("48 83 BB ?? ?? ?? ?? 00 75 ?? 4C 8B 05 ?? ?? ?? ?? 4C 89 44 24 40 BA 08 00 00 00 B9 ?? ?? ?? ?? E8 ?? ?? ?? ?? 48 89 44 24 48 48 85 C0 74 ?? 48 8B 93 ","?? ?? ?? ?? 48 8B C8 E8 ?? ?? ?? ?? 90 48 89 83 ?? ?? ?? ??"));
        creates.retain(|&address| {
            self.image.relative(address + 57, 1, 5) == self.function(Role::ViewCtor)
        });
        hit = self.unique(creates, "map view creation")?;
        let create = self.root(hit);
        layout.owner_view_offset = self.value(hit, 3, false)?;
        if create == 0
            || layout.owner_view_offset != self.value(hit, 66, false)?
            || layout.owner_view_offset == 0
            || layout.owner_view_offset > 4096
            || self.value(hit, 28, false)? < 0x3c8
        {
            return Err("minimap map view allocation contract changed".into());
        }
        let mut owners = Vec::new();
        for (pattern, offset) in [
            (
                "48 8B 4B ?? 48 85 C9 74 05 E8 ?? ?? ?? ?? 48 8B 4B 08 E8 ?? ?? ?? ??",
                9,
            ),
            (
                "48 8B 8B ?? ?? ?? ?? 48 85 C9 74 05 E8 ?? ?? ?? ?? 48 8B 4B 08 E8 ?? ?? ?? ??",
                12,
            ),
        ] {
            for address in self.scan(pattern) {
                if self.image.relative(address + offset, 1, 5) == create {
                    owners.push(self.value(address, 3, offset == 9)?);
                }
            }
        }
        layout.menu_owner_offset = self.unique(owners, "menu owner member")?;
        let mut menu = self.scan(
            "40 88 BB ?? ?? ?? ?? 48 8D 8B ?? ?? ?? ?? E8 ?? ?? ?? ?? 90 48 89 BB ?? ?? ?? ??",
        );
        menu.retain(|&address| self.references_vtable(self.root(address), ".?AVCSMenuManImp@CS@@"));
        hit = self.unique(menu, "loading data construction")?;
        layout.menu_info_offset = self.value(hit, 10, false)?;
        let loading_ctor = self.image.relative(hit + 14, 1, 5);
        let mut loading_offsets = Vec::new();
        for address in
            self.image
                .scan_all("66 89 41 ??", loading_ctor, loading_ctor.saturating_add(49))
        {
            loading_offsets.push(self.value(address, 3, true)?);
        }
        for address in self.image.scan_all(
            "66 89 81 ?? ?? ?? ??",
            loading_ctor,
            loading_ctor.saturating_add(49),
        ) {
            loading_offsets.push(self.value(address, 3, false)?);
        }
        layout.screen_state_offset = layout
            .menu_info_offset
            .checked_add(self.unique(loading_offsets, "loading state member")?)
            .ok_or("minimap loading state offset overflow")?;
        layout.grace_stride = self.operand(
            self.local(
                Role::ViewCtor,
                &[
                    "48 69 D7 ?? ?? ?? ?? 48 03 53 08",
                    "48 69 D6 ?? ?? ?? ?? 48 03 57 08",
                ],
            ),
            3,
            "grace stride multiply",
            false,
        )?;
        if layout.grace_stride
            != self.operand(
                self.local(
                    Role::ViewCtor,
                    &["48 81 43 10 ?? ?? ?? ??", "48 81 47 10 ?? ?? ?? ??"],
                ),
                4,
                "grace stride increment",
                false,
            )?
        {
            return Err("minimap grace strides disagree".into());
        }
        let mut normal_offsets = Vec::new();
        for address in self.local(
            Role::GraceCtor,
            &["C6 86 ?? ?? ?? ?? 00 C7 86 ?? ?? ?? ?? FF FF FF FF C6 86 ?? ?? ?? ?? 01"],
        ) {
            normal_offsets.push(self.value(address, 19, false)?);
        }
        for address in self.local(
            Role::GraceCtor,
            &["41 C6 86 ?? ?? ?? ?? 00 41 C7 86 ?? ?? ?? ?? FF FF FF FF 41 C6 86 ?? ?? ?? ?? 01"],
        ) {
            normal_offsets.push(self.value(address, 22, false)?);
        }
        layout.grace_normal_offset = self.unique(normal_offsets, "grace normal member")?;
        if layout.grace_normal_offset.checked_add(8) != Some(layout.grace_stride)
            || layout.grace_stride < 0x2d0
            || layout.grace_stride > 4096
        {
            return Err("minimap grace terminal field disagrees".into());
        }
        let alt_normal = !self
            .local(Role::GraceCtor, &["89 86 C8 02 00 00"])
            .is_empty();
        let alt_forbidden = !self
            .local(Role::GraceCtor, &["89 86 08 03 00 00"])
            .is_empty();
        if alt_normal != alt_forbidden {
            return Err("minimap alternate grace icon fields disagree".into());
        }
        layout.alternate_icons = u32::from(alt_normal);
        if self
            .local(Role::ViewCtor, &["BA 3C 00 00 00 44 8D 4A 04 44 8D 42 E0"])
            .is_empty()
        {
            return Err("minimap surface converter is missing".into());
        }
        layout.map_mask = if self
            .local(Role::ViewCtor, &["BA 3D 00 00 00 44 8D 4A 03 44 8D 42 DF"])
            .is_empty()
        {
            3
        } else {
            7
        };
        hit = self.unique(
            self.local(
                Role::ViewCtor,
                &[
                    "0F B6 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 89 07",
                    "0F B6 0D ?? ?? ?? ?? E8 ?? ?? ?? ?? 89 03",
                ],
            ),
            "map reveal",
        )?;
        result.reveal = self.image.relative_address(hit, 3, 7);
        if self.image.relative(hit + 7, 1, 5) != self.function(Role::PieceMasks) {
            return Err("minimap reveal/piece-mask call disagrees".into());
        }
        for (caller, callee) in [
            (Role::Submit, Role::Allocate),
            (Role::Allocate, Role::PendingInsert),
            (Role::Allocate, Role::Retire),
            (Role::Flush, Role::Enqueue),
            (Role::Complete, Role::Cleanup),
            (Role::Convert, Role::LegacyLookup),
        ] {
            self.call(caller, callee)?;
        }
        let event_reader_called=RULES.iter().filter(|rule|rule.role==Role::PieceMasks && rule.body.is_some_and(|body|self.image.matches(self.function(Role::PieceMasks),body))).any(|rule|rule.calls.iter().any(|offset|{
            let reader=self.image.relative(self.function(Role::PieceMasks)+offset,1,5);
            reader!=0 && self.image.matches(reader,"48 83 EC 28 8B 12 85 D2 74 0F E8 ?? ?? ?? ?? 85 C0 0F 95 C0 48 83 C4 28 C3 32 C0 48 83 C4 28 C3") && self.image.relative(reader+10,1,5)==self.function(Role::EventStorage)
        }));
        if !event_reader_called {
            return Err("minimap event-storage reader/caller contract changed".into());
        }
        Ok(result)
    }
    fn guard_fields(&self) -> Result<()> {
        self.required(
            "player raw/map",
            Role::Player,
            &["89 47 14 C7 47 24 FF FF FF FF"],
        )?;
        self.required(
            "player XY",
            Role::Player,
            &["48 89 47 28 89 4F 24", "48 89 47 28 89 5F 24"],
        )?;
        self.required(
            "player underground",
            Role::Player,
            &["80 7F 17 0C 0F 94 C0 88 47 30"],
        )?;
        self.required(
            "player angle",
            Role::Player,
            &["F3 0F 11 4F 34", "F3 0F 11 47 34"],
        )?;
        let converted = self.required(
            "death XY",
            Role::Death,
            &["48 8D 97 AC 00 00 00 E8 ?? ?? ?? ??"],
        )?;
        if !converted
            .iter()
            .any(|address| self.image.relative(address + 7, 1, 5) == self.function(Role::Convert))
        {
            return Err("minimap death converter call changed".into());
        }
        self.required(
            "death validity/map",
            Role::Death,
            &[
                "88 97 A9 00 00 00 44 89 BF B4 00 00 00",
                "88 8F A9 00 00 00 44 89 BF B4 00 00 00",
            ],
        )?;
        self.required("death home map", Role::Death, &["8B 91 78 02 00 00"])?;
        self.required(
            "beacon slots/capacity",
            Role::MarkerSaveBind,
            &["48 89 51 08 48 89 59 10"],
        )?;
        self.required(
            "beacon active count",
            Role::MarkerSaveBind,
            &["49 83 7E 40 00"],
        )?;
        self.required(
            "beacon record stride",
            Role::MarkerAdd,
            &["48 C1 E0 04", "48 C1 FB 04"],
        )?;
        self.required(
            "beacon icon",
            Role::MarkerNumber,
            &["48 63 4F 18 48 03 C9 48 8B 47 10 0F B6 54 C8 0D"],
        )?;
        self.required(
            "beacon slot number",
            Role::MarkerNumber,
            &["44 8B 47 18 41 FF C0"],
        )?;
        let adds=self.required("beacon record",Role::MarkerListAdd,&["88 54 24 44 49 8B 00 48 89 44 24 3C 44 88 4C 24 45 48 8D 54 24 38 E8 ?? ?? ?? ?? 8B F0 85 C0"])?;
        if !adds.iter().any(|address| {
            self.image.relative(address + 22, 1, 5) == self.function(Role::MarkerAdd)
        }) {
            return Err("minimap beacon list/add call changed".into());
        }
        self.required(
            "beacon save binding",
            Role::MarkerBind,
            &["48 89 51 38 48 83 C1 08 E8 ?? ?? ?? ??"],
        )?;
        let binds = self.required(
            "beacon view binding",
            Role::ViewCtor,
            &[
                "49 8D 8F 00 03 00 00 E8 ?? ?? ?? ??",
                "49 8D 8E 00 03 00 00 E8 ?? ?? ?? ??",
            ],
        )?;
        if !binds.iter().any(|address| {
            self.image.relative(address + 7, 1, 5) == self.function(Role::MarkerBind)
        }) {
            return Err("minimap beacon view/binder call changed".into());
        }
        let limits = self.required(
            "beacon limit",
            Role::MarkerPlace,
            &["FF C0 39 05 ?? ?? ?? ??"],
        )?;
        let addresses = limits
            .iter()
            .map(|address| self.image.relative_address(address + 2, 2, 6))
            .collect();
        if self.value(self.unique(addresses, "beacon limit global")?, 0, false)? != 5 {
            return Err("minimap beacon placement limit changed".into());
        }
        self.required(
            "beacon numbered icon",
            Role::MarkerPlace,
            &["41 B9 01 00 00 00"],
        )?;
        let saves = self.required(
            "beacon reserved slots",
            Role::MarkerSaveCtor,
            &["41 B8 0A 00 00 00 48 8D 8F ?? ?? ?? ?? E8 ?? ?? ?? ??"],
        )?;
        if saves.iter().any(|address| {
            self.image.relative(address + 13, 1, 5) != self.function(Role::MarkerSaveBind)
        }) {
            return Err("minimap beacon save/binder call changed".into());
        }
        self.required(
            "converter count",
            Role::Death,
            &["48 3B 9F 80 02 00 00", "48 39 9F 80 02 00 00"],
        )?;
        self.required("converter array", Role::Death, &["4C 8D A7 F8 00 00 00"])?;
        self.required("converter stride", Role::Death, &["48 83 C6 30"])?;
        self.required(
            "grace identity",
            Role::GraceCtor,
            &[
                "41 0F 10 06 0F 11 86 38 02 00 00",
                "41 0F 10 07 41 0F 11 86 38 02 00 00",
            ],
        )?;
        self.required(
            "grace base icon",
            Role::GraceCtor,
            &["89 86 48 02 00 00", "41 89 86 48 02 00 00"],
        )?;
        self.required(
            "grace forbidden icon",
            Role::GraceCtor,
            &["89 86 88 02 00 00", "41 89 86 88 02 00 00"],
        )?;
        Ok(())
    }
}
pub(super) fn resolve(image: &PeImage<'_>) -> Result<Bindings> {
    let mut scanner = Scanner::new(image);
    scanner.load_ranges()?;
    scanner.find_functions()?;
    let result = scanner.extract()?;
    scanner.guard_fields()?;
    for address in [
        result.manager,
        result.allocator,
        result.retire_pool_owner,
        result.menu,
        result.game_data,
        result.event_flags,
        result.field,
        result.repository,
    ] {
        if !scanner.data_contains(address, size_of::<usize>()) {
            return Err("minimap global is outside the data section".into());
        }
    }
    if !scanner.data_contains(result.reveal, 1) {
        return Err("minimap reveal flag is outside the data section".into());
    }
    Ok(result)
}
