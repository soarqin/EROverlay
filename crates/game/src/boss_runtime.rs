//! Boss save state and opt-in revival, using the existing scanned game contracts.
use crate::{
    GameServices, map,
    memory::{pointer, read},
};
use eroverlay_abi::{BossState, FlagWrite};
use std::sync::atomic::{AtomicU8, Ordering};

impl GameServices {
    pub fn boss_state(&mut self) -> Option<BossState> {
        let b = self.bindings?;
        let display = self.read_display_state();
        if display.valid == 0 || display.loading_or_logo != 0 || display.full_screen_map != 0 {
            return None;
        }
        let c = map::context(self.base, &b)?;
        let map = map::world_map(c.view)?;
        let in_game_time_ms: u32 = read(c.game_data + 0xa0)?;
        let deaths: u32 = read(c.game_data + 0x94)?;
        if in_game_time_ms == 0 || in_game_time_ms > 3_599_999_999 || deaths > i32::MAX as u32 {
            return None;
        }
        let manager = pointer(self.base + b.event_flags as usize);
        let mut graveyard = [2];
        self.flags.read_many(manager, &[101], &mut graveyard);
        let graveyard_reached = u32::from(graveyard[0]);
        if !map::unchanged(&c, &b.layout) || pointer(self.base + b.event_flags as usize) != manager
        {
            return None;
        }
        // This token is a context identity, not an address exposed for dereferencing.
        let context = (c.game_data as u64).rotate_left(11)
            ^ (c.player as u64).rotate_left(27)
            ^ (manager as u64).rotate_left(43)
            ^ self.generation;
        Some(BossState {
            size: size_of::<BossState>() as u32,
            valid: 1,
            context,
            raw_map_id: map.raw_map_id,
            deaths,
            in_game_time_ms,
            graveyard_reached,
        })
    }

    pub fn boss_flags(&mut self, ids: &[u32], out: &mut [u8]) -> bool {
        if ids.len() != out.len() || ids.len() > 4096 {
            return false;
        }
        out.fill(2);
        let Some(before) = self.boss_state() else {
            return false;
        };
        let Some(b) = self.bindings else {
            return false;
        };
        let manager = pointer(self.base + b.event_flags as usize);
        self.flags.read_many(manager, ids, out);
        if self
            .boss_state()
            .is_none_or(|s| s.context != before.context)
        {
            out.fill(2);
            return false;
        }
        true
    }

    pub fn revive_boss(&mut self, context: u64, primary: u32, extras: &[FlagWrite]) -> bool {
        if primary == 0 || primary == u32::MAX || extras.len() > 2048 {
            return false;
        }
        let Some(before) = self.boss_state().filter(|s| s.context == context) else {
            return false;
        };
        let Some(b) = self.bindings else {
            return false;
        };
        let manager = pointer(self.base + b.event_flags as usize);
        let mut targets = Vec::with_capacity(extras.len() + 1);
        for f in extras {
            if f.id == 0 || f.id == u32::MAX || f.value > 1 || f.id == primary {
                return false;
            }
            if let Some((_, value)) = targets.iter().find(|(id, _)| *id == f.id) {
                if *value != (f.value != 0) {
                    return false;
                }
            } else {
                targets.push((f.id, f.value != 0));
            }
        }
        targets.push((primary, false));
        let Some(writes) = self.flags.prepare_writes(manager, &targets) else {
            return false;
        };
        let Some((address, mask, _)) = writes.last().copied() else {
            return false;
        };
        if read::<u8>(address).is_none_or(|byte| byte & mask == 0)
            || self
                .boss_state()
                .is_none_or(|s| s.context != before.context)
            || pointer(self.base + b.event_flags as usize) != manager
        {
            return false;
        }
        for (address, mask, value) in writes {
            // SAFETY: every byte was resolved in the current flag manager and
            // verified committed+writable before this bounded mutation. Byte
            // atomic RMW preserves adjacent event bits, matching native C++.
            let byte = unsafe { AtomicU8::from_ptr(address as *mut u8) };
            if value {
                byte.fetch_or(mask, Ordering::Relaxed);
            } else {
                byte.fetch_and(!mask, Ordering::Relaxed);
            }
        }
        self.flags.reset();
        true
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::runtime_perf::NativeMemory;

    fn fixture() -> NativeMemory {
        let mut f = NativeMemory::new();
        // One native category covers flags 0..127, including the graveyard.
        f.word(0xf001c, 128);
        f.word(0xf0220, 0);
        f.bytes[0xf0301] = 0b1001_0101;
        f.bytes[0xf030c] = 1 << 2;
        f.word(0x2094, 37);
        f.word(0x20a0, 123456);
        f.word(0x5014, 0x0a00_0000);
        f
    }

    #[test]
    fn production_state_reads_deaths_igt_and_fresh_save_identity() {
        let mut f = fixture();
        let mut service = f.service();
        let initial = service.boss_state().unwrap();
        assert_eq!(
            (
                initial.deaths,
                initial.in_game_time_ms,
                initial.graveyard_reached
            ),
            (37, 123456, 1)
        );
        assert_eq!(initial.raw_map_id, 0x0a00_0000);
        f.word(0x2094, 38);
        f.word(0x20a0, 123457);
        let next = service.boss_state().unwrap();
        assert_eq!(
            (next.deaths, next.in_game_time_ms, next.context),
            (38, 123457, initial.context)
        );
        f.ptr(0x2058, 0x3800);
        assert_ne!(service.boss_state().unwrap().context, initial.context);
        assert!(!service.revive_boss(initial.context, 8, &[]));
        assert_eq!(f.bytes[0xf0301], 0b1001_0101);
        f.word(0x20a0, 0);
        assert!(service.boss_state().is_none());
        f.word(0x20a0, u32::MAX);
        assert!(service.boss_state().is_none());
    }

    #[test]
    fn batch_flags_distinguish_missing_and_copy_shared_bytes_once() {
        let f = fixture();
        let mut service = f.service();
        let mut out = [9; 7];
        assert!(service.boss_flags(&[8, 9, 15, 101, 0, u32::MAX, 999], &mut out));
        assert_eq!(out, [1, 0, 1, 1, 2, 2, 2]);
        crate::memory::set_tracking(true);
        let before = crate::memory::syscall_counters();
        assert!(service.boss_flags(&[8, 9, 15], &mut [0; 3]));
        let short = crate::memory::syscall_counters().since(before);
        let before = crate::memory::syscall_counters();
        assert!(service.boss_flags(&[8; 512], &mut [0; 512]));
        let repeated = crate::memory::syscall_counters().since(before);
        crate::memory::set_tracking(false);
        assert_eq!(
            repeated.rpm_calls, short.rpm_calls,
            "duplicate flags must not cause per-flag byte copies"
        );
    }

    #[test]
    fn absent_graveyard_flag_does_not_hide_ordinary_boss_state() {
        let mut f = fixture();
        f.word(0xf001c, 8);
        f.word(0xf0220, 1);
        let mut service = f.service();
        let state = service.boss_state().unwrap();
        assert_eq!(state.graveyard_reached, 2);
        assert_eq!((state.deaths, state.in_game_time_ms), (37, 123456));
        let mut flags = [9; 2];
        assert!(service.boss_flags(&[8, 101], &mut flags));
        assert_eq!(flags, [1, 2]);
    }

    #[test]
    fn revival_resolves_all_targets_before_writing_and_preserves_neighbors() {
        let mut f = fixture();
        let mut service = f.service();
        let context = service.boss_state().unwrap().context;
        let initial = f.bytes[0xf0301];
        for extras in [
            vec![
                FlagWrite { id: 9, value: 1 },
                FlagWrite { id: 999, value: 0 },
            ],
            vec![FlagWrite { id: 9, value: 1 }, FlagWrite { id: 9, value: 0 }],
            vec![FlagWrite { id: 8, value: 0 }],
            vec![FlagWrite { id: 9, value: 2 }],
        ] {
            assert!(!service.revive_boss(context, 8, &extras));
            assert_eq!(f.bytes[0xf0301], initial);
        }
        assert!(service.revive_boss(
            context,
            8,
            &[
                FlagWrite { id: 9, value: 1 },
                FlagWrite { id: 15, value: 0 }
            ]
        ));
        assert_eq!(f.bytes[0xf0301], 0b0101_0100);
        assert_eq!(f.bytes[0xf0301] & 0b0011_1110, initial & 0b0011_1110);
        assert!(!service.revive_boss(context, 8, &[FlagWrite { id: 9, value: 0 }]));
        assert_eq!(f.bytes[0xf0301], 0b0101_0100);
        f.bytes[0x1718] = 1;
        assert!(service.boss_state().is_none());
    }
}
