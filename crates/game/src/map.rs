use crate::{
    Bindings,
    memory::{pointer, read, read_memory, read_pointer},
};
use eroverlay_abi::{DisplayState, GameLayout, MapState};

#[derive(Default, Clone, Copy)]
pub struct MapContext {
    pub menu: usize,
    pub owner: usize,
    pub view: usize,
    pub game_data: usize,
    pub player: usize,
}
pub fn context(base: usize, b: &Bindings) -> Option<MapContext> {
    let menu = pointer(base + b.menu as usize);
    let game_data = pointer(base + b.game_data as usize);
    if menu == 0 || game_data == 0 {
        return None;
    }
    let owner = pointer(menu + b.layout.menu_owner_offset as usize);
    if owner == 0 || read::<u16>(menu + b.layout.screen_state_offset as usize)? != 0 {
        return None;
    }
    let view = pointer(owner + b.layout.owner_view_offset as usize);
    let player = pointer(game_data + 0x58);
    if view == 0 || player == 0 {
        return None;
    }
    Some(MapContext {
        menu,
        owner,
        view,
        game_data,
        player,
    })
}
pub fn unchanged(c: &MapContext, l: &GameLayout) -> bool {
    pointer(c.menu + l.menu_owner_offset as usize) == c.owner
        && pointer(c.owner + l.owner_view_offset as usize) == c.view
        && pointer(c.game_data + 0x58) == c.player
        && read::<u16>(c.menu + l.screen_state_offset as usize) == Some(0)
}
pub fn world_map(view: usize) -> Option<MapState> {
    let mut location = [0u8; 20];
    let mut death = [0u8; 12];
    if view == 0
        || !read_memory(view + 0x24, &mut location)
        || !read_memory(view + 0xac, &mut death)
    {
        return None;
    }
    let x = f32::from_le_bytes(location[4..8].try_into().ok()?);
    let y = f32::from_le_bytes(location[8..12].try_into().ok()?);
    let angle = f32::from_le_bytes(location[16..20].try_into().ok()?);
    if !x.is_finite() || !y.is_finite() || !angle.is_finite() {
        return None;
    }
    let dx = f32::from_le_bytes(death[..4].try_into().ok()?);
    let dy = f32::from_le_bytes(death[4..8].try_into().ok()?);
    Some(MapState {
        raw_map_id: read(view + 0x14)?,
        map_id: i32::from_le_bytes(location[..4].try_into().ok()?),
        x,
        y,
        underground: i32::from(location[12] != 0),
        ori_deg: angle,
        death_valid: u32::from(read::<u8>(view + 0xa9)? != 0 && dx.is_finite() && dy.is_finite()),
        death_x: dx,
        death_y: dy,
        death_map_id: i32::from_le_bytes(death[8..12].try_into().ok()?),
        view_model: view,
        camera_yaw_cos: 1.0,
        ..Default::default()
    })
}
fn type_matches(object: usize, expected: &[u8], primary: bool) -> Option<bool> {
    let table = read_pointer(object)?;
    let locator = read_pointer(table.checked_sub(8)?)?;
    let mut col = [0u8; 24];
    if !read_memory(locator, &mut col) {
        return None;
    }
    let word = |i: usize| u32::from_le_bytes(col[i * 4..i * 4 + 4].try_into().unwrap());
    if word(0) != 1 || (primary && word(1) != 0) || word(5) == 0 || word(3) == 0 {
        return None;
    }
    let address = locator
        .checked_sub(word(5) as usize)?
        .checked_add(word(3) as usize)?
        .checked_add(16)?;
    let mut name = [0u8; 64];
    if expected.len() >= name.len() || !read_memory(address, &mut name[..expected.len() + 1]) {
        return None;
    }
    if name[0..3] != *b".?A" || (name[3] != b'V' && name[3] != b'U') {
        return None;
    }
    Some(name[..expected.len()] == *expected && name[expected.len()] == 0)
}
pub fn display(base: usize, b: &Bindings) -> DisplayState {
    let hidden = DisplayState {
        valid: 0,
        loading_or_logo: 1,
        full_screen_map: 0,
    };
    display_checked(base, b).unwrap_or(hidden)
}
fn display_checked(base: usize, b: &Bindings) -> Option<DisplayState> {
    let menu = read_pointer(base + b.menu as usize)?;
    if menu == 0 {
        return Some(DisplayState {
            valid: 1,
            loading_or_logo: 1,
            full_screen_map: 0,
        });
    }
    let l = &b.layout;
    if !type_matches(menu, b".?AVCSMenuManImp@CS@@", true)?
        || !type_matches(
            menu + l.menu_info_offset as usize,
            b".?AVLoadingScreenData@CS@@",
            true,
        )?
    {
        return None;
    }
    let loading = read::<u16>(menu + l.screen_state_offset as usize)?;
    let controls = menu + l.menu_owner_offset as usize + 0x10;
    let loading_menu = read::<u8>(controls + 6)?;
    let now_loading = read::<u8>(controls + 21)?;
    let world_map = read::<u8>(controls + 61)?;
    let game_data = read_pointer(base + b.game_data as usize)?;
    let player = if game_data == 0 {
        0
    } else {
        read_pointer(game_data + 0x58)?
    };
    let mut state = DisplayState {
        valid: 1,
        loading_or_logo: u32::from(
            loading != 0 || (loading_menu | now_loading) & 1 != 0 || game_data == 0 || player == 0,
        ),
        full_screen_map: u32::from(world_map & 1 != 0),
    };
    let (mut owner, mut view) = (0, 0);
    if state.loading_or_logo == 0 {
        owner = read_pointer(menu + l.menu_owner_offset as usize)?;
        if owner == 0 {
            return None;
        }
        view = read_pointer(owner + l.owner_view_offset as usize)?;
        if view == 0 {
            return None;
        }
        let end = (l.menu_owner_offset as usize).checked_sub(8)?;
        if !(0x30..=0x30 + 16 * 8).contains(&end) {
            return None;
        }
        let count = read::<u64>(menu + end)? as usize;
        if count > (end - 0x30) / 8 {
            return None;
        }
        for i in 0..count {
            let root = read_pointer(menu + 0x30 + i * 8)?;
            if root == 0 {
                return None;
            }
            state.loading_or_logo |= u32::from(type_matches(root, b".?AVLogoMenu@@", false)?);
        }
    }
    if read_pointer(base + b.menu as usize)? != menu
        || read_pointer(base + b.game_data as usize)? != game_data
        || (game_data != 0 && read_pointer(game_data + 0x58)? != player)
        || (owner != 0
            && (read_pointer(menu + l.menu_owner_offset as usize)? != owner
                || read_pointer(owner + l.owner_view_offset as usize)? != view))
    {
        return None;
    }
    Some(state)
}

#[cfg(test)]
mod tests {
    use super::*;
    struct Fixture {
        bytes: Vec<u8>,
        bindings: Bindings,
    }
    impl Fixture {
        fn put_u64(&mut self, offset: usize, value: u64) {
            self.bytes[offset..offset + 8].copy_from_slice(&value.to_le_bytes());
        }
        fn put_u32(&mut self, offset: usize, value: u32) {
            self.bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        }
        fn pointer(&mut self, offset: usize, target: usize) {
            self.put_u64(offset, self.bytes.as_ptr() as u64 + target as u64);
        }
        fn rtti(
            &mut self,
            object: usize,
            table: usize,
            col: usize,
            descriptor: usize,
            name: &[u8],
        ) {
            self.pointer(object, table);
            self.pointer(table - 8, col);
            self.put_u32(col, 1);
            self.put_u32(col + 12, descriptor as u32);
            self.put_u32(col + 20, col as u32);
            self.bytes[descriptor + 16..descriptor + 16 + name.len()].copy_from_slice(name);
        }
        fn new() -> Self {
            let mut f = Self {
                bytes: vec![0u8; 0x10000],
                bindings: Bindings {
                    menu: 0x20,
                    game_data: 0x28,
                    layout: GameLayout {
                        menu_owner_offset: 0x78,
                        owner_view_offset: 0x100,
                        menu_info_offset: 0x708,
                        screen_state_offset: 0x718,
                        ..Default::default()
                    },
                    ..Default::default()
                },
            };
            f.pointer(0x20, 0x1000);
            f.pointer(0x28, 0x2000);
            f.pointer(0x2058, 0x3000);
            f.pointer(0x1078, 0x4000);
            f.pointer(0x4100, 0x5000);
            f.rtti(0x1000, 0x6000, 0x6100, 0x6200, b".?AVCSMenuManImp@CS@@");
            f.rtti(
                0x1708,
                0x7000,
                0x7100,
                0x7200,
                b".?AVLoadingScreenData@CS@@",
            );
            f
        }
        fn state(&self) -> DisplayState {
            display(self.bytes.as_ptr() as usize, &self.bindings)
        }
    }
    #[test]
    fn visibility_only_loading_logo_and_full_map_hide() {
        let mut f = Fixture::new();
        let s = f.state();
        assert_eq!((s.valid, s.loading_or_logo, s.full_screen_map), (1, 0, 0));
        // An unrelated menu-control byte must not hide the minimap.
        f.bytes[0x1000 + 0x78 + 0x10 + 10] = 1;
        assert_eq!(f.state().loading_or_logo, 0);
        f.bytes[0x1000 + 0x78 + 0x10 + 61] = 1;
        assert_eq!(f.state().full_screen_map, 1);
        f.bytes[0x1718] = 1;
        assert_eq!(f.state().loading_or_logo, 1);
        f.bytes[0x1718] = 0;
        f.pointer(0x1030, 0x8000);
        f.put_u64(0x1070, 1);
        f.rtti(0x8000, 0x8100, 0x8200, 0x8300, b".?AVLogoMenu@@");
        assert_eq!(f.state().loading_or_logo, 1);
        f.bytes[0x8310] = b'x';
        assert_eq!(f.state().valid, 0);
        f.put_u64(0x20, usize::MAX as u64);
        assert_eq!(f.state().valid, 0);
    }
    #[test]
    fn world_map_ignores_underground_padding_and_rejects_nan() {
        let mut f = Fixture::new();
        let v = 0x5000;
        f.put_u32(v + 0x24, 10);
        f.put_u32(v + 0x28, 1f32.to_bits());
        f.put_u32(v + 0x2c, 2f32.to_bits());
        f.put_u32(v + 0x30, 0xffff_ff00);
        f.put_u32(v + 0x34, 45f32.to_bits());
        let state = world_map(f.bytes.as_ptr() as usize + v).unwrap();
        assert_eq!(state.underground, 0);
        assert_eq!(state.ori_deg, 45.0);
        f.put_u32(v + 0x28, f32::NAN.to_bits());
        assert!(world_map(f.bytes.as_ptr() as usize + v).is_none());
    }
    #[test]
    fn live_map_recheck_detects_location_and_owner_changes_without_update() {
        let mut f = Fixture::new();
        let base = f.bytes.as_ptr() as usize;
        let mut service = crate::GameServices::with_bindings(base, Some(f.bindings));
        let view = base + 0x5000;
        service.last_view = view;
        service.last_player = base + 0x3000;
        let mut cached = world_map(view).unwrap();
        cached.generation = 42;
        cached.active_masks = [1, 2, 4];
        service.state = Some(cached);
        let initial = service.read_map_state().unwrap();
        assert_eq!(initial.x, 0.0);
        f.put_u32(0x5014, 0x3c010200);
        f.put_u32(0x5028, 123f32.to_bits());
        f.bytes[0x5030] = 1;
        let after = service.read_map_state().unwrap();
        assert_eq!(
            (after.raw_map_id, after.x, after.underground),
            (0x3c010200, 123.0, 1)
        );
        assert_eq!((after.generation, after.active_masks), (42, [1, 2, 4]));
        assert_eq!(service.map_state().unwrap().x, 0.0);
        // A recycled view/player must not inherit cached masks or generation.
        f.pointer(0x4100, 0x5800);
        assert!(service.read_map_state().is_none());
        f.pointer(0x4100, 0x5000);
        f.pointer(0x2058, 0x3800);
        assert!(service.read_map_state().is_none());
        f.pointer(0x2058, 0x3000);
        f.bytes[0x1000 + 0x78 + 0x10 + 61] = 1;
        assert!(service.read_map_state().is_none());
    }
}
