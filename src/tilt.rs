//! 3D windows: a dragged window is a card held by one point.
//!
//! While the hand moves, the card leans into the motion — its leading edge
//! swings away from the viewer, in true perspective — and when the hand stops
//! or lets go it swings back and settles with a small overshoot. The spot
//! under the pointer is the pivot: it never leaves the pointer.
//!
//! The model is two angles on one damped spring each. The spring's target is
//! the drag velocity, squashed so no speed tips the card past `MAX_ANGLE`; a
//! window nobody is moving has both angles at exactly zero and is drawn by the
//! ordinary path. Integration is a fixed substep, so the swing is the same at
//! any refresh rate.

/// The lean at full speed and `strength = 1`, in radians (about 20°).
const MAX_ANGLE: f64 = 0.35;
/// Drag speed, in logical pixels per second, that leans the card to about
/// three quarters of the way.
const REFERENCE_SPEED: f64 = 1500.0;
/// Natural frequency of the swing, rad/s, and its damping ratio: under one, so
/// the card overshoots once before it settles.
const FREQUENCY: f64 = 13.0;
const DAMPING: f64 = 0.42;
/// How fast the measured velocity follows the pointer's, per second. Pointer
/// events arrive in bursts; raw, they would shake the card.
const VELOCITY_FOLLOW: f64 = 18.0;
const SUBSTEP: f64 = 1.0 / 240.0;
/// A stall longer than this is simulated as this much.
const MAX_FRAME: f64 = 0.1;
/// Below both, the card is flat and snaps to exactly zero.
const REST_ANGLE: f64 = 0.0015;
const REST_RATE: f64 = 0.02;
/// The eye's distance from the screen, in units of the window's longer side.
const EYE_DISTANCE: f64 = 1.6;

/// A card's attitude: what the renderer needs to draw it.
#[derive(Clone, Copy, Debug, Default, PartialEq)]
pub struct Pose {
    /// Rotation about the vertical axis, radians. Positive: the right-hand
    /// side is the far one.
    pub yaw: f64,
    /// Rotation about the horizontal axis. Positive: the bottom is far.
    pub pitch: f64,
    /// The held spot, as a fraction of the frame.
    pub pivot: [f64; 2],
}

impl Pose {
    pub fn is_flat(&self) -> bool {
        self.yaw == 0.0 && self.pitch == 0.0
    }

    /// Where the frame's point `(x, y)` lands on screen. `frame` is the
    /// undeformed frame as `[x, y, w, h]`; all in the same pixel units.
    pub fn project(&self, frame: [f64; 4], x: f64, y: f64) -> [f64; 2] {
        if self.is_flat() {
            return [x, y];
        }
        let px = frame[0] + self.pivot[0] * frame[2];
        let py = frame[1] + self.pivot[1] * frame[3];
        let eye = EYE_DISTANCE * frame[2].max(frame[3]).max(1.0);
        let (dx, dy) = (x - px, y - py);
        let (sin_yaw, cos_yaw) = self.yaw.sin_cos();
        let (sin_pitch, cos_pitch) = self.pitch.sin_cos();
        // Yaw first, then pitch; z grows away from the viewer.
        let x1 = dx * cos_yaw;
        let z1 = dx * sin_yaw;
        let y2 = dy * cos_pitch - z1 * sin_pitch;
        let z2 = dy * sin_pitch + z1 * cos_pitch;
        let shrink = eye / (eye + z2).max(eye * 0.25);
        [px + x1 * shrink, py + y2 * shrink]
    }

    /// The light on the card, for the fragment stage: how much brighter the
    /// near side is than the middle along each axis (per unit of frame
    /// fraction), then where the glint's streak sits across the frame and how
    /// bright it is. All zero when flat or with `shine` off.
    pub fn light(&self, shine: bool) -> [f32; 4] {
        if !shine || self.is_flat() {
            return [0.0; 4];
        }
        let lean = (self.yaw.hypot(self.pitch) / MAX_ANGLE).min(2.0);
        // The far side falls into shade, the near one catches the light.
        let gx = -self.yaw * 0.9;
        let gy = -self.pitch * 0.9;
        // The streak crosses the card as it turns, like a reflection would.
        let across = 0.5 + (self.yaw - 0.6 * self.pitch) / MAX_ANGLE * 0.75;
        let glint = 0.16 * (lean * lean).min(1.0);
        [gx as f32, gy as f32, across as f32, glint as f32]
    }

    /// How far, as a fraction of the frame's longer side, any point of the
    /// frame can be from where it rests.
    pub fn reach(&self) -> f64 {
        // Perspective magnifies the near side by at most eye/(eye - sin a),
        // with the pivot at worst a full side away.
        let lean = self.yaw.abs().max(self.pitch.abs()).sin();
        (EYE_DISTANCE / (EYE_DISTANCE - lean).max(0.4) - 1.0) * 2.0 + 0.02
    }
}

#[derive(Clone, Debug)]
pub struct TiltModel {
    held: bool,
    pivot: [f64; 2],
    /// Motion fed in since the last step, logical pixels.
    pending: [f64; 2],
    /// Smoothed drag velocity, logical pixels per second.
    velocity: [f64; 2],
    /// Yaw and pitch, and their rates.
    angle: [f64; 2],
    rate: [f64; 2],
    /// Fraction of a substep the last frame left unsimulated.
    carry: f64,
}

impl TiltModel {
    /// A flat card picked up at `(u, v)`, a fraction of its frame.
    pub fn new(u: f64, v: f64) -> Self {
        Self {
            held: true,
            pivot: [u.clamp(0.0, 1.0), v.clamp(0.0, 1.0)],
            pending: [0.0; 2],
            velocity: [0.0; 2],
            angle: [0.0; 2],
            rate: [0.0; 2],
            carry: 0.0,
        }
    }

    /// Picked up again at `(u, v)`. A card still swinging keeps its pivot
    /// until it is flat, or the picture would jump.
    pub fn grab(&mut self, u: f64, v: f64) {
        self.held = true;
        if self.angle == [0.0; 2] {
            self.pivot = [u.clamp(0.0, 1.0), v.clamp(0.0, 1.0)];
        }
    }

    pub fn release(&mut self) {
        self.held = false;
        self.pending = [0.0; 2];
    }

    pub fn is_held(&self) -> bool {
        self.held
    }

    /// The hand carried the card by `(dx, dy)` logical pixels.
    pub fn moved(&mut self, dx: f64, dy: f64) {
        self.pending[0] += dx;
        self.pending[1] += dy;
    }

    pub fn pose(&self) -> Pose {
        Pose {
            yaw: self.angle[0],
            pitch: self.angle[1],
            pivot: self.pivot,
        }
    }

    /// Advance by `dt` seconds. Returns whether the card still needs its
    /// model: false once it has been let go and has come to rest.
    pub fn step(&mut self, dt: f64, strength: f64) -> bool {
        let dt = dt.clamp(0.0, MAX_FRAME);
        if dt > 0.0 {
            let measured = if self.held {
                [self.pending[0] / dt, self.pending[1] / dt]
            } else {
                [0.0; 2]
            };
            self.pending = [0.0; 2];
            let follow = 1.0 - (-VELOCITY_FOLLOW * dt).exp();
            for (v, m) in self.velocity.iter_mut().zip(measured) {
                *v += (m - *v) * follow;
            }
        }
        let limit = MAX_ANGLE * strength.max(0.0);
        let target = [
            limit * (self.velocity[0] / REFERENCE_SPEED).tanh(),
            limit * (self.velocity[1] / REFERENCE_SPEED).tanh(),
        ];
        let mut time = self.carry + dt;
        while time >= SUBSTEP {
            time -= SUBSTEP;
            for ((angle, rate), target) in self.angle.iter_mut().zip(&mut self.rate).zip(target) {
                let pull =
                    FREQUENCY * FREQUENCY * (target - *angle) - 2.0 * DAMPING * FREQUENCY * *rate;
                *rate += pull * SUBSTEP;
                *angle += *rate * SUBSTEP;
            }
        }
        self.carry = time;
        let still = target.iter().all(|t| t.abs() < REST_ANGLE)
            && self.angle.iter().all(|a| a.abs() < REST_ANGLE)
            && self.rate.iter().all(|r| r.abs() < REST_RATE);
        if still {
            self.angle = [0.0; 2];
            self.rate = [0.0; 2];
            self.velocity = [0.0; 2];
            return self.held;
        }
        true
    }

    /// Flat and unmoved: nothing to draw and nothing to animate until the
    /// hand moves again.
    pub fn is_idle(&self) -> bool {
        self.angle == [0.0; 2]
            && self.rate == [0.0; 2]
            && self.velocity == [0.0; 2]
            && self.pending == [0.0; 2]
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const FRAME: f64 = 1.0 / 60.0;

    fn drag(model: &mut TiltModel, speed: [f64; 2], seconds: f64, strength: f64) {
        let frames = (seconds / FRAME).round() as usize;
        for _ in 0..frames {
            model.moved(speed[0] * FRAME, speed[1] * FRAME);
            model.step(FRAME, strength);
        }
    }

    fn settle(model: &mut TiltModel) -> usize {
        for frame in 0..600 {
            if !model.step(FRAME, 1.0) {
                return frame;
            }
        }
        panic!("the card never came to rest: {:?}", model.pose());
    }

    #[test]
    fn a_card_nobody_moves_stays_flat() {
        let mut model = TiltModel::new(0.5, 0.1);
        for _ in 0..120 {
            assert!(model.step(FRAME, 1.0), "held, so the model is kept");
            assert!(model.pose().is_flat());
        }
    }

    #[test]
    fn dragging_leans_the_leading_edge_away() {
        let mut model = TiltModel::new(0.5, 0.5);
        drag(&mut model, [1500.0, 0.0], 0.6, 1.0);
        let right = model.pose();
        assert!(right.yaw > 0.15 && right.yaw <= MAX_ANGLE, "{right:?}");
        assert_eq!(right.pitch, 0.0);

        let mut model = TiltModel::new(0.5, 0.5);
        drag(&mut model, [0.0, -1500.0], 0.6, 1.0);
        let up = model.pose();
        assert!(up.pitch < -0.15 && up.yaw == 0.0, "{up:?}");
    }

    #[test]
    fn no_speed_tips_the_card_past_its_limit() {
        for strength in [0.5, 1.0, 2.0] {
            let mut model = TiltModel::new(0.0, 0.0);
            let mut peak: f64 = 0.0;
            for _ in 0..240 {
                model.moved(400.0, -400.0);
                model.step(FRAME, strength);
                let pose = model.pose();
                peak = peak.max(pose.yaw.abs()).max(pose.pitch.abs());
            }
            // The spring overshoots its target on the way in, by less than
            // a third at this damping.
            assert!(peak <= MAX_ANGLE * strength * 1.3, "{strength}: {peak}");
            assert!(peak >= MAX_ANGLE * strength * 0.9, "{strength}: {peak}");
        }
    }

    #[test]
    fn a_released_card_swings_back_past_flat_and_comes_to_rest() {
        let mut model = TiltModel::new(0.5, 0.0);
        drag(&mut model, [2000.0, 0.0], 0.5, 1.0);
        model.release();
        let mut overshot = false;
        let mut frames = 0;
        while model.step(FRAME, 1.0) {
            overshot |= model.pose().yaw < -0.005;
            frames += 1;
            assert!(frames < 600, "never settled");
        }
        assert!(overshot, "an underdamped swing crosses flat once");
        assert!(frames < 90, "settles within a second and a half: {frames}");
        assert!(model.pose().is_flat());
    }

    #[test]
    fn a_held_card_that_stops_goes_flat_and_keeps_its_model() {
        let mut model = TiltModel::new(0.5, 0.0);
        drag(&mut model, [2000.0, 500.0], 0.4, 1.0);
        for _ in 0..180 {
            assert!(model.step(FRAME, 1.0));
        }
        assert!(model.pose().is_flat());
        assert!(model.is_held() && model.is_idle());
        model.moved(3.0, 0.0);
        assert!(!model.is_idle());
    }

    #[test]
    fn the_swing_does_not_depend_on_the_refresh_rate() {
        let run = |hz: f64| {
            let dt = 1.0 / hz;
            let mut model = TiltModel::new(0.5, 0.5);
            let frames = (0.25 * hz).round() as usize;
            for _ in 0..frames {
                model.moved(1200.0 * dt, 300.0 * dt);
                model.step(dt, 1.0);
            }
            model.pose()
        };
        let (slow, fast) = (run(60.0), run(240.0));
        assert!((slow.yaw - fast.yaw).abs() < 0.02, "{slow:?} {fast:?}");
        assert!((slow.pitch - fast.pitch).abs() < 0.02, "{slow:?} {fast:?}");
    }

    #[test]
    fn a_stall_is_not_fast_forwarded() {
        let mut model = TiltModel::new(0.5, 0.5);
        model.moved(40.0, 0.0);
        model.step(5.0, 1.0);
        let pose = model.pose();
        assert!(pose.yaw.is_finite() && pose.yaw.abs() <= MAX_ANGLE * 1.3);
    }

    #[test]
    fn zero_strength_never_leaves_flat() {
        let mut model = TiltModel::new(0.5, 0.5);
        drag(&mut model, [3000.0, 3000.0], 0.5, 0.0);
        assert!(model.pose().is_flat());
    }

    #[test]
    fn a_regrab_mid_swing_keeps_the_pivot() {
        let mut model = TiltModel::new(0.2, 0.1);
        drag(&mut model, [2000.0, 0.0], 0.4, 1.0);
        model.release();
        model.step(FRAME, 1.0);
        model.grab(0.9, 0.9);
        assert_eq!(model.pose().pivot, [0.2, 0.1]);
        model.release();
        settle(&mut model);
        model.grab(0.9, 0.9);
        assert_eq!(model.pose().pivot, [0.9, 0.9]);
    }

    #[test]
    fn the_pivot_stays_under_the_pointer() {
        let frame = [100.0, 50.0, 800.0, 600.0];
        let pose = Pose {
            yaw: 0.3,
            pitch: -0.2,
            pivot: [0.25, 0.05],
        };
        let at = pose.project(frame, 300.0, 80.0);
        assert!((at[0] - 300.0).abs() < 1e-9 && (at[1] - 80.0).abs() < 1e-9);
    }

    #[test]
    fn a_flat_pose_projects_every_point_onto_itself() {
        let pose = Pose {
            pivot: [0.3, 0.7],
            ..Pose::default()
        };
        assert_eq!(
            pose.project([10.0, 20.0, 300.0, 200.0], 123.0, 45.0),
            [123.0, 45.0]
        );
        assert_eq!(pose.light(true), [0.0; 4]);
    }

    #[test]
    fn the_far_side_is_drawn_smaller_than_the_near_one() {
        let frame = [0.0, 0.0, 800.0, 600.0];
        let pose = Pose {
            yaw: 0.3,
            pitch: 0.0,
            pivot: [0.5, 0.5],
        };
        let height_at = |x: f64| {
            let top = pose.project(frame, x, 0.0);
            let bottom = pose.project(frame, x, 600.0);
            bottom[1] - top[1]
        };
        let (near, far) = (height_at(0.0), height_at(800.0));
        assert!(near > 600.0 && far < 600.0, "{near} {far}");
        // The far side is also pulled in toward the pivot by the turn.
        assert!(pose.project(frame, 800.0, 300.0)[0] < 800.0);
    }

    #[test]
    fn reach_bounds_every_corner() {
        let frame = [0.0, 0.0, 900.0, 500.0];
        for (yaw, pitch) in [(0.7, 0.7), (-0.7, 0.7), (0.35, -0.35), (-0.1, 0.0)] {
            for pivot in [[0.0, 0.0], [1.0, 1.0], [0.5, 0.5], [1.0, 0.0]] {
                let pose = Pose { yaw, pitch, pivot };
                let reach = pose.reach() * 900.0;
                for (x, y) in [(0.0, 0.0), (900.0, 0.0), (0.0, 500.0), (900.0, 500.0)] {
                    let at = pose.project(frame, x, y);
                    let moved = (at[0] - x).hypot(at[1] - y);
                    assert!(moved <= reach, "{pose:?} ({x},{y}): {moved} > {reach}");
                }
            }
        }
    }

    #[test]
    fn the_light_follows_the_lean() {
        let pose = Pose {
            yaw: 0.3,
            pitch: 0.0,
            pivot: [0.5, 0.5],
        };
        let light = pose.light(true);
        // Right side far: darker to the right, streak right of centre.
        assert!(light[0] < 0.0 && light[1] == 0.0);
        assert!(light[2] > 0.5 && light[3] > 0.0);
        assert_eq!(pose.light(false), [0.0; 4]);
    }
}
