//! Configuration shortcut names. Parsing runs only while loading configuration.
use crate::{MOD_ALT, MOD_CTRL, MOD_SHIFT, MOD_WIN};

/// Encodes one Win32 key and exact modifiers, or zero for disabled/invalid text.
/// Generic modifiers may stand alone or combine; side-specific modifiers are
/// standalone keys and include their own generic modifier bit for strict polling.
pub fn parse_chord(text: &str) -> u32 {
    let text = text.trim();
    if text.is_empty() || text.eq_ignore_ascii_case("none") || text.len() > 256 {
        return 0;
    }
    let mut modifiers = 0;
    let mut key = 0;
    let mut count = 0;
    let mut side_modifier = false;
    for name in text.split('+').map(str::trim) {
        if name.is_empty() || !name.is_ascii() {
            return 0;
        }
        count += 1;
        let upper = name.to_ascii_uppercase();
        let modifier = match upper.as_str() {
            "CTRL" | "CONTROL" => MOD_CTRL,
            "SHIFT" => MOD_SHIFT,
            "ALT" => MOD_ALT,
            "WIN" => MOD_WIN,
            _ => 0,
        };
        if modifier != 0 {
            if modifiers & modifier != 0 {
                return 0;
            }
            modifiers |= modifier;
            continue;
        }
        let Some(vk) = virtual_key(&upper) else {
            return 0;
        };
        if key != 0 {
            return 0;
        }
        key = vk;
        let own_modifier = match vk {
            0xa0 | 0xa1 => MOD_SHIFT,
            0xa2 | 0xa3 => MOD_CTRL,
            0xa4 | 0xa5 => MOD_ALT,
            0x5b | 0x5c => MOD_WIN,
            _ => 0,
        };
        if own_modifier != 0 {
            side_modifier = true;
            modifiers |= own_modifier;
        }
    }
    if side_modifier && count != 1 {
        0
    } else {
        key | modifiers
    }
}

fn virtual_key(name: &str) -> Option<u32> {
    if name.len() == 1 && name.as_bytes()[0].is_ascii_alphanumeric() {
        return Some(u32::from(name.as_bytes()[0]));
    }
    if let Some(digits) = name.strip_prefix('F')
        && !digits.starts_with('0')
        && let Ok(number) = digits.parse::<u32>()
        && (1..=24).contains(&number)
    {
        return Some(0x6f + number);
    }
    if let Some(digit) = name
        .strip_prefix("NUMPAD")
        .or_else(|| name.strip_prefix("NUM"))
        && digit.len() == 1
        && digit.as_bytes()[0].is_ascii_digit()
    {
        return Some(0x60 + u32::from(digit.as_bytes()[0] - b'0'));
    }
    Some(match name {
        "LBUTTON" | "LMB" => 0x01,
        "RBUTTON" | "RMB" => 0x02,
        "CANCEL" => 0x03,
        "MBUTTON" | "MMB" => 0x04,
        "XBUTTON1" => 0x05,
        "XBUTTON2" => 0x06,
        "BACK" | "BACKSPACE" => 0x08,
        "TAB" => 0x09,
        "CLEAR" => 0x0c,
        "RETURN" | "ENTER" => 0x0d,
        "PAUSE" => 0x13,
        "CAPITAL" | "CAPSLOCK" => 0x14,
        "KANA" | "HANGUL" => 0x15,
        "JUNJA" => 0x17,
        "FINAL" => 0x18,
        "HANJA" | "KANJI" => 0x19,
        "ESCAPE" | "ESC" => 0x1b,
        "CONVERT" => 0x1c,
        "NONCONVERT" => 0x1d,
        "ACCEPT" => 0x1e,
        "MODECHANGE" => 0x1f,
        "SPACE" => 0x20,
        "PAGEUP" | "PRIOR" => 0x21,
        "PAGEDOWN" | "NEXT" => 0x22,
        "END" => 0x23,
        "HOME" => 0x24,
        "LEFT" => 0x25,
        "UP" => 0x26,
        "RIGHT" => 0x27,
        "DOWN" => 0x28,
        "SELECT" => 0x29,
        "PRINT" | "PRINTSCREEN" | "SNAPSHOT" => 0x2c,
        "EXECUTE" => 0x2b,
        "INSERT" | "INS" => 0x2d,
        "DELETE" | "DEL" => 0x2e,
        "HELP" => 0x2f,
        "LWIN" => 0x5b,
        "RWIN" => 0x5c,
        "APPS" => 0x5d,
        "SLEEP" => 0x5f,
        "MULTIPLY" => 0x6a,
        "ADD" => 0x6b,
        "SEPARATOR" => 0x6c,
        "SUBTRACT" | "MINUS" => 0x6d,
        "DECIMAL" => 0x6e,
        "DIVIDE" => 0x6f,
        "NUMLOCK" => 0x90,
        "SCROLL" => 0x91,
        "LSHIFT" => 0xa0,
        "RSHIFT" => 0xa1,
        "LCTRL" | "LCONTROL" => 0xa2,
        "RCTRL" | "RCONTROL" => 0xa3,
        "LALT" | "LMENU" => 0xa4,
        "RALT" | "RMENU" => 0xa5,
        "BROWSER_BACK" => 0xa6,
        "BROWSER_FORWARD" => 0xa7,
        "BROWSER_REFRESH" => 0xa8,
        "BROWSER_STOP" => 0xa9,
        "BROWSER_SEARCH" => 0xaa,
        "BROWSER_FAVORITES" => 0xab,
        "BROWSER_HOME" => 0xac,
        "VOLUME_MUTE" => 0xad,
        "VOLUME_DOWN" => 0xae,
        "VOLUME_UP" => 0xaf,
        "MEDIA_NEXT_TRACK" => 0xb0,
        "MEDIA_PREV_TRACK" => 0xb1,
        "MEDIA_STOP" => 0xb2,
        "MEDIA_PLAY_PAUSE" => 0xb3,
        "LAUNCH_MAIL" => 0xb4,
        "LAUNCH_MEDIA_SELECT" => 0xb5,
        "LAUNCH_APP1" => 0xb6,
        "LAUNCH_APP2" => 0xb7,
        ";" | "OEM_1" => 0xba,
        "=" | "OEM_PLUS" => 0xbb,
        "," | "OEM_COMMA" => 0xbc,
        "-" | "OEM_MINUS" => 0xbd,
        "." | "OEM_PERIOD" => 0xbe,
        "/" | "OEM_2" => 0xbf,
        "~" | "`" | "OEM_3" => 0xc0,
        "[" | "OEM_4" => 0xdb,
        "\\" | "OEM_5" => 0xdc,
        "]" | "OEM_6" => 0xdd,
        "'" | "OEM_7" => 0xde,
        "OEM_8" => 0xdf,
        "PROCESSKEY" => 0xe5,
        "PACKET" => 0xe7,
        "ATTN" => 0xf6,
        "CRSEL" => 0xf7,
        "EXSEL" => 0xf8,
        "EREOF" => 0xf9,
        "PLAY" => 0xfa,
        "ZOOM" => 0xfb,
        "PA1" => 0xfd,
        "OEM_CLEAR" => 0xfe,
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;
    #[test]
    fn all_alphanumeric_function_and_numpad_keys() {
        for byte in b'A'..=b'Z' {
            assert_eq!(parse_chord(&char::from(byte).to_string()), u32::from(byte));
        }
        for byte in b'0'..=b'9' {
            assert_eq!(parse_chord(&char::from(byte).to_string()), u32::from(byte));
        }
        for n in 1..=24 {
            assert_eq!(parse_chord(&format!("f{n}")), 0x6f + n);
        }
        for n in 0..=9 {
            assert_eq!(parse_chord(&format!("NUMPAD{n}")), 0x60 + n);
            assert_eq!(parse_chord(&format!("num{n}")), 0x60 + n);
        }
    }
    #[test]
    fn modifiers_are_exact_and_side_keys_stand_alone() {
        assert_eq!(parse_chord("ctrl+shift+f12"), MOD_CTRL | MOD_SHIFT | 0x7b);
        assert_eq!(
            parse_chord("WIN+ALT+M"),
            MOD_WIN | MOD_ALT | u32::from(b'M')
        );
        assert_eq!(parse_chord("CONTROL"), MOD_CTRL);
        assert_eq!(parse_chord("CTRL+WIN"), MOD_CTRL | MOD_WIN);
        for (name, vk, modifier) in [
            ("LCTRL", 0xa2, MOD_CTRL),
            ("RCONTROL", 0xa3, MOD_CTRL),
            ("LSHIFT", 0xa0, MOD_SHIFT),
            ("RSHIFT", 0xa1, MOD_SHIFT),
            ("LMENU", 0xa4, MOD_ALT),
            ("RALT", 0xa5, MOD_ALT),
            ("LWIN", 0x5b, MOD_WIN),
            ("RWIN", 0x5c, MOD_WIN),
        ] {
            assert_eq!(parse_chord(name), vk | modifier);
            assert_eq!(parse_chord(&format!("ALT+{name}")), 0);
        }
    }
    #[test]
    fn punctuation_aliases_and_extended_names() {
        for (a, b, vk) in [
            ("LMB", "LBUTTON", 1),
            ("RMB", "RBUTTON", 2),
            ("MMB", "MBUTTON", 4),
            ("BACKSPACE", "BACK", 8),
            ("ENTER", "RETURN", 13),
            ("PRIOR", "PAGEUP", 0x21),
            ("NEXT", "PAGEDOWN", 0x22),
            ("PRINT", "PRINTSCREEN", 0x2c),
            ("INS", "INSERT", 0x2d),
            ("DEL", "DELETE", 0x2e),
            ("MINUS", "SUBTRACT", 0x6d),
            ("=", "OEM_PLUS", 0xbb),
            ("-", "OEM_MINUS", 0xbd),
            ("~", "`", 0xc0),
            ("KANJI", "HANJA", 0x19),
        ] {
            assert_eq!(parse_chord(a), vk);
            assert_eq!(parse_chord(b), vk);
        }
        for name in [
            "XBUTTON2",
            "KANA",
            "JUNJA",
            "FINAL",
            "CONVERT",
            "NONCONVERT",
            "ACCEPT",
            "MODECHANGE",
            "CANCEL",
            "CLEAR",
            "PAUSE",
            "CAPSLOCK",
            "SPACE",
            "ESC",
            "LEFT",
            "UP",
            "RIGHT",
            "DOWN",
            "SELECT",
            "EXECUTE",
            "HELP",
            "APPS",
            "SLEEP",
            "MULTIPLY",
            "ADD",
            "SEPARATOR",
            "DECIMAL",
            "DIVIDE",
            "NUMLOCK",
            "SCROLL",
            "BROWSER_BACK",
            "BROWSER_FORWARD",
            "BROWSER_REFRESH",
            "BROWSER_STOP",
            "BROWSER_SEARCH",
            "BROWSER_FAVORITES",
            "BROWSER_HOME",
            "VOLUME_MUTE",
            "VOLUME_DOWN",
            "VOLUME_UP",
            "MEDIA_NEXT_TRACK",
            "MEDIA_PREV_TRACK",
            "MEDIA_STOP",
            "MEDIA_PLAY_PAUSE",
            "LAUNCH_MAIL",
            "LAUNCH_MEDIA_SELECT",
            "LAUNCH_APP1",
            "LAUNCH_APP2",
            "OEM_8",
            "PROCESSKEY",
            "PACKET",
            "ATTN",
            "CRSEL",
            "EXSEL",
            "EREOF",
            "PLAY",
            "ZOOM",
            "PA1",
            "OEM_CLEAR",
        ] {
            assert_ne!(parse_chord(name), 0, "{name}");
        }
    }
    #[test]
    fn disabled_and_malformed_inputs_cannot_become_unintended_shortcuts() {
        for value in [
            "",
            "none",
            "NONE+M",
            "+",
            "CTRL+",
            "+M",
            "CTRL++M",
            "M+N",
            "CTRL+M+N",
            "M+M",
            "CTRL+CONTROL",
            "LCTRL+M",
            "WIN+RWIN",
            "F0",
            "F01",
            "F25",
            "NUM10",
            "NUMPAD00",
            "UNKNOWN+M",
            "控制+M",
        ] {
            assert_eq!(parse_chord(value), 0, "{value}");
        }
    }
}
