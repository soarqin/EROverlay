//! Win32 POD events to egui input, owned independently by each interactive plugin.
use eroverlay_abi as abi;
#[derive(Default)]
pub struct Input {
    pos: egui::Pos2,
    keys: Vec<egui::Key>,
    buttons: [bool; 5],
    focused: bool,
    modifiers: egui::Modifiers,
}
fn modifiers(flags: u32) -> egui::Modifiers {
    egui::Modifiers {
        alt: flags & abi::MOD_ALT != 0,
        ctrl: flags & abi::MOD_CTRL != 0,
        shift: flags & abi::MOD_SHIFT != 0,
        mac_cmd: false,
        command: flags & abi::MOD_CTRL != 0,
    }
}
fn key(v: u32) -> Option<egui::Key> {
    use egui::Key::*;
    Some(match v {
        0x08 => Backspace,
        0x09 => Tab,
        0x0d => Enter,
        0x1b => Escape,
        0x20 => Space,
        0x21 => PageUp,
        0x22 => PageDown,
        0x23 => End,
        0x24 => Home,
        0x25 => ArrowLeft,
        0x26 => ArrowUp,
        0x27 => ArrowRight,
        0x28 => ArrowDown,
        0x2d => Insert,
        0x2e => Delete,
        0x30 => Num0,
        0x31 => Num1,
        0x32 => Num2,
        0x33 => Num3,
        0x34 => Num4,
        0x35 => Num5,
        0x36 => Num6,
        0x37 => Num7,
        0x38 => Num8,
        0x39 => Num9,
        0x41 => A,
        0x42 => B,
        0x43 => C,
        0x44 => D,
        0x45 => E,
        0x46 => F,
        0x47 => G,
        0x48 => H,
        0x49 => I,
        0x4a => J,
        0x4b => K,
        0x4c => L,
        0x4d => M,
        0x4e => N,
        0x4f => O,
        0x50 => P,
        0x51 => Q,
        0x52 => R,
        0x53 => S,
        0x54 => T,
        0x55 => U,
        0x56 => V,
        0x57 => W,
        0x58 => X,
        0x59 => Y,
        0x5a => Z,
        0x70 => F1,
        0x71 => F2,
        0x72 => F3,
        0x73 => F4,
        0x74 => F5,
        0x75 => F6,
        0x76 => F7,
        0x77 => F8,
        0x78 => F9,
        0x79 => F10,
        0x7a => F11,
        0x7b => F12,
        _ => return None,
    })
}
fn button(i: usize) -> egui::PointerButton {
    [
        egui::PointerButton::Primary,
        egui::PointerButton::Secondary,
        egui::PointerButton::Middle,
        egui::PointerButton::Extra1,
        egui::PointerButton::Extra2,
    ][i]
}
impl Input {
    fn reset(&mut self, events: &mut Vec<egui::Event>) {
        for key in self.keys.drain(..) {
            events.push(egui::Event::Key {
                key,
                physical_key: None,
                pressed: false,
                repeat: false,
                modifiers: self.modifiers,
            });
        }
        for i in 0..5 {
            if self.buttons[i] {
                events.push(egui::Event::PointerButton {
                    pos: self.pos,
                    button: button(i),
                    pressed: false,
                    modifiers: self.modifiers,
                });
                self.buttons[i] = false;
            }
        }
        events.push(egui::Event::PointerGone);
        self.modifiers = egui::Modifiers::default();
    }
    pub fn clear(&mut self) {
        *self = Self::default();
    }
    pub fn raw(
        &mut self,
        frame: &abi::FrameInfo,
        time: f64,
        input: &[abi::UiInput],
    ) -> egui::RawInput {
        let mut events = Vec::with_capacity(input.len());
        for e in input {
            if e.size as usize != size_of::<abi::UiInput>() {
                continue;
            }
            self.modifiers = modifiers(e.flags);
            let pressed = e.flags & abi::UI_INPUT_DOWN != 0;
            match e.kind {
                abi::UI_INPUT_POINTER if e.x.is_finite() && e.y.is_finite() => {
                    self.pos = egui::pos2(e.x, e.y);
                    events.push(egui::Event::PointerMoved(self.pos));
                }
                abi::UI_INPUT_BUTTON if e.code < 5 => {
                    if e.x.is_finite() && e.y.is_finite() {
                        self.pos = egui::pos2(e.x, e.y);
                    }
                    self.buttons[e.code as usize] = pressed;
                    events.push(egui::Event::PointerButton {
                        pos: self.pos,
                        button: button(e.code as usize),
                        pressed,
                        modifiers: self.modifiers,
                    });
                }
                abi::UI_INPUT_WHEEL if e.x.is_finite() && e.y.is_finite() => {
                    events.push(egui::Event::MouseWheel {
                        unit: egui::MouseWheelUnit::Line,
                        delta: egui::vec2(e.x, e.y),
                        modifiers: self.modifiers,
                    })
                }
                abi::UI_INPUT_KEY => {
                    if let Some(key) = key(e.code) {
                        if pressed {
                            if !self.keys.contains(&key) {
                                self.keys.push(key);
                            }
                        } else {
                            self.keys.retain(|k| *k != key);
                        }
                        events.push(egui::Event::Key {
                            key,
                            physical_key: Some(key),
                            pressed,
                            repeat: e.flags & abi::UI_INPUT_REPEAT != 0,
                            modifiers: self.modifiers,
                        });
                    }
                }
                abi::UI_INPUT_TEXT => {
                    if let Some(c) = char::from_u32(e.code).filter(|c| !c.is_control()) {
                        events.push(egui::Event::Text(c.to_string()));
                    }
                }
                abi::UI_INPUT_FOCUS => {
                    self.focused = e.code != 0;
                    if !self.focused {
                        self.reset(&mut events);
                    }
                    events.push(egui::Event::WindowFocused(self.focused));
                }
                abi::UI_INPUT_RESET => self.reset(&mut events),
                _ => {}
            }
        }
        egui::RawInput {
            screen_rect: Some(egui::Rect::from_min_size(
                egui::Pos2::ZERO,
                egui::vec2(frame.width, frame.height),
            )),
            time: Some(time),
            predicted_dt: frame.delta_seconds.clamp(0.001, 0.1),
            events,
            focused: self.focused,
            modifiers: self.modifiers,
            ..Default::default()
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn reset_releases_keys_buttons_and_does_not_type_control_characters() {
        let event = |kind, code, flags| abi::UiInput {
            size: size_of::<abi::UiInput>() as u32,
            kind,
            code,
            flags,
            ..Default::default()
        };
        let mut input = Input::default();
        let raw = input.raw(
            &abi::FrameInfo {
                width: 1920.,
                height: 1080.,
                ..Default::default()
            },
            1.,
            &[
                event(abi::UI_INPUT_KEY, 0x41, abi::UI_INPUT_DOWN),
                event(abi::UI_INPUT_BUTTON, 0, abi::UI_INPUT_DOWN),
                event(abi::UI_INPUT_TEXT, 10, 0),
                event(abi::UI_INPUT_RESET, 0, 0),
            ],
        );
        assert!(input.keys.is_empty());
        assert!(!input.buttons[0]);
        assert!(!raw.events.iter().any(|e| matches!(e, egui::Event::Text(_))));
        assert!(
            raw.events
                .iter()
                .any(|e| matches!(e, egui::Event::Key { pressed: false, .. }))
        );
    }
}
