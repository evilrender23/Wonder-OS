use alacritty_terminal::term::TermMode;
use eframe::egui::{Key, Modifiers};

/// xterm's modifier parameter: 1 + shift(1) + alt(2) + ctrl(4).
fn modifier_param(m: Modifiers) -> u8 {
    1 + m.shift as u8 + 2 * m.alt as u8 + 4 * m.ctrl as u8
}

/// The bytes a non-text key sends. Printable keys arrive as text events
/// instead and return `None` here, unless Ctrl turns them into a control code.
pub fn encode_key(key: Key, m: Modifiers, mode: TermMode) -> Option<Vec<u8>> {
    let mp = modifier_param(m);
    let modified = mp > 1;
    let cursor_key = |c: char| {
        if modified {
            format!("\x1b[1;{mp}{c}")
        } else if mode.contains(TermMode::APP_CURSOR) {
            format!("\x1bO{c}")
        } else {
            format!("\x1b[{c}")
        }
        .into_bytes()
    };
    let tilde = |n: u8| {
        if modified {
            format!("\x1b[{n};{mp}~")
        } else {
            format!("\x1b[{n}~")
        }
        .into_bytes()
    };
    let f1_to_f4 = |c: char| {
        if modified {
            format!("\x1b[1;{mp}{c}")
        } else {
            format!("\x1bO{c}")
        }
        .into_bytes()
    };
    let alt_prefixed = |bytes: &[u8]| {
        let mut out = Vec::with_capacity(bytes.len() + 1);
        if m.alt {
            out.push(0x1b);
        }
        out.extend_from_slice(bytes);
        out
    };

    Some(match key {
        Key::Enter => alt_prefixed(b"\r"),
        Key::Tab if m.shift => b"\x1b[Z".to_vec(),
        Key::Tab => alt_prefixed(b"\t"),
        Key::Backspace if m.ctrl => alt_prefixed(b"\x08"),
        Key::Backspace => alt_prefixed(b"\x7f"),
        Key::Escape => alt_prefixed(b"\x1b"),
        Key::ArrowUp => cursor_key('A'),
        Key::ArrowDown => cursor_key('B'),
        Key::ArrowRight => cursor_key('C'),
        Key::ArrowLeft => cursor_key('D'),
        Key::Home => cursor_key('H'),
        Key::End => cursor_key('F'),
        Key::Insert => tilde(2),
        Key::Delete => tilde(3),
        Key::PageUp => tilde(5),
        Key::PageDown => tilde(6),
        Key::F1 => f1_to_f4('P'),
        Key::F2 => f1_to_f4('Q'),
        Key::F3 => f1_to_f4('R'),
        Key::F4 => f1_to_f4('S'),
        Key::F5 => tilde(15),
        Key::F6 => tilde(17),
        Key::F7 => tilde(18),
        Key::F8 => tilde(19),
        Key::F9 => tilde(20),
        Key::F10 => tilde(21),
        Key::F11 => tilde(23),
        Key::F12 => tilde(24),
        _ if m.ctrl => alt_prefixed(&[control_code(key)?]),
        _ => return None,
    })
}

fn control_code(key: Key) -> Option<u8> {
    Some(match key {
        Key::Space | Key::Num2 => 0,
        Key::OpenBracket | Key::Num3 => 27,
        Key::Backslash | Key::Num4 => 28,
        Key::CloseBracket | Key::Num5 => 29,
        Key::Num6 => 30,
        Key::Slash | Key::Num7 => 31,
        Key::Num8 | Key::Questionmark => 127,
        _ => {
            let name = key.name().as_bytes();
            match name {
                [c] if c.is_ascii_alphabetic() => c.to_ascii_uppercase() - b'A' + 1,
                _ => return None,
            }
        }
    })
}

/// Typed text; Alt sends it behind an escape, as readline and friends expect.
pub fn encode_text(text: &str, m: Modifiers) -> Vec<u8> {
    let mut out = Vec::with_capacity(text.len() + 1);
    if m.alt && !m.ctrl {
        out.push(0x1b);
    }
    out.extend_from_slice(text.as_bytes());
    out
}

pub fn encode_paste(text: &str, mode: TermMode) -> Vec<u8> {
    if mode.contains(TermMode::BRACKETED_PASTE) {
        // An escape inside the paste could close the bracket early and run
        // what follows as typed input.
        let clean: String = text
            .chars()
            .filter(|&c| c != '\x1b' && c != '\x03')
            .collect();
        format!("\x1b[200~{clean}\x1b[201~").into_bytes()
    } else {
        text.replace("\r\n", "\r").replace('\n', "\r").into_bytes()
    }
}

/// `button`: 0 left, 1 middle, 2 right, 64/65 wheel; add 32 for motion.
pub fn encode_mouse(
    button: u8,
    pressed: bool,
    col: usize,
    row: usize,
    m: Modifiers,
    mode: TermMode,
) -> Option<Vec<u8>> {
    let mods = 4 * m.shift as u8 + 8 * m.alt as u8 + 16 * m.ctrl as u8;
    if mode.contains(TermMode::SGR_MOUSE) {
        let end = if pressed { 'M' } else { 'm' };
        return Some(format!("\x1b[<{};{};{}{end}", button + mods, col + 1, row + 1).into_bytes());
    }
    // The legacy encoding has no release-of-which-button and one byte per coordinate.
    let code = if pressed { button } else { 3 } + mods;
    (col < 223 && row < 223).then(|| {
        vec![
            0x1b,
            b'[',
            b'M',
            32 + code,
            32 + 1 + col as u8,
            32 + 1 + row as u8,
        ]
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    const NONE: Modifiers = Modifiers::NONE;

    fn key(k: Key, m: Modifiers) -> Option<String> {
        encode_key(k, m, TermMode::default()).map(|b| String::from_utf8(b).unwrap())
    }

    #[test]
    fn plain_keys() {
        assert_eq!(key(Key::Enter, NONE).unwrap(), "\r");
        assert_eq!(key(Key::Backspace, NONE).unwrap(), "\x7f");
        assert_eq!(key(Key::ArrowUp, NONE).unwrap(), "\x1b[A");
        assert_eq!(key(Key::Delete, NONE).unwrap(), "\x1b[3~");
        assert_eq!(key(Key::F1, NONE).unwrap(), "\x1bOP");
        assert_eq!(key(Key::F5, NONE).unwrap(), "\x1b[15~");
        assert_eq!(key(Key::A, NONE), None);
    }

    #[test]
    fn application_cursor_mode_switches_arrows_to_ss3() {
        let out = encode_key(Key::ArrowLeft, NONE, TermMode::APP_CURSOR).unwrap();
        assert_eq!(out, b"\x1bOD");
        let out = encode_key(Key::ArrowLeft, Modifiers::CTRL, TermMode::APP_CURSOR).unwrap();
        assert_eq!(out, b"\x1b[1;5D");
    }

    #[test]
    fn modifiers_use_the_xterm_parameter() {
        assert_eq!(key(Key::ArrowRight, Modifiers::CTRL).unwrap(), "\x1b[1;5C");
        assert_eq!(key(Key::ArrowRight, Modifiers::SHIFT).unwrap(), "\x1b[1;2C");
        assert_eq!(key(Key::PageUp, Modifiers::ALT).unwrap(), "\x1b[5;3~");
        assert_eq!(key(Key::F2, Modifiers::SHIFT).unwrap(), "\x1b[1;2Q");
        assert_eq!(key(Key::Tab, Modifiers::SHIFT).unwrap(), "\x1b[Z");
    }

    #[test]
    fn control_codes() {
        assert_eq!(key(Key::A, Modifiers::CTRL).unwrap(), "\x01");
        assert_eq!(key(Key::Z, Modifiers::CTRL).unwrap(), "\x1a");
        assert_eq!(key(Key::Space, Modifiers::CTRL).unwrap(), "\0");
        assert_eq!(key(Key::OpenBracket, Modifiers::CTRL).unwrap(), "\x1b");
        assert_eq!(
            key(Key::D, Modifiers::CTRL | Modifiers::ALT).unwrap(),
            "\x1b\x04"
        );
        assert_eq!(key(Key::Backspace, Modifiers::ALT).unwrap(), "\x1b\x7f");
    }

    #[test]
    fn alt_text_gets_an_escape_prefix() {
        assert_eq!(encode_text("b", Modifiers::ALT), b"\x1bb");
        assert_eq!(encode_text("ñ", NONE), "ñ".as_bytes());
    }

    #[test]
    fn paste_is_bracketed_only_when_asked_and_cannot_break_out() {
        assert_eq!(encode_paste("a\nb", TermMode::default()), b"a\rb");
        let out = encode_paste("ls\x1b[201~rm", TermMode::BRACKETED_PASTE);
        assert_eq!(out, b"\x1b[200~ls[201~rm\x1b[201~");
    }

    #[test]
    fn mouse_reports() {
        let sgr = encode_mouse(0, true, 4, 9, NONE, TermMode::SGR_MOUSE).unwrap();
        assert_eq!(sgr, b"\x1b[<0;5;10M");
        let sgr = encode_mouse(2, false, 0, 0, Modifiers::CTRL, TermMode::SGR_MOUSE).unwrap();
        assert_eq!(sgr, b"\x1b[<18;1;1m");
        let legacy = encode_mouse(0, false, 0, 0, NONE, TermMode::default()).unwrap();
        assert_eq!(legacy, [0x1b, b'[', b'M', 35, 33, 33]);
        assert_eq!(
            encode_mouse(0, true, 300, 0, NONE, TermMode::default()),
            None
        );
    }
}
