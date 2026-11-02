//! Read-only observations for the offline DLL fixture; absent from installed DLLs.
use crate::{Instance, data::Encounter};
use eroverlay_abi as abi;
use std::{
    collections::{BTreeMap, BTreeSet},
    ffi::c_void,
};

#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct UiProbe {
    pub size: u32,
    pub schema: u32,
    pub sequence: u64,
    pub revision: u64,
    pub raw_map: u32,
    pub current_group: u32,
    pub selected_group: u32,
    pub flags: u32,
    pub group_count: u32,
    pub drawn_count: u32,
    pub catalog_count: u32,
    pub logical_rows: u32,
    pub play_region: u32,
    pub reserved: u32,
    pub search_rect: [f32; 4],
    pub remaining_rect: [f32; 4],
    pub region_rect: [f32; 4],
    pub reload_rect: [f32; 4],
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct UiGroup {
    pub id: u32,
    pub flags: u32,
    pub logical_bosses: u32,
    pub interaction_flags: u32,
    pub header_rect: [f32; 4],
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct UiRow {
    pub flag: u32,
    pub map: u32,
    pub group: u32,
    pub state: u32,
    pub status_rect: [f32; 4],
    pub name_rect: [f32; 4],
    pub interaction_flags: u32,
    pub reserved: u32,
}
#[repr(C)]
#[derive(Clone, Copy, Default)]
pub struct CatalogRow {
    pub flag: u32,
    pub map: u32,
    pub group: u32,
    pub completion_count: u32,
    pub completion: [u32; 4],
}

#[derive(Default)]
pub(crate) struct Layout {
    sequence: u64,
    full: bool,
    interactive: bool,
    pub search_rect: [f32; 4],
    pub remaining_rect: [f32; 4],
    pub region_rect: [f32; 4],
    pub reload_rect: [f32; 4],
    drawn: Vec<UiRow>,
    headers: BTreeMap<u32, ([f32; 4], u32)>,
}
impl Layout {
    pub fn begin(&mut self, sequence: u64, full: bool, interactive: bool) {
        self.sequence = sequence;
        self.full = full;
        self.interactive = interactive;
        self.search_rect = [0.; 4];
        self.remaining_rect = [0.; 4];
        self.region_rect = [0.; 4];
        self.reload_rect = [0.; 4];
        self.drawn.clear();
        self.headers.clear();
    }
    pub fn header(&mut self, group: u32, response: &egui::Response) {
        self.headers
            .insert(group, (rect(response.rect), interaction_flags(response)));
    }
    pub fn boss_row(
        &mut self,
        boss: &Encounter,
        group: u32,
        state: u8,
        status: &egui::Response,
        name: &egui::Response,
    ) {
        self.drawn.push(UiRow {
            flag: boss.flag,
            map: boss.map_id,
            group,
            state: state.into(),
            status_rect: rect(status.rect),
            name_rect: rect(name.rect),
            interaction_flags: interaction_flags(status),
            reserved: 0,
        });
    }
}
pub(crate) fn rect(value: egui::Rect) -> [f32; 4] {
    [value.min.x, value.min.y, value.max.x, value.max.y]
}
fn interaction_flags(response: &egui::Response) -> u32 {
    u32::from(response.hovered())
        | (u32::from(response.has_focus()) << 1)
        | (u32::from(response.clicked()) << 2)
        | (u32::from(response.enabled()) << 3)
}
pub(crate) struct State<'a> {
    pub revision: u64,
    pub raw_map: u32,
    pub current: Option<u32>,
    pub selected: Option<u32>,
    pub remaining: bool,
    pub modal: bool,
    pub closed: &'a BTreeSet<u32>,
    pub logical_rows: usize,
    pub layout: &'a Layout,
}

/// All buffers are caller-owned POD. Counts are returned even when capacity is
/// insufficient; no container, borrowed Rust reference or UI context crosses DLLs.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn eroverlay_test_boss_ui(
    instance: *mut c_void,
    out: *mut UiProbe,
    groups: *mut UiGroup,
    group_cap: usize,
    rows: *mut UiRow,
    row_cap: usize,
    catalog: *mut CatalogRow,
    catalog_cap: usize,
) -> i32 {
    crate::guard(|| {
        if instance.is_null()
            || !instance.cast::<Instance>().is_aligned()
            || out.is_null()
            || !out.is_aligned()
        {
            return abi::ERR_INVALID;
        }
        // SAFETY: the fixture contract provides a readable size word and a live
        // plugin instance; the full output is written only after size validation.
        if unsafe { out.cast::<u32>().read() } < size_of::<UiProbe>() as u32 {
            return abi::ERR_ABI;
        }
        let instance = unsafe { &*instance.cast::<Instance>() };
        let Ok(ui) = instance.ui.try_lock() else {
            return abi::ERR_BUSY;
        };
        let Ok(snapshot) = instance.snapshot.try_lock() else {
            return abi::ERR_BUSY;
        };
        let Some(snapshot) = snapshot.as_ref() else {
            return abi::ERR_UNAVAILABLE;
        };
        let Some(data) = snapshot.catalog.as_ref() else {
            return abi::ERR_UNAVAILABLE;
        };
        let state = ui.probe_state();
        let layout = state.layout;
        let result = UiProbe {
            size: size_of::<UiProbe>() as u32,
            schema: 2,
            sequence: layout.sequence,
            revision: state.revision,
            raw_map: state.raw_map,
            current_group: state.current.unwrap_or(u32::MAX),
            selected_group: state.selected.unwrap_or(u32::MAX),
            flags: u32::from(layout.full)
                | (u32::from(layout.interactive) << 1)
                | (u32::from(state.remaining) << 2)
                | (u32::from(state.modal) << 3),
            group_count: data.groups.len() as u32,
            drawn_count: layout.drawn.len() as u32,
            catalog_count: data.rows.len() as u32,
            logical_rows: state.logical_rows as u32,
            play_region: snapshot.region.unwrap_or(u32::MAX),
            reserved: 0,
            search_rect: layout.search_rect,
            remaining_rect: layout.remaining_rect,
            region_rect: layout.region_rect,
            reload_rect: layout.reload_rect,
        };
        unsafe { out.write(result) };
        if group_cap < data.groups.len()
            || row_cap < layout.drawn.len()
            || catalog_cap < data.rows.len()
            || (!data.groups.is_empty() && (groups.is_null() || !groups.is_aligned()))
            || (!layout.drawn.is_empty() && (rows.is_null() || !rows.is_aligned()))
            || (!data.rows.is_empty() && (catalog.is_null() || !catalog.is_aligned()))
            || data
                .rows
                .iter()
                .any(|r| r.completion.len() > 4 || r.group >= data.groups.len())
        {
            return abi::ERR_INVALID;
        }
        for (i, group) in data.groups.iter().enumerate() {
            let header = layout.headers.get(&group.id);
            let value = UiGroup {
                id: group.id,
                flags: u32::from(!state.closed.contains(&group.id))
                    | (u32::from(state.current == Some(group.id)) << 1)
                    | (u32::from(header.is_some()) << 2),
                logical_bosses: data.rows.iter().filter(|row| row.group == i).count() as u32,
                interaction_flags: header.map_or(0, |h| h.1),
                header_rect: header.map_or([0.; 4], |h| h.0),
            };
            unsafe { groups.add(i).write(value) };
        }
        for (i, row) in layout.drawn.iter().enumerate() {
            unsafe { rows.add(i).write(*row) };
        }
        for (i, row) in data.rows.iter().enumerate() {
            let mut completion = [0; 4];
            completion[..row.completion.len()].copy_from_slice(&row.completion);
            unsafe {
                catalog.add(i).write(CatalogRow {
                    flag: row.flag,
                    map: row.map_id,
                    group: data.groups[row.group].id,
                    completion_count: row.completion.len() as u32,
                    completion,
                })
            };
        }
        abi::OK
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn observation_pods_have_fixed_c_layouts() {
        assert_eq!(size_of::<UiProbe>(), 128);
        assert_eq!(size_of::<UiGroup>(), 32);
        assert_eq!(size_of::<UiRow>(), 56);
        assert_eq!(size_of::<CatalogRow>(), 32);
    }
}
