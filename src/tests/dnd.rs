//! Drag-and-drop with a mouse: a drag started on a press reaches the window
//! the pointer is dragged onto, whether it starts in a window or in a layer
//! surface (desktop icons live in a bottom layer).

use driftwm::config::BTN_LEFT;
use smithay::utils::{Logical, Point};
use wayland_protocols_wlr::layer_shell::v1::client::{zwlr_layer_shell_v1, zwlr_layer_surface_v1};

use super::client::{ClientId, DataDeviceEvent, LayerConfigureProps};
use super::input_backend::{FakeDevice, pointer_to, pointer_to_screen, press, release};
use super::{Fixture, window_by_app_id};

fn map_window(
    f: &mut Fixture,
    id: ClientId,
    app_id: &str,
    at: (i32, i32),
) -> (
    wayland_client::protocol::wl_surface::WlSurface,
    Point<f64, Logical>,
) {
    let window = f.client(id).create_window();
    let surface = window.surface.clone();
    window.set_app_id(app_id);
    window.commit();
    f.roundtrip(id);
    let window = f.client(id).window(&surface);
    window.set_size(300, 300);
    window.attach_new_buffer();
    window.ack_last_and_commit();
    f.double_roundtrip(id);
    let w = window_by_app_id(f, app_id).unwrap();
    f.state().stage.set_position(&w, Point::from(at));
    let origin = f.state().stage.position_of(&w).unwrap().to_f64();
    (surface, origin)
}

fn drop_target(
    f: &mut Fixture,
) -> (
    ClientId,
    wayland_client::protocol::wl_surface::WlSurface,
    Point<f64, Logical>,
) {
    let b = f.add_client();
    let (surface, origin) = map_window(f, b, "target", (450, -150));
    f.client(b).get_data_device();
    f.client(b).state.accept_drags = true;
    f.double_roundtrip(b);
    (b, surface, origin)
}

fn assert_offered(
    f: &mut Fixture,
    b: ClientId,
    surface: &wayland_client::protocol::wl_surface::WlSurface,
) {
    let events = &f.client(b).state.data_device_events;
    assert!(
        events.contains(&DataDeviceEvent::Enter {
            surface: surface.clone()
        }),
        "the drag must be offered to the window under the pointer: {events:?}"
    );
    assert!(
        events.contains(&DataDeviceEvent::Drop),
        "and dropped there: {events:?}"
    );
}

#[test]
fn a_mouse_drag_reaches_another_window() {
    let mut f = Fixture::new();
    f.add_output(1, (1920, 1080));
    let a = f.add_client();
    let (a_surface, a_origin) = map_window(&mut f, a, "source", (-500, -150));
    f.client(a).get_data_device();
    f.double_roundtrip(a);
    let (b, b_surface, b_origin) = drop_target(&mut f);
    let mouse = FakeDevice::mouse();

    pointer_to(&mut f, &mouse, a_origin + Point::from((100.0, 100.0)));
    press(&mut f, &mouse, BTN_LEFT);
    f.double_roundtrip(a);
    let serial = f
        .client(a)
        .state
        .pointer_button_serial
        .expect("press reached the source");
    f.client(a).start_drag(&a_surface, serial);
    f.roundtrip(a);
    pointer_to(&mut f, &mouse, a_origin + Point::from((120.0, 110.0)));
    pointer_to(&mut f, &mouse, b_origin + Point::from((100.0, 100.0)));
    f.double_roundtrip(b);
    pointer_to(&mut f, &mouse, b_origin + Point::from((110.0, 100.0)));
    release(&mut f, &mouse, BTN_LEFT);
    f.double_roundtrip(b);
    assert_offered(&mut f, b, &b_surface);
}

#[test]
fn a_mouse_drag_from_a_bottom_layer_reaches_a_window() {
    from_bottom_layer((200, 200));
}

/// The desktop icons' layer covers the whole screen, and takes all input
/// while one of its drags is out: the window above it still gets the drop.
#[test]
fn a_full_screen_bottom_layer_does_not_take_the_drop_from_a_window_above() {
    from_bottom_layer((1920, 1080));
}

fn from_bottom_layer(size: (u32, u32)) {
    let mut f = Fixture::new();
    f.add_output(1, (1920, 1080));
    let a = f.add_client();
    let created = f
        .client(a)
        .create_layer(None, zwlr_layer_shell_v1::Layer::Bottom, "desktop");
    let layer_surface = created.surface.clone();
    created.set_configure_props(LayerConfigureProps {
        size: Some(size),
        anchor: Some(zwlr_layer_surface_v1::Anchor::Top | zwlr_layer_surface_v1::Anchor::Left),
        exclusive_zone: Some(0),
        ..Default::default()
    });
    created.commit();
    f.roundtrip(a);
    let layer = f.client(a).layer(&layer_surface);
    layer.set_size(size.0 as u16, size.1 as u16);
    layer.attach_new_buffer();
    layer.ack_last_and_commit();
    f.double_roundtrip(a);
    f.client(a).get_data_device();
    f.double_roundtrip(a);
    let (b, b_surface, b_origin) = drop_target(&mut f);
    let mouse = FakeDevice::mouse();

    pointer_to_screen(&mut f, &mouse, Point::from((50.0, 50.0)));
    press(&mut f, &mouse, BTN_LEFT);
    f.double_roundtrip(a);
    let serial = f
        .client(a)
        .state
        .pointer_button_serial
        .expect("press reached the layer");
    f.client(a).start_drag(&layer_surface, serial);
    f.roundtrip(a);
    pointer_to_screen(&mut f, &mouse, Point::from((70.0, 60.0)));
    pointer_to(&mut f, &mouse, b_origin + Point::from((100.0, 100.0)));
    f.double_roundtrip(b);
    pointer_to(&mut f, &mouse, b_origin + Point::from((110.0, 100.0)));
    release(&mut f, &mouse, BTN_LEFT);
    f.double_roundtrip(b);
    assert_offered(&mut f, b, &b_surface);
}

/// The other way: a file dragged out of a window onto a desktop icon (a small
/// bottom-layer surface in the clear) is offered to the desktop.
#[test]
fn a_mouse_drag_from_a_window_reaches_a_bottom_layer() {
    let mut f = Fixture::new();
    f.add_output(1, (1920, 1080));
    let desk = f.add_client();
    let created = f
        .client(desk)
        .create_layer(None, zwlr_layer_shell_v1::Layer::Bottom, "desktop");
    let layer_surface = created.surface.clone();
    created.set_configure_props(LayerConfigureProps {
        size: Some((200, 200)),
        anchor: Some(zwlr_layer_surface_v1::Anchor::Top | zwlr_layer_surface_v1::Anchor::Left),
        exclusive_zone: Some(0),
        ..Default::default()
    });
    created.commit();
    f.roundtrip(desk);
    let layer = f.client(desk).layer(&layer_surface);
    layer.set_size(200, 200);
    layer.attach_new_buffer();
    layer.ack_last_and_commit();
    f.double_roundtrip(desk);
    f.client(desk).get_data_device();
    f.client(desk).state.accept_drags = true;
    f.double_roundtrip(desk);

    let a = f.add_client();
    let (a_surface, a_origin) = map_window(&mut f, a, "source", (450, -150));
    f.client(a).get_data_device();
    f.double_roundtrip(a);
    let mouse = FakeDevice::mouse();

    pointer_to(&mut f, &mouse, a_origin + Point::from((100.0, 100.0)));
    press(&mut f, &mouse, BTN_LEFT);
    f.double_roundtrip(a);
    let serial = f
        .client(a)
        .state
        .pointer_button_serial
        .expect("press reached the window");
    f.client(a).start_drag(&a_surface, serial);
    f.roundtrip(a);
    pointer_to(&mut f, &mouse, a_origin + Point::from((80.0, 90.0)));
    pointer_to_screen(&mut f, &mouse, Point::from((50.0, 50.0)));
    f.double_roundtrip(desk);
    pointer_to_screen(&mut f, &mouse, Point::from((60.0, 50.0)));
    release(&mut f, &mouse, BTN_LEFT);
    f.double_roundtrip(desk);
    assert_offered(&mut f, desk, &layer_surface);
}
