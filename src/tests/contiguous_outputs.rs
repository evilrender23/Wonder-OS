//! `[output] contiguous`: monitors look at the canvas side by side, as their
//! `[[outputs]]` positions place them, so a window dragged across the bezel
//! stays on one continuous canvas. `align-outputs` lines them up again.

use smithay::utils::Point;

use super::{Fixture, config};
use crate::state::output_state;
use driftwm::config::Action;

/// Two monitors as on the user's desk: a 2560×1440 on the left, a 1920×1080
/// to its right and 193 px lower.
const DESK: &str = r#"
[output]
contiguous = true

[[outputs]]
name = "HEADLESS-1"
position = [0, 0]

[[outputs]]
name = "HEADLESS-2"
position = [2560, 193]
"#;

fn cam(o: &smithay::output::Output) -> Point<f64, smithay::utils::Logical> {
    output_state(o).camera
}

fn assert_close(a: Point<f64, smithay::utils::Logical>, b: (f64, f64)) {
    assert!(
        (a.x - b.0).abs() < 1e-6 && (a.y - b.1).abs() < 1e-6,
        "camera {a:?} != {b:?}"
    );
}

#[test]
fn a_second_monitor_starts_beside_the_first() {
    let mut f = Fixture::with_config(config(DESK));
    let left = f.add_output(1, (2560, 1440));
    let right = f.add_output(2, (1920, 1080));
    let c = cam(&left);
    assert_close(cam(&right), (c.x + 2560.0, c.y + 193.0));
    assert_eq!(output_state(&right).zoom, output_state(&left).zoom);
}

#[test]
fn off_by_default_every_monitor_opens_on_its_own_origin() {
    let mut f = Fixture::with_config(config(&DESK.replace("contiguous = true", "")));
    let _left = f.add_output(1, (2560, 1440));
    let right = f.add_output(2, (1920, 1080));
    assert_close(cam(&right), (-960.0, -540.0));
}

#[test]
fn the_shared_zoom_scales_the_offset() {
    let mut f = Fixture::with_config(config(DESK));
    let left = f.add_output(1, (2560, 1440));
    {
        let mut os = output_state(&left);
        os.zoom = 0.5;
        os.camera = Point::from((100.0, -40.0));
    }
    let right = f.add_output(2, (1920, 1080));
    // 2560 layout px at zoom 0.5 is 5120 canvas units.
    assert_close(cam(&right), (100.0 + 5120.0, -40.0 + 386.0));
    assert_eq!(output_state(&right).zoom, 0.5);
}

#[test]
fn align_outputs_lines_the_others_up_with_the_active_one() {
    let mut f = Fixture::with_config(config(DESK));
    let left = f.add_output(1, (2560, 1440));
    let right = f.add_output(2, (1920, 1080));
    f.state().focused_output = Some(left.clone());
    // Pan each monitor somewhere unrelated, at different zooms.
    {
        let mut os = output_state(&left);
        os.camera = Point::from((-3000.0, 700.0));
        os.zoom = 2.0;
    }
    {
        let mut os = output_state(&right);
        os.camera = Point::from((9000.0, 9000.0));
        os.zoom = 0.7;
    }

    // A different zoom can't glide, so it lands at once.
    f.state().execute_action(&Action::AlignOutputs);
    assert_close(cam(&right), (-3000.0 + 1280.0, 700.0 + 96.5));
    assert_eq!(output_state(&right).zoom, 2.0);
    assert_close(cam(&left), (-3000.0, 700.0));

    // Same zoom: it glides — the target is set, the camera follows.
    output_state(&right).camera = Point::from((50.0, 50.0));
    f.state().execute_action(&Action::AlignOutputs);
    let target = output_state(&right).camera_target.expect("animated align");
    assert_close(target, (-3000.0 + 1280.0, 700.0 + 96.5));
    output_state(&right).camera_target = None;
}

#[test]
fn the_active_monitors_landing_view_is_what_others_align_to() {
    let mut f = Fixture::with_config(config(DESK));
    let left = f.add_output(1, (2560, 1440));
    let right = f.add_output(2, (1920, 1080));
    f.state().focused_output = Some(left.clone());
    output_state(&left).camera_target = Some(Point::from((400.0, 400.0)));
    f.state().align_outputs(false);
    assert_close(cam(&right), (400.0 + 2560.0, 400.0 + 193.0));
    output_state(&left).camera_target = None;
}

#[test]
fn moving_a_monitor_in_the_config_moves_its_view_along() {
    let mut f = Fixture::with_config(config(DESK));
    let left = f.add_output(1, (2560, 1440));
    let right = f.add_output(2, (1920, 1080));
    f.state().focused_output = Some(left.clone());
    // The LG moves to the left of the MSI.
    let swapped = DESK
        .replace("position = [0, 0]", "position = [1920, 0]")
        .replace("position = [2560, 193]", "position = [0, 193]");
    f.state().reload_config_from_contents(&swapped);
    let c = output_state(&left).camera;
    let pending = output_state(&right).camera_target;
    let target = pending.unwrap_or_else(|| cam(&right));
    assert_close(target, (c.x - 1920.0, c.y + 193.0));
    output_state(&right).camera_target = None;
    // Only the udev loop drains mode intents (the default "preferred" rule
    // queues one per output on every reload).
    f.state().pending_mode_changes.clear();
}

#[test]
fn align_outputs_with_one_monitor_is_a_no_op() {
    let mut f = Fixture::with_config(config(DESK));
    let only = f.add_output(1, (2560, 1440));
    let before = cam(&only);
    f.state().execute_action(&Action::AlignOutputs);
    assert_close(cam(&only), (before.x, before.y));
    assert!(output_state(&only).camera_target.is_none());
}
