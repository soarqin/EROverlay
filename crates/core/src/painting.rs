use crate::{STAGING, plugin::Plugin, staging::Staging};
use eroverlay_abi::{ERR_BUSY, FrameInfo, OK};

pub(super) struct Packet {
    pub staging: Staging,
    pub valid: bool,
    pub fresh: bool,
    viewport: [f32; 2],
    sequence: Option<u64>,
}
impl Packet {
    fn new() -> Self {
        Self {
            staging: Staging::default(),
            valid: false,
            fresh: false,
            viewport: [0.0; 2],
            sequence: None,
        }
    }
}

/// One complete core-owned packet per plugin. Neither borrowed pointers nor
/// partial callback output replace the previous successfully published packet.
pub(super) struct Painting {
    pub packets: Vec<Packet>,
    scratch: Staging,
    pub generation: u64,
}
impl Painting {
    pub fn new(count: usize) -> Self {
        Self {
            packets: (0..count).map(|_| Packet::new()).collect(),
            scratch: Staging::default(),
            generation: 0,
        }
    }
    pub fn reset(&mut self, generation: u64) {
        self.generation = generation;
        for packet in &mut self.packets {
            packet.staging.clear();
            packet.valid = false;
            packet.sequence = None;
        }
    }
    pub fn prepare(&mut self, index: usize, plugin: &Plugin, frame: &FrameInfo) -> i32 {
        let packet = &mut self.packets[index];
        packet.fresh = false;
        let viewport = [frame.width, frame.height];
        if packet.viewport != viewport {
            packet.viewport = viewport;
            packet.valid = false;
        }
        if packet.sequence.is_some_and(|last| last >= frame.sequence) {
            return ERR_BUSY;
        }
        self.scratch.clear();
        STAGING.with(|slot| slot.set(&mut self.scratch));
        struct Clear;
        impl Drop for Clear {
            fn drop(&mut self) {
                STAGING.with(|slot| slot.set(std::ptr::null_mut()));
            }
        }
        let _clear = Clear;
        let code = plugin.render(frame);
        // A duplicate callback may replay the committed packet/image, but must
        // not advance input/egui again after a callback has already attempted it.
        packet.sequence = Some(frame.sequence);
        if code == OK {
            std::mem::swap(&mut packet.staging, &mut self.scratch);
            packet.valid = true;
            packet.fresh = true;
        } else if code != ERR_BUSY {
            // An invalid/panicking callback is not a temporary lock miss.
            packet.valid = false;
        }
        code
    }
}
