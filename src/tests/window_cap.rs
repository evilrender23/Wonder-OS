//! The screen-fraction cap: a window's visual frame opens no larger than
//! nine tenths of its output per axis, and a drag or a `grow-window` step stops
//! there. The requests that name their own target — fit, fullscreen, fill,
//! resize-to-snap, a rule `size`, `driftwm msg resize` — are the ways past it.

use smithay::desktop::Window;
use smithay::utils::{Logical, Point, SERIAL_COUNTER, Size};

use super::client::ClientId;
use super::{
    Fixture, adopt_last_configure, config, last_configured, map_window, map_window_with_limits,
    window_by_app_id,
};
use crate::state::StageWindow;
use driftwm::config::{Action, Direction};

type ClientSurface = wayland_client::protocol::wl_surface::WlSurface;

/// Nine tenths of the 1920×1080 output every scenario here uses.
const CAP: (i32, i32) = (1728, 972);

fn frame_size(f: &mut Fixture, window: &Window) -> (i32, i32) {
    let rect = f
        .state()
        .visual_frame_rect(&StageWindow::Client(window.clone()))
        .expect("the window is mapped");
    (
        (rect.x_high - rect.x_low).round() as i32,
        (rect.y_high - rect.y_low).round() as i32,
    )
}

/// Open a window whose first buffer is `size`, without answering whatever the
/// compositor says about it.
fn open(f: &mut Fixture, id: ClientId, app_id: &str, size: (u16, u16)) -> ClientSurface {
    let window = f.client(id).create_window();
    let surface = window.surface.clone();
    window.set_app_id(app_id);
    window.commit();
    f.roundtrip(id);

    let window = f.client(id).window(&surface);
    window.set_size(size.0, size.1);
    window.attach_new_buffer();
    window.ack_last_and_commit();
    f.double_roundtrip(id);
    surface
}

fn focus(f: &mut Fixture, window: &Window) {
    let serial = SERIAL_COUNTER.next_serial();
    f.state().raise_and_focus(window, serial);
}

#[test]
fn a_window_that_opens_larger_than_the_cap_is_asked_down_to_it() {
    let mut f = Fixture::new();
    f.add_output(1, (1920, 1080));
    let id = f.add_client();
    let surface = open(&mut f, id, "big", (1920, 1080));

    adopt_last_configure(&mut f, id, &surface);

    let window = window_by_app_id(&mut f, "big").unwrap();
    assert_eq!(frame_size(&mut f, &window), CAP);
}

#[test]
fn only_the_axis_past_the_cap_is_shrunk() {
    let mut f = Fixture::new();
    f.add_output(1, (1920, 1080));
    let id = f.add_client();
    let surface = open(&mut f, id, "wide", (1900, 300));

    let (_, h) = last_configured(&mut f, id, &surface);
    assert_eq!(h, 300, "the height was under the cap and is left alone");
    adopt_last_configure(&mut f, id, &surface);

    let window = window_by_app_id(&mut f, "wide").unwrap();
    assert_eq!(frame_size(&mut f, &window).0, CAP.0);
}

#[test]
fn the_first_configure_tells_the_client_the_cap_as_its_bounds() {
    let mut f = Fixture::new();
    f.add_output(1, (1920, 1080));
    let id = f.add_client();
    let window = f.client(id).create_window();
    let surface = window.surface.clone();
    window.commit();
    f.roundtrip(id);

    let (bw, bh) = f.client(id).window(&surface).configures_received[0]
        .1
        .bounds
        .expect("the initial configure carries bounds");
    assert!(bw > 0 && bw <= CAP.0, "bounds width {bw}");
    assert!(bh > 0 && bh <= CAP.1, "bounds height {bh}");
}

#[test]
fn a_window_under_the_cap_opens_at_its_own_size_with_no_extra_configure() {
    let mut f = Fixture::new();
    f.add_output(1, (1920, 1080));
    let id = f.add_client();
    let _surface = open(&mut f, id, "small", (800, 600));

    let window = window_by_app_id(&mut f, "small").unwrap();
    assert_eq!(window.geometry().size, Size::from((800, 600)));
    assert!(
        f.state().stage.position_of(&window).is_some(),
        "placed on its first sized commit"
    );
    assert!(f.state().pending_center.is_empty());
    assert!(f.state().pending_size.is_empty());
}

/// The cap is one request. A client that answers it at the size it came with is
/// placed as it is, and is not asked again by later configures.
#[test]
fn a_client_that_keeps_its_size_is_placed_and_not_asked_again() {
    let mut f = Fixture::new();
    f.add_output(1, (1920, 1080));
    let id = f.add_client();
    let surface = open(&mut f, id, "stubborn", (1900, 1000));
    let window = window_by_app_id(&mut f, "stubborn").unwrap();

    f.client(id).window(&surface).ack_last_and_commit();
    f.double_roundtrip(id);

    assert_eq!(window.geometry().size, Size::from((1900, 1000)));
    assert!(f.state().stage.position_of(&window).is_some());
    assert_eq!(
        super::configured_size(&window),
        Size::from((1900, 1000)),
        "the compositor's own idea of the size follows the client's answer"
    );
}

#[test]
fn a_declared_minimum_above_the_cap_outranks_it() {
    let mut f = Fixture::new();
    f.add_output(1, (1920, 1080));
    let id = f.add_client();
    let _ = map_window_with_limits(&mut f, id, "fixed", (1900, 1000), (1900, 1000), (0, 0));

    let window = window_by_app_id(&mut f, "fixed").unwrap();
    assert_eq!(window.geometry().size, Size::from((1900, 1000)));
    assert!(
        f.state().stage.position_of(&window).is_some(),
        "nothing was asked of it, so it is placed on its first sized commit"
    );
}

#[test]
fn a_rule_size_past_the_cap_is_honored() {
    let mut f = Fixture::with_config(config(
        "[[window_rules]]\napp_id = \"ruled\"\nsize = [1900, 1000]\n",
    ));
    f.add_output(1, (1920, 1080));
    let id = f.add_client();
    let surface = open(&mut f, id, "ruled", (400, 300));
    adopt_last_configure(&mut f, id, &surface);

    let window = window_by_app_id(&mut f, "ruled").unwrap();
    assert_eq!(frame_size(&mut f, &window), (1900, 1000));
}

#[test]
fn a_grow_step_stops_at_the_cap() {
    let mut f = Fixture::with_config(config("[navigation]\nresize_step = 5000\n"));
    f.add_output(1, (1920, 1080));
    let id = f.add_client();
    let surface = map_window(&mut f, id, "a", (400, 300));
    let window = window_by_app_id(&mut f, "a").unwrap();
    f.state().map_window(
        StageWindow::Client(window.clone()),
        Point::from((100, 100)),
        true,
    );
    focus(&mut f, &window);

    f.state()
        .execute_action(&Action::GrowWindow(Direction::Right));
    f.double_roundtrip(id);
    adopt_last_configure(&mut f, id, &surface);
    f.state()
        .execute_action(&Action::GrowWindow(Direction::Down));
    f.double_roundtrip(id);
    adopt_last_configure(&mut f, id, &surface);

    assert_eq!(frame_size(&mut f, &window), CAP);
}

/// A shrink never meets the cap: a window already past it walks down from where
/// it is rather than dropping to the cap first.
#[test]
fn a_shrink_step_of_a_window_past_the_cap_walks_from_its_own_size() {
    let mut f = Fixture::with_config(config("[navigation]\nresize_step = 20\n"));
    f.add_output(1, (1920, 1080));
    let id = f.add_client();
    let surface = map_window(&mut f, id, "a", (1900, 300));
    let window = window_by_app_id(&mut f, "a").unwrap();
    focus(&mut f, &window);

    f.state()
        .execute_action(&Action::ShrinkWindow(Direction::Right));

    assert_eq!(last_configured(&mut f, id, &surface), (1880, 300));
}

#[test]
fn a_drag_resize_stops_at_the_cap() {
    let mut f = Fixture::new();
    f.add_output(1, (1920, 1080));
    f.state().with_output_state(|os| {
        os.zoom = 1.0;
        os.camera = Point::from((0.0, 0.0));
    });
    let id = f.add_client();
    let surface = map_window(&mut f, id, "a", (400, 300));
    let window = window_by_app_id(&mut f, "a").unwrap();
    f.state().map_window(
        StageWindow::Client(window.clone()),
        Point::from((100, 100)),
        true,
    );

    let grab: Point<f64, Logical> = Point::from((480.0, 380.0));
    assert!(f.state().try_start_gesture_resize(grab, false));
    let pointer = f.state().seat.get_pointer().unwrap();
    let event = smithay::input::pointer::MotionEvent {
        location: Point::from((9000.0, 9000.0)),
        serial: SERIAL_COUNTER.next_serial(),
        time: 0,
    };
    pointer.motion(f.state(), None, &event);
    f.double_roundtrip(id);
    adopt_last_configure(&mut f, id, &surface);
    let serial = SERIAL_COUNTER.next_serial();
    pointer.unset_grab(f.state(), serial, 0);

    assert_eq!(frame_size(&mut f, &window), CAP);
}

#[test]
fn fit_still_fills_past_the_cap() {
    let mut f = Fixture::new();
    f.add_output(1, (1920, 1080));
    let id = f.add_client();
    let surface = map_window(&mut f, id, "a", (400, 300));
    let window = window_by_app_id(&mut f, "a").unwrap();

    f.state().toggle_fit_window(&window);
    let (w, h) = last_configured(&mut f, id, &surface);

    assert!(w > CAP.0 && h > CAP.1, "fit asked for {w}×{h}");
}
