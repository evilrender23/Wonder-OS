//! The Compiz-style effects end to end: the magic lamp, wobbly windows and the
//! pointer's motion blur, as state machines on the headless fixture and as
//! pixels through a real GL context.
//!
//! The pixel scenarios are `#[ignore]`d out of the default lane (they need
//! Mesa's surfaceless EGL and self-skip without it) and take [`gl::lock`].

use std::time::Duration;

use image::RgbaImage;
use smithay::output::Output;
use smithay::utils::{Logical, Physical, Point, SERIAL_COUNTER, Scale, Size};

use crate::ipc::dispatch;
use crate::ipc::protocol::{Request, WindowSelector};

use super::{
    Fixture, gl, map_window, map_window_shm, motion, settle, tick_until_settled, window_by_app_id,
};

const OUTPUT: (u32, u32) = (1280, 800);
const WINDOW: (u16, u16) = (480, 320);
/// One 240 Hz frame.
const FRAME: Duration = Duration::from_micros(4167);

/// Wobbly windows are opt-in, so the scenes switch them on unless the test
/// brings its own `[effects.wobbly]` table.
fn config_toml(extra: &str) -> String {
    let wobbly = if extra.contains("[effects.wobbly]") {
        ""
    } else {
        "[effects.wobbly]\nenabled = true\n\n"
    };
    format!(
        "[background]\n\
         type = \"none\"\n\
         \n\
         [decorations]\n\
         default_mode = \"client\"\n\
         border_width = 0\n\
         corner_radius = 0\n\
         shadow = false\n\
         \n\
         {wobbly}{extra}"
    )
}

/// A window that shows what a warp does to it: a dark title bar with three
/// buttons, a sidebar, text-like stripes and three coloured blocks, opaque to
/// its very edge. Premultiplied little-endian BGRA.
fn window_pixels(w: i32, h: i32) -> Vec<u8> {
    let mut buf = vec![0u8; (w * h * 4) as usize];
    for y in 0..h {
        for x in 0..w {
            let rgb: [u8; 3] = if y < 28 {
                let button = (0..3).find(|i| {
                    let cx = 18 + i * 20;
                    (x - cx).pow(2) + (y - 14).pow(2) <= 36
                });
                match button {
                    Some(0) => [0xED, 0x6A, 0x5E],
                    Some(1) => [0xF4, 0xBF, 0x4F],
                    Some(_) => [0x61, 0xC5, 0x54],
                    None => [0x2B, 0x2D, 0x3A],
                }
            } else if x < 110 {
                if (y - 28) % 26 > 8 && (y - 28) % 26 < 18 && x > 14 && x < 92 {
                    [0x8A, 0x90, 0xA8]
                } else {
                    [0x3A, 0x3D, 0x4F]
                }
            } else if y > h - 92 && y < h - 20 {
                match (x - 110) * 3 / (w - 110) {
                    _ if (x - 110) % ((w - 110) / 3) < 14 => [0xF4, 0xF5, 0xF8],
                    0 => [0x3B, 0x82, 0xF6],
                    1 => [0x10, 0xB9, 0x81],
                    _ => [0xEF, 0x44, 0x44],
                }
            } else if (y - 28) % 18 > 9
                && (y - 28) % 18 < 14
                && x > 128
                && x < w - 24 - ((y - 28) / 18 % 5) * 31
            {
                [0x4B, 0x50, 0x63]
            } else {
                [0xF4, 0xF5, 0xF8]
            };
            let i = ((y * w + x) * 4) as usize;
            buf[i..i + 4].copy_from_slice(&[rgb[2], rgb[1], rgb[0], 255]);
        }
    }
    buf
}

/// Where the scene's window sits on the output: a new window is centred on
/// the canvas origin, and the camera is put so that lands here.
const WINDOW_AT: (u32, u32) = (340, 90);

/// A fixture with a GL renderer, one output and one settled [`WINDOW`] at
/// [`WINDOW_AT`].
fn scene(test: &str, extra: &str) -> Option<(Fixture, Output)> {
    let renderer = gl::surfaceless_renderer(test)?;
    let mut f = Fixture::with_config(super::config(&config_toml(extra)));
    let output = f.add_output(1, (OUTPUT.0 as u16, OUTPUT.1 as u16));
    gl::install(&mut f, renderer);
    let client = f.add_client();
    let pixels = window_pixels(WINDOW.0 as i32, WINDOW.1 as i32);
    map_window_shm(
        &mut f,
        client,
        "jelly",
        WINDOW,
        (WINDOW.0 as i32, WINDOW.1 as i32),
        &pixels,
    );
    f.state().with_output_state(|os| {
        os.camera = Point::from((
            -(WINDOW.0 as f64) / 2.0 - WINDOW_AT.0 as f64,
            -(WINDOW.1 as f64) / 2.0 - WINDOW_AT.1 as f64,
        ));
        os.camera_target = None;
        os.zoom = 1.0;
        os.zoom_target = None;
        os.zoom_animation_anchor = None;
    });
    settle(&mut f);
    tick_until_settled(&mut f);
    Some((f, output))
}

fn teardown(f: &mut Fixture, output: &Output) {
    f.state().render.remove_output(&output.name());
    gl::uninstall(f);
}

/// Compose one live frame for `output` and read it back.
fn live_frame(f: &mut Fixture, output: &Output) -> RgbaImage {
    let mut backend = f
        .state()
        .backend
        .take()
        .expect("the fixture has a renderer");
    let bytes = {
        let renderer = backend.renderer();
        let elements = crate::render::compose_frame(f.state(), renderer, output, Vec::new());
        let refs: Vec<&crate::render::OutputRenderElements> = elements.iter().collect();
        crate::render::render_elements_to_rgba(
            renderer,
            Size::<i32, Physical>::from((OUTPUT.0 as i32, OUTPUT.1 as i32)),
            Scale::from(1.0),
            &refs,
        )
    };
    f.state().backend = Some(backend);
    RgbaImage::from_raw(OUTPUT.0, OUTPUT.1, bytes.expect("render the live frame"))
        .expect("the readback is the frame's own size")
}

/// Save a scenario's frame where `DRIFTWM_TEST_DUMP` points, to look at.
fn dump(img: &RgbaImage, name: &str) {
    if let Some(dir) = std::env::var_os("DRIFTWM_TEST_DUMP") {
        let _ = img.save(std::path::Path::new(&dir).join(format!("{name}.png")));
    }
}

/// Inclusive bounds `(x0, y0, x1, y1)` of everything drawn, or `None` for an
/// empty frame.
fn painted_bounds(img: &RgbaImage) -> Option<(u32, u32, u32, u32)> {
    let (mut x0, mut y0, mut x1, mut y1) = (u32::MAX, u32::MAX, 0u32, 0u32);
    for (x, y, p) in img.enumerate_pixels() {
        if p[3] > 8 {
            x0 = x0.min(x);
            y0 = y0.min(y);
            x1 = x1.max(x);
            y1 = y1.max(y);
        }
    }
    (x0 <= x1 && y0 <= y1).then_some((x0, y0, x1, y1))
}

/// Inclusive `(first, last)` painted column of row `y`.
fn row_span(img: &RgbaImage, y: u32) -> Option<(u32, u32)> {
    let painted: Vec<u32> = (0..img.width())
        .filter(|x| img.get_pixel(*x, y)[3] > 8)
        .collect();
    Some((*painted.first()?, *painted.last()?))
}

fn set_minimized(f: &mut Fixture, id: u64, value: bool) {
    dispatch(
        Request::Minimize {
            window: Some(WindowSelector::Id(id)),
            value: Some(value),
        },
        f.state(),
    )
    .expect("the minimize request is answered");
}

fn window_id(f: &mut Fixture) -> u64 {
    let window = window_by_app_id(f, "jelly").expect("the scene's window");
    f.state().stage.id_of(&window).expect("a stage id").0
}

fn tick(f: &mut Fixture, frames: usize) {
    for _ in 0..frames {
        f.state().tick_window_animations(FRAME);
    }
}

#[test]
#[ignore = "needs Mesa surfaceless EGL; run with --include-ignored"]
fn the_lamp_pours_the_window_into_the_dock_and_back() {
    let _gl = gl::lock();
    let Some((mut f, output)) = scene("magic lamp", "") else {
        return;
    };
    let baseline = live_frame(&mut f, &output);
    dump(&baseline, "lamp_baseline");
    let rest = painted_bounds(&baseline).expect("the window is drawn");
    assert_eq!(rest, (340, 90, 819, 409));
    let dock = (OUTPUT.0 / 2, OUTPUT.1 - 40);

    let id = window_id(&mut f);
    set_minimized(&mut f, id, true);
    assert!(f.state().has_active_animations());

    // The first frame of the flight is the window, untouched.
    let first = live_frame(&mut f, &output);
    dump(&first, "lamp_00");
    assert_eq!(painted_bounds(&first), Some(rest));
    let differing = first
        .pixels()
        .zip(baseline.pixels())
        .filter(|(a, b)| (0..4).any(|i| a[i].abs_diff(b[i]) > 3))
        .count();
    assert_eq!(
        differing, 0,
        "the lamp starts from the window's own picture"
    );

    let mut last_top = rest.1;
    let mut narrowed = false;
    for step in 1..=9 {
        tick(&mut f, 10);
        let img = live_frame(&mut f, &output);
        dump(&img, &format!("lamp_{step:02}"));
        let Some(b) = painted_bounds(&img) else {
            assert!(step >= 8, "the picture vanished early, at step {step}");
            continue;
        };
        // Two pixels of slack: the antialiased edge of a stretched picture
        // is as much wider as the picture is stretched.
        assert!(
            b.1 + 2 >= last_top,
            "the top edge only moves toward the dock"
        );
        assert!(b.3 <= dock.1 + 2, "nothing is drawn past the dock");
        last_top = last_top.max(b.1);
        // Rows nearer the dock are narrower, and centred on it.
        let (near, far) = (row_span(&img, b.3 - 2), row_span(&img, b.1 + 2));
        if let (Some(near), Some(far)) = (near, far)
            && step >= 3
        {
            assert!(near.1 - near.0 < far.1 - far.0, "step {step}: funnel");
            // ...and lean toward it: the window sits left of the dock.
            assert!(near.0 + near.1 > far.0 + far.1, "step {step}: lean");
            assert!(near.0 + near.1 <= 2 * dock.0 + 46, "step {step}: overshoot");
            narrowed = true;
        }
    }
    assert!(narrowed);
    tick(&mut f, 12);
    assert!(f.state().closing_snapshots.is_empty());
    assert!(painted_bounds(&live_frame(&mut f, &output)).is_none());

    // And back out: the same film backwards, ending on the live window.
    set_minimized(&mut f, id, false);
    assert_eq!(f.state().closing_snapshots.len(), 1);
    let mut last_top = u32::MAX;
    for step in 0..=9 {
        let img = live_frame(&mut f, &output);
        dump(&img, &format!("lamp_back_{step:02}"));
        if let Some(b) = painted_bounds(&img) {
            assert!(
                b.1 <= last_top.saturating_add(2),
                "the top edge only moves back up"
            );
            last_top = last_top.min(b.1);
        }
        tick(&mut f, 10);
    }
    tick(&mut f, 12);
    tick_until_settled(&mut f);
    assert!(f.state().closing_snapshots.is_empty());
    let back = live_frame(&mut f, &output);
    assert!(back == baseline, "restored pixel-identical to before");

    teardown(&mut f, &output);
}

/// Canvas point of the scene window's title bar, mid-width: where a hand
/// would take it.
fn handle() -> Point<f64, Logical> {
    Point::from((0.0, -(WINDOW.1 as f64) / 2.0 + 14.0))
}

fn end_drag(f: &mut Fixture) {
    let pointer = f.state().seat.get_pointer().expect("a pointer");
    pointer.unset_grab(f.state(), SERIAL_COUNTER.next_serial(), 0);
}

fn tick_wobble(f: &mut Fixture, frames: usize) {
    for _ in 0..frames {
        f.state().tick_wobbly(FRAME);
    }
}

fn settle_wobble(f: &mut Fixture) {
    for _ in 0..4000 {
        if !f.state().wobbly.is_active() {
            return;
        }
        f.state().tick_wobbly(FRAME);
    }
    panic!("the jelly never came to rest");
}

#[test]
#[ignore = "needs Mesa surfaceless EGL; run with --include-ignored"]
fn a_dragged_window_trails_behind_the_hand_and_settles() {
    let _gl = gl::lock();
    let Some((mut f, output)) = scene("wobbly", "") else {
        return;
    };
    let baseline = live_frame(&mut f, &output);
    let rest = painted_bounds(&baseline).expect("the window is drawn");

    // Taking hold of it changes nothing: no grid until it actually moves.
    assert!(f.state().try_start_gesture_move(handle(), false));
    assert!(!f.state().wobbly.is_active());
    assert!(live_frame(&mut f, &output) == baseline);

    // A flick to the right, faster than the springs follow.
    const FLICK: i32 = 180;
    for step in 1..=6 {
        motion(&mut f, handle() + Point::from((step as f64 * 30.0, 0.0)));
        tick_wobble(&mut f, 6);
        dump(&live_frame(&mut f, &output), &format!("wobbly_{step:02}"));
    }
    assert!(f.state().wobbly.is_active());
    assert!(f.state().has_active_animations());
    let stretched = live_frame(&mut f, &output);
    let b = painted_bounds(&stretched).expect("the window is drawn");
    let moved_rest = (rest.0 + FLICK as u32, rest.2 + FLICK as u32);
    assert!(b.0 + 20 < moved_rest.0, "the far side lags behind: {b:?}");
    assert!(
        b.2 <= moved_rest.1 + 30,
        "it does not fly ahead of the hand: {b:?}"
    );
    // The spot of the title bar that was taken is still exactly under the
    // hand: the same pixels as at rest, 180 px over.
    let held = (WINDOW_AT.0 + WINDOW.0 as u32 / 2, WINDOW_AT.1 + 14);
    for dx in 0..3 {
        assert_eq!(
            stretched.get_pixel(held.0 + FLICK as u32 + dx, held.1),
            baseline.get_pixel(held.0 + dx, held.1)
        );
    }
    // The sides are curves, not slanted lines: held at the top, they trail
    // more the further down they are, and not in proportion.
    let span = |y: u32| row_span(&stretched, y).expect("a painted row");
    let (top, mid, bottom) = (span(rest.1 + 30), span(rest.1 + 160), span(rest.3 - 10));
    assert!(
        bottom.0 + 8 < top.0,
        "the bottom trails the top: {top:?} {bottom:?}"
    );
    let bend = |a: u32, b: u32, c: u32| (b as f64 - (a as f64 + c as f64) / 2.0).abs();
    assert!(
        bend(top.0, mid.0, bottom.0).max(bend(top.1, mid.1, bottom.1)) > 2.0,
        "an edge bends: {top:?} {mid:?} {bottom:?}"
    );

    // Held still, it swings in under the hand; let go, it comes to rest.
    for step in 7..=12 {
        tick_wobble(&mut f, 12);
        dump(&live_frame(&mut f, &output), &format!("wobbly_{step:02}"));
    }
    end_drag(&mut f);
    settle_wobble(&mut f);
    assert!(!f.state().has_active_animations());
    let after = live_frame(&mut f, &output);
    dump(&after, "wobbly_13_rest");
    let shifted = image::imageops::crop_imm(&baseline, rest.0, rest.1, 480, 320).to_image();
    let landed =
        image::imageops::crop_imm(&after, rest.0 + FLICK as u32, rest.1, 480, 320).to_image();
    assert!(shifted == landed, "at rest it is the plain window again");
    assert_eq!(
        painted_bounds(&after),
        Some((moved_rest.0, rest.1, moved_rest.1, rest.3))
    );

    teardown(&mut f, &output);
}

/// Look at the scene from further out: the window at half size, its corner
/// at (400, 200) on screen.
fn zoom_out(f: &mut Fixture) {
    f.state().with_output_state(|os| {
        os.zoom = 0.5;
        os.camera = Point::from((
            -(WINDOW.0 as f64) / 2.0 - 800.0,
            -(WINDOW.1 as f64) / 2.0 - 400.0,
        ));
    });
    f.state().mark_all_dirty();
}

#[test]
#[ignore = "needs Mesa surfaceless EGL; run with --include-ignored"]
fn both_warps_follow_a_zoomed_out_canvas() {
    let _gl = gl::lock();
    let Some((mut f, output)) = scene("warp-zoom", "") else {
        return;
    };
    zoom_out(&mut f);
    let baseline = live_frame(&mut f, &output);
    let rest = painted_bounds(&baseline).expect("the window is drawn");
    assert_eq!(rest, (400, 200, 639, 359));

    // 180 canvas px of flick are 90 on this screen.
    assert!(f.state().try_start_gesture_move(handle(), false));
    for step in 1..=6 {
        motion(&mut f, handle() + Point::from((step as f64 * 30.0, 0.0)));
        tick_wobble(&mut f, 6);
    }
    let stretched = live_frame(&mut f, &output);
    dump(&stretched, "zoom_wobbly");
    let b = painted_bounds(&stretched).expect("the window is drawn");
    assert!(b.0 + 8 < rest.0 + 90, "the far side lags behind: {b:?}");
    assert!(
        b.0 >= rest.0 && b.2 <= rest.2 + 90 + 15,
        "at half size: {b:?}"
    );
    assert!(b.1 + 3 >= rest.1 && b.3 <= rest.3 + 12, "{b:?}");
    let (top, bottom) = (
        row_span(&stretched, rest.1 + 12).expect("a painted row"),
        row_span(&stretched, rest.3 - 6).expect("a painted row"),
    );
    assert!(
        bottom.0 + 4 < top.0,
        "the bottom trails: {top:?} {bottom:?}"
    );
    end_drag(&mut f);
    settle_wobble(&mut f);
    let moved = live_frame(&mut f, &output);
    let crop =
        |img: &RgbaImage, x: u32| image::imageops::crop_imm(img, x, 200, 240, 160).to_image();
    assert!(
        crop(&baseline, 400) == crop(&moved, 490),
        "the plain window again"
    );

    // The lamp from there: every frame between the window and the dock, and
    // the window back as it was.
    let id = window_id(&mut f);
    let dock = (OUTPUT.0 / 2, OUTPUT.1 - 40);
    set_minimized(&mut f, id, true);
    let mut narrowest = u32::MAX;
    for step in 0..8 {
        tick(&mut f, 10);
        let frame = live_frame(&mut f, &output);
        if step == 4 {
            dump(&frame, "zoom_lamp");
        }
        let Some(b) = painted_bounds(&frame) else {
            continue;
        };
        assert!(b.0 + 2 >= 490 && b.2 <= 729 + 2, "inside the funnel: {b:?}");
        assert!(b.1 + 2 >= 200 && b.3 <= dock.1 + 2, "{b:?}");
        if let Some(neck) = row_span(&frame, b.3.saturating_sub(3)) {
            narrowest = narrowest.min(neck.1 - neck.0);
        }
    }
    assert!(narrowest < 80, "it narrows toward the dock: {narrowest}");
    tick(&mut f, 200);
    assert!(painted_bounds(&live_frame(&mut f, &output)).is_none());
    set_minimized(&mut f, id, false);
    tick(&mut f, 400);
    tick_until_settled(&mut f);
    assert!(live_frame(&mut f, &output) == moved, "back as it was");

    teardown(&mut f, &output);
}

/// A plain fixture with one settled window centred on the canvas origin and
/// the camera at [`scene`]'s place, for the state machines that need no GL.
fn stage(extra: &str) -> Fixture {
    stage_with_client(extra).0
}

fn stage_with_client(extra: &str) -> (Fixture, super::client::ClientId) {
    let mut f = Fixture::with_config(super::config(&config_toml(extra)));
    f.add_output(1, (OUTPUT.0 as u16, OUTPUT.1 as u16));
    let client = f.add_client();
    map_window(&mut f, client, "jelly", WINDOW);
    f.state().with_output_state(|os| {
        os.camera = Point::from((
            -(WINDOW.0 as f64) / 2.0 - WINDOW_AT.0 as f64,
            -(WINDOW.1 as f64) / 2.0 - WINDOW_AT.1 as f64,
        ));
        os.camera_target = None;
        os.zoom = 1.0;
        os.zoom_target = None;
        os.zoom_animation_anchor = None;
    });
    settle(&mut f);
    tick_until_settled(&mut f);
    (f, client)
}

fn drag_by(f: &mut Fixture, dx: f64) {
    assert!(f.state().try_start_gesture_move(handle(), false));
    motion(f, handle() + Point::from((dx, 0.0)));
}

#[test]
fn a_drag_starts_the_jelly_and_rest_ends_it() {
    let mut f = stage("");
    assert!(!f.state().has_active_animations());
    drag_by(&mut f, 120.0);
    assert!(f.state().wobbly.is_active());
    assert!(f.state().has_active_animations());
    // Held, it settles under the hand and stops costing frames...
    settle_wobble(&mut f);
    assert!(!f.state().has_active_animations());
    // ...and the next move shakes it again.
    motion(&mut f, handle() + Point::from((180.0, 40.0)));
    assert!(f.state().wobbly.is_active());
    end_drag(&mut f);
    settle_wobble(&mut f);
    assert!(!f.state().has_active_animations());
}

#[test]
fn a_switched_off_jelly_never_starts() {
    let mut f = stage("[effects.wobbly]\nenabled = false\n");
    drag_by(&mut f, 120.0);
    assert!(!f.state().wobbly.is_active());
    end_drag(&mut f);
}

#[test]
fn an_excluded_app_stays_rigid() {
    let mut f = stage("[effects.wobbly]\nenabled = true\nexclude = [\"Jelly\"]\n");
    drag_by(&mut f, 120.0);
    assert!(!f.state().wobbly.is_active());
    end_drag(&mut f);
}

#[test]
fn switching_the_jelly_off_mid_wobble_stops_it() {
    let mut f = stage("");
    drag_by(&mut f, 120.0);
    assert!(f.state().wobbly.is_active());
    f.state()
        .reload_config_from_contents(&config_toml("[effects.wobbly]\nenabled = false\n"));
    f.state().pending_mode_changes.clear();
    f.state().tick_wobbly(FRAME);
    assert!(!f.state().wobbly.is_active());
    assert!(!f.state().has_active_animations());
    end_drag(&mut f);
}

#[test]
fn a_pinned_window_stays_rigid() {
    let mut f = stage("");
    let window = window_by_app_id(&mut f, "jelly").expect("the window");
    f.state().pin_window(&window).expect("pinned");
    tick_until_settled(&mut f);
    let from = f
        .state()
        .stage
        .position_of(&crate::state::StageWindow::Client(window.clone()));
    f.state().wobble_dragged(
        &crate::state::StageWindow::Client(window),
        from,
        from.expect("mapped") + Point::from((90, 0)),
        handle(),
    );
    assert!(!f.state().wobbly.is_active());
}

#[test]
fn a_window_that_dies_mid_wobble_takes_its_grid_with_it() {
    let (mut f, client) = stage_with_client("");
    drag_by(&mut f, 120.0);
    end_drag(&mut f);
    assert!(f.state().wobbly.is_active());
    f.kill_client(client);
    f.pump(4);
    f.state().tick_wobbly(FRAME);
    assert!(!f.state().wobbly.is_active());
}

#[test]
#[ignore = "needs Mesa surfaceless EGL; run with --include-ignored"]
fn minimizing_pours_and_restoring_pours_back() {
    let _gl = gl::lock();
    let Some((mut f, output)) = scene("minimizing_p", "") else {
        return;
    };
    let id = window_id(&mut f);
    set_minimized(&mut f, id, true);
    assert!(f.state().has_active_animations());
    // 380 ms at 240 Hz is 92 frames.
    tick(&mut f, 80);
    assert_eq!(f.state().closing_snapshots.len(), 1, "still pouring");
    tick(&mut f, 20);
    assert!(f.state().closing_snapshots.is_empty());
    assert!(!f.state().has_active_animations());

    set_minimized(&mut f, id, false);
    let window = window_by_app_id(&mut f, "jelly").expect("the window");
    assert!(
        f.state().poured_by_lamp(&window),
        "the flight is the window"
    );
    assert!(
        !f.state().window_animations.is_active(),
        "the lamp replaces the open fade"
    );
    tick(&mut f, 100);
    assert!(f.state().closing_snapshots.is_empty());
    assert!(!f.state().poured_by_lamp(&window));
    assert!(!f.state().has_active_animations());
    teardown(&mut f, &output);
}

#[test]
#[ignore = "needs Mesa surfaceless EGL; run with --include-ignored"]
fn a_lamp_turns_around_mid_flight_either_way() {
    let _gl = gl::lock();
    let Some((mut f, output)) = scene("a_lamp_turns", "") else {
        return;
    };
    let id = window_id(&mut f);
    let window = window_by_app_id(&mut f, "jelly").expect("the window");
    set_minimized(&mut f, id, true);
    tick(&mut f, 40);
    // Back out before it reached the dock: the same flight reverses.
    set_minimized(&mut f, id, false);
    assert_eq!(f.state().closing_snapshots.len(), 1);
    assert!(f.state().poured_by_lamp(&window));
    tick(&mut f, 20);
    // And in again before it landed.
    set_minimized(&mut f, id, true);
    assert_eq!(f.state().closing_snapshots.len(), 1);
    assert!(!f.state().poured_by_lamp(&window));
    tick(&mut f, 20);
    set_minimized(&mut f, id, false);
    assert_eq!(f.state().closing_snapshots.len(), 1);
    tick(&mut f, 100);
    assert!(
        f.state().closing_snapshots.is_empty(),
        "no flight is left over"
    );
    assert!(!f.state().poured_by_lamp(&window));
    assert!(!f.state().has_active_animations());
    teardown(&mut f, &output);
}

#[test]
#[ignore = "needs Mesa surfaceless EGL; run with --include-ignored"]
fn without_the_lamp_minimize_flies_as_before() {
    let _gl = gl::lock();
    let Some((mut f, output)) = scene("without_the_", "[effects.magic_lamp]\nenabled = false\n")
    else {
        return;
    };
    let id = window_id(&mut f);
    let window = window_by_app_id(&mut f, "jelly").expect("the window");
    set_minimized(&mut f, id, true);
    assert_eq!(f.state().closing_snapshots.len(), 1);
    assert!(!f.state().closing_snapshots[0].is_lamp());
    for _ in 0..2000 {
        f.state().tick_window_animations(FRAME);
    }
    assert!(f.state().closing_snapshots.is_empty());
    set_minimized(&mut f, id, false);
    assert!(f.state().closing_snapshots.is_empty(), "no flight back");
    assert!(!f.state().poured_by_lamp(&window));
    assert!(f.state().window_animations.is_active(), "it fades in");
    teardown(&mut f, &output);
}

#[test]
#[ignore = "needs Mesa surfaceless EGL; run with --include-ignored"]
fn instant_animations_skip_the_lamp() {
    let _gl = gl::lock();
    let Some((mut f, output)) = scene("instant_anim", "[effects]\nanimation_speed = 1.0\n") else {
        return;
    };
    let id = window_id(&mut f);
    let window = window_by_app_id(&mut f, "jelly").expect("the window");
    set_minimized(&mut f, id, true);
    assert!(f.state().closing_snapshots.iter().all(|s| !s.is_lamp()));
    tick(&mut f, 2);
    assert!(f.state().closing_snapshots.is_empty());
    set_minimized(&mut f, id, false);
    assert!(!f.state().poured_by_lamp(&window));
    teardown(&mut f, &output);
}

#[test]
#[ignore = "needs Mesa surfaceless EGL; run with --include-ignored"]
fn switching_the_lamp_off_mid_flight_lands_it_at_once() {
    let _gl = gl::lock();
    let Some((mut f, output)) = scene("switching_th", "") else {
        return;
    };
    let id = window_id(&mut f);
    let window = window_by_app_id(&mut f, "jelly").expect("the window");
    set_minimized(&mut f, id, true);
    tick(&mut f, 100);
    set_minimized(&mut f, id, false);
    tick(&mut f, 10);
    assert!(f.state().poured_by_lamp(&window));
    f.state()
        .reload_config_from_contents(&config_toml("[effects.magic_lamp]\nenabled = false\n"));
    f.state().pending_mode_changes.clear();
    tick(&mut f, 1);
    assert!(f.state().closing_snapshots.is_empty());
    assert!(!f.state().poured_by_lamp(&window), "the window is shown");
    teardown(&mut f, &output);
}

const CURSOR: i32 = 24;

/// A plain white square for a cursor, hotspot at its corner: the theme
/// installed on the machine must not decide what these tests see.
fn square_cursor(f: &mut Fixture) {
    use smithay::backend::allocator::Fourcc;
    use smithay::backend::renderer::element::memory::MemoryRenderBuffer;
    let buffer = MemoryRenderBuffer::from_slice(
        &vec![0xFF; (CURSOR * CURSOR * 4) as usize],
        Fourcc::Argb8888,
        (CURSOR, CURSOR),
        1,
        smithay::utils::Transform::Normal,
        None,
    );
    f.state().cursor.cursor_buffers.insert(
        "default".into(),
        crate::state::CursorFrames {
            frames: vec![(buffer, Point::from((0, 0)), 0)],
            total_duration_ms: 0,
        },
    );
    f.state().cursor.cursor_status = smithay::input::pointer::CursorImageStatus::default_named();
}

/// Put the pointer at screen point `at` at time `now` and draw the frame,
/// cursor included. Returns the frame and whether a smear was part of it.
fn pointer_frame(f: &mut Fixture, output: &Output, now: f64, at: (f64, f64)) -> (RgbaImage, bool) {
    let (camera, zoom) = f
        .state()
        .with_output_state(|os| (os.camera, os.zoom))
        .expect("the scene has an output");
    let pointer = f.state().pointer.clone();
    pointer.set_location(Point::from((at.0 + camera.x, at.1 + camera.y)));
    f.state().cursor_blur.clock = Some(now);
    let mut backend = f
        .state()
        .backend
        .take()
        .expect("the fixture has a renderer");
    let (bytes, smeared) = {
        let renderer = backend.renderer();
        let cursor = crate::render::build_cursor_elements(
            f.state(),
            renderer,
            output,
            camera,
            zoom,
            1.0,
            1.0,
        );
        let smeared = cursor
            .iter()
            .any(|e| matches!(e, crate::render::OutputRenderElements::CursorSmear(_)));
        let elements = crate::render::compose_frame(f.state(), renderer, output, cursor);
        let refs: Vec<&crate::render::OutputRenderElements> = elements.iter().collect();
        let bytes = crate::render::render_elements_to_rgba(
            renderer,
            Size::<i32, Physical>::from((OUTPUT.0 as i32, OUTPUT.1 as i32)),
            Scale::from(1.0),
            &refs,
        );
        (bytes, smeared)
    };
    f.state().backend = Some(backend);
    let image = RgbaImage::from_raw(OUTPUT.0, OUTPUT.1, bytes.expect("render the frame"))
        .expect("the readback is the frame's own size");
    (image, smeared)
}

/// Everything drawn below the scene's window, where the pointer tests run.
fn drawn_below_window(img: &RgbaImage) -> Option<(u32, u32, u32, u32)> {
    let mut bounds: Option<(u32, u32, u32, u32)> = None;
    for (x, y, p) in img.enumerate_pixels() {
        if y > 450 && p.0[3] != 0 {
            let b = bounds.get_or_insert((x, y, x, y));
            *b = (b.0.min(x), b.1.min(y), b.2.max(x), b.3.max(y));
        }
    }
    bounds
}

const SECOND: f64 = 1.0;
const HZ: f64 = 240.0;

#[test]
#[ignore = "needs Mesa surfaceless EGL; run with --include-ignored"]
fn a_fast_pointer_smears_behind_itself_and_a_still_one_does_not() {
    let _gl = gl::lock();
    let Some((mut f, output)) = scene("cursor", "") else {
        return;
    };
    square_cursor(&mut f);
    let y = 600.0;
    let square = |x: u32| Some((x, 600, x + CURSOR as u32 - 1, 600 + CURSOR as u32 - 1));

    // At rest, and creeping along, the cursor is all there is.
    let mut now = 10.0;
    for step in 0..6 {
        now += SECOND / HZ;
        let x = 200.0 + (step / 3) as f64;
        let (frame, smeared) = pointer_frame(&mut f, &output, now, (x, y));
        assert!(!smeared);
        assert_eq!(drawn_below_window(&frame), square(x as u32));
    }
    assert!(!f.state().has_active_animations());

    // 20 px a frame at 240 Hz: 4800 px/s, a 216 px path in the 45 ms shutter.
    let mut x = 201.0;
    let mut last_len = 0;
    let mut frame = None;
    for step in 0..16 {
        now += SECOND / HZ;
        x += 20.0;
        let (img, smeared) = pointer_frame(&mut f, &output, now, (x, y));
        dump(&img, &format!("cursor_{step:02}"));
        assert!(smeared, "frame {step}");
        let b = drawn_below_window(&img).expect("the cursor is drawn");
        assert_eq!(
            (b.1, b.2, b.3),
            (600, x as u32 + 23, 623),
            "it stays in its lane"
        );
        let len = x as u32 - b.0;
        assert!(len + 2 >= last_len, "the smear grows as it speeds up");
        assert!(len <= 220, "and is no longer than the path: {len}");
        last_len = len;
        frame = Some(img);
    }
    assert!(last_len > 150, "a full-length smear: {last_len}");
    assert!(f.state().has_active_animations());
    let frame = frame.expect("a moving frame");
    // The cursor itself is as sharp as ever...
    let head = x as u32;
    for dx in [0, 12, 23] {
        assert_eq!(frame.get_pixel(head + dx, 612).0, [255; 4]);
    }
    // ...and behind it the smear thins out smoothly toward the tail: no
    // ghost cursors, no steps.
    let density = |from: u32| -> f64 {
        (from..from + 16)
            .flat_map(|px| (604..620).map(move |py| (px, py)))
            .map(|(px, py)| frame.get_pixel(px, py).0[3] as f64)
            .sum::<f64>()
            / 256.0
    };
    let bands: Vec<f64> = (1..=10).map(|i| density(head - i * 18)).collect();
    for pair in bands.windows(2) {
        assert!(
            pair[1] <= pair[0] + 1.0,
            "it thins toward the tail: {bands:?}"
        );
    }
    assert!(bands[0] > 40.0 && bands[0] < 255.0, "{bands:?}");
    assert!(bands[9] < bands[0] / 4.0, "{bands:?}");
    for px in head - 150..head - 4 {
        let (a, b) = (
            frame.get_pixel(px, 612).0[3] as i32,
            frame.get_pixel(px + 1, 612).0[3] as i32,
        );
        assert!((a - b).abs() < 40, "a step at x = {px}: {a} then {b}");
    }

    // Stopped, the smear shrinks back into the cursor within the shutter time
    // and the frame is the resting one again.
    for step in 0..14 {
        now += SECOND / HZ;
        let (img, _) = pointer_frame(&mut f, &output, now, (x, y));
        dump(&img, &format!("cursor_stop_{step:02}"));
        let b = drawn_below_window(&img).expect("the cursor is drawn");
        assert_eq!((b.1, b.2, b.3), (600, x as u32 + 23, 623), "nothing ahead");
        let len = x as u32 - b.0;
        assert!(
            len <= last_len + 2,
            "it only shrinks: {len} after {last_len}"
        );
        last_len = len;
    }
    let (rest, smeared) = pointer_frame(&mut f, &output, now + SECOND / HZ, (x, y));
    assert!(!smeared);
    assert_eq!(drawn_below_window(&rest), square(x as u32));
    assert!(!f.state().has_active_animations());

    teardown(&mut f, &output);
}

#[test]
#[ignore = "needs Mesa surfaceless EGL; run with --include-ignored"]
fn a_pointer_that_is_put_somewhere_leaves_no_streak() {
    let _gl = gl::lock();
    let Some((mut f, output)) = scene("cursor-warp", "") else {
        return;
    };
    square_cursor(&mut f);
    let mut now = 10.0;
    let mut x = 100.0;
    for _ in 0..12 {
        now += SECOND / HZ;
        x += 20.0;
        pointer_frame(&mut f, &output, now, (x, 600.0));
    }
    // Across the screen in one frame: nobody's hand.
    now += SECOND / HZ;
    let (frame, smeared) = pointer_frame(&mut f, &output, now, (1100.0, 700.0));
    assert!(!smeared);
    assert_eq!(drawn_below_window(&frame), Some((1100, 700, 1123, 723)));

    // A camera pan under a still pointer is not the pointer moving.
    for _ in 0..12 {
        now += SECOND / HZ;
        f.state().with_output_state(|os| os.camera.x += 25.0);
        let (_, smeared) = pointer_frame(&mut f, &output, now, (1100.0, 700.0));
        assert!(!smeared);
    }

    // An output that was not drawn for a while does not join its old path to
    // where the pointer is now.
    for _ in 0..6 {
        now += SECOND / HZ;
        x += 20.0;
        pointer_frame(&mut f, &output, now, (x, 600.0));
    }
    now += 0.5;
    let (_, smeared) = pointer_frame(&mut f, &output, now, (x + 60.0, 600.0));
    assert!(!smeared);

    teardown(&mut f, &output);
}

#[test]
#[ignore = "needs Mesa surfaceless EGL; run with --include-ignored"]
fn the_smear_obeys_its_switch_and_the_cursor_hiding() {
    let _gl = gl::lock();
    let Some((mut f, output)) = scene("cursor-off", "[effects.cursor_blur]\nenabled = false\n")
    else {
        return;
    };
    square_cursor(&mut f);
    let mut now = 10.0;
    let mut x = 100.0;
    let mut sweep = |f: &mut Fixture, frames: usize| {
        let mut smeared = false;
        for _ in 0..frames {
            now += SECOND / HZ;
            x += 20.0;
            smeared = pointer_frame(f, &output, now, (x, 600.0)).1;
        }
        smeared
    };
    assert!(!sweep(&mut f, 12), "switched off");
    assert!(!f.state().has_active_animations());

    // Switched on live, it starts with the next motion...
    f.state().reload_config_from_contents(&config_toml(""));
    f.state().pending_mode_changes.clear();
    square_cursor(&mut f);
    assert!(sweep(&mut f, 12));
    assert!(f.state().has_active_animations());

    // ...a cursor hidden by touch has none, and does not get one back the
    // moment it reappears somewhere...
    f.state().cursor.hidden_by_touch = true;
    assert!(!sweep(&mut f, 2));
    f.state().cursor.hidden_by_touch = false;
    assert!(!sweep(&mut f, 1));
    assert!(sweep(&mut f, 12));

    // ...and switched off mid-smear it is gone with the next frame.
    f.state()
        .reload_config_from_contents(&config_toml("[effects.cursor_blur]\nenabled = false\n"));
    f.state().pending_mode_changes.clear();
    square_cursor(&mut f);
    assert!(!sweep(&mut f, 1));
    f.state().tick_cursor_blur();
    assert!(!f.state().has_active_animations());

    teardown(&mut f, &output);
}
