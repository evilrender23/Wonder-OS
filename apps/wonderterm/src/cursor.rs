use eframe::egui::{
    Color32, Mesh, Painter, Pos2, Rect, Shape, Stroke, StrokeKind, Vec2, epaint::RectShape, pos2,
    vec2,
};

use crate::config::{Animation, Cursor as CursorConfig, Vfx};
use crate::theme::alpha;

/// A critically damped spring reaches within 1% of its target at `omega * t ≈ 6.6`.
const SETTLE: f32 = 6.6;
const RIPPLE_SECS: f32 = 0.45;
const SPARK_SECS: f32 = 0.6;
/// A cursor nobody touches stops blinking, so an idle terminal stops repainting.
const BLINK_IDLE_SECS: f64 = 20.0;

#[derive(Clone, Copy, Default)]
struct Corner {
    pos: Pos2,
    vel: Vec2,
    secs: f32,
}

struct Ripple {
    rect: Rect,
    age: f32,
}

struct Spark {
    pos: Pos2,
    vel: Vec2,
    age: f32,
    size: f32,
}

/// The cursor as four independently sprung corners (top-left, top-right,
/// bottom-right, bottom-left). Giving the corners that face the direction of
/// travel a shorter spring than the ones behind makes the shape stretch along
/// its path and snap back on arrival; a change of shape (block to beam) morphs
/// through the same springs.
#[derive(Default)]
pub struct CursorAnim {
    corners: [Corner; 4],
    target: Option<Rect>,
    ripples: Vec<Ripple>,
    sparks: Vec<Spark>,
    blink_epoch: f64,
    rng: u32,
}

fn rect_corners(r: Rect) -> [Pos2; 4] {
    [
        r.left_top(),
        r.right_top(),
        r.right_bottom(),
        r.left_bottom(),
    ]
}

impl CursorAnim {
    /// Restart the blink cycle at fully visible: typing must not hide the cursor.
    pub fn touch(&mut self, now: f64) {
        self.blink_epoch = now;
    }

    pub fn set_target(&mut self, rect: Rect, cell: Vec2, cfg: &CursorConfig, now: f64) {
        let Some(old) = self.target else {
            self.snap(rect);
            return;
        };
        if old == rect {
            return;
        }
        self.target = Some(rect);
        self.touch(now);
        if cfg.animation == Animation::None {
            self.snap(rect);
            return;
        }

        let from = self.center();
        let travel = rect.center() - from;
        let typing = travel.y.abs() < cell.y * 0.5 && travel.x.abs() <= cell.x * 2.5;
        let long = (cfg.duration_ms / 1000.0).max(0.001);
        let short = (cfg.typing_duration_ms / 1000.0).max(0.001);

        let mut secs = [if typing { short } else { long }; 4];
        if !typing && cfg.animation == Animation::Smear && travel.length() > 0.5 {
            let dir = travel.normalized();
            let half = rect.size() * 0.5;
            let lead = rect_corners(rect).map(|p| {
                let o = p - rect.center();
                vec2(o.x / half.x.max(0.5), o.y / half.y.max(0.5)).dot(dir)
            });
            let mut order = [0, 1, 2, 3];
            order.sort_by(|&a, &b| lead[b].total_cmp(&lead[a]));
            let trail = cfg.trail.clamp(0.0, 0.95);
            for (rank, &corner) in order.iter().enumerate() {
                secs[corner] = long * (1.0 - trail * (3 - rank) as f32 / 3.0);
            }
        }
        for (corner, secs) in self.corners.iter_mut().zip(secs) {
            corner.secs = secs;
        }

        if !typing {
            match cfg.vfx {
                Vfx::None => {}
                Vfx::Ripple => self.ripples.push(Ripple { rect, age: 0.0 }),
                Vfx::Sparkle => self.emit_sparks(from, rect.center(), cell),
            }
        }
    }

    fn snap(&mut self, rect: Rect) {
        self.target = Some(rect);
        for (corner, pos) in self.corners.iter_mut().zip(rect_corners(rect)) {
            *corner = Corner {
                pos,
                vel: Vec2::ZERO,
                secs: 0.0,
            };
        }
    }

    fn center(&self) -> Pos2 {
        let sum = self
            .corners
            .iter()
            .fold(Vec2::ZERO, |acc, c| acc + c.pos.to_vec2());
        (sum / 4.0).to_pos2()
    }

    fn random(&mut self) -> f32 {
        // xorshift32; visual noise only.
        let mut x = self.rng | 1;
        x ^= x << 13;
        x ^= x >> 17;
        x ^= x << 5;
        self.rng = x;
        (x >> 8) as f32 / (1u32 << 24) as f32
    }

    fn emit_sparks(&mut self, from: Pos2, to: Pos2, cell: Vec2) {
        let count = ((to - from).length() / cell.x).clamp(6.0, 28.0) as usize;
        for i in 0..count {
            let t = (i as f32 + self.random()) / count as f32;
            let jitter = vec2(self.random() - 0.5, self.random() - 0.5);
            let spark = Spark {
                pos: from + (to - from) * t + jitter * cell.y * 0.6,
                vel: vec2(jitter.x * 90.0, -20.0 - self.random() * 50.0),
                age: self.random() * 0.15,
                size: 1.5 + self.random() * 2.0,
            };
            self.sparks.push(spark);
        }
    }

    /// Advance by `dt` seconds. Returns whether anything is still moving.
    pub fn step(&mut self, dt: f32) -> bool {
        let Some(target) = self.target else {
            return false;
        };
        let mut moving = false;
        for (corner, dest) in self.corners.iter_mut().zip(rect_corners(target)) {
            let offset = corner.pos - dest;
            if corner.secs <= 0.0 || (offset.length() < 0.2 && corner.vel.length() < 4.0) {
                corner.pos = dest;
                corner.vel = Vec2::ZERO;
                continue;
            }
            let omega = SETTLE / corner.secs;
            let decay = (-omega * dt).exp();
            let b = corner.vel + offset * omega;
            corner.pos = dest + (offset + b * dt) * decay;
            corner.vel = (corner.vel - b * (omega * dt)) * decay;
            moving = true;
        }

        for r in &mut self.ripples {
            r.age += dt;
        }
        self.ripples.retain(|r| r.age < RIPPLE_SECS);
        for s in &mut self.sparks {
            s.age += dt;
            s.pos += s.vel * dt;
            s.vel.y += 160.0 * dt;
        }
        self.sparks.retain(|s| s.age < SPARK_SECS);

        moving || !self.ripples.is_empty() || !self.sparks.is_empty()
    }

    pub fn quad(&self) -> [Pos2; 4] {
        self.corners.map(|c| c.pos)
    }

    pub fn bounds(&self) -> Rect {
        Rect::from_points(&self.quad())
    }

    fn at_rest(&self) -> bool {
        self.target.is_some_and(|t| self.quad() == rect_corners(t))
    }

    /// Opacity of the blinking cursor at `now`, and how long it holds that
    /// value (`None` = it is mid-fade and wants the next frame; `Some(inf)` =
    /// it has stopped blinking).
    pub fn blink(&self, now: f64, cfg: &CursorConfig, blinking: bool) -> (f32, Option<f64>) {
        let since = now - self.blink_epoch;
        if !blinking || since > BLINK_IDLE_SECS {
            return (1.0, Some(f64::INFINITY));
        }
        let half = (cfg.blink_interval_ms as f64 / 1000.0).max(0.1);
        let fade = half * 0.3;
        let t = since.rem_euclid(half * 2.0);
        let smooth = |x: f64| (x * x * (3.0 - 2.0 * x)) as f32;
        if t < half - fade {
            (1.0, Some(half - fade - t))
        } else if t < half {
            (1.0 - smooth((t - (half - fade)) / fade), None)
        } else if t < half * 2.0 - fade {
            (0.0, Some(half * 2.0 - fade - t))
        } else {
            (smooth((t - (half * 2.0 - fade)) / fade), None)
        }
    }

    pub fn paint_glow(&self, painter: &Painter, color: Color32, opacity: f32) {
        let spread = 7.0;
        painter.add(
            RectShape::filled(
                self.bounds().expand(spread * 0.5),
                spread,
                alpha(color, 0.30 * opacity),
            )
            .with_blur_width(spread * 2.0),
        );
    }

    pub fn paint_vfx(&self, painter: &Painter, color: Color32) {
        for r in &self.ripples {
            let t = r.age / RIPPLE_SECS;
            let eased = 1.0 - (1.0 - t).powi(3);
            painter.rect_stroke(
                r.rect.expand(1.0 + eased * r.rect.height() * 0.9),
                3.0 + eased * 6.0,
                Stroke::new(1.5, alpha(color, 0.55 * (1.0 - t))),
                StrokeKind::Middle,
            );
        }
        for s in &self.sparks {
            let t = (s.age / SPARK_SECS).clamp(0.0, 1.0);
            let size = s.size * (1.0 - t * 0.6);
            painter.rect_filled(
                Rect::from_center_size(s.pos, Vec2::splat(size)),
                size * 0.5,
                alpha(color, 0.9 * (1.0 - t)),
            );
        }
    }

    /// `hollow` draws the outline only: the window is not focused.
    pub fn paint(&self, painter: &Painter, color: Color32, opacity: f32, hollow: bool) {
        let fill = alpha(color, opacity);
        let quad = self.quad();
        if hollow {
            let mut outline = quad.to_vec();
            outline.push(quad[0]);
            painter.add(Shape::line(outline, Stroke::new(1.0, fill)));
        } else if self.at_rest() {
            painter.rect_filled(self.bounds(), 1.5, fill);
        } else {
            // Mid-flight the corners can cross. While they have not, a convex
            // polygon gets antialiased edges; a crossed quad falls back to
            // two plain triangles.
            if is_convex(&quad) {
                painter.add(Shape::convex_polygon(quad.to_vec(), fill, Stroke::NONE));
            } else {
                let mut mesh = Mesh::default();
                for p in quad {
                    mesh.colored_vertex(p, fill);
                }
                mesh.add_triangle(0, 1, 2);
                mesh.add_triangle(0, 2, 3);
                painter.add(Shape::mesh(mesh));
            }
        }
    }
}

fn is_convex(quad: &[Pos2; 4]) -> bool {
    let turn = |i: usize| {
        let (a, b, c) = (quad[i], quad[(i + 1) % 4], quad[(i + 2) % 4]);
        (b - a).x * (c - b).y - (b - a).y * (c - b).x
    };
    (0..4).all(|i| turn(i) > 0.0) || (0..4).all(|i| turn(i) < 0.0)
}

/// Where the cursor sits inside its cell for each shape. `width` is one cell
/// or two for a wide character.
pub fn shape_rect(shape: crate::config::Shape, cell: Rect, pixel: f32) -> Rect {
    use crate::config::Shape::*;
    let thickness = (cell.height() / 9.0).max(2.0 * pixel).round_to(pixel);
    match shape {
        Block => cell,
        Beam => Rect::from_min_max(cell.min, pos2(cell.left() + thickness, cell.bottom())),
        Underline => Rect::from_min_max(pos2(cell.left(), cell.bottom() - thickness), cell.max),
    }
}

trait RoundTo {
    fn round_to(self, step: f32) -> f32;
}

impl RoundTo for f32 {
    fn round_to(self, step: f32) -> f32 {
        (self / step).round() * step
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::config::Shape as CursorShape;

    const CELL: Vec2 = vec2(8.0, 16.0);

    fn cell_at(col: f32, row: f32) -> Rect {
        Rect::from_min_size(pos2(col * CELL.x, row * CELL.y), CELL)
    }

    fn settle(anim: &mut CursorAnim) -> usize {
        let mut frames = 0;
        while anim.step(1.0 / 60.0) {
            frames += 1;
            assert!(frames < 600, "the cursor never came to rest");
        }
        frames
    }

    #[test]
    fn first_target_is_placed_without_animating() {
        let mut anim = CursorAnim::default();
        anim.set_target(cell_at(3.0, 2.0), CELL, &CursorConfig::default(), 0.0);
        assert!(!anim.step(0.016));
        assert_eq!(anim.bounds(), cell_at(3.0, 2.0));
    }

    #[test]
    fn a_jump_lands_exactly_on_the_target() {
        let cfg = CursorConfig::default();
        let mut anim = CursorAnim::default();
        anim.set_target(cell_at(0.0, 0.0), CELL, &cfg, 0.0);
        anim.set_target(cell_at(60.0, 20.0), CELL, &cfg, 1.0);
        let frames = settle(&mut anim);
        assert!(frames > 3, "a long jump should take several frames");
        assert_eq!(anim.bounds(), cell_at(60.0, 20.0));
    }

    #[test]
    fn smear_stretches_the_cursor_along_its_path() {
        let cfg = CursorConfig {
            vfx: Vfx::None,
            ..Default::default()
        };
        let mut anim = CursorAnim::default();
        anim.set_target(cell_at(0.0, 0.0), CELL, &cfg, 0.0);
        anim.set_target(cell_at(40.0, 0.0), CELL, &cfg, 1.0);
        let mut widest: f32 = 0.0;
        while anim.step(1.0 / 120.0) {
            widest = widest.max(anim.bounds().width());
            // Critical damping: the leading edge never overshoots the target.
            assert!(anim.bounds().right() <= cell_at(40.0, 0.0).right() + 0.01);
        }
        assert!(widest > CELL.x * 4.0, "widest was {widest}");

        let glide = CursorConfig {
            animation: Animation::Glide,
            ..cfg
        };
        let mut anim = CursorAnim::default();
        anim.set_target(cell_at(0.0, 0.0), CELL, &glide, 0.0);
        anim.set_target(cell_at(40.0, 0.0), CELL, &glide, 1.0);
        while anim.step(1.0 / 120.0) {
            assert!((anim.bounds().width() - CELL.x).abs() < 0.01);
        }
    }

    #[test]
    fn typing_steps_are_short_and_play_no_effect() {
        let cfg = CursorConfig::default();
        let mut anim = CursorAnim::default();
        anim.set_target(cell_at(0.0, 0.0), CELL, &cfg, 0.0);
        anim.set_target(cell_at(1.0, 0.0), CELL, &cfg, 1.0);
        assert!(anim.ripples.is_empty());
        let typing = settle(&mut anim);
        anim.set_target(cell_at(1.0, 12.0), CELL, &cfg, 2.0);
        assert_eq!(anim.ripples.len(), 1);
        assert!(typing < settle(&mut anim));
    }

    #[test]
    fn animation_none_teleports() {
        let cfg = CursorConfig {
            animation: Animation::None,
            vfx: Vfx::None,
            ..Default::default()
        };
        let mut anim = CursorAnim::default();
        anim.set_target(cell_at(0.0, 0.0), CELL, &cfg, 0.0);
        anim.set_target(cell_at(9.0, 9.0), CELL, &cfg, 1.0);
        assert!(!anim.step(0.016));
        assert_eq!(anim.bounds(), cell_at(9.0, 9.0));
    }

    #[test]
    fn blink_fades_and_stops_when_idle() {
        let cfg = CursorConfig::default();
        let mut anim = CursorAnim::default();
        anim.touch(10.0);
        assert_eq!(anim.blink(10.1, &cfg, true).0, 1.0);
        assert_eq!(anim.blink(10.9, &cfg, true).0, 0.0);
        let (mid, hold) = anim.blink(10.5, &cfg, true);
        assert!(mid > 0.0 && mid < 1.0 && hold.is_none());
        assert_eq!(anim.blink(10.9, &cfg, false).0, 1.0);
        assert_eq!(anim.blink(10.9 + BLINK_IDLE_SECS, &cfg, true).0, 1.0);
    }

    #[test]
    fn crossed_quads_are_not_treated_as_convex() {
        let r = rect_corners(cell_at(0.0, 0.0));
        assert!(is_convex(&r));
        assert!(!is_convex(&[r[0], r[2], r[1], r[3]]));
    }

    #[test]
    fn shapes_stay_inside_the_cell() {
        let cell = cell_at(2.0, 2.0);
        for shape in [
            CursorShape::Block,
            CursorShape::Beam,
            CursorShape::Underline,
        ] {
            assert!(cell.contains_rect(shape_rect(shape, cell, 1.0)));
        }
        assert_eq!(shape_rect(CursorShape::Beam, cell, 1.0).width(), 2.0);
    }
}
