//! The text-caret effect end to end: a client reports its caret over
//! `zwp_text_input_v3`, the compositor follows it with an overlay that exists
//! only while the caret travels, and never sends the client anything for it.

use std::time::Duration;

use wayland_protocols::wp::text_input::zv3::client::zwp_text_input_v3::ZwpTextInputV3;

use image::RgbaImage;
use smithay::output::Output;
use smithay::utils::{Physical, Scale, Size};

use super::client::ClientId;
use super::render_pixels::solid_bgra;
use super::{Fixture, config, gl, map_window, map_window_shm, server_surface, window_by_app_id};

const TICK: Duration = Duration::from_millis(16);

/// A focused window whose client has enabled text input, caret at `(20, 30)`.
fn editing(f: &mut Fixture, app_id: &str) -> (ClientId, ZwpTextInputV3) {
    f.add_output(1, (1920, 1080));
    let id = f.add_client();
    map_window(f, id, app_id, (600, 400));
    // Nothing else is animating, so a frame asked for is the caret's.
    super::tick_until_settled(f);
    super::settle(f);
    let text_input = f.client(id).get_text_input();
    f.double_roundtrip(id);
    text_input.enable();
    move_caret(f, id, &text_input, (20, 30));
    (id, text_input)
}

fn move_caret(f: &mut Fixture, id: ClientId, text_input: &ZwpTextInputV3, at: (i32, i32)) {
    text_input.set_cursor_rectangle(at.0, at.1, 2, 18);
    text_input.commit();
    f.double_roundtrip(id);
}

fn visible(f: &mut Fixture) -> bool {
    f.state().text_caret.anim.visible()
}

/// Tick the effect until it has left the screen; how many frames that took.
fn ticks_until_gone(f: &mut Fixture) -> usize {
    for n in 0..600 {
        if !visible(f) {
            return n;
        }
        f.state().tick_text_caret(TICK);
    }
    panic!("the text caret effect never went away");
}

#[test]
fn a_text_field_is_told_it_has_focus_without_any_input_method() {
    let mut f = Fixture::new();
    let (id, _text_input) = editing(&mut f, "editor");
    assert_eq!(f.client(id).state.text_input_focus, [true]);
}

#[test]
fn the_first_caret_is_placed_without_drawing_anything() {
    let mut f = Fixture::new();
    editing(&mut f, "editor");
    assert!(f.state().text_caret.anim.target().is_some());
    assert!(!visible(&mut f));
    assert!(!f.state().has_active_animations());
}

#[test]
fn a_caret_jump_plays_over_its_window_and_then_goes_away() {
    let mut f = Fixture::new();
    let (id, text_input) = editing(&mut f, "editor");

    move_caret(&mut f, id, &text_input, (320, 210));
    assert!(visible(&mut f));
    assert!(
        f.state().has_active_animations(),
        "the jump asks for frames"
    );
    let window = window_by_app_id(&mut f, "editor").unwrap();
    assert!(f.state().text_caret.shows_on(&server_surface(&window)));

    let frames = ticks_until_gone(&mut f);
    assert!(frames > 3, "the jump was animated, not snapped ({frames})");
    assert!(!f.state().has_active_animations());
    let target = f.state().text_caret.anim.target().unwrap();
    assert_eq!((target.x, target.y), (320.0, 210.0));
}

#[test]
fn an_uncommitted_rectangle_moves_nothing() {
    let mut f = Fixture::new();
    let (id, text_input) = editing(&mut f, "editor");
    text_input.set_cursor_rectangle(320, 210, 2, 18);
    f.double_roundtrip(id);
    assert!(!visible(&mut f));
}

#[test]
fn an_excluded_application_is_left_alone() {
    let mut f = Fixture::with_config(config("[effects.text_cursor]\nexclude = [\"editor\"]\n"));
    let (id, text_input) = editing(&mut f, "editor");
    move_caret(&mut f, id, &text_input, (320, 210));
    assert!(!visible(&mut f));
}

/// Gecko reports the caret a key press late; following it would draw the
/// effect a step behind the real caret.
#[test]
fn an_application_that_reports_its_caret_late_is_left_alone() {
    for app_id in ["firefox", "org.mozilla.Thunderbird", "zen-beta"] {
        let mut f = Fixture::with_config(config(""));
        let (id, text_input) = editing(&mut f, app_id);
        move_caret(&mut f, id, &text_input, (320, 210));
        assert!(!visible(&mut f), "{app_id}");
    }
}

#[test]
fn disabled_the_compositor_does_not_stand_in_as_input_method() {
    let mut f = Fixture::with_config(config("[effects.text_cursor]\nenabled = false\n"));
    let (id, text_input) = editing(&mut f, "editor");
    move_caret(&mut f, id, &text_input, (320, 210));
    assert!(f.client(id).state.text_input_focus.is_empty());
    assert!(!visible(&mut f));
}

#[test]
fn disabling_the_text_field_drops_the_effect() {
    let mut f = Fixture::new();
    let (id, text_input) = editing(&mut f, "editor");
    move_caret(&mut f, id, &text_input, (320, 210));
    assert!(visible(&mut f));

    text_input.disable();
    text_input.commit();
    f.double_roundtrip(id);
    assert!(!visible(&mut f));
}

#[test]
fn the_effect_does_not_follow_focus_to_another_window() {
    let mut f = Fixture::new();
    let (id, text_input) = editing(&mut f, "editor");
    move_caret(&mut f, id, &text_input, (320, 210));
    assert!(visible(&mut f));

    let other = f.add_client();
    map_window(&mut f, other, "other", (300, 200));
    assert!(!visible(&mut f));
    assert_eq!(f.client(id).state.text_input_focus, [true, false]);
}

#[test]
fn a_destroyed_text_input_is_forgotten_cleanly() {
    let mut f = Fixture::new();
    let (id, text_input) = editing(&mut f, "editor");
    move_caret(&mut f, id, &text_input, (320, 210));
    text_input.destroy();
    f.double_roundtrip(id);
    ticks_until_gone(&mut f);
    assert!(f.client(id).protocol_error().is_none());
}

/// The frame the backend would submit for `output`, rendered offscreen.
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
            Size::<i32, Physical>::from((1280, 800)),
            Scale::from(1.0),
            &refs,
        )
    };
    f.state().backend = Some(backend);
    RgbaImage::from_raw(1280, 800, bytes.expect("render the live frame"))
        .expect("the frame fills the buffer")
}

/// Inclusive bounds `(x0, y0, x1, y1)` of the pixels `want` picks, if any.
fn bounds(img: &RgbaImage, want: impl Fn([u8; 4]) -> bool) -> Option<(u32, u32, u32, u32)> {
    let mut found: Option<(u32, u32, u32, u32)> = None;
    for (x, y, p) in img.enumerate_pixels() {
        if want(p.0) {
            let (x0, y0, x1, y1) = found.unwrap_or((x, y, x, y));
            found = Some((x0.min(x), y0.min(y), x1.max(x), y1.max(y)));
        }
    }
    found
}

#[test]
#[ignore = "needs Mesa surfaceless EGL; run with --include-ignored"]
fn the_travelling_caret_is_drawn_between_its_two_positions_and_then_erased() {
    let _gl = gl::lock();
    let Some(renderer) = gl::surfaceless_renderer("text caret") else {
        return;
    };

    let mut f = Fixture::with_config(config(
        "[background]\ntype = \"none\"\n\
         [decorations]\ndefault_mode = \"client\"\nborder_width = 0\nshadow = false\n\
         [effects.text_cursor]\ncolor = \"#0000ff\"\nglow = false\nvfx = \"none\"\n",
    ));
    let output = f.add_output(1, (1280, 800));
    gl::install(&mut f, renderer);

    // An opaque red window, so anything blue in the frame is the effect.
    let id = f.add_client();
    let red = solid_bgra(600, 400, [0, 0, 255, 255], false);
    map_window_shm(&mut f, id, "editor", (600, 400), (600, 400), &red);
    super::tick_until_settled(&mut f);
    super::settle(&mut f);
    let text_input = f.client(id).get_text_input();
    f.double_roundtrip(id);
    text_input.enable();
    move_caret(&mut f, id, &text_input, (20, 30));

    let is_red = |p: [u8; 4]| p[0] > 200 && p[2] < 50;
    let is_blue = |p: [u8; 4]| p[2] > 200 && p[0] < 50;

    let still = live_frame(&mut f, &output);
    let (wx, wy, _, _) = bounds(&still, is_red).expect("the window is on screen");
    assert_eq!(
        bounds(&still, is_blue),
        None,
        "a resting caret draws nothing"
    );

    move_caret(&mut f, id, &text_input, (320, 210));
    f.state().tick_text_caret(TICK);
    f.state().tick_text_caret(TICK);
    let flying = live_frame(&mut f, &output);
    let (x0, y0, x1, y1) = bounds(&flying, is_blue).expect("the caret in flight is drawn");
    // Somewhere on the way from (20, 30) to the far corner of (320, 210, 2, 18),
    // in the window's own coordinates, and not yet arrived.
    assert!(
        x0 >= wx + 19 && y0 >= wy + 29 && x1 <= wx + 323 && y1 <= wy + 229,
        "the caret is off its path: {:?} around window origin {:?}",
        (x0, y0, x1, y1),
        (wx, wy)
    );
    assert!(
        x0 < wx + 300,
        "two frames in, the trailing edge is still behind"
    );

    ticks_until_gone(&mut f);
    let landed = live_frame(&mut f, &output);
    assert_eq!(
        bounds(&landed, is_blue),
        None,
        "the landed caret is the client's"
    );

    let output_name = output.name();
    f.state().render.remove_output(&output_name);
    gl::uninstall(&mut f);
}
