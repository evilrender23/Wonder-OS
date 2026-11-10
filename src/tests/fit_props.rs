//! fit-window, for any window size, output size and starting view:
//! * the fitted frame covers the usable area edge to edge (border outside);
//! * the view lands at zoom 1 with that frame fully on screen;
//! * fitting and unfitting gives the client back its exact pre-fit size.
//!
//! Real clients over the fixture: the configure the client sees is the
//! contract, so that's what is asserted.

use proptest::prelude::*;
use smithay::utils::Point;

use super::{Fixture, window_by_app_id};

fn map_settled(
    f: &mut Fixture,
    id: super::client::ClientId,
    app_id: &str,
    size: (u16, u16),
) -> wayland_client::protocol::wl_surface::WlSurface {
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

fn last_configured_size(
    f: &mut Fixture,
    id: super::client::ClientId,
    surface: &wayland_client::protocol::wl_surface::WlSurface,
) -> (i32, i32) {
    let size = f
        .client(id)
        .window(surface)
        .configures_received
        .last()
        .map(|c| c.1.size);
    size.unwrap_or((0, 0))
}

proptest! {
    #![proptest_config(ProptestConfig { cases: 64, ..ProptestConfig::default() })]

    #[test]
    fn fit_fills_the_usable_area_and_unfit_restores_the_size(
        w in 64u16..1900, h in 64u16..1100,
        out in prop::sample::select(vec![(1280u16, 800u16), (1920, 1080), (2560, 1440), (1366, 768), (1365, 767), (1279, 801)]),
        cam in (-5000.0..5000.0f64, -5000.0..5000.0f64), zoom in 0.25..=1.0f64,
    ) {
        let mut f = Fixture::new();
        // Pins the on-canvas behaviour that `[spaces] maximize_to_space` replaces.
        f.state().config.maximize_to_space = false;
        f.add_output(1, out);
        let id = f.add_client();
        let surface = map_settled(&mut f, id, "fit", (w, h));
        let window = window_by_app_id(&mut f, "fit").unwrap();
        f.state().with_output_state(|os| {
            os.zoom = zoom;
            os.camera = Point::from(cam);
        });

        f.state().toggle_fit_window(&window);
        f.double_roundtrip(id);
        let (fw, fh) = last_configured_size(&mut f, id, &surface);
        let usable = f.state().get_usable_area();
        prop_assert_eq!((fw, fh), (usable.size.w, usable.size.h), "fit configures the usable size");
        // Commit at the fitted size, as a real client would.
        let cw = f.client(id).window(&surface);
        cw.set_size(fw as u16, fh as u16);
        cw.ack_last_and_commit();
        f.double_roundtrip(id);
        // The view is parked while the geometry animation holds; run it out
        // (past the 500 ms endpoint-hold cap) and the view must land on the
        // window.
        let start = std::time::Instant::now();
        for i in 0..80u64 {
            f.state().tick_window_animations_at(
                std::time::Duration::from_millis(16),
                start + std::time::Duration::from_millis(600 + 16 * i),
            );
        }
        let (camera, z) = f.state().landing_view();
        prop_assert_eq!(z, 1.0, "fit lands at zoom 1");
        let loc = f.state().stage.position_of(&window).unwrap();
        prop_assert!(loc.x as f64 >= camera.x + usable.loc.x as f64 - 0.5
            && (loc.x + fw) as f64 <= camera.x + (usable.loc.x + usable.size.w) as f64 + 0.5,
            "fitted window on screen horizontally: loc {loc:?} cam {camera:?} usable {usable:?}");
        prop_assert!(loc.y as f64 >= camera.y + usable.loc.y as f64 - 0.5
            && (loc.y + fh) as f64 <= camera.y + (usable.loc.y + usable.size.h) as f64 + 0.5,
            "fitted window on screen vertically: loc {loc:?} cam {camera:?} usable {usable:?}");

        f.state().toggle_fit_window(&window);
        f.double_roundtrip(id);
        prop_assert_eq!(last_configured_size(&mut f, id, &surface), (w as i32, h as i32),
            "unfit restores the exact pre-fit size");
        prop_assert!(!f.state().stage.is_fit(&window));
    }
}
