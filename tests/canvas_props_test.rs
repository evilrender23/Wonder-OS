//! Properties of the canvas math every frame and every input event leans on:
//! the screen↔canvas transform, zoom anchoring, the rule-coordinate
//! conversions, fit/visibility queries and the minimal camera reveal. The
//! example tests in canvas_*_test.rs pin specific values; these say what must
//! hold for *all* of them, across the range IPC accepts (±1e9 canvas px) and
//! the zoom range the compositor allows ([MIN_ZOOM_FLOOR, MAX_ZOOM]).

use driftwm::canvas::{
    CanvasPos, Chrome, MAX_ZOOM, MIN_ZOOM_FLOOR, ScreenPos, camera_for_center, canvas_to_screen,
    closest_point_on_rect, content_to_rule, internal_to_rule, is_point_visible,
    minimal_reveal_camera, rule_to_content, rule_to_internal, rule_to_screen_top_left,
    screen_to_canvas, screen_top_left_to_rule, snap_zoom, viewport_center, visible_canvas_rect,
    visible_fraction, zoom_anchor_camera, zoom_to_fit,
};
use driftwm::layout::snap::SnapRect;
use proptest::prelude::*;
use smithay::utils::{Logical, Point, Rectangle, Size};

/// Canvas range IPC accepts (see ipc_bounds) — where f64 still resolves
/// sub-pixel positions.
const RANGE: f64 = 1e9;

fn zoom() -> impl Strategy<Value = f64> {
    prop_oneof![
        MIN_ZOOM_FLOOR..=MAX_ZOOM,
        Just(MIN_ZOOM_FLOOR),
        Just(MAX_ZOOM),
        Just(0.5),
    ]
}

fn coord() -> impl Strategy<Value = f64> {
    prop_oneof![-RANGE..RANGE, -5000.0..5000.0f64, Just(0.0)]
}

fn point() -> impl Strategy<Value = Point<f64, Logical>> {
    (coord(), coord()).prop_map(Point::from)
}

fn viewport() -> impl Strategy<Value = Size<i32, Logical>> {
    (320..7680i32, 240..4320i32).prop_map(Size::from)
}

fn size() -> impl Strategy<Value = Size<i32, Logical>> {
    (1..8000i32, 1..8000i32).prop_map(Size::from)
}

fn chrome() -> impl Strategy<Value = Chrome> {
    (0..64i32, 0..16i32).prop_map(|(bar, border)| Chrome { bar, border })
}

/// Absolute tolerance for a value of magnitude `m` after a few f64 ops.
fn tol(m: f64) -> f64 {
    1e-6 + m.abs() * 1e-12
}

proptest! {
    #![proptest_config(ProptestConfig { cases: 1024, ..ProptestConfig::default() })]

    // --- screen ↔ canvas ------------------------------------------------

    #[test]
    fn screen_canvas_screen_round_trips(s in point(), cam in point(), z in zoom()) {
        let s = Point::from((s.x.rem_euclid(8000.0), s.y.rem_euclid(5000.0)));
        let back = canvas_to_screen(screen_to_canvas(ScreenPos(s), cam, z), cam, z).0;
        // The canvas value is ~cam + s/z; its rounding error, scaled back by z.
        let err = tol(cam.x.abs().max(cam.y.abs()) * z + s.x.abs().max(s.y.abs()));
        prop_assert!((back.x - s.x).abs() <= err, "{back:?} vs {s:?}");
        prop_assert!((back.y - s.y).abs() <= err, "{back:?} vs {s:?}");
    }

    #[test]
    fn canvas_screen_canvas_round_trips(c in point(), cam in point(), z in zoom()) {
        let back = screen_to_canvas(canvas_to_screen(CanvasPos(c), cam, z), cam, z).0;
        let m = c.x.abs().max(c.y.abs()).max(cam.x.abs()).max(cam.y.abs());
        prop_assert!((back.x - c.x).abs() <= tol(m) + m * 1e-15 / z, "{back:?} vs {c:?}");
        prop_assert!((back.y - c.y).abs() <= tol(m) + m * 1e-15 / z, "{back:?} vs {c:?}");
    }

    #[test]
    fn the_transform_is_monotonic(a in coord(), d in 1e-3..1e4f64, cam in point(), z in zoom()) {
        // Ordering on the canvas is ordering on screen: drags never invert.
        let lo = canvas_to_screen(CanvasPos(Point::from((a, a))), cam, z).0;
        let hi = canvas_to_screen(CanvasPos(Point::from((a + d, a + d))), cam, z).0;
        prop_assert!(hi.x >= lo.x && hi.y >= lo.y);
    }

    // --- zoom anchoring -------------------------------------------------

    #[test]
    fn zooming_keeps_the_anchor_under_the_cursor(
        anchor in point(), sx in 0.0..7680.0f64, sy in 0.0..4320.0f64, z in zoom(),
    ) {
        let screen = Point::from((sx, sy));
        let cam = zoom_anchor_camera(anchor, screen, z);
        let at = canvas_to_screen(CanvasPos(anchor), cam, z).0;
        let err = tol(anchor.x.abs().max(anchor.y.abs()) * z);
        prop_assert!((at.x - sx).abs() <= err && (at.y - sy).abs() <= err, "{at:?} vs {screen:?}");
    }

    #[test]
    fn camera_for_center_inverts_viewport_center(
        x in coord(), y in coord(), z in zoom(), vp in viewport(),
    ) {
        let cam = camera_for_center(x, y, z, vp);
        let (cx, cy) = viewport_center(cam, z, vp);
        let m = x.abs().max(y.abs()) + vp.w.max(vp.h) as f64 / z;
        prop_assert!((cx - x).abs() <= tol(m) && (cy - y).abs() <= tol(m));
    }

    // --- rule coordinates (integer, exact) ------------------------------

    #[test]
    fn rule_and_internal_are_exact_inverses(x in -100_000_000..100_000_000i32, y in -100_000_000..100_000_000i32, s in size()) {
        let loc = rule_to_internal(x, y, s);
        prop_assert_eq!(internal_to_rule(loc, s), (x, y));
    }

    #[test]
    fn rule_and_content_are_exact_inverses(
        x in -100_000_000..100_000_000i32, y in -100_000_000..100_000_000i32, s in size(), c in chrome(),
    ) {
        let loc = rule_to_content(x, y, s, c);
        prop_assert_eq!(content_to_rule(loc, s, c), (x, y));
    }

    #[test]
    fn frame_and_content_sizes_are_inverses(s in size(), c in chrome()) {
        prop_assert_eq!(c.content_size(c.frame_size(s)), s);
        let loc = Point::from((s.w, -s.h));
        prop_assert_eq!(c.content_loc(c.frame_loc(loc)), loc);
    }

    #[test]
    fn content_size_never_deflates_below_one_pixel(w in 0..100i32, h in 0..100i32, c in chrome()) {
        let inner = c.content_size(Size::from((w, h)));
        prop_assert!(inner.w >= 1 && inner.h >= 1);
    }

    #[test]
    fn screen_pinned_rule_round_trips(
        x in -20_000..20_000i32, y in -20_000..20_000i32, s in size(), out in viewport(),
    ) {
        let top_left = rule_to_screen_top_left(x, y, s, out);
        prop_assert_eq!(screen_top_left_to_rule(top_left, s, out), (x, y));
    }

    // --- fitting and visibility -----------------------------------------

    #[test]
    fn zoom_to_fit_stays_in_range_and_fits(
        bx in -1_000_000..1_000_000i32, by in -1_000_000..1_000_000i32,
        bw in 1..2_000_000i32, bh in 1..2_000_000i32, vp in viewport(), pad in 0.0..100.0f64,
    ) {
        let bbox = Rectangle::new(Point::from((bx, by)), Size::from((bw, bh)));
        let z = zoom_to_fit(bbox, vp, pad);
        prop_assert!((MIN_ZOOM_FLOOR..=MAX_ZOOM).contains(&z));
        if z > MIN_ZOOM_FLOOR {
            // Not clamped at the floor: the box plus padding fits the viewport.
            prop_assert!(bw as f64 * z <= (vp.w as f64 - 2.0 * pad).max(1.0) + 1e-6);
            prop_assert!(bh as f64 * z <= (vp.h as f64 - 2.0 * pad).max(1.0) + 1e-6);
        }
    }

    #[test]
    fn the_visible_rect_covers_every_on_screen_point(
        cam in (-1_000_000..1_000_000i32, -1_000_000..1_000_000i32), vp in viewport(), z in zoom(),
        fx in 0.0..1.0f64, fy in 0.0..1.0f64,
    ) {
        let cam_i = Point::<i32, Logical>::from(cam);
        let rect = visible_canvas_rect(cam_i, vp, z);
        let screen = Point::from((fx * vp.w as f64, fy * vp.h as f64));
        let c = screen_to_canvas(ScreenPos(screen), cam_i.to_f64(), z).0;
        prop_assert!(c.x >= rect.loc.x as f64 && c.x <= (rect.loc.x + rect.size.w) as f64);
        prop_assert!(c.y >= rect.loc.y as f64 && c.y <= (rect.loc.y + rect.size.h) as f64);
    }

    #[test]
    fn a_point_is_visible_exactly_when_it_lands_on_screen(
        c in point(), cam in point(), vp in viewport(), z in zoom(),
    ) {
        let s = canvas_to_screen(CanvasPos(c), cam, z).0;
        let margin = 1e-3;
        let inside = s.x > margin && s.y > margin
            && s.x < vp.w as f64 - margin && s.y < vp.h as f64 - margin;
        let outside = s.x < -margin || s.y < -margin
            || s.x > vp.w as f64 + margin || s.y > vp.h as f64 + margin;
        let visible = is_point_visible(c, cam, vp, z);
        if inside { prop_assert!(visible, "{s:?} on a {vp:?} screen"); }
        if outside { prop_assert!(!visible, "{s:?} off a {vp:?} screen"); }
    }

    #[test]
    fn visible_fraction_is_a_fraction(
        loc in (-100_000..100_000i32, -100_000..100_000i32), s in size(),
        cam in point(), vp in viewport(), z in zoom(),
    ) {
        let f = visible_fraction(Point::from(loc), s, cam, vp, z);
        prop_assert!((0.0..=1.0).contains(&f), "{f}");
    }

    #[test]
    fn a_rect_inside_the_view_is_wholly_visible(
        vp in viewport(), z in zoom(), cam in (-100_000..100_000i32, -100_000..100_000i32),
        fx in 0.0..0.5f64, fy in 0.0..0.5f64, fw in 0.01..0.5f64, fh in 0.01..0.5f64,
    ) {
        let (vw, vh) = (vp.w as f64 / z, vp.h as f64 / z);
        let loc = Point::from((cam.0 + (fx * vw) as i32, cam.1 + (fy * vh) as i32));
        let s = Size::from((((fw * vw) as i32).max(1), ((fh * vh) as i32).max(1)));
        let cam_f = Point::<i32, Logical>::from(cam).to_f64();
        let f = visible_fraction(loc, s, cam_f, vp, z);
        prop_assert!((f - 1.0).abs() < 1e-9, "{f}");
    }

    #[test]
    fn snap_zoom_snaps_only_near_one_and_is_idempotent(z in zoom()) {
        let s = snap_zoom(z);
        prop_assert_eq!(snap_zoom(s), s);
        prop_assert!((s - z).abs() <= 0.05 + 1e-12);
        if (z - 1.0).abs() > 0.05 { prop_assert_eq!(s, z); }
    }

    #[test]
    fn the_closest_point_on_a_rect_is_on_it_and_closest(
        o in point(), loc in (-100_000..100_000i32, -100_000..100_000i32), s in size(),
        fx in 0.0..=1.0f64, fy in 0.0..=1.0f64,
    ) {
        let p = closest_point_on_rect(o, Point::from(loc), s);
        let (x0, y0) = (loc.0 as f64, loc.1 as f64);
        let (x1, y1) = (x0 + s.w as f64, y0 + s.h as f64);
        prop_assert!(p.x >= x0 && p.x <= x1 && p.y >= y0 && p.y <= y1);
        // Any other point of the rect is at least as far.
        let q = Point::<f64, Logical>::from((x0 + fx * s.w as f64, y0 + fy * s.h as f64));
        let d = |a: Point<f64, Logical>| (a.x - o.x).hypot(a.y - o.y);
        prop_assert!(d(p) <= d(q) + tol(o.x.abs().max(o.y.abs())));
    }

    // --- minimal reveal -------------------------------------------------

    #[test]
    fn reveal_shows_the_frame_inside_the_insets_or_centers_it(
        fx in -1_000_000.0..1_000_000.0f64, fy in -1_000_000.0..1_000_000.0f64,
        fw in 1.0..6000.0f64, fh in 1.0..6000.0f64,
        cam in point(), z in zoom(), vp in viewport(), inset in 0.0..64.0f64,
    ) {
        let frame = SnapRect { x_low: fx, x_high: fx + fw, y_low: fy, y_high: fy + fh };
        let usable = Rectangle::new(Point::from((0, 0)), vp);
        let c = minimal_reveal_camera(&frame, cam, z, usable, inset);
        for (low, high, cam_axis, span) in [
            (frame.x_low, frame.x_high, c.x, vp.w as f64 / z),
            (frame.y_low, frame.y_high, c.y, vp.h as f64 / z),
        ] {
            let e = tol(low.abs() + span);
            if high - low + 2.0 * inset <= span {
                prop_assert!(low >= cam_axis + inset - e, "low edge hidden");
                prop_assert!(high <= cam_axis + span - inset + e, "high edge hidden");
            } else {
                prop_assert!(((low + high) / 2.0 - (cam_axis + span / 2.0)).abs() <= e, "not centered");
            }
        }
    }

    #[test]
    fn reveal_leaves_an_already_visible_frame_alone(
        cam in (-100_000.0..100_000.0f64, -100_000.0..100_000.0f64), z in zoom(), vp in viewport(),
        fx in 0.2..0.4f64, fy in 0.2..0.4f64, fw in 0.05..0.3f64, fh in 0.05..0.3f64,
    ) {
        let (vw, vh) = (vp.w as f64 / z, vp.h as f64 / z);
        let x_low = cam.0 + fx * vw;
        let y_low = cam.1 + fy * vh;
        let frame = SnapRect { x_low, x_high: x_low + fw * vw, y_low, y_high: y_low + fh * vh };
        let cam = Point::from(cam);
        let c = minimal_reveal_camera(&frame, cam, z, Rectangle::new(Point::from((0, 0)), vp), 0.0);
        prop_assert_eq!(c, cam);
    }
}
