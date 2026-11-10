//! Minimal virtual keyboard: reads chords from stdin, one per line
//! ("super+Right", "super+shift+equal", "Tab"), and types them through
//! zwp_virtual_keyboard_v1 — the same path a real keypress takes into the
//! compositor's keybindings. Used by the chaos soak.
//!
//! The keymap is the system's `us` layout, compiled by `xkbcli`.
//!
//! Usage: drift-vkbd < chords.txt

use std::io::{BufRead, Write};
use std::os::fd::{AsFd, FromRawFd, OwnedFd};

use wayland_client::protocol::{wl_registry, wl_seat};
use wayland_client::{Connection, Dispatch, QueueHandle, delegate_noop};
use wayland_protocols_misc::zwp_virtual_keyboard_v1::client::{
    zwp_virtual_keyboard_manager_v1::ZwpVirtualKeyboardManagerV1,
    zwp_virtual_keyboard_v1::ZwpVirtualKeyboardV1,
};

#[derive(Default)]
struct App {
    seat: Option<wl_seat::WlSeat>,
    manager: Option<ZwpVirtualKeyboardManagerV1>,
}

/// evdev keycode and, for modifiers, the `us` keymap's modifier mask.
fn key(name: &str) -> Option<(u32, u32)> {
    Some(match name.to_ascii_lowercase().as_str() {
        "super" | "mod" | "logo" => (125, 64),
        "shift" => (42, 1),
        "ctrl" | "control" => (29, 4),
        "alt" => (56, 8),
        "left" => (105, 0),
        "right" => (106, 0),
        "up" => (103, 0),
        "down" => (108, 0),
        "equal" => (13, 0),
        "minus" => (12, 0),
        "0" => (11, 0),
        "tab" => (15, 0),
        "return" | "enter" => (28, 0),
        "escape" | "esc" => (1, 0),
        "space" => (57, 0),
        "q" => (16, 0),
        "w" => (17, 0),
        "e" => (18, 0),
        "r" => (19, 0),
        "t" => (20, 0),
        "y" => (21, 0),
        "u" => (22, 0),
        "i" => (23, 0),
        "o" => (24, 0),
        "p" => (25, 0),
        "a" => (30, 0),
        "s" => (31, 0),
        "d" => (32, 0),
        "f" => (33, 0),
        "g" => (34, 0),
        "h" => (35, 0),
        "j" => (36, 0),
        "k" => (37, 0),
        "l" => (38, 0),
        "z" => (44, 0),
        "x" => (45, 0),
        "c" => (46, 0),
        "v" => (47, 0),
        "b" => (48, 0),
        "n" => (49, 0),
        "m" => (50, 0),
        "1" => (2, 0),
        "2" => (3, 0),
        "3" => (4, 0),
        "4" => (5, 0),
        "5" => (6, 0),
        "6" => (7, 0),
        "7" => (8, 0),
        "8" => (9, 0),
        "9" => (10, 0),
        "comma" => (51, 0),
        "period" => (52, 0),
        "slash" => (53, 0),
        "backspace" => (14, 0),
        _ => return None,
    })
}

fn keymap_fd() -> (OwnedFd, u32) {
    let out = std::process::Command::new("xkbcli")
        .args(["compile-keymap", "--layout", "us"])
        .output()
        .expect("xkbcli compile-keymap");
    assert!(out.status.success(), "xkbcli failed");
    let mut bytes = out.stdout;
    bytes.push(0);
    let fd = unsafe {
        let fd = libc::memfd_create(c"drift-vkbd-keymap".as_ptr(), libc::MFD_CLOEXEC);
        assert!(fd >= 0);
        OwnedFd::from_raw_fd(fd)
    };
    let mut file = std::fs::File::from(fd.try_clone().unwrap());
    file.write_all(&bytes).unwrap();
    (fd, bytes.len() as u32)
}

fn main() {
    let conn = Connection::connect_to_env().expect("no wayland display");
    let mut queue = conn.new_event_queue();
    let qh = queue.handle();
    conn.display().get_registry(&qh, ());
    let mut app = App::default();
    queue.roundtrip(&mut app).unwrap();
    let seat = app.seat.clone().expect("no wl_seat");
    let manager = app.manager.clone().expect("compositor lacks zwp_virtual_keyboard_v1");
    let kbd: ZwpVirtualKeyboardV1 = manager.create_virtual_keyboard(&seat, &qh, ());
    let (fd, size) = keymap_fd();
    kbd.keymap(1 /* xkb_v1 */, fd.as_fd(), size);
    queue.roundtrip(&mut app).unwrap();

    let start = std::time::Instant::now();
    let now = || start.elapsed().as_millis() as u32;
    let stdin = std::io::stdin();
    for line in stdin.lock().lines() {
        let Ok(line) = line else { break };
        let parts: Vec<&str> = line.trim().split('+').filter(|p| !p.is_empty()).collect();
        let Some((last, mods)) = parts.split_last() else { continue };
        let (Some((code, _)), Some(mods)) = (key(last), mods.iter().map(|m| key(m)).collect::<Option<Vec<_>>>())
        else {
            eprintln!("drift-vkbd: unknown key in {line:?}");
            continue;
        };
        let mut mask = 0;
        for &(mcode, mbit) in &mods {
            kbd.key(now(), mcode, 1);
            mask |= mbit;
            kbd.modifiers(mask, 0, 0, 0);
        }
        kbd.key(now(), code, 1);
        kbd.key(now(), code, 0);
        for &(mcode, mbit) in mods.iter().rev() {
            kbd.key(now(), mcode, 0);
            mask &= !mbit;
            kbd.modifiers(mask, 0, 0, 0);
        }
        if queue.roundtrip(&mut app).is_err() {
            break;
        }
    }
}

impl Dispatch<wl_registry::WlRegistry, ()> for App {
    fn event(app: &mut Self, reg: &wl_registry::WlRegistry, event: wl_registry::Event, _: &(), _: &Connection, qh: &QueueHandle<Self>) {
        if let wl_registry::Event::Global { name, interface, .. } = event {
            match interface.as_str() {
                "wl_seat" if app.seat.is_none() => app.seat = Some(reg.bind(name, 1, qh, ())),
                "zwp_virtual_keyboard_manager_v1" => app.manager = Some(reg.bind(name, 1, qh, ())),
                _ => {}
            }
        }
    }
}

delegate_noop!(App: ignore wl_seat::WlSeat);
delegate_noop!(App: ignore ZwpVirtualKeyboardManagerV1);
delegate_noop!(App: ignore ZwpVirtualKeyboardV1);
