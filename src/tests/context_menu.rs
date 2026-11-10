//! `[mouse] context_menu`: the command runs for a plain right-click on empty
//! canvas or a title bar, and never inside a window. Every scenario asserts
//! from what the user sees: whether the command ran and with what, and that the
//! app still received the click.

use std::path::PathBuf;
use std::time::{Duration, Instant};

use driftwm::config::BTN_RIGHT;
use smithay::desktop::Window;
use smithay::utils::{Logical, Point};

use super::client::ClientId;
use super::input_backend::{FakeDevice, pointer_to, press, release};
use super::real::TempDir;
use super::{Fixture, config, give_ssd, map_window, window_by_app_id};

/// Long enough for `sh` to run.
const QUIET: Duration = Duration::from_millis(400);

struct Menu {
    _dir: TempDir,
    out: PathBuf,
}

impl Menu {
    /// A fixture whose `context_menu` command records its target and window.
    fn fixture(extra: &str) -> (Fixture, Self) {
        let dir = TempDir::new();
        let out = dir.path().join("menu");
        let toml = format!(
            "[mouse]\ncontext_menu = \"echo $DRIFTWM_MENU_TARGET $DRIFTWM_MENU_APP_ID > '{0}.partial' && mv '{0}.partial' '{0}'\"\n{extra}",
            out.display()
        );
        (Fixture::with_config(config(&toml)), Self { _dir: dir, out })
    }

    /// What the command wrote, or `None` if it did not run within [`QUIET`].
    fn opened(&self, f: &mut Fixture) -> Option<String> {
        let deadline = Instant::now() + QUIET;
        while Instant::now() < deadline {
            f.pump(1);
            if let Ok(text) = std::fs::read_to_string(&self.out) {
                return Some(text.trim().to_string());
            }
            std::thread::sleep(Duration::from_millis(5));
        }
        None
    }
}

fn mapped_window(f: &mut Fixture) -> (ClientId, Window) {
    f.add_output(1, (1920, 1080));
    let id = f.add_client();
    map_window(f, id, "app", (400, 300));
    let window = window_by_app_id(f, "app").unwrap();
    f.state().with_output_state(|os| {
        os.camera_target = None;
        os.zoom_target = None;
    });
    (id, window)
}

fn center_of(f: &mut Fixture, window: &Window) -> Point<f64, Logical> {
    let pos = f.state().stage.position_of(window).expect("staged");
    let size = window.geometry().size;
    Point::from((
        pos.x as f64 + size.w as f64 / 2.0,
        pos.y as f64 + size.h as f64 / 2.0,
    ))
}

fn right_click(f: &mut Fixture, at: Point<f64, Logical>) {
    let mouse = FakeDevice::mouse();
    pointer_to(f, &mouse, at);
    press(f, &mouse, BTN_RIGHT);
    release(f, &mouse, BTN_RIGHT);
}

fn client_buttons(f: &mut Fixture, id: ClientId) -> Vec<(u32, u32)> {
    f.double_roundtrip(id);
    f.client(id).state.pointer_buttons.clone()
}

#[test]
fn right_click_inside_a_window_belongs_to_the_app() {
    let (mut f, menu) = Menu::fixture("");
    let (id, window) = mapped_window(&mut f);
    let at = center_of(&mut f, &window);

    right_click(&mut f, at);

    assert_eq!(
        client_buttons(&mut f, id),
        vec![(BTN_RIGHT, 1), (BTN_RIGHT, 0)],
        "the app must see the whole click"
    );
    assert_eq!(
        menu.opened(&mut f),
        None,
        "an app that stays silent still keeps its right-click"
    );
}

#[test]
fn empty_canvas_opens_the_menu_at_once() {
    let (mut f, menu) = Menu::fixture("");
    f.add_output(1, (1920, 1080));

    right_click(&mut f, Point::from((300.0, 300.0)));

    assert_eq!(menu.opened(&mut f).as_deref(), Some("canvas"));
}

#[test]
fn ssd_title_bar_opens_the_window_menu() {
    let (mut f, menu) = Menu::fixture("");
    let (_, window) = mapped_window(&mut f);
    give_ssd(&mut f, &window);
    let loc = f.state().stage.position_of(&window).expect("staged");
    let bar = f.state().config.decorations.title_bar_height as f64;
    let at = Point::from((loc.x as f64 + 200.0, loc.y as f64 - bar / 2.0));

    right_click(&mut f, at);

    assert_eq!(menu.opened(&mut f).as_deref(), Some("titlebar app"));
}

#[test]
fn right_drag_is_not_a_click() {
    let (mut f, menu) = Menu::fixture("");
    f.add_output(1, (1920, 1080));
    let mouse = FakeDevice::mouse();

    pointer_to(&mut f, &mouse, Point::from((300.0, 300.0)));
    press(&mut f, &mouse, BTN_RIGHT);
    pointer_to(&mut f, &mouse, Point::from((360.0, 300.0)));
    release(&mut f, &mouse, BTN_RIGHT);

    assert_eq!(menu.opened(&mut f), None);
}

#[test]
fn bound_right_click_belongs_to_its_binding() {
    let (mut f, menu) = Menu::fixture("[mouse.on-canvas]\n\"right\" = \"zoom-reset\"\n");
    f.add_output(1, (1920, 1080));

    right_click(&mut f, Point::from((300.0, 300.0)));

    assert_eq!(menu.opened(&mut f), None);
}

#[test]
fn unset_command_leaves_right_click_alone() {
    let mut f = Fixture::new();
    let (id, window) = mapped_window(&mut f);
    let at = center_of(&mut f, &window);

    right_click(&mut f, at);

    assert_eq!(
        client_buttons(&mut f, id),
        vec![(BTN_RIGHT, 1), (BTN_RIGHT, 0)]
    );
    assert!(f.state().pending_context_menu.is_none());
}
