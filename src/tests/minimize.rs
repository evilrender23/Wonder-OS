//! Minimizing a window into the dock and bringing it back: the window keeps
//! its place and id, is drawn and reachable nowhere while minimized, hands the
//! keyboard on, and comes back focused. The client hears about it through the
//! xdg-toplevel `suspended` state.

use smithay::reexports::wayland_protocols::xdg::shell::server::xdg_toplevel;
use smithay::utils::Point;

use driftwm::config::BTN_LEFT;

use crate::decorations::DecorationHit;
use crate::input::DecoTarget;
use crate::ipc::dispatch;
use crate::ipc::protocol::{Reply, Request, Response, WindowSelector};
use crate::state::StageWindow;

use super::input_backend::{FakeDevice, pointer_to, press, release};
use super::{
    Fixture, give_ssd, keyboard_focus, map_window, server_surface, settle, window_by_app_id,
};

fn origin_view(f: &mut Fixture) {
    f.state().with_output_state(|os| {
        os.camera = Point::from((0.0, 0.0));
        os.camera_target = None;
        os.zoom = 1.0;
        os.zoom_target = None;
        os.zoom_animation_anchor = None;
    });
}

fn minimize(f: &mut Fixture, id: u64, value: Option<bool>) -> Reply {
    dispatch(
        Request::Minimize {
            window: Some(WindowSelector::Id(id)),
            value,
        },
        f.state(),
    )
}

fn pending_suspended(window: &smithay::desktop::Window) -> bool {
    window
        .toplevel()
        .expect("toplevel")
        .with_pending_state(|s| s.states.contains(xdg_toplevel::State::Suspended))
}

/// Two windows side by side, `b` focused (mapped last), camera at the origin.
fn two_windows() -> (Fixture, smithay::desktop::Window, smithay::desktop::Window) {
    let mut f = Fixture::new();
    f.add_output(1, (1920, 1080));
    let id = f.add_client();
    map_window(&mut f, id, "a", (400, 300));
    map_window(&mut f, id, "b", (400, 300));
    origin_view(&mut f);
    settle(&mut f);
    let a = window_by_app_id(&mut f, "a").unwrap();
    let b = window_by_app_id(&mut f, "b").unwrap();
    (f, a, b)
}

#[test]
fn minimizing_hides_the_window_and_hands_focus_on() {
    let (mut f, a, b) = two_windows();
    assert_eq!(keyboard_focus(&mut f), Some(server_surface(&b)));
    let id = f.state().stage.id_of(&b).unwrap().0;
    let before = f.state().stage.position_of(&b);

    assert_eq!(
        minimize(&mut f, id, Some(true)),
        Ok(Response::Minimized(true))
    );
    settle(&mut f);

    let state = f.state();
    assert!(state.is_minimized(&b));
    assert_eq!(
        state.stage.position_of(&b),
        before,
        "keeps its stage entry and place"
    );
    assert!(!state.is_canvas_window(&b), "out of every canvas relation");
    assert!(
        state
            .snap_rect_for(&StageWindow::Client(b.clone()))
            .is_none()
    );
    assert!(
        !state.stage.focus_history().iter().any(|w| *w == b),
        "out of the focus cycle"
    );
    assert!(pending_suspended(&b), "the client is told it is hidden");
    assert_eq!(
        keyboard_focus(&mut f),
        Some(server_surface(&a)),
        "focus moves to the window still in view"
    );
    assert_eq!(minimize(&mut f, id, None), Ok(Response::Minimized(true)));
}

#[test]
fn a_minimized_window_is_not_hit_by_the_pointer() {
    let (mut f, _a, b) = two_windows();
    let loc = f.state().stage.position_of(&b).unwrap();
    let inside = Point::from((loc.x as f64 + 50.0, loc.y as f64 + 50.0));
    assert!(f.state().surface_under(inside, None).is_some());

    f.state().minimize_window(&b).unwrap();
    let under = f
        .state()
        .surface_under(inside, None)
        .and_then(|(t, _)| f.state().window_for_surface_root(&t.0));
    assert_ne!(under, Some(b), "the pointer passes through where it was");
}

#[test]
fn restoring_brings_it_back_focused_where_it_was() {
    let (mut f, _a, b) = two_windows();
    let id = f.state().stage.id_of(&b).unwrap().0;
    let before = f.state().stage.position_of(&b);
    f.state().minimize_window(&b).unwrap();
    settle(&mut f);

    assert_eq!(
        minimize(&mut f, id, Some(false)),
        Ok(Response::Minimized(false))
    );
    settle(&mut f);

    let state = f.state();
    assert!(!state.is_minimized(&b));
    assert_eq!(state.stage.position_of(&b), before);
    assert_eq!(state.stage.id_of(&b).map(|e| e.0), Some(id), "same window");
    assert!(!pending_suspended(&b));
    assert_eq!(keyboard_focus(&mut f), Some(server_surface(&b)));
    assert_eq!(
        f.state().stage.focus_history().first(),
        Some(&StageWindow::Client(b.clone())),
        "back at the front of the cycle"
    );
}

#[test]
fn focusing_a_minimized_window_restores_it() {
    let (mut f, _a, b) = two_windows();
    let id = f.state().stage.id_of(&b).unwrap().0;
    f.state().minimize_window(&b).unwrap();

    let reply = dispatch(Request::Focus(Some(WindowSelector::Id(id))), f.state());
    assert!(matches!(reply, Ok(Response::Focused(Some(_)))), "{reply:?}");
    settle(&mut f);

    assert!(!f.state().is_minimized(&b));
    assert_eq!(keyboard_focus(&mut f), Some(server_surface(&b)));
}

#[test]
fn the_state_reports_minimized_windows_with_their_place() {
    let (mut f, _a, b) = two_windows();
    let id = f.state().stage.id_of(&b).unwrap().0;
    f.state().minimize_window(&b).unwrap();

    let windows = f.state().window_inventory();
    let entry = windows
        .iter()
        .find(|w| w.id == id)
        .expect("a minimized window is still listed");
    assert!(entry.minimized);
    assert!(!entry.is_focused);
    assert!(windows.iter().filter(|w| w.id != id).all(|w| !w.minimized));
}

#[test]
fn a_dialog_minimizes_with_its_parent() {
    let mut f = Fixture::new();
    f.add_output(1, (1920, 1080));
    let cid = f.add_client();
    let parent_surface = map_window(&mut f, cid, "parent", (500, 400));
    let parent_toplevel = f.client(cid).window(&parent_surface).xdg_toplevel.clone();
    let dialog = f.client(cid).create_window();
    let dsurface = dialog.surface.clone();
    dialog.set_app_id("dialog");
    dialog.set_parent(Some(&parent_toplevel));
    dialog.commit();
    f.roundtrip(cid);
    let dwin = f.client(cid).window(&dsurface);
    dwin.set_size(300, 200);
    dwin.attach_new_buffer();
    dwin.ack_last_and_commit();
    f.double_roundtrip(cid);
    settle(&mut f);

    let parent = window_by_app_id(&mut f, "parent").unwrap();
    let dialog = window_by_app_id(&mut f, "dialog").unwrap();

    // Minimizing from the dialog takes the whole group.
    f.state().minimize_window(&dialog).unwrap();
    assert!(f.state().is_minimized(&parent));
    assert!(f.state().is_minimized(&dialog));

    // And restoring the parent brings the dialog back with it.
    f.state().restore_and_activate(&parent);
    settle(&mut f);
    assert!(!f.state().is_minimized(&parent));
    assert!(!f.state().is_minimized(&dialog));
}

#[test]
fn a_client_minimize_request_puts_it_away() {
    let mut f = Fixture::new();
    f.add_output(1, (1920, 1080));
    let id = f.add_client();
    let surface = map_window(&mut f, id, "app", (400, 300));
    origin_view(&mut f);
    settle(&mut f);
    let window = window_by_app_id(&mut f, "app").unwrap();

    f.client(id).window(&surface).xdg_toplevel.set_minimized();
    f.roundtrip(id);
    settle(&mut f);

    assert!(f.state().is_minimized(&window));
    assert!(keyboard_focus(&mut f).is_none(), "nothing left to focus");
}

#[test]
fn the_minimize_button_sits_left_of_close_and_minimizes() {
    let (mut f, _a, b) = two_windows();
    give_ssd(&mut f, &b);
    let loc = f.state().stage.position_of(&b).unwrap();
    let width = b.geometry().size.w;
    let bar = f.state().config.decorations.title_bar_height;
    let rect = crate::decorations::minimize_button_rect(loc, width, bar);
    let centre = Point::from((
        rect.loc.x as f64 + rect.size.w as f64 / 2.0,
        rect.loc.y as f64 + rect.size.h as f64 / 2.0,
    ));
    let maximize = crate::decorations::maximize_button_rect(loc, width, bar);
    assert_eq!(
        rect.loc.x + rect.size.w,
        maximize.loc.x,
        "flush left of maximize"
    );

    assert!(crate::decorations::window_controls_fit(width, bar));
    let hit = f.state().decoration_under(centre);
    assert!(
        matches!(hit, Some((DecoTarget::Client(ref w), DecorationHit::MinimizeButton)) if *w == b),
        "{:?}",
        hit.map(|(_, h)| h)
    );
}

#[test]
fn clicking_the_maximize_button_fits_the_window_and_clicking_again_restores_it() {
    let (mut f, _a, b) = two_windows();
    give_ssd(&mut f, &b);
    let mouse = FakeDevice::mouse();
    let click_maximize = |f: &mut Fixture| {
        let loc = f.state().stage.position_of(&b).unwrap();
        let width = b.geometry().size.w;
        let bar = f.state().config.decorations.title_bar_height;
        let rect = crate::decorations::maximize_button_rect(loc, width, bar);
        let centre = Point::from((
            rect.loc.x as f64 + rect.size.w as f64 / 2.0,
            rect.loc.y as f64 + rect.size.h as f64 / 2.0,
        ));
        let hit = f.state().decoration_under(centre);
        assert!(
            matches!(hit, Some((_, DecorationHit::MaximizeButton))),
            "{:?}",
            hit.map(|(_, h)| h)
        );
        f.state().with_output_state(|os| {
            os.camera = Point::from((centre.x - 960.0, centre.y - 540.0));
            os.camera_target = None;
            os.zoom = 1.0;
            os.zoom_target = None;
            os.zoom_animation_anchor = None;
        });
        pointer_to(f, &mouse, centre);
        press(f, &mouse, BTN_LEFT);
        release(f, &mouse, BTN_LEFT);
        settle(f);
    };

    click_maximize(&mut f);
    assert!(f.state().stage.is_fit(&b), "first click fits");
    click_maximize(&mut f);
    assert!(!f.state().stage.is_fit(&b), "second click restores");
}

#[test]
fn a_narrow_bar_keeps_its_drag_area_instead_of_a_minimize_button() {
    let bar = 25;
    assert!(!crate::decorations::window_controls_fit(100, bar));
    let loc = Point::from((0, 100));
    let rect = crate::decorations::minimize_button_rect(loc, 100, bar);
    let centre = Point::from((
        rect.loc.x as f64 + rect.size.w as f64 / 2.0,
        rect.loc.y as f64 + rect.size.h as f64 / 2.0,
    ));
    assert!(!crate::decorations::minimize_button_contains(
        centre, loc, 100, bar
    ));
    assert!(crate::decorations::title_bar_contains(
        centre, loc, 100, bar
    ));
}

#[test]
fn widgets_and_stand_ins_are_refused() {
    let (mut f, _a, _b) = two_windows();
    let sid = f.state().insert_suspended_for_test(
        1,
        Point::from((900, 300)),
        smithay::utils::Size::from((300, 200)),
        "s",
        "S",
    );
    let element = StageWindow::Suspended(f.state().find_suspended(sid).unwrap());
    let id = f.state().stage.id_of(&element).unwrap().0;
    assert!(minimize(&mut f, id, Some(true)).is_err());
    f.state().dismiss_suspended(sid);
    settle(&mut f);
}

#[test]
fn closing_a_minimized_window_leaves_nothing_behind() {
    let mut f = Fixture::new();
    f.add_output(1, (1920, 1080));
    let id = f.add_client();
    let surface = map_window(&mut f, id, "app", (400, 300));
    settle(&mut f);
    let window = window_by_app_id(&mut f, "app").unwrap();
    f.state().minimize_window(&window).unwrap();
    assert_eq!(f.state().minimized.len(), 1);

    f.client(id).window(&surface).destroy();
    f.roundtrip(id);
    settle(&mut f);
    assert!(f.state().minimized.is_empty());
}

#[test]
fn restore_minimized_brings_back_the_latest() {
    let (mut f, a, b) = two_windows();
    f.state().minimize_window(&a).unwrap();
    f.state().minimize_window(&b).unwrap();
    f.state()
        .execute_action(&driftwm::config::Action::RestoreMinimized);
    settle(&mut f);
    assert!(!f.state().is_minimized(&b), "the latest comes back first");
    assert!(f.state().is_minimized(&a));
    assert_eq!(keyboard_focus(&mut f), Some(server_surface(&b)));
}
