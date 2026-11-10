//! Tab groups: server-decorated windows of one app share a frame. One member
//! is on the canvas, the rest are hidden behind it, and the bar's tabs — or the
//! tab actions, or anything that activates a hidden member — switch between
//! them in place.

use smithay::desktop::Window;
use smithay::reexports::wayland_protocols::xdg::shell::server::xdg_toplevel;
use smithay::reexports::wayland_server::Resource;
use smithay::utils::{Logical, Point, Size};
use wayland_client::protocol::wl_surface::WlSurface as ClientSurface;

use driftwm::config::{Action, BTN_LEFT, DecorationMode};

use crate::decorations::{DecorationHit, DecorationKey};
use crate::input::DecoTarget;
use crate::ipc::dispatch;
use crate::ipc::protocol::{Request, Response, WindowSelector};

use super::client::ClientId;
use super::input_backend::{FakeDevice, pointer_to, press, release, touch_down, touch_up};
use super::{Fixture, keyboard_focus, map_window, server_surface, settle};

/// A fixture whose windows get the server-side title bar on their first sized
/// commit, with one client connected.
fn ssd_fixture() -> (Fixture, ClientId) {
    let mut f = Fixture::new();
    f.add_output(1, (1920, 1080));
    f.state().config.decorations.default_mode = DecorationMode::Server;
    let id = f.add_client();
    (f, id)
}

/// Map a window and hand back both ends of it.
fn open(f: &mut Fixture, id: ClientId, app_id: &str, size: (u16, u16)) -> (ClientSurface, Window) {
    let before: Vec<Window> = clients(f);
    let surface = map_window(f, id, app_id, size);
    settle(f);
    let window = clients(f)
        .into_iter()
        .find(|w| !before.contains(w))
        .expect("the new window is on the stage");
    (surface, window)
}

fn clients(f: &mut Fixture) -> Vec<Window> {
    f.state()
        .stage
        .windows()
        .filter_map(|w| w.client())
        .cloned()
        .collect()
}

fn position(f: &mut Fixture, window: &Window) -> Point<i32, Logical> {
    f.state().stage.position_of(window).unwrap()
}

fn configured_size(window: &Window) -> Option<Size<i32, Logical>> {
    window.toplevel().unwrap().with_pending_state(|s| s.size)
}

fn told_suspended(window: &Window) -> bool {
    window
        .toplevel()
        .unwrap()
        .with_pending_state(|s| s.states.contains(xdg_toplevel::State::Suspended))
}

/// `(tab count, active index)` of the bar `window` wears, once a frame has been
/// through the render pass's bookkeeping.
fn bar_tabs(f: &mut Fixture, window: &Window) -> Option<(usize, usize)> {
    f.state().tab_bar_of(&server_surface(window))
}

/// Canvas-space centre of tab `index` on the bar `front` wears.
fn tab_centre(f: &mut Fixture, front: &Window, index: usize) -> Point<f64, Logical> {
    let loc = position(f, front);
    let width = front.geometry().size.w;
    let bar = f.state().config.decorations.title_bar_height;
    let (count, active) = bar_tabs(f, front).expect("front of a group");
    let (_, rect) = crate::decorations::tab_rects(width, bar, count, active)
        .into_iter()
        .find(|(i, _)| *i == index)
        .expect("tab is laid out");
    Point::from((
        loc.x as f64 + rect.loc.x + rect.size.w / 2.0,
        (loc.y - bar) as f64 + rect.loc.y + rect.size.h / 2.0,
    ))
}

fn centre_view_on(f: &mut Fixture, at: Point<f64, Logical>) {
    f.state().with_output_state(|os| {
        os.camera = Point::from((at.x - 960.0, at.y - 540.0));
        os.camera_target = None;
        os.zoom = 1.0;
        os.zoom_target = None;
        os.zoom_animation_anchor = None;
    });
}

fn has_bar(f: &mut Fixture, window: &Window) -> bool {
    let id = server_surface(window).id();
    f.state()
        .decorations
        .contains_key(&DecorationKey::Surface(id))
}

#[test]
fn a_second_window_of_the_app_opens_as_the_active_tab_in_the_first_ones_frame() {
    let (mut f, id) = ssd_fixture();
    let (_, a) = open(&mut f, id, "term", (400, 300));
    assert!(has_bar(&mut f, &a));
    assert!(f.state().tab_groups.is_empty(), "one window is no group");
    let frame = position(&mut f, &a);

    let (_, b) = open(&mut f, id, "term", (520, 360));

    let state = f.state();
    assert_eq!(state.tab_groups.len(), 1);
    assert_eq!(state.tab_groups[0].members.len(), 2);
    assert_eq!(state.tab_groups[0].active, 1, "the newcomer is in front");
    assert!(state.hidden_from_canvas(&a));
    assert!(!state.hidden_from_canvas(&b));
    assert_eq!(state.stage.position_of(&b), Some(frame));
    assert_eq!(
        configured_size(&b),
        Some(Size::from((400, 300))),
        "configured to the frame's size"
    );
    assert!(told_suspended(&a), "the hidden tab stops drawing");
    assert!(!told_suspended(&b));
    assert_eq!(bar_tabs(&mut f, &b), Some((2, 1)));
    assert_eq!(bar_tabs(&mut f, &a), None, "a hidden tab fronts nothing");
    assert_eq!(keyboard_focus(&mut f), Some(server_surface(&b)));

    let visible: Vec<Window> = clients(&mut f)
        .into_iter()
        .filter(|w| !f.state().hidden_from_canvas(w))
        .collect();
    assert_eq!(
        visible,
        std::slice::from_ref(&b),
        "one window on the canvas"
    );

    let (titles, active) = f.state().tab_titles_of(&server_surface(&b)).unwrap();
    assert_eq!((titles.len(), active), (2, 1));
}

#[test]
fn a_third_window_joins_the_same_frame() {
    let (mut f, id) = ssd_fixture();
    let (_, a) = open(&mut f, id, "term", (400, 300));
    let frame = position(&mut f, &a);
    let (_, b) = open(&mut f, id, "term", (400, 300));
    let (_, c) = open(&mut f, id, "term", (400, 300));

    assert_eq!(f.state().tab_groups.len(), 1);
    assert_eq!(bar_tabs(&mut f, &c), Some((3, 2)));
    assert!(f.state().hidden_from_canvas(&a));
    assert!(f.state().hidden_from_canvas(&b));
    assert_eq!(position(&mut f, &c), frame);
}

#[test]
fn a_mouse_press_on_an_inactive_tab_switches_to_it_in_place() {
    let (mut f, id) = ssd_fixture();
    let (_, a) = open(&mut f, id, "term", (400, 300));
    let (_, b) = open(&mut f, id, "term", (400, 300));
    let frame = position(&mut f, &b);

    let on_first = tab_centre(&mut f, &b, 0);
    let on_second = tab_centre(&mut f, &b, 1);
    centre_view_on(&mut f, on_first);
    let hit = f.state().decoration_under(on_first);
    assert!(
        matches!(hit, Some((DecoTarget::Client(ref w), DecorationHit::Tab(0))) if *w == b),
        "{:?}",
        hit.map(|(_, h)| h)
    );
    let hit = f.state().decoration_under(on_second);
    assert!(
        matches!(hit, Some((_, DecorationHit::TitleBar))),
        "the active tab is the bar's drag zone: {:?}",
        hit.map(|(_, h)| h)
    );

    let mouse = FakeDevice::mouse();
    pointer_to(&mut f, &mouse, on_first);
    press(&mut f, &mouse, BTN_LEFT);
    release(&mut f, &mouse, BTN_LEFT);
    settle(&mut f);

    assert!(!f.state().hidden_from_canvas(&a), "the clicked tab shows");
    assert!(f.state().hidden_from_canvas(&b));
    assert_eq!(position(&mut f, &a), frame, "the frame didn't move");
    assert_eq!(bar_tabs(&mut f, &a), Some((2, 0)));
    assert_eq!(keyboard_focus(&mut f), Some(server_surface(&a)));
    assert!(!told_suspended(&a));
    assert!(told_suspended(&b));
}

#[test]
fn a_tap_on_an_inactive_tab_switches_to_it() {
    let (mut f, id) = ssd_fixture();
    let (_, a) = open(&mut f, id, "term", (400, 300));
    let (_, b) = open(&mut f, id, "term", (400, 300));
    let frame = position(&mut f, &b);

    let on_first = tab_centre(&mut f, &b, 0);
    centre_view_on(&mut f, on_first);
    touch_down(&mut f, on_first, 0);
    touch_up(&mut f, 0);
    settle(&mut f);

    assert!(!f.state().hidden_from_canvas(&a));
    assert!(f.state().hidden_from_canvas(&b));
    assert_eq!(position(&mut f, &a), frame);
}

#[test]
fn closing_the_active_tab_reveals_the_other_and_dissolves_the_group() {
    let (mut f, id) = ssd_fixture();
    let (_, a) = open(&mut f, id, "term", (400, 300));
    let (b_surface, b) = open(&mut f, id, "term", (400, 300));
    let frame = position(&mut f, &b);

    f.client(id).window(&b_surface).destroy();
    f.roundtrip(id);
    settle(&mut f);

    let state = f.state();
    assert!(state.tab_groups.is_empty(), "one window left is no group");
    assert!(state.tab_hidden.is_empty());
    assert!(!state.hidden_from_canvas(&a));
    assert_eq!(state.stage.position_of(&a), Some(frame));
    assert!(!told_suspended(&a));
    assert_eq!(bar_tabs(&mut f, &a), None);
    assert_eq!(keyboard_focus(&mut f), Some(server_surface(&a)));
}

#[test]
fn closing_a_hidden_tab_leaves_the_front_one_alone() {
    let (mut f, id) = ssd_fixture();
    let (a_surface, _a) = open(&mut f, id, "term", (400, 300));
    let (_, b) = open(&mut f, id, "term", (400, 300));
    let (_, c) = open(&mut f, id, "term", (400, 300));
    let frame = position(&mut f, &c);

    f.client(id).window(&a_surface).destroy();
    f.roundtrip(id);
    settle(&mut f);

    assert_eq!(bar_tabs(&mut f, &c), Some((2, 1)));
    assert_eq!(position(&mut f, &c), frame);
    assert!(f.state().hidden_from_canvas(&b));
    assert_eq!(keyboard_focus(&mut f), Some(server_surface(&c)));
}

#[test]
fn windows_of_another_app_or_without_our_bar_keep_their_own_frame() {
    let (mut f, id) = ssd_fixture();
    let (_, a) = open(&mut f, id, "term", (400, 300));
    let (_, other) = open(&mut f, id, "editor", (400, 300));
    assert!(f.state().tab_groups.is_empty(), "another app");
    assert!(!f.state().hidden_from_canvas(&a));
    assert!(!f.state().hidden_from_canvas(&other));

    // Client-side decorated from here on: same app, but no bar of ours.
    f.state().config.decorations.default_mode = DecorationMode::Client;
    let (_, csd) = open(&mut f, id, "term", (400, 300));
    assert!(!has_bar(&mut f, &csd));
    assert!(f.state().tab_groups.is_empty(), "client-side decorated");
    assert!(!f.state().hidden_from_canvas(&a));
}

#[test]
fn a_dialog_stays_a_window_and_hides_with_its_tab() {
    let (mut f, id) = ssd_fixture();
    let (a_surface, a) = open(&mut f, id, "term", (500, 400));

    let before = clients(&mut f);
    let parent = f.client(id).window(&a_surface).xdg_toplevel.clone();
    let dialog = f.client(id).create_window();
    let dialog_surface = dialog.surface.clone();
    dialog.set_app_id("term");
    dialog.set_parent(Some(&parent));
    dialog.commit();
    f.roundtrip(id);
    let dialog = f.client(id).window(&dialog_surface);
    dialog.set_size(300, 200);
    dialog.attach_new_buffer();
    dialog.ack_last_and_commit();
    f.double_roundtrip(id);
    settle(&mut f);
    let dialog = clients(&mut f)
        .into_iter()
        .find(|w| !before.contains(w))
        .unwrap();

    assert!(f.state().tab_groups.is_empty(), "a dialog is never a tab");
    assert!(!f.state().hidden_from_canvas(&a));
    assert!(!f.state().hidden_from_canvas(&dialog));

    // A second root window tabs with the parent; the dialog goes with it.
    let (_, b) = open(&mut f, id, "term", (500, 400));
    assert_eq!(bar_tabs(&mut f, &b), Some((2, 1)));
    assert!(f.state().hidden_from_canvas(&a));
    assert!(f.state().hidden_from_canvas(&dialog));

    f.state().execute_action(&Action::PrevTab);
    settle(&mut f);
    assert!(!f.state().hidden_from_canvas(&a));
    assert!(
        !f.state().hidden_from_canvas(&dialog),
        "back with its parent"
    );
    assert!(f.state().hidden_from_canvas(&b));
}

#[test]
fn tabs_off_gives_every_window_its_own_frame() {
    let (mut f, id) = ssd_fixture();
    f.state().config.decorations.tabs = false;
    let (_, a) = open(&mut f, id, "term", (400, 300));
    let (_, b) = open(&mut f, id, "term", (400, 300));

    assert!(f.state().tab_groups.is_empty());
    assert!(!f.state().hidden_from_canvas(&a));
    assert!(!f.state().hidden_from_canvas(&b));
    assert!(has_bar(&mut f, &a) && has_bar(&mut f, &b));
}

#[test]
fn turning_tabs_off_on_reload_splits_the_frames_up() {
    let (mut f, id) = ssd_fixture();
    let (_, a) = open(&mut f, id, "term", (400, 300));
    let (_, b) = open(&mut f, id, "term", (400, 300));
    let (_, c) = open(&mut f, id, "term", (400, 300));
    let frame = position(&mut f, &c);
    let bar = f.state().config.decorations.title_bar_height;

    f.state()
        .reload_config_from_contents("[decorations]\ndefault_mode = \"server\"\ntabs = false\n");
    f.state().pending_mode_changes.clear();
    settle(&mut f);

    assert!(f.state().tab_groups.is_empty());
    assert!(f.state().tab_hidden.is_empty());
    for w in [&a, &b, &c] {
        assert!(!f.state().hidden_from_canvas(w));
        assert!(!told_suspended(w));
    }
    assert_eq!(position(&mut f, &c), frame);
    assert_eq!(position(&mut f, &a), frame + Point::from((bar, bar)));
    assert_eq!(
        position(&mut f, &b),
        frame + Point::from((2 * bar, 2 * bar))
    );
}

#[test]
fn next_and_prev_tab_cycle_the_focused_frame_wrapping() {
    let (mut f, id) = ssd_fixture();
    let (_, a) = open(&mut f, id, "term", (400, 300));
    let (_, b) = open(&mut f, id, "term", (400, 300));
    let (_, c) = open(&mut f, id, "term", (400, 300));
    let frame = position(&mut f, &c);
    let front = |f: &mut Fixture| f.state().tab_groups[0].active;
    assert_eq!(front(&mut f), 2);

    f.state().execute_action(&Action::NextTab);
    settle(&mut f);
    assert_eq!(front(&mut f), 0, "wraps past the last");
    assert!(!f.state().hidden_from_canvas(&a));
    assert_eq!(position(&mut f, &a), frame);
    assert_eq!(keyboard_focus(&mut f), Some(server_surface(&a)));

    f.state().execute_action(&Action::NextTab);
    settle(&mut f);
    assert_eq!(front(&mut f), 1);
    assert_eq!(keyboard_focus(&mut f), Some(server_surface(&b)));

    f.state().execute_action(&Action::PrevTab);
    f.state().execute_action(&Action::PrevTab);
    settle(&mut f);
    assert_eq!(front(&mut f), 2, "wraps before the first");
    assert_eq!(position(&mut f, &c), frame);
    assert!(f.state().hidden_from_canvas(&a) && f.state().hidden_from_canvas(&b));
}

#[test]
fn detach_tab_gives_the_window_its_own_frame_for_good() {
    let (mut f, id) = ssd_fixture();
    let (_, a) = open(&mut f, id, "term", (400, 300));
    let (_, b) = open(&mut f, id, "term", (400, 300));
    let (_, c) = open(&mut f, id, "term", (400, 300));
    let frame = position(&mut f, &c);
    let bar = f.state().config.decorations.title_bar_height;

    f.state().execute_action(&Action::DetachTab);
    settle(&mut f);

    assert!(!f.state().hidden_from_canvas(&c));
    assert_eq!(f.state().tab_group_index_of(&server_surface(&c)), None);
    assert_eq!(position(&mut f, &c), frame + Point::from((bar, bar)));
    assert_eq!(keyboard_focus(&mut f), Some(server_surface(&c)));
    assert_eq!(bar_tabs(&mut f, &b), Some((2, 1)), "the frame closes ranks");
    assert_eq!(position(&mut f, &b), frame);
    assert!(f.state().hidden_from_canvas(&a));

    // A window opened later joins the frame that was left, not the loner.
    let (_, d) = open(&mut f, id, "term", (400, 300));
    assert_eq!(bar_tabs(&mut f, &d), Some((3, 2)));
    assert_eq!(position(&mut f, &d), frame);
    assert_eq!(f.state().tab_group_index_of(&server_surface(&c)), None);
    assert!(!f.state().hidden_from_canvas(&c));
}

#[test]
fn a_detached_last_pair_never_regroups_with_each_other() {
    let (mut f, id) = ssd_fixture();
    let (_, a) = open(&mut f, id, "term", (400, 300));
    let (_, b) = open(&mut f, id, "term", (400, 300));

    f.state().execute_action(&Action::DetachTab);
    settle(&mut f);
    assert!(f.state().tab_groups.is_empty());
    assert!(!f.state().hidden_from_canvas(&a) && !f.state().hidden_from_canvas(&b));

    // `b` was detached; a newcomer tabs with `a`.
    let (_, c) = open(&mut f, id, "term", (400, 300));
    assert_eq!(bar_tabs(&mut f, &c), Some((2, 1)));
    assert!(f.state().hidden_from_canvas(&a));
    assert!(!f.state().hidden_from_canvas(&b));
}

#[test]
fn focusing_a_hidden_tab_over_ipc_brings_it_to_the_front() {
    let (mut f, id) = ssd_fixture();
    let (_, a) = open(&mut f, id, "term", (400, 300));
    let (_, b) = open(&mut f, id, "term", (400, 300));
    let frame = position(&mut f, &b);
    let a_id = f.state().stage.id_of(&a).unwrap().0;

    let reply = dispatch(Request::Focus(Some(WindowSelector::Id(a_id))), f.state());
    assert!(matches!(reply, Ok(Response::Focused(_))), "{reply:?}");
    settle(&mut f);

    assert!(!f.state().hidden_from_canvas(&a));
    assert!(f.state().hidden_from_canvas(&b));
    assert_eq!(position(&mut f, &a), frame);
    assert_eq!(keyboard_focus(&mut f), Some(server_surface(&a)));
}

#[test]
fn activating_a_hidden_tab_switches_its_frame() {
    let (mut f, id) = ssd_fixture();
    let (_, a) = open(&mut f, id, "term", (400, 300));
    let (_, b) = open(&mut f, id, "term", (400, 300));

    f.state().restore_and_activate(&a);
    settle(&mut f);

    assert!(!f.state().hidden_from_canvas(&a));
    assert!(f.state().hidden_from_canvas(&b));
    assert_eq!(keyboard_focus(&mut f), Some(server_surface(&a)));
}

#[test]
fn minimizing_the_frame_keeps_the_other_tabs_put_away() {
    let (mut f, id) = ssd_fixture();
    let (_, a) = open(&mut f, id, "term", (400, 300));
    let (_, b) = open(&mut f, id, "term", (400, 300));
    let frame = position(&mut f, &b);

    f.state().minimize_window(&b).unwrap();
    settle(&mut f);
    assert!(f.state().is_minimized(&b));
    assert!(f.state().hidden_from_canvas(&a), "no other tab pops up");
    assert!(keyboard_focus(&mut f).is_none());

    f.state().restore_and_activate(&b);
    settle(&mut f);
    assert!(!f.state().hidden_from_canvas(&b));
    assert!(f.state().hidden_from_canvas(&a));
    assert_eq!(position(&mut f, &b), frame);

    // Activating the hidden tab of a minimized frame brings the frame back
    // with that tab in it.
    f.state().minimize_window(&b).unwrap();
    f.state().restore_and_activate(&a);
    settle(&mut f);
    assert!(!f.state().hidden_from_canvas(&a));
    assert!(f.state().hidden_from_canvas(&b));
    assert!(
        !f.state().is_minimized(&b),
        "it is a hidden tab, not docked"
    );
    assert_eq!(position(&mut f, &a), frame);
}

#[test]
fn the_state_reports_tab_membership() {
    let (mut f, id) = ssd_fixture();
    let (_, a) = open(&mut f, id, "term", (400, 300));
    let (_, b) = open(&mut f, id, "term", (400, 300));
    let (_, lone) = open(&mut f, id, "editor", (400, 300));
    let ids = [&a, &b, &lone].map(|w| f.state().stage.id_of(w).unwrap().0);

    let Ok(Response::State(state)) = dispatch(Request::State, f.state()) else {
        panic!("state reply");
    };
    let info = |id: u64| state.windows.iter().find(|w| w.id == id).unwrap();
    let group = info(ids[0]).tab_group.expect("grouped");
    assert_eq!(info(ids[1]).tab_group, Some(group));
    assert!(!info(ids[0]).tab_active);
    assert!(info(ids[1]).tab_active);
    assert_eq!(info(ids[2]).tab_group, None);
    assert!(info(ids[2]).tab_active);
}

#[test]
fn closing_every_tab_leaves_nothing_behind() {
    let (mut f, id) = ssd_fixture();
    let (a_surface, _) = open(&mut f, id, "term", (400, 300));
    let (b_surface, _) = open(&mut f, id, "term", (400, 300));
    let (c_surface, _) = open(&mut f, id, "term", (400, 300));
    f.state().execute_action(&Action::DetachTab);
    settle(&mut f);

    for surface in [b_surface, a_surface, c_surface] {
        f.client(id).window(&surface).destroy();
        f.roundtrip(id);
        settle(&mut f);
    }
    let state = f.state();
    assert!(state.tab_groups.is_empty());
    assert!(state.tab_hidden.is_empty());
    assert!(state.tab_detached.is_empty());
}
