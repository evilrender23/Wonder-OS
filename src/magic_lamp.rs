//! The genie: a window's picture poured down a funnel into a point.
//!
//! Pure geometry. A [`Lamp`] maps a point of the picture, as a fraction
//! `(u, v)` of its rectangle, to where it is drawn at `progress` 0 (the
//! undeformed rectangle) through 1 (collapsed onto the target). The funnel is
//! fixed in space: it runs from the picture's far edge to the target, its two
//! sides S-curves that start at the picture's sides and end a neck's width
//! apart. First the near edge of the picture stretches down the funnel to the
//! target while the sides bend into it; then the whole picture slides on down
//! the funnel and drains away through the neck. Everything is written along one axis, the one
//! pointing at the target, so the same code serves a dock on any side.

/// Width of the funnel's narrow end in logical pixels: about a dock icon.
pub const NECK_WIDTH: f64 = 44.0;
/// The near edge has reached the target, and the funnel its full shape, at
/// this progress...
const STRETCHED: f64 = 0.42;
/// ...and the far edge sets off at this one, so the two overlap.
const DRAIN_STARTS: f64 = 0.22;
/// The picture fades over the last stretch, where it is a sliver anyway.
const FADE_STARTS: f64 = 0.88;

#[derive(Clone, Copy, Debug, PartialEq)]
enum Axis {
    Vertical,
    Horizontal,
}

#[derive(Clone, Copy, Debug, PartialEq)]
pub struct Lamp {
    /// `(x, y, w, h)` of the picture at rest.
    rect: [f64; 4],
    target: [f64; 2],
    /// Width of the narrow end, in the rectangle's units.
    neck: f64,
    axis: Axis,
}

fn smoothstep(t: f64) -> f64 {
    let t = t.clamp(0.0, 1.0);
    t * t * (3.0 - 2.0 * t)
}

fn smootherstep(t: f64) -> f64 {
    let t = t.clamp(0.0, 1.0);
    t * t * t * (t * (t * 6.0 - 15.0) + 10.0)
}

impl Lamp {
    /// The funnel runs along whichever axis the target lies further out on;
    /// a target inside the rectangle (a window covering the dock) pours
    /// vertically, which is where a dock is. `neck` is the width of the
    /// narrow end in the rectangle's units ([`NECK_WIDTH`] × the view scale).
    pub fn new(rect: [f64; 4], target: [f64; 2], neck: f64) -> Self {
        let outside = |lo: f64, len: f64, t: f64| (lo - t).max(t - (lo + len)).max(0.0);
        let dx = outside(rect[0], rect[2], target[0]);
        let dy = outside(rect[1], rect[3], target[1]);
        let axis = if dx > dy {
            Axis::Horizontal
        } else {
            Axis::Vertical
        };
        Self {
            rect,
            target,
            neck: neck.max(1.0),
            axis,
        }
    }

    /// Everything the animation ever covers: the rectangle and the neck.
    pub fn bounds(&self) -> [f64; 4] {
        let half = self.neck / 2.0;
        let x0 = self.rect[0].min(self.target[0] - half);
        let y0 = self.rect[1].min(self.target[1] - half);
        let x1 = (self.rect[0] + self.rect[2]).max(self.target[0] + half);
        let y1 = (self.rect[1] + self.rect[3]).max(self.target[1] + half);
        [x0, y0, x1 - x0, y1 - y0]
    }

    /// How many times narrower than the picture the neck is: the hardest the
    /// picture is ever squeezed.
    pub fn max_squeeze(&self) -> f64 {
        let across = match self.axis {
            Axis::Vertical => self.rect[2],
            Axis::Horizontal => self.rect[3],
        };
        (across / self.neck).max(1.0)
    }

    /// Opacity at `progress`: opaque until the very end.
    pub fn alpha(progress: f64) -> f32 {
        (1.0 - smoothstep((progress - FADE_STARTS) / (1.0 - FADE_STARTS))) as f32
    }

    /// Where the point at `(u, v)` of the picture is drawn at `progress`.
    pub fn warp(&self, u: f64, v: f64, progress: f64) -> [f64; 2] {
        // `along` runs toward the target, `across` is the squeezed direction.
        let (along0, along_len, across0, across_len, t_along, t_across, a, c) = match self.axis {
            Axis::Vertical => (
                self.rect[1],
                self.rect[3],
                self.rect[0],
                self.rect[2],
                self.target[1],
                self.target[0],
                v,
                u,
            ),
            Axis::Horizontal => (
                self.rect[0],
                self.rect[2],
                self.rect[1],
                self.rect[3],
                self.target[0],
                self.target[1],
                u,
                v,
            ),
        };
        let p = progress.clamp(0.0, 1.0);
        let stretch = smoothstep(p / STRETCHED);
        let drain = {
            let t = ((p - DRAIN_STARTS) / (1.0 - DRAIN_STARTS)).clamp(0.0, 1.0);
            // Accelerating: the picture is sucked in, it does not drift in.
            t * t * (0.55 + 0.45 * t)
        };

        // The funnel's mouth is the edge further from the target; rows are
        // counted from there.
        let (edge_lo, edge_hi) = (along0, along0 + along_len);
        let (far, near, row) = if (t_along - edge_lo).abs() >= (t_along - edge_hi).abs() {
            (edge_lo, edge_hi, a)
        } else {
            (edge_hi, edge_lo, 1.0 - a)
        };
        // Stretched, the picture spans the funnel from mouth to neck. Draining
        // slides it on at that length, and what passes the neck is gone: those
        // rows pile up on the target with no height.
        let span = t_along - far;
        let stretched = far + row * (near + (t_along - near) * stretch - far);
        let slid = stretched + span * drain;
        let along = if span >= 0.0 {
            slid.min(t_along)
        } else {
            slid.max(t_along)
        };

        // How far down the funnel this row now is: 0 at its mouth, 1 at the
        // neck. The S-curve is what gives the sides their genie shape.
        let depth = if span.abs() > 1e-6 {
            smootherstep((along - far) / span)
        } else {
            1.0
        };
        let squeeze = stretch * depth;
        let half_neck = self.neck.min(across_len) / 2.0;
        let lo = across0 + (t_across - half_neck - across0) * squeeze;
        let hi = across0 + across_len + (t_across + half_neck - across0 - across_len) * squeeze;
        let across = lo + c * (hi - lo);

        match self.axis {
            Axis::Vertical => [across, along],
            Axis::Horizontal => [along, across],
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const RECT: [f64; 4] = [100.0, 50.0, 400.0, 300.0];

    fn below() -> Lamp {
        Lamp::new(RECT, [300.0, 700.0], NECK_WIDTH)
    }

    #[test]
    fn progress_zero_is_the_untouched_rectangle() {
        for lamp in [below(), Lamp::new(RECT, [-200.0, 100.0], NECK_WIDTH)] {
            for (u, v) in [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0), (1.0, 1.0), (0.3, 0.8)] {
                let p = lamp.warp(u, v, 0.0);
                assert!((p[0] - (RECT[0] + u * RECT[2])).abs() < 1e-9);
                assert!((p[1] - (RECT[1] + v * RECT[3])).abs() < 1e-9);
            }
        }
        assert_eq!(Lamp::alpha(0.0), 1.0);
    }

    #[test]
    fn progress_one_is_inside_the_neck_and_invisible() {
        for target in [
            [300.0, 700.0],
            [300.0, -400.0],
            [-300.0, 200.0],
            [900.0, 200.0],
        ] {
            let lamp = Lamp::new(RECT, target, NECK_WIDTH);
            for (u, v) in [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0), (1.0, 1.0), (0.5, 0.5)] {
                let p = lamp.warp(u, v, 1.0);
                assert!((p[0] - target[0]).abs() <= NECK_WIDTH / 2.0 + 1e-6, "{p:?}");
                assert!((p[1] - target[1]).abs() <= NECK_WIDTH / 2.0 + 1e-6, "{p:?}");
            }
        }
        assert_eq!(Lamp::alpha(1.0), 0.0);
        assert_eq!(Lamp::alpha(0.5), 1.0);
    }

    #[test]
    fn the_picture_narrows_toward_the_target_and_only_moves_toward_it() {
        let lamp = below();
        let width_at = |v: f64, p: f64| lamp.warp(1.0, v, p)[0] - lamp.warp(0.0, v, p)[0];
        // Mid-flight the end nearer the dock is narrower than the far end.
        assert!(width_at(1.0, 0.4) < width_at(0.0, 0.4) - 50.0);
        let mut last_top = f64::MIN;
        let mut last_bottom = f64::MIN;
        for step in 0..=100 {
            let p = step as f64 / 100.0;
            let top = lamp.warp(0.5, 0.0, p)[1];
            let bottom = lamp.warp(0.5, 1.0, p)[1];
            assert!(top >= last_top - 1e-9 && bottom >= last_bottom - 1e-9);
            assert!(top <= bottom + 1e-9, "rows crossed at {p}");
            (last_top, last_bottom) = (top, bottom);
        }
    }

    #[test]
    fn the_mapping_is_continuous_in_time_and_never_mirrors() {
        let lamp = below();
        let mut prev = lamp.warp(0.2, 0.3, 0.0);
        for step in 1..=1000 {
            let p = step as f64 / 1000.0;
            let here = lamp.warp(0.2, 0.3, p);
            assert!(
                (here[0] - prev[0]).hypot(here[1] - prev[1]) < 3.0,
                "jump at {p}"
            );
            assert!(lamp.warp(0.0, 0.3, p)[0] <= lamp.warp(1.0, 0.3, p)[0]);
            prev = here;
        }
    }

    #[test]
    fn a_target_to_the_side_pours_sideways() {
        let lamp = Lamp::new(RECT, [1200.0, 200.0], NECK_WIDTH);
        let height_at = |u: f64| lamp.warp(u, 1.0, 0.4)[1] - lamp.warp(u, 0.0, 0.4)[1];
        assert!(height_at(1.0) < height_at(0.0) - 50.0);
        assert!(lamp.warp(0.0, 0.5, 0.7)[0] > RECT[0]);
    }

    #[test]
    fn a_target_inside_the_window_still_collapses_onto_it() {
        let lamp = Lamp::new(RECT, [300.0, 300.0], NECK_WIDTH);
        let top = lamp.warp(0.5, 0.0, 1.0);
        let bottom = lamp.warp(0.5, 1.0, 1.0);
        assert!((top[1] - 300.0).abs() < 1e-6 && (bottom[1] - 300.0).abs() < 1e-6);
        assert!(lamp.warp(0.5, 0.0, 0.5)[1] <= lamp.warp(0.5, 1.0, 0.5)[1]);
    }

    #[test]
    fn bounds_cover_the_whole_flight() {
        for target in [[300.0, 700.0], [-300.0, 200.0], [300.0, 300.0]] {
            let lamp = Lamp::new(RECT, target, NECK_WIDTH);
            let b = lamp.bounds();
            for step in 0..=20 {
                for (u, v) in [(0.0, 0.0), (1.0, 0.0), (0.0, 1.0), (1.0, 1.0)] {
                    let p = lamp.warp(u, v, step as f64 / 20.0);
                    assert!(p[0] >= b[0] - 1e-6 && p[0] <= b[0] + b[2] + 1e-6);
                    assert!(p[1] >= b[1] - 1e-6 && p[1] <= b[1] + b[3] + 1e-6);
                }
            }
        }
    }

    #[test]
    fn a_window_narrower_than_the_neck_does_not_widen() {
        let lamp = Lamp::new([0.0, 0.0, 20.0, 20.0], [10.0, 500.0], NECK_WIDTH);
        let w = lamp.warp(1.0, 1.0, 0.6)[0] - lamp.warp(0.0, 1.0, 0.6)[0];
        assert!(w <= 20.0 + 1e-9);
    }
}
