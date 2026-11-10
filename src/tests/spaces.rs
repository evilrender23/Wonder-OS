//! Spaces: a maximized or fullscreened window gets a screen of its own beside
//! the Desktop, `space-left` / `space-right` step along the row without
//! ending anything, and restoring, minimizing or closing the window ends its
//! space and puts it back on the canvas where it came from.

use smithay::desktop::Window;
use smithay::reexports::wayland_protocols::xdg::shell::server::xdg_toplevel;
use smithay::utils::Point;
use wayland_client::protocol::wl_surface::WlSurface;

use driftwm::config::{Action, Config, Direction, parse_action};

use super::client::ClientId;
use super::{
    Fixture, adopt_last_configure, config, keyboard_focus, map_window, server_surface, settle,
    window_by_app_id,
};

struct Scene {
    f: Fixture,
    id: ClientId,
    a: Window,
    b: Window,
    a_surface: WlSurface,
    b_surface: WlSurface,
}

/// Two windows on one 1920x1080 output, `b` focused, camera at the origin.
fn scene_with(config: Config) -> Scene {
    let mut f = Fixture::with_config(config);
    f.add_output(1, (1920, 1080));
    let id = f.add_client();
    let a_surface = map_window(&mut f, id, "a", (400, 300));
    let b_surface = map_window(&mut f, id, "b", (500, 350));
    f.state().with_output_state(|os| {
        os.camera = Point::from((0.0, 0.0));
        os.camera_target = None;
        os.zoom = 1.0;
        os.zoom_target = None;
        os.zoom_animation_anchor = None;
    });
    settle(&mut f);
    let a = window_by_app_id(&mut f, "a").unwrap();
    let b = window_by_app_id(&mut f, "b").unwrap();
    Scene {
        f,
        id,
        a,
        b,
        a_surface,
        b_surface,
    }
}

fn scene() -> Scene {
    scene_with(Config::default())
}

/// The client asks to be maximized and takes up the size it is given.
fn maximize(s: &mut Scene, surface: &WlSurface) {
    s.f.client(s.id).window(surface).set_maximized();
    s.f.double_roundtrip(s.id);
    adopt_last_configure(&mut s.f, s.id, surface);
    settle(&mut s.f);
}

/// `(spaces on the output, the one on screen)`.
fn row(f: &mut Fixture) -> (usize, usize) {
    let output = f.state().active_output().unwrap();
    (
        f.state().space_count_on(&output),
        f.state().active_space_on(&output),
    )
}

fn pending_state(window: &Window, state: xdg_toplevel::State) -> bool {
    window
        .toplevel()
        .expect("toplevel")
        .with_pending_state(|s| s.states.contains(state))
}

fn configured_size(window: &Window) -> (i32, i32) {
    let size = window
        .toplevel()
        .expect("toplevel")
        .with_pending_state(|s| s.size)
        .expect("a configured size");
    (size.w, size.h)
}

#[test]
fn maximizing_sends_the_window_to_a_new_space() {
    let mut s = scene();
    assert_eq!(row(&mut s.f), (1, 0), "only the Desktop to begin with");
    let b_surface = s.b_surface.clone();

    maximize(&mut s, &b_surface);

    assert_eq!(row(&mut s.f), (2, 1), "a space past the Desktop, on screen");
    let state = s.f.state();
    assert!(state.stage.is_fullscreen(&s.b), "it fills the output");
    assert!(
        !state.stage.is_fit(&s.b),
        "rather than being fit on the canvas"
    );
    assert_eq!(configured_size(&s.b), (1920, 1080));
    assert!(
        pending_state(&s.b, xdg_toplevel::State::Maximized),
        "the client is told it is maximized"
    );
    assert!(
        !pending_state(&s.b, xdg_toplevel::State::Fullscreen),
        "not fullscreen, so it keeps its own toolbars"
    );
    assert!(
        !state.hidden_from_canvas(&s.a),
        "the other window is still on the Desktop"
    );
    assert!(state.is_canvas_window(&s.a));
    assert_eq!(keyboard_focus(&mut s.f), Some(server_surface(&s.b)));
}

#[test]
fn a_fullscreen_request_makes_a_space_in_real_fullscreen() {
    let mut s = scene();
    s.f.client(s.id).window(&s.b_surface).set_fullscreen(None);
    s.f.double_roundtrip(s.id);

    assert_eq!(row(&mut s.f), (2, 1));
    assert!(pending_state(&s.b, xdg_toplevel::State::Fullscreen));
    assert!(!pending_state(&s.b, xdg_toplevel::State::Maximized));
}

#[test]
fn space_left_parks_the_space_and_space_right_returns_to_it() {
    let mut s = scene();
    let b_surface = s.b_surface.clone();
    maximize(&mut s, &b_surface);

    s.f.state().execute_action(&Action::SpaceLeft);
    s.f.double_roundtrip(s.id);

    assert_eq!(
        row(&mut s.f),
        (2, 0),
        "on the Desktop, the space still there"
    );
    let state = s.f.state();
    assert!(!state.is_fullscreen(), "the viewport is the canvas again");
    assert!(state.hidden_from_canvas(&s.b), "its window is on no canvas");
    assert!(!state.is_canvas_window(&s.b));
    assert!(state.stage.position_of(&s.b).is_some(), "but still alive");
    assert!(
        !state.stage.focus_history().iter().any(|w| *w == s.b),
        "and out of the focus cycle"
    );
    assert_eq!(configured_size(&s.b), (1920, 1080), "at its full size");
    assert!(pending_state(&s.b, xdg_toplevel::State::Maximized));
    assert_eq!(
        keyboard_focus(&mut s.f),
        Some(server_surface(&s.a)),
        "focus goes to a window on the Desktop"
    );

    s.f.state().execute_action(&Action::SpaceLeft);
    assert_eq!(row(&mut s.f), (2, 0), "nothing lies left of the Desktop");

    s.f.state().execute_action(&Action::SpaceRight);
    s.f.double_roundtrip(s.id);

    assert_eq!(row(&mut s.f), (2, 1));
    let state = s.f.state();
    assert!(state.stage.is_fullscreen(&s.b));
    assert!(!state.hidden_from_canvas(&s.b));
    assert_eq!(keyboard_focus(&mut s.f), Some(server_surface(&s.b)));
    assert_eq!(
        configured_size(&s.b),
        (1920, 1080),
        "the window never changed size across the round trip"
    );

    s.f.state().execute_action(&Action::SpaceRight);
    assert_eq!(row(&mut s.f), (2, 1), "no wrap-around past the last space");
}

#[test]
fn two_maximized_windows_line_up_in_the_order_they_were_made() {
    let mut s = scene();
    let (a_surface, b_surface) = (s.a_surface.clone(), s.b_surface.clone());
    maximize(&mut s, &b_surface);
    maximize(&mut s, &a_surface);

    assert_eq!(row(&mut s.f), (3, 2), "the newer space is on the right");
    assert!(s.f.state().stage.is_fullscreen(&s.a));
    assert!(s.f.state().hidden_from_canvas(&s.b), "the older one waits");
    assert_eq!(
        configured_size(&s.b),
        (1920, 1080),
        "still filling its space, not sent back to the canvas"
    );

    s.f.state().execute_action(&Action::SpaceLeft);
    assert_eq!(row(&mut s.f), (3, 1));
    assert!(s.f.state().stage.is_fullscreen(&s.b));
    assert!(s.f.state().hidden_from_canvas(&s.a));

    s.f.state().execute_action(&Action::SpaceLeft);
    assert_eq!(row(&mut s.f), (3, 0));
    assert!(s.f.state().hidden_from_canvas(&s.a));
    assert!(s.f.state().hidden_from_canvas(&s.b));

    s.f.state().execute_action(&Action::SpaceLeft);
    assert_eq!(row(&mut s.f), (3, 0));

    for expected in [1, 2, 2] {
        s.f.state().execute_action(&Action::SpaceRight);
        assert_eq!(row(&mut s.f), (3, expected));
    }
    assert!(s.f.state().stage.is_fullscreen(&s.a));
}

#[test]
fn unmaximizing_restores_the_window_and_removes_its_space() {
    let mut s = scene();
    let b_surface = s.b_surface.clone();
    let before = s.f.state().stage.position_of(&s.b);
    maximize(&mut s, &b_surface);

    s.f.client(s.id).window(&b_surface).unset_maximized();
    s.f.double_roundtrip(s.id);

    assert_eq!(row(&mut s.f), (1, 0), "back on the Desktop, the space gone");
    assert_eq!(configured_size(&s.b), (500, 350), "at the size it had");
    assert!(!pending_state(&s.b, xdg_toplevel::State::Maximized));
    adopt_last_configure(&mut s.f, s.id, &b_surface);
    settle(&mut s.f);
    let state = s.f.state();
    assert!(!state.stage.is_fullscreen(&s.b));
    assert_eq!(state.stage.position_of(&s.b), before, "where it was");
    assert!(!state.hidden_from_canvas(&s.b));
}

#[test]
fn unmaximizing_a_parked_space_restores_it_on_the_desktop() {
    let mut s = scene();
    let b_surface = s.b_surface.clone();
    let before = s.f.state().stage.position_of(&s.b);
    maximize(&mut s, &b_surface);
    s.f.state().execute_action(&Action::SpaceLeft);

    s.f.state().decoration_unfit(&s.b);
    s.f.double_roundtrip(s.id);

    assert_eq!(row(&mut s.f), (1, 0));
    assert_eq!(configured_size(&s.b), (500, 350));
    assert!(!pending_state(&s.b, xdg_toplevel::State::Maximized));
    adopt_last_configure(&mut s.f, s.id, &b_surface);
    settle(&mut s.f);
    let state = s.f.state();
    assert_eq!(state.stage.position_of(&s.b), before);
    assert!(!state.hidden_from_canvas(&s.b));
    assert!(state.is_canvas_window(&s.b));
}

#[test]
fn the_fit_binding_toggles_the_space() {
    let mut s = scene();
    s.f.state().execute_action(&Action::FitWindow);
    s.f.double_roundtrip(s.id);
    assert_eq!(row(&mut s.f), (2, 1), "the focused window went to a space");
    assert!(s.f.state().stage.is_fullscreen(&s.b));

    s.f.state().execute_action(&Action::FitWindow);
    s.f.double_roundtrip(s.id);
    assert_eq!(
        row(&mut s.f),
        (1, 0),
        "and the second press brought it back"
    );
    assert!(!s.f.state().stage.is_fullscreen(&s.b));
    assert!(!s.f.state().stage.is_fit(&s.b));
    assert_eq!(configured_size(&s.b), (500, 350));
}

#[test]
fn closing_the_window_of_a_space_removes_the_space() {
    let mut s = scene();
    let b_surface = s.b_surface.clone();
    maximize(&mut s, &b_surface);

    s.f.client(s.id).window(&b_surface).destroy();
    s.f.double_roundtrip(s.id);

    assert_eq!(row(&mut s.f), (1, 0));
    assert!(!s.f.state().is_fullscreen());
    assert!(s.f.state().spaces.is_empty());
}

#[test]
fn closing_the_window_of_a_parked_space_removes_the_space() {
    let mut s = scene();
    let (a_surface, b_surface) = (s.a_surface.clone(), s.b_surface.clone());
    maximize(&mut s, &b_surface);
    maximize(&mut s, &a_surface);

    s.f.client(s.id).window(&b_surface).destroy();
    s.f.double_roundtrip(s.id);

    assert_eq!(
        row(&mut s.f),
        (2, 1),
        "the space on screen moved up the row"
    );
    assert!(s.f.state().stage.is_fullscreen(&s.a));
}

#[test]
fn minimizing_the_window_of_a_space_ends_the_space() {
    let mut s = scene();
    let b_surface = s.b_surface.clone();
    maximize(&mut s, &b_surface);

    s.f.state().minimize_window(&s.b).unwrap();
    s.f.double_roundtrip(s.id);

    assert_eq!(row(&mut s.f), (1, 0));
    assert!(s.f.state().is_minimized(&s.b));
    assert_eq!(configured_size(&s.b), (500, 350));
}

#[test]
fn the_left_and_right_focus_keys_step_through_spaces_when_there_are_any() {
    let mut s = scene();
    s.f.state()
        .execute_action(&Action::FocusNearest(Direction::Right));
    assert_eq!(
        row(&mut s.f),
        (1, 0),
        "with only the Desktop they move focus"
    );

    let b_surface = s.b_surface.clone();
    maximize(&mut s, &b_surface);

    s.f.state()
        .execute_action(&Action::FocusNearest(Direction::Left));
    assert_eq!(row(&mut s.f), (2, 0), "left goes back to the Desktop");
    assert!(s.f.state().hidden_from_canvas(&s.b), "without ending it");

    s.f.state()
        .execute_action(&Action::FocusNearest(Direction::Right));
    assert_eq!(row(&mut s.f), (2, 1), "right returns to the space");
}

#[test]
fn heading_for_the_canvas_steps_aside_without_ending_the_space() {
    let mut s = scene();
    let b_surface = s.b_surface.clone();
    maximize(&mut s, &b_surface);

    s.f.state().execute_action(&Action::ZoomOut);

    assert_eq!(row(&mut s.f), (2, 0));
    assert_eq!(configured_size(&s.b), (1920, 1080));
}

#[test]
fn activating_a_parked_window_shows_its_space() {
    let mut s = scene();
    let b_surface = s.b_surface.clone();
    maximize(&mut s, &b_surface);
    s.f.state().execute_action(&Action::SpaceLeft);

    s.f.state().activate_window_output_local(&s.b);

    assert_eq!(row(&mut s.f), (2, 1));
    assert_eq!(keyboard_focus(&mut s.f), Some(server_surface(&s.b)));
}

#[test]
fn unplugging_the_output_puts_parked_windows_back_on_the_canvas() {
    let mut s = scene();
    let b_surface = s.b_surface.clone();
    maximize(&mut s, &b_surface);
    s.f.state().execute_action(&Action::SpaceLeft);
    let output = s.f.state().active_output().unwrap();
    // A survivor, so the unplugged output really goes away.
    s.f.add_output(2, (1920, 1080));

    s.f.remove_output(&output);

    assert!(s.f.state().spaces.is_empty());
    assert!(!s.f.state().hidden_from_canvas(&s.b));
    assert_eq!(configured_size(&s.b), (500, 350));
}

#[test]
fn with_the_option_off_maximize_fits_on_the_canvas() {
    let mut s = scene_with(config("[spaces]\nmaximize_to_space = false\n"));
    let (a_surface, b_surface) = (s.a_surface.clone(), s.b_surface.clone());

    s.f.client(s.id).window(&b_surface).set_maximized();
    s.f.double_roundtrip(s.id);

    assert_eq!(row(&mut s.f), (1, 0), "no space is made");
    assert!(
        s.f.state().stage.is_fit(&s.b),
        "the window is fit as before"
    );
    assert!(!s.f.state().stage.is_fullscreen(&s.b));

    s.f.client(s.id).window(&a_surface).set_fullscreen(None);
    s.f.double_roundtrip(s.id);
    assert!(s.f.state().stage.is_fullscreen(&s.a));
    assert_eq!(row(&mut s.f), (1, 0), "nor for a fullscreen");
    assert!(s.f.state().spaces.is_empty());

    s.f.state().execute_action(&Action::SpaceLeft);
    assert!(
        s.f.state().stage.is_fullscreen(&s.a),
        "and the space actions do nothing"
    );
}

#[test]
fn the_actions_and_the_option_parse() {
    assert_eq!(parse_action("space-left"), Ok(Action::SpaceLeft));
    assert_eq!(parse_action("space-right"), Ok(Action::SpaceRight));
    assert!(Config::default().maximize_to_space, "on by default");
    assert!(!config("[spaces]\nmaximize_to_space = false\n").maximize_to_space);
    let wheel = |up| {
        Config::default()
            .mouse_wheel_step_lookup_ctx(
                &smithay::input::keyboard::ModifiersState {
                    logo: true,
                    ctrl: true,
                    ..Default::default()
                },
                up,
                driftwm::config::BindingContext::OnWindow,
            )
            .cloned()
    };
    assert_eq!(
        wheel(true),
        Some(driftwm::config::MouseAction::Action(Action::SpaceLeft))
    );
    assert_eq!(
        wheel(false),
        Some(driftwm::config::MouseAction::Action(Action::SpaceRight))
    );
}
