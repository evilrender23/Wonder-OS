//! Compiz's wobbly-window model: a 4×4 grid of masses joined by springs.
//!
//! The grid is stored as *offsets* from where each mass would sit on an
//! undeformed window, so a window at rest is the all-zero grid and renders
//! through the normal path. One mass, the anchor, is the one nearest the grab
//! point; it stays at offset zero and drags the rest along. The mass is rarely
//! exactly under the hand, so the grid is drawn shifted by whatever the patch
//! says the grabbed spot itself has strayed: that spot is glued to the pointer
//! to the pixel, with no lag. A rigid window move is fed in as the opposite shift of every
//! other mass: they stay where they were on screen and the springs pull them
//! along. The anchor stays pinned after release too, so the window swings
//! around it and settles exactly where it was dropped instead of drifting.
//!
//! The constants are Compiz's (spring 8, friction 3, mass 15, one step per
//! 15 ms) rewritten as continuous accelerations, then integrated with a fixed
//! substep and interpolated, so the motion is the same at any refresh rate.

/// Masses per side.
pub const GRID: usize = 4;
const COUNT: usize = GRID * GRID;

const COMPIZ_MASS: f64 = 15.0;
const COMPIZ_STEP: f64 = 0.015;
const SUBSTEP: f64 = 1.0 / 480.0;
/// A stall longer than this is simulated as this much: the jelly resumes
/// instead of fast-forwarding through a second of physics in one frame.
const MAX_FRAME: f64 = 0.1;
/// Below both, the grid is at rest and snaps to exactly zero.
const REST_OFFSET: f64 = 0.05;
const REST_SPEED: f64 = 2.0;
/// How fast a mass that just became the anchor gives up its offset, per second.
const ANCHOR_PULL: f64 = 30.0;

/// One 2D offset per mass, row-major, in logical pixels.
pub type Offsets = [[f64; 2]; COUNT];

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct WobblyParams {
    pub stiffness: f64,
    pub friction: f64,
}

impl Default for WobblyParams {
    fn default() -> Self {
        Self {
            stiffness: 8.0,
            friction: 3.0,
        }
    }
}

#[derive(Clone, Debug)]
pub struct WobblyModel {
    current: Offsets,
    previous: Offsets,
    velocity: Offsets,
    anchor: usize,
    /// The grabbed spot, as a fraction of the frame.
    held: [f64; 2],
    /// What is left of the jump a re-grab elsewhere would have caused.
    carry: [f64; 2],
    accumulator: f64,
    grabbed: bool,
    /// Longest offset a mass may reach: a flick must not smear the window
    /// across the whole screen.
    max_stretch: f64,
}

impl WobblyModel {
    /// A resting grid grabbed at `(u, v)`, the grab point as a fraction of the
    /// window frame. `extent` is the frame's longer side.
    pub fn new(u: f64, v: f64, extent: f64) -> Self {
        Self {
            current: [[0.0; 2]; COUNT],
            previous: [[0.0; 2]; COUNT],
            velocity: [[0.0; 2]; COUNT],
            anchor: nearest_mass(u, v),
            held: [u.clamp(0.0, 1.0), v.clamp(0.0, 1.0)],
            carry: [0.0; 2],
            accumulator: 0.0,
            grabbed: true,
            max_stretch: max_stretch_for(extent),
        }
    }

    /// Grab again, possibly somewhere else, while the grid is still moving.
    pub fn grab(&mut self, u: f64, v: f64, extent: f64) {
        // The picture is drawn about the grabbed spot; a new spot would make
        // it jump by the difference, so that is carried and eased away.
        let grid = self.blended();
        let held = [u.clamp(0.0, 1.0), v.clamp(0.0, 1.0)];
        let (was, now) = (
            patch_offset(&grid, self.held[0], self.held[1]),
            patch_offset(&grid, held[0], held[1]),
        );
        self.carry = [
            self.carry[0] + now[0] - was[0],
            self.carry[1] + now[1] - was[1],
        ];
        self.held = held;
        self.anchor = nearest_mass(u, v);
        self.velocity[self.anchor] = [0.0; 2];
        self.grabbed = true;
        self.max_stretch = max_stretch_for(extent);
    }

    /// Longest offset a mass may reach, in logical pixels.
    pub fn max_stretch(&self) -> f64 {
        // The drawn grid is the simulated one less the grabbed spot's own
        // offset, plus what a re-grab carried over.
        3.0 * self.max_stretch
    }

    pub fn release(&mut self) {
        self.grabbed = false;
    }

    pub fn is_grabbed(&self) -> bool {
        self.grabbed
    }

    /// The window moved rigidly by `(dx, dy)`: every mass but the anchor stays
    /// behind on screen.
    pub fn moved(&mut self, dx: f64, dy: f64) {
        if !(dx.is_finite() && dy.is_finite()) || (dx == 0.0 && dy == 0.0) {
            return;
        }
        for i in 0..COUNT {
            if i == self.anchor {
                continue;
            }
            for grid in [&mut self.current, &mut self.previous] {
                grid[i] = clamp_len([grid[i][0] - dx, grid[i][1] - dy], self.max_stretch);
            }
        }
    }

    /// Whether every mass is home and still.
    pub fn at_rest(&self) -> bool {
        self.current
            .iter()
            .chain(self.velocity.iter())
            .chain([&self.carry])
            .all(|p| p[0] == 0.0 && p[1] == 0.0)
    }

    /// Advance by `dt` seconds. Returns whether the grid is still disturbed.
    pub fn step(&mut self, dt: f64, params: WobblyParams) -> bool {
        if self.at_rest() {
            self.accumulator = 0.0;
            self.previous = self.current;
            return false;
        }
        let spring = params.stiffness / (COMPIZ_MASS * COMPIZ_STEP * COMPIZ_STEP);
        let damping = params.friction / (COMPIZ_MASS * COMPIZ_STEP);
        self.accumulator += dt.clamp(0.0, MAX_FRAME);
        while self.accumulator >= SUBSTEP {
            self.accumulator -= SUBSTEP;
            self.previous = self.current;
            self.substep(spring, damping);
        }
        if self.settled() {
            self.current = [[0.0; 2]; COUNT];
            self.previous = self.current;
            self.velocity = self.current;
            self.carry = [0.0; 2];
            self.accumulator = 0.0;
            return false;
        }
        true
    }

    /// The grid to draw: the last two substeps blended by how far the frame
    /// time has run into the next one, so a 144 Hz frame never shows the
    /// 480 Hz step it happened to land on; shifted so the grabbed spot has
    /// not moved at all.
    pub fn offsets(&self) -> Offsets {
        let mut out = self.blended();
        let shift = patch_offset(&out, self.held[0], self.held[1]);
        for o in &mut out {
            *o = [
                o[0] - shift[0] + self.carry[0],
                o[1] - shift[1] + self.carry[1],
            ];
        }
        out
    }

    fn blended(&self) -> Offsets {
        let t = (self.accumulator / SUBSTEP).clamp(0.0, 1.0);
        let mut out = self.current;
        for (o, p) in out.iter_mut().zip(self.previous.iter()) {
            o[0] = p[0] + (o[0] - p[0]) * t;
            o[1] = p[1] + (o[1] - p[1]) * t;
        }
        out
    }

    fn settled(&self) -> bool {
        let rest = REST_OFFSET * REST_OFFSET;
        let still = REST_SPEED * REST_SPEED;
        self.current
            .iter()
            .chain([&self.carry])
            .all(|p| p[0] * p[0] + p[1] * p[1] < rest)
            && self
                .velocity
                .iter()
                .all(|v| v[0] * v[0] + v[1] * v[1] < still)
    }

    fn substep(&mut self, spring: f64, damping: f64) {
        let mut force = [[0.0f64; 2]; COUNT];
        let current = &self.current;
        let mut pull = |a: usize, b: usize| {
            let stretch = [
                0.5 * spring * (current[b][0] - current[a][0]),
                0.5 * spring * (current[b][1] - current[a][1]),
            ];
            force[a] = [force[a][0] + stretch[0], force[a][1] + stretch[1]];
            force[b] = [force[b][0] - stretch[0], force[b][1] - stretch[1]];
        };
        for row in 0..GRID {
            for col in 0..GRID {
                let i = row * GRID + col;
                if col + 1 < GRID {
                    pull(i, i + 1);
                }
                if row + 1 < GRID {
                    pull(i, i + GRID);
                }
            }
        }
        let keep = (-ANCHOR_PULL * SUBSTEP).exp();
        self.carry = [self.carry[0] * keep, self.carry[1] * keep];
        let anchor = self.anchor;
        let masses = self.current.iter_mut().zip(self.velocity.iter_mut());
        for (i, ((at, speed), force)) in masses.zip(force).enumerate() {
            if i == anchor {
                // Usually already zero; after a re-grab elsewhere the new
                // anchor eases home instead of jumping there.
                *speed = [0.0; 2];
                *at = [at[0] * keep, at[1] * keep];
                continue;
            }
            for axis in 0..2 {
                speed[axis] += (force[axis] - damping * speed[axis]) * SUBSTEP;
                at[axis] += speed[axis] * SUBSTEP;
            }
        }
    }
}

fn max_stretch_for(extent: f64) -> f64 {
    (extent * 0.5).clamp(120.0, 600.0)
}

fn clamp_len(p: [f64; 2], max: f64) -> [f64; 2] {
    let len = (p[0] * p[0] + p[1] * p[1]).sqrt();
    if len > max {
        [p[0] * max / len, p[1] * max / len]
    } else {
        p
    }
}

fn nearest_mass(u: f64, v: f64) -> usize {
    let pick = |t: f64| {
        let t = if t.is_finite() { t } else { 0.5 };
        (t.clamp(0.0, 1.0) * (GRID - 1) as f64).round() as usize
    };
    pick(v) * GRID + pick(u)
}

fn bernstein(t: f64) -> [f64; 4] {
    let s = 1.0 - t;
    [s * s * s, 3.0 * s * s * t, 3.0 * s * t * t, t * t * t]
}

/// How far outside the frame the patch is still evaluated. Chrome and popups
/// further out move with the nearest edge: a cubic extrapolated far past its
/// control points runs away.
const PATCH_OVERHANG: f64 = 0.12;

/// The displacement of the point at `(u, v)` of the window frame: the grid
/// read as the control points of a bicubic Bézier patch, which is what makes
/// the deformed window a smooth surface instead of nine flat facets.
pub fn patch_offset(offsets: &Offsets, u: f64, v: f64) -> [f64; 2] {
    let bu = bernstein(u.clamp(-PATCH_OVERHANG, 1.0 + PATCH_OVERHANG));
    let bv = bernstein(v.clamp(-PATCH_OVERHANG, 1.0 + PATCH_OVERHANG));
    let mut out = [0.0; 2];
    for (row, wv) in bv.iter().enumerate() {
        for (col, wu) in bu.iter().enumerate() {
            let w = wu * wv;
            out[0] += w * offsets[row * GRID + col][0];
            out[1] += w * offsets[row * GRID + col][1];
        }
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    fn run(m: &mut WobblyModel, dt: f64, seconds: f64) -> bool {
        let mut disturbed = true;
        for _ in 0..(seconds / dt).ceil() as usize {
            disturbed = m.step(dt, WobblyParams::default());
        }
        disturbed
    }

    #[test]
    fn a_fresh_grid_is_exactly_identity_and_costs_nothing() {
        let mut m = WobblyModel::new(0.5, 0.0, 800.0);
        assert!(m.at_rest());
        assert!(!m.step(1.0 / 60.0, WobblyParams::default()));
        assert_eq!(m.offsets(), [[0.0; 2]; COUNT]);
        assert_eq!(patch_offset(&m.offsets(), 0.3, 0.7), [0.0, 0.0]);
    }

    #[test]
    fn a_move_leaves_every_mass_but_the_anchor_behind() {
        let mut m = WobblyModel::new(0.0, 0.0, 800.0);
        m.moved(30.0, -10.0);
        let o = m.offsets();
        assert_eq!(o[0], [0.0, 0.0]);
        for p in &o[1..] {
            assert_eq!(*p, [-30.0, 10.0]);
        }
    }

    #[test]
    fn it_settles_to_exact_identity_at_any_frame_rate() {
        for dt in [1.0 / 60.0, 1.0 / 144.0, 1.0 / 240.0] {
            let mut m = WobblyModel::new(0.5, 0.0, 800.0);
            m.moved(120.0, 60.0);
            m.release();
            assert!(m.step(dt, WobblyParams::default()));
            assert!(!run(&mut m, dt, 6.0), "still moving at dt {dt}");
            assert!(m.at_rest());
            assert_eq!(m.offsets(), [[0.0; 2]; COUNT]);
        }
    }

    #[test]
    fn nothing_ever_swings_further_out_than_it_started() {
        for dt in [1.0 / 60.0, 1.0 / 240.0, 0.5] {
            let mut m = WobblyModel::new(1.0, 1.0, 800.0);
            m.moved(-200.0, 150.0);
            let reach = |m: &WobblyModel| {
                m.offsets()
                    .iter()
                    .map(|p| p[0].hypot(p[1]))
                    .fold(0.0, f64::max)
            };
            let start = reach(&m);
            for _ in 0..600 {
                m.step(dt, WobblyParams::default());
                assert!(
                    reach(&m) <= start * 1.05,
                    "dt {dt}: {} > {start}",
                    reach(&m)
                );
            }
        }
    }

    #[test]
    fn the_extremes_of_the_config_range_stay_stable() {
        for (stiffness, friction) in [(20.0, 0.5), (1.0, 20.0), (20.0, 20.0), (1.0, 0.5)] {
            let mut m = WobblyModel::new(0.5, 0.5, 800.0);
            m.moved(300.0, 0.0);
            let params = WobblyParams {
                stiffness,
                friction,
            };
            for _ in 0..(60 * 120) {
                m.step(1.0 / 60.0, params);
            }
            assert!(m.at_rest(), "k {stiffness} f {friction} never settled");
        }
    }

    #[test]
    fn the_same_wall_time_gives_the_same_shape_at_60_and_240_hz() {
        let shape = |dt: f64| {
            let mut m = WobblyModel::new(0.5, 0.0, 800.0);
            m.moved(100.0, 40.0);
            run(&mut m, dt, 0.25);
            m.offsets()
        };
        let (slow, fast) = (shape(1.0 / 60.0), shape(1.0 / 240.0));
        for (a, b) in slow.iter().zip(fast.iter()) {
            assert!((a[0] - b[0]).abs() < 0.5 && (a[1] - b[1]).abs() < 0.5);
        }
    }

    #[test]
    fn a_long_stall_is_bounded_work_and_does_not_explode() {
        let mut m = WobblyModel::new(0.5, 0.0, 800.0);
        m.moved(100.0, 0.0);
        m.step(3600.0, WobblyParams::default());
        assert!(m.offsets().iter().all(|p| p[0].abs() <= 100.0 + 1e-6));
    }

    #[test]
    fn a_flick_is_clamped_instead_of_smearing_across_the_screen() {
        let mut m = WobblyModel::new(0.5, 0.0, 800.0);
        m.moved(50_000.0, 0.0);
        assert!(m.offsets().iter().all(|p| p[0].abs() <= 400.0 + 1e-6));
        m.moved(f64::NAN, 0.0);
        assert!(m.offsets().iter().all(|p| p[0].is_finite()));
    }

    #[test]
    fn the_far_side_lags_and_then_overshoots() {
        let mut m = WobblyModel::new(0.0, 0.0, 800.0);
        m.moved(80.0, 0.0);
        m.release();
        let far = COUNT - 1;
        assert!(m.offsets()[far][0] < 0.0);
        let mut overshoot: f64 = 0.0;
        for _ in 0..(240 * 4) {
            m.step(1.0 / 240.0, WobblyParams::default());
            overshoot = overshoot.max(m.offsets()[far][0]);
        }
        assert!(overshoot > 0.5, "no overshoot: {overshoot}");
        assert!(overshoot < 80.0);
    }

    #[test]
    fn regrabbing_elsewhere_eases_the_new_anchor_home() {
        let mut m = WobblyModel::new(0.0, 0.0, 800.0);
        m.moved(60.0, 0.0);
        m.grab(1.0, 1.0, 800.0);
        let before = m.offsets()[COUNT - 1][0].abs();
        run(&mut m, 1.0 / 120.0, 0.2);
        assert!(m.offsets()[COUNT - 1][0].abs() < before * 0.05);
    }

    #[test]
    fn the_grabbed_spot_never_leaves_the_hand() {
        // Held between masses, where the anchor alone would let it stray.
        let mut m = WobblyModel::new(0.5, 0.04, 800.0);
        for i in 0..200 {
            m.moved(7.0, if i % 2 == 0 { 3.0 } else { -2.0 });
            m.step(1.0 / 144.0, WobblyParams::default());
            let held = patch_offset(&m.offsets(), 0.5, 0.04);
            assert!(held[0].abs() < 1e-9 && held[1].abs() < 1e-9, "{held:?}");
        }
    }

    #[test]
    fn regrabbing_elsewhere_does_not_make_the_picture_jump() {
        let mut m = WobblyModel::new(0.1, 0.1, 800.0);
        m.moved(90.0, -40.0);
        run(&mut m, 1.0 / 240.0, 0.05);
        let before = m.offsets();
        m.grab(0.9, 0.7, 800.0);
        for (a, b) in before.iter().zip(m.offsets().iter()) {
            assert!((a[0] - b[0]).abs() < 1e-9 && (a[1] - b[1]).abs() < 1e-9);
        }
        m.release();
        assert!(!run(&mut m, 1.0 / 240.0, 6.0));
        assert_eq!(m.offsets(), [[0.0; 2]; COUNT]);
    }

    #[test]
    fn the_patch_passes_through_its_corners_and_is_linear_for_a_linear_grid() {
        let mut grid = [[0.0; 2]; COUNT];
        for row in 0..GRID {
            for col in 0..GRID {
                grid[row * GRID + col] = [col as f64 * 3.0, row as f64 * 6.0];
            }
        }
        assert_eq!(patch_offset(&grid, 0.0, 0.0), [0.0, 0.0]);
        let far = patch_offset(&grid, 1.0, 1.0);
        assert!((far[0] - 9.0).abs() < 1e-9 && (far[1] - 18.0).abs() < 1e-9);
        let mid = patch_offset(&grid, 0.5, 0.25);
        assert!((mid[0] - 4.5).abs() < 1e-9 && (mid[1] - 4.5).abs() < 1e-9);
        // Chrome far outside the frame follows the edge, not a runaway cubic.
        assert_eq!(
            patch_offset(&grid, 9.0, 0.5),
            patch_offset(&grid, 1.12, 0.5)
        );
    }
}
