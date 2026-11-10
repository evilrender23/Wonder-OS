//! Pointer motion blur: the path the cursor swept during a short exposure.
//!
//! A [`Trail`] keeps the pointer's recent positions on one output, in that
//! output's screen space, stamped with time. The smear drawn each frame is
//! the cursor image integrated along the stretch of that path that falls
//! inside the exposure window, so its length is speed × exposure whatever
//! the refresh rate, and it shrinks back into the cursor within one exposure
//! of the pointer stopping. The path is handed to the shader as a quadratic
//! Bézier — head, tail, and a control point that makes it pass through the
//! midpoint — which follows a curving flick without any per-sample state.

use std::collections::VecDeque;

/// How long the virtual shutter stays open at strength 1, in seconds.
const EXPOSURE: f64 = 0.045;
/// A smear shorter than this is not drawn at all...
const MIN_LENGTH: f64 = 5.0;
/// ...and it reaches its full opacity at this length, so it grows out of the
/// cursor instead of popping in.
const FULL_LENGTH: f64 = 22.0;
/// One step faster than this is a warp, not motion: no hand moves a pointer
/// at 30 000 px/s, but a constraint, an output change or a `warp` does.
const WARP_SPEED: f64 = 30_000.0;
/// ...and so is one long step, however slow the frame was.
const WARP_DISTANCE: f64 = 700.0;

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Smear {
    /// Where the cursor is now.
    pub head: [f64; 2],
    /// Bézier control point.
    pub control: [f64; 2],
    /// Where it was one exposure ago.
    pub tail: [f64; 2],
    /// Length of the path, in the units the positions were pushed in.
    pub length: f64,
    /// Overall opacity, 0–1: the fade-in over the first few pixels of speed.
    pub opacity: f32,
}

impl Smear {
    /// The point at `s` along the path: 0 is the head, 1 the tail.
    pub fn at(&self, s: f64) -> [f64; 2] {
        let k = 1.0 - s;
        [
            k * k * self.head[0] + 2.0 * k * s * self.control[0] + s * s * self.tail[0],
            k * k * self.head[1] + 2.0 * k * s * self.control[1] + s * s * self.tail[1],
        ]
    }

    /// Box containing the whole path, `(x0, y0, x1, y1)`. A quadratic Bézier
    /// lies inside the hull of its three points.
    pub fn bounds(&self) -> [f64; 4] {
        let xs = [self.head[0], self.control[0], self.tail[0]];
        let ys = [self.head[1], self.control[1], self.tail[1]];
        [
            xs.iter().copied().fold(f64::INFINITY, f64::min),
            ys.iter().copied().fold(f64::INFINITY, f64::min),
            xs.iter().copied().fold(f64::NEG_INFINITY, f64::max),
            ys.iter().copied().fold(f64::NEG_INFINITY, f64::max),
        ]
    }
}

/// The exposure for a configured strength, in seconds.
pub fn exposure(strength: f64) -> f64 {
    EXPOSURE * strength.clamp(0.0, 2.0)
}

#[derive(Clone, Debug, Default)]
pub struct Trail {
    /// `(time in seconds, position)`, oldest first.
    samples: VecDeque<(f64, [f64; 2])>,
}

impl Trail {
    /// Forget the path: the next position starts a new one. For anything
    /// that moves the pointer without the hand moving it.
    pub fn reset(&mut self) {
        self.samples.clear();
    }

    /// Record where the pointer is at `now`. A step too fast or too long to
    /// be a hand restarts the path there, so a warp never draws a streak.
    pub fn push(&mut self, now: f64, pos: [f64; 2], exposure: f64) {
        if !(pos[0].is_finite() && pos[1].is_finite() && now.is_finite()) {
            self.reset();
            return;
        }
        if let Some(&(t, last)) = self.samples.back() {
            let dist = (pos[0] - last[0]).hypot(pos[1] - last[1]);
            let dt = now - t;
            if dt < 0.0 || dist > WARP_DISTANCE || (dt > 0.0 && dist / dt > WARP_SPEED) {
                self.reset();
            } else if dt == 0.0 {
                self.samples.pop_back();
            }
        }
        self.samples.push_back((now, pos));
        // Keep one sample older than the window: the tail is interpolated
        // between it and the first one inside.
        while self.samples.len() > 2 && self.samples[1].0 <= now - exposure {
            self.samples.pop_front();
        }
    }

    /// Whether there is a smear to draw at `now`: frames are owed until it
    /// has shrunk back into the cursor, and one more to erase it. A pointer
    /// creeping along owes none.
    pub fn is_live(&self, now: f64, exposure: f64) -> bool {
        self.smear(now, exposure).is_some()
    }

    fn position_at(&self, t: f64) -> Option<[f64; 2]> {
        let (first_t, first) = *self.samples.front()?;
        if t <= first_t {
            return Some(first);
        }
        for pair in self.samples.iter().zip(self.samples.iter().skip(1)) {
            let (&(t0, a), &(t1, b)) = pair;
            if t <= t1 {
                let k = if t1 > t0 { (t - t0) / (t1 - t0) } else { 1.0 };
                return Some([a[0] + (b[0] - a[0]) * k, a[1] + (b[1] - a[1]) * k]);
            }
        }
        self.samples.back().map(|s| s.1)
    }

    fn path_length(&self, now: f64, exposure: f64) -> f64 {
        let start = now - exposure;
        let Some(mut prev) = self.position_at(start) else {
            return 0.0;
        };
        let mut length = 0.0;
        for &(t, p) in &self.samples {
            if t > start {
                length += (p[0] - prev[0]).hypot(p[1] - prev[1]);
                prev = p;
            }
        }
        length
    }

    /// The smear to draw at `now`, or `None` when the pointer is at rest or
    /// too slow for one.
    pub fn smear(&self, now: f64, exposure: f64) -> Option<Smear> {
        if exposure <= 0.0 {
            return None;
        }
        let head = self.samples.back()?.1;
        let length = self.path_length(now, exposure);
        if length < MIN_LENGTH {
            return None;
        }
        let tail = self.position_at(now - exposure)?;
        let mid = self.position_at(now - exposure / 2.0)?;
        // The control point that makes the curve pass through `mid` at its
        // half-way parameter.
        let control = [
            2.0 * mid[0] - (head[0] + tail[0]) / 2.0,
            2.0 * mid[1] - (head[1] + tail[1]) / 2.0,
        ];
        // An uneven pace along a straight line (stopping, most of all) would
        // put the control point beyond an end and the curve past the cursor.
        // Along the chord it stays between the ends; only sideways is free.
        let chord = [tail[0] - head[0], tail[1] - head[1]];
        let span = chord[0] * chord[0] + chord[1] * chord[1];
        let control = if span > 0.0 {
            let rel = [control[0] - head[0], control[1] - head[1]];
            let along = (rel[0] * chord[0] + rel[1] * chord[1]) / span;
            let fix = along.clamp(0.0, 1.0) - along;
            [control[0] + chord[0] * fix, control[1] + chord[1] * fix]
        } else {
            control
        };
        let ramp = ((length - MIN_LENGTH) / (FULL_LENGTH - MIN_LENGTH)).clamp(0.0, 1.0);
        Some(Smear {
            head,
            control,
            tail,
            length,
            opacity: (ramp * ramp * (3.0 - 2.0 * ramp)) as f32,
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const E: f64 = EXPOSURE;

    #[test]
    fn a_stopping_pointer_never_smears_ahead_of_itself() {
        let (mut trail, stop) = sweep(240.0, 4000.0, 0.2);
        let head = 4000.0 * stop;
        for i in 1..12 {
            let now = stop + i as f64 / 240.0;
            trail.push(now, [head, 100.0], E);
            let Some(smear) = trail.smear(now, E) else {
                continue;
            };
            for step in 0..=20 {
                let x = smear.at(step as f64 / 20.0)[0];
                assert!(x <= head + 1e-9, "frame {i}: {x} is ahead of {head}");
            }
        }
    }

    fn sweep(hz: f64, speed: f64, seconds: f64) -> (Trail, f64) {
        let mut trail = Trail::default();
        let frames = (seconds * hz).round() as usize;
        let mut now = 0.0;
        for i in 0..=frames {
            now = i as f64 / hz;
            trail.push(now, [speed * now, 100.0], E);
        }
        (trail, now)
    }

    #[test]
    fn a_resting_pointer_draws_nothing() {
        let mut trail = Trail::default();
        assert!(trail.smear(0.0, E).is_none());
        for i in 0..100 {
            trail.push(i as f64 / 240.0, [50.0, 50.0], E);
        }
        assert!(trail.smear(100.0 / 240.0, E).is_none());
        assert!(!trail.is_live(100.0 / 240.0, E));
    }

    #[test]
    fn a_slow_pointer_stays_under_the_threshold() {
        let (trail, now) = sweep(144.0, 80.0, 0.5);
        assert!(trail.smear(now, E).is_none());
    }

    #[test]
    fn the_smear_trails_behind_along_the_motion() {
        let (trail, now) = sweep(144.0, 3000.0, 0.3);
        let smear = trail.smear(now, E).expect("fast pointer smears");
        assert_eq!(smear.head, [3000.0 * now, 100.0]);
        assert!(smear.tail[0] < smear.head[0]);
        assert!((smear.tail[1] - 100.0).abs() < 1e-9);
        assert_eq!(smear.opacity, 1.0);
        let mid = smear.at(0.5);
        assert!(mid[0] < smear.head[0] && mid[0] > smear.tail[0]);
    }

    #[test]
    fn the_length_is_speed_times_exposure_at_any_refresh_rate() {
        for hz in [60.0, 144.0, 240.0] {
            let (trail, now) = sweep(hz, 2400.0, 0.5);
            let smear = trail.smear(now, E).unwrap();
            assert!(
                (smear.length - 2400.0 * E).abs() < 1.0,
                "{hz} Hz: {}",
                smear.length
            );
        }
    }

    #[test]
    fn it_shrinks_away_within_one_exposure_of_stopping() {
        let (mut trail, stop) = sweep(240.0, 4000.0, 0.3);
        let pos = [4000.0 * stop, 100.0];
        let mut last = f64::MAX;
        for i in 1..=12 {
            let now = stop + i as f64 / 240.0;
            trail.push(now, pos, E);
            let length = trail.smear(now, E).map_or(0.0, |s| s.length);
            assert!(length <= last);
            assert_eq!(trail.is_live(now, E), i < 11, "frame {i}");
            last = length;
        }
        assert!(trail.smear(stop + 12.0 / 240.0, E).is_none());
        assert!(!trail.is_live(stop + 12.0 / 240.0, E));
    }

    #[test]
    fn a_warp_restarts_the_path_instead_of_streaking() {
        let (mut trail, now) = sweep(144.0, 3000.0, 0.3);
        trail.push(now + 1.0 / 144.0, [3000.0 * now + 1500.0, 900.0], E);
        assert!(trail.smear(now + 1.0 / 144.0, E).is_none());
        // Slow in time but far: still a warp.
        let mut trail = Trail::default();
        trail.push(0.0, [0.0, 0.0], E);
        trail.push(0.04, [900.0, 0.0], E);
        assert!(trail.smear(0.04, E).is_none());
    }

    #[test]
    fn a_curved_flick_bends_the_smear() {
        let mut trail = Trail::default();
        let mut now = 0.0;
        for i in 0..=40 {
            now = i as f64 / 240.0;
            let a = now * 20.0;
            trail.push(now, [300.0 * a.cos(), 300.0 * a.sin()], E);
        }
        let smear = trail.smear(now, E).unwrap();
        let chord_mid = [
            (smear.head[0] + smear.tail[0]) / 2.0,
            (smear.head[1] + smear.tail[1]) / 2.0,
        ];
        let mid = smear.at(0.5);
        assert!((mid[0] - chord_mid[0]).hypot(mid[1] - chord_mid[1]) > 5.0);
        // On the circle, within the polyline's error.
        assert!((mid[0].hypot(mid[1]) - 300.0).abs() < 3.0);
        let b = smear.bounds();
        for s in [0.0, 0.25, 0.5, 0.75, 1.0] {
            let p = smear.at(s);
            assert!(p[0] >= b[0] - 1e-9 && p[0] <= b[2] + 1e-9);
            assert!(p[1] >= b[1] - 1e-9 && p[1] <= b[3] + 1e-9);
        }
    }

    #[test]
    fn strength_scales_the_exposure_and_zero_disables() {
        assert_eq!(exposure(0.0), 0.0);
        assert!((exposure(2.0) - 2.0 * exposure(1.0)).abs() < 1e-12);
        let (trail, now) = sweep(144.0, 3000.0, 0.3);
        assert!(trail.smear(now, exposure(0.0)).is_none());
    }

    #[test]
    fn history_stays_bounded() {
        let (trail, _) = sweep(1000.0, 3000.0, 5.0);
        assert!(trail.samples.len() <= (E * 1000.0) as usize + 4);
    }

    #[test]
    fn garbage_input_resets() {
        let (mut trail, now) = sweep(144.0, 3000.0, 0.3);
        trail.push(now + 0.01, [f64::NAN, 0.0], E);
        assert!(trail.smear(now + 0.01, E).is_none());
    }
}
