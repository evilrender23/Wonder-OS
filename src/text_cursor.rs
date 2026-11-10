//! The text-caret effect, as pure motion: no protocol, no renderer.
//!
//! This is wonderterm's cursor (`apps/wonderterm/src/cursor.rs`) lifted out of
//! the terminal so the compositor can play it over any client's text field.
//! The caret is four corners on four critically damped springs; the corners
//! facing the direction of travel get a shorter spring than the ones behind,
//! so the shape stretches along its path and snaps back on arrival.
//!
//! One thing differs from the terminal, on purpose: the client still draws its
//! own caret, in its own design, and nothing here replaces it. The effect is an
//! overlay that exists only while the caret travels — it fades out once the
//! springs settle, leaving the client's caret alone on screen.
//!
//! Coordinates are surface-local logical pixels, so moving the window or the
//! camera never reads as caret motion.

use crate::config::{TextCursorAnimation, TextCursorConfig, TextCursorVfx};

/// A critically damped spring reaches within 1% of its target at `omega * t ≈ 6.6`.
const SETTLE: f32 = 6.6;
const RIPPLE_SECS: f32 = 0.45;
const SPARK_SECS: f32 = 0.6;
/// How long the overlay takes to fade once the caret has landed.
const FADE_SECS: f32 = 0.14;
/// Upper bound on everything [`TextCursorAnim::set_target`] can start, besides
/// the springs themselves: what keeps the render loop awake after a move.
pub const EFFECT_TAIL_SECS: f32 = 0.8;

pub type Vec2 = [f32; 2];

fn add(a: Vec2, b: Vec2) -> Vec2 {
    [a[0] + b[0], a[1] + b[1]]
}

fn sub(a: Vec2, b: Vec2) -> Vec2 {
    [a[0] - b[0], a[1] - b[1]]
}

fn mul(a: Vec2, k: f32) -> Vec2 {
    [a[0] * k, a[1] * k]
}

fn len(a: Vec2) -> f32 {
    a[0].hypot(a[1])
}

/// The caret a client reported, in surface-local logical pixels.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct CaretRect {
    pub x: f32,
    pub y: f32,
    pub w: f32,
    pub h: f32,
}

impl CaretRect {
    /// What the overlay draws for a rectangle a client sent through
    /// `zwp_text_input_v3.set_cursor_rectangle`. Toolkits report a beam as a
    /// zero- or one-pixel-wide rectangle, which is given a visible thickness;
    /// anything implausible for a caret (no height, or taller than any line of
    /// text) yields `None`.
    pub fn from_client(x: i32, y: i32, w: i32, h: i32) -> Option<Self> {
        if h <= 0 || h > 512 || !(0..=512).contains(&w) {
            return None;
        }
        let h = h as f32;
        let beam = (h / 9.0).max(2.0).round();
        Some(Self {
            x: x as f32,
            y: y as f32,
            w: (w as f32).max(beam),
            h,
        })
    }

    fn corners(self) -> [Vec2; 4] {
        [
            [self.x, self.y],
            [self.x + self.w, self.y],
            [self.x + self.w, self.y + self.h],
            [self.x, self.y + self.h],
        ]
    }

    fn center(self) -> Vec2 {
        [self.x + self.w * 0.5, self.y + self.h * 0.5]
    }
}

#[derive(Clone, Copy, Default)]
struct Corner {
    pos: Vec2,
    vel: Vec2,
    secs: f32,
}

struct Ripple {
    rect: CaretRect,
    age: f32,
}

struct Spark {
    pos: Vec2,
    vel: Vec2,
    age: f32,
    size: f32,
}

/// A ripple ring as the renderer needs it.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct RippleShape {
    /// The ring's rectangle (centre line of the stroke): x, y, w, h.
    pub rect: [f32; 4],
    pub radius: f32,
    pub alpha: f32,
}

/// A spark as the renderer needs it.
#[derive(Clone, Copy, Debug, PartialEq)]
pub struct SparkShape {
    pub center: Vec2,
    pub size: f32,
    pub alpha: f32,
}

#[derive(Default)]
pub struct TextCursorAnim {
    corners: [Corner; 4],
    target: Option<CaretRect>,
    ripples: Vec<Ripple>,
    sparks: Vec<Spark>,
    moving: bool,
    /// Seconds since the springs last settled; drives the fade-out.
    rested: f32,
    rng: u32,
}

impl TextCursorAnim {
    /// Forget the caret: the next target is placed without animating. Used when
    /// the text field goes away (focus change, `disable`).
    pub fn reset(&mut self) {
        *self = Self {
            rng: self.rng,
            ..Self::default()
        };
    }

    pub fn target(&self) -> Option<CaretRect> {
        self.target
    }

    /// Point the caret at `rect`. Returns whether that started any motion.
    pub fn set_target(&mut self, rect: CaretRect, cfg: &TextCursorConfig) -> bool {
        let Some(old) = self.target else {
            self.snap(rect);
            return false;
        };
        if old == rect {
            return false;
        }
        self.target = Some(rect);
        if cfg.animation == TextCursorAnimation::None {
            self.snap(rect);
            return false;
        }

        // A caret has no cell grid; its own height stands in for one.
        let line = rect.h.max(old.h);
        let from = self.center();
        let travel = sub(rect.center(), from);
        let typing = travel[1].abs() < line * 0.5 && travel[0].abs() <= line * 1.5;
        let long = (cfg.duration_ms as f32 / 1000.0).max(0.001);
        let short = (cfg.typing_duration_ms as f32 / 1000.0).max(0.001);

        let mut secs = [if typing { short } else { long }; 4];
        let distance = len(travel);
        if !typing && cfg.animation == TextCursorAnimation::Smear && distance > 0.5 {
            let dir = mul(travel, 1.0 / distance);
            let half = [(rect.w * 0.5).max(0.5), (rect.h * 0.5).max(0.5)];
            let lead = rect.corners().map(|p| {
                let o = sub(p, rect.center());
                o[0] / half[0] * dir[0] + o[1] / half[1] * dir[1]
            });
            let mut order = [0, 1, 2, 3];
            order.sort_by(|&a, &b| lead[b].total_cmp(&lead[a]));
            let trail = (cfg.trail as f32).clamp(0.0, 0.95);
            for (rank, &corner) in order.iter().enumerate() {
                secs[corner] = long * (1.0 - trail * (3 - rank) as f32 / 3.0);
            }
        }
        for (corner, secs) in self.corners.iter_mut().zip(secs) {
            corner.secs = secs;
        }
        self.moving = true;
        self.rested = 0.0;

        if !typing {
            match cfg.vfx {
                TextCursorVfx::None => {}
                TextCursorVfx::Ripple => {
                    // One ring at a time: a held arrow key would otherwise
                    // stack them into a blur.
                    self.ripples.clear();
                    self.ripples.push(Ripple { rect, age: 0.0 });
                }
                TextCursorVfx::Sparkle => self.emit_sparks(from, rect.center(), line * 0.5),
            }
        }
        true
    }

    fn snap(&mut self, rect: CaretRect) {
        self.target = Some(rect);
        for (corner, pos) in self.corners.iter_mut().zip(rect.corners()) {
            *corner = Corner {
                pos,
                vel: [0.0; 2],
                secs: 0.0,
            };
        }
        self.moving = false;
        self.rested = FADE_SECS;
    }

    fn center(&self) -> Vec2 {
        let sum = self.corners.iter().fold([0.0; 2], |acc, c| add(acc, c.pos));
        mul(sum, 0.25)
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

    fn emit_sparks(&mut self, from: Vec2, to: Vec2, step: f32) {
        const MAX_SPARKS: usize = 48;
        let count = (len(sub(to, from)) / step.max(1.0)).clamp(6.0, 28.0) as usize;
        for i in 0..count {
            let t = (i as f32 + self.random()) / count as f32;
            let jitter = [self.random() - 0.5, self.random() - 0.5];
            let spark = Spark {
                pos: add(
                    add(from, mul(sub(to, from), t)),
                    mul(jitter, step * 2.0 * 0.6),
                ),
                vel: [jitter[0] * 90.0, -20.0 - self.random() * 50.0],
                age: self.random() * 0.15,
                size: 1.5 + self.random() * 2.0,
            };
            self.sparks.push(spark);
        }
        if self.sparks.len() > MAX_SPARKS {
            let extra = self.sparks.len() - MAX_SPARKS;
            self.sparks.drain(..extra);
        }
    }

    /// Advance by `dt` seconds. Returns whether there is still something to draw.
    pub fn step(&mut self, dt: f32) -> bool {
        let Some(target) = self.target else {
            return false;
        };
        let mut moving = false;
        for (corner, dest) in self.corners.iter_mut().zip(target.corners()) {
            let offset = sub(corner.pos, dest);
            if corner.secs <= 0.0 || (len(offset) < 0.2 && len(corner.vel) < 4.0) {
                corner.pos = dest;
                corner.vel = [0.0; 2];
                continue;
            }
            let omega = SETTLE / corner.secs;
            let decay = (-omega * dt).exp();
            let b = add(corner.vel, mul(offset, omega));
            corner.pos = add(dest, mul(add(offset, mul(b, dt)), decay));
            corner.vel = mul(sub(corner.vel, mul(b, omega * dt)), decay);
            moving = true;
        }
        if moving {
            self.rested = 0.0;
        } else if self.rested < FADE_SECS {
            self.rested = (self.rested + dt).min(FADE_SECS);
        }
        self.moving = moving;

        for r in &mut self.ripples {
            r.age += dt;
        }
        self.ripples.retain(|r| r.age < RIPPLE_SECS);
        for s in &mut self.sparks {
            s.age += dt;
            s.pos = add(s.pos, mul(s.vel, dt));
            s.vel[1] += 160.0 * dt;
        }
        self.sparks.retain(|s| s.age < SPARK_SECS);

        self.visible()
    }

    /// Whether the overlay has anything on screen.
    pub fn visible(&self) -> bool {
        self.presence() > 0.0 || !self.ripples.is_empty() || !self.sparks.is_empty()
    }

    /// Opacity of the caret overlay: 1 in flight, fading to 0 after landing.
    pub fn presence(&self) -> f32 {
        if self.target.is_none() {
            0.0
        } else if self.moving {
            1.0
        } else {
            let t = 1.0 - self.rested / FADE_SECS;
            t * t * (3.0 - 2.0 * t)
        }
    }

    /// The caret's corners: top-left, top-right, bottom-right, bottom-left.
    pub fn quad(&self) -> [Vec2; 4] {
        self.corners.map(|c| c.pos)
    }

    pub fn ripples(&self) -> impl Iterator<Item = RippleShape> + '_ {
        self.ripples.iter().map(|r| {
            let t = r.age / RIPPLE_SECS;
            let eased = 1.0 - (1.0 - t).powi(3);
            let grow = 1.0 + eased * r.rect.h * 0.9;
            RippleShape {
                rect: [
                    r.rect.x - grow,
                    r.rect.y - grow,
                    r.rect.w + 2.0 * grow,
                    r.rect.h + 2.0 * grow,
                ],
                radius: 3.0 + eased * 6.0,
                alpha: 0.55 * (1.0 - t),
            }
        })
    }

    pub fn sparks(&self) -> impl Iterator<Item = SparkShape> + '_ {
        self.sparks.iter().map(|s| {
            let t = (s.age / SPARK_SECS).clamp(0.0, 1.0);
            SparkShape {
                center: s.pos,
                size: s.size * (1.0 - t * 0.6),
                alpha: 0.9 * (1.0 - t),
            }
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    const LINE: f32 = 18.0;

    fn caret(x: f32, y: f32) -> CaretRect {
        CaretRect {
            x,
            y,
            w: 2.0,
            h: LINE,
        }
    }

    fn bounds_w(anim: &TextCursorAnim) -> f32 {
        let q = anim.quad();
        let xs = q.map(|p| p[0]);
        xs.iter().cloned().fold(f32::MIN, f32::max) - xs.iter().cloned().fold(f32::MAX, f32::min)
    }

    fn settle(anim: &mut TextCursorAnim) -> usize {
        let mut frames = 0;
        while anim.step(1.0 / 60.0) {
            frames += 1;
            assert!(frames < 600, "the caret overlay never went away");
        }
        frames
    }

    #[test]
    fn first_target_is_placed_without_drawing_anything() {
        let mut anim = TextCursorAnim::default();
        assert!(!anim.set_target(caret(30.0, 20.0), &TextCursorConfig::default()));
        assert!(!anim.visible());
        assert!(!anim.step(0.016));
    }

    #[test]
    fn a_jump_lands_on_the_target_and_then_leaves_the_clients_caret_alone() {
        let cfg = TextCursorConfig::default();
        let mut anim = TextCursorAnim::default();
        anim.set_target(caret(0.0, 0.0), &cfg);
        assert!(anim.set_target(caret(600.0, 300.0), &cfg));
        assert_eq!(anim.presence(), 1.0);
        let frames = settle(&mut anim);
        assert!(frames > 3, "a long jump should take several frames");
        assert_eq!(anim.quad(), caret(600.0, 300.0).corners());
        assert_eq!(anim.presence(), 0.0);
        assert!(!anim.visible());
    }

    #[test]
    fn smear_stretches_along_the_path_and_glide_does_not() {
        let cfg = TextCursorConfig {
            vfx: TextCursorVfx::None,
            ..Default::default()
        };
        let mut anim = TextCursorAnim::default();
        anim.set_target(caret(0.0, 0.0), &cfg);
        anim.set_target(caret(400.0, 0.0), &cfg);
        let mut widest: f32 = 0.0;
        for _ in 0..40 {
            anim.step(1.0 / 120.0);
            widest = widest.max(bounds_w(&anim));
            // Critical damping: the leading edge never overshoots.
            assert!(anim.quad()[1][0] <= 402.01);
        }
        assert!(widest > 40.0, "widest was {widest}");

        let glide = TextCursorConfig {
            animation: TextCursorAnimation::Glide,
            ..cfg
        };
        let mut anim = TextCursorAnim::default();
        anim.set_target(caret(0.0, 0.0), &glide);
        anim.set_target(caret(400.0, 0.0), &glide);
        for _ in 0..40 {
            anim.step(1.0 / 120.0);
            assert!((bounds_w(&anim) - 2.0).abs() < 0.01);
        }
    }

    #[test]
    fn typing_steps_are_short_and_play_no_effect() {
        let cfg = TextCursorConfig::default();
        let mut anim = TextCursorAnim::default();
        anim.set_target(caret(0.0, 0.0), &cfg);
        anim.set_target(caret(9.0, 0.0), &cfg);
        assert_eq!(anim.ripples().count(), 0);
        let typing = settle(&mut anim);
        anim.set_target(caret(9.0, 240.0), &cfg);
        assert_eq!(anim.ripples().count(), 1);
        assert!(typing < settle(&mut anim));
    }

    #[test]
    fn sparkle_emits_a_bounded_number_of_sparks() {
        let cfg = TextCursorConfig {
            vfx: TextCursorVfx::Sparkle,
            ..Default::default()
        };
        let mut anim = TextCursorAnim::default();
        anim.set_target(caret(0.0, 0.0), &cfg);
        for i in 1..20 {
            anim.set_target(caret(0.0, i as f32 * 200.0), &cfg);
        }
        let n = anim.sparks().count();
        assert!((6..=48).contains(&n), "{n} sparks");
        settle(&mut anim);
        assert_eq!(anim.sparks().count(), 0);
    }

    #[test]
    fn animation_none_never_draws() {
        let cfg = TextCursorConfig {
            animation: TextCursorAnimation::None,
            ..Default::default()
        };
        let mut anim = TextCursorAnim::default();
        anim.set_target(caret(0.0, 0.0), &cfg);
        assert!(!anim.set_target(caret(90.0, 90.0), &cfg));
        assert!(!anim.step(0.016));
    }

    #[test]
    fn reset_makes_the_next_caret_appear_in_place() {
        let cfg = TextCursorConfig::default();
        let mut anim = TextCursorAnim::default();
        anim.set_target(caret(0.0, 0.0), &cfg);
        anim.set_target(caret(500.0, 0.0), &cfg);
        anim.reset();
        assert!(!anim.visible());
        assert!(!anim.set_target(caret(100.0, 100.0), &cfg));
        assert!(!anim.visible());
    }

    #[test]
    fn client_rectangles_are_sanitised() {
        // GTK reports a beam as a zero-width rectangle.
        let beam = CaretRect::from_client(10, 20, 0, 18).unwrap();
        assert_eq!((beam.w, beam.h), (2.0, 18.0));
        // A terminal's block cell is kept as it is.
        assert_eq!(CaretRect::from_client(0, 0, 9, 18).unwrap().w, 9.0);
        assert!(CaretRect::from_client(0, 0, 0, 0).is_none());
        assert!(CaretRect::from_client(0, 0, 4000, 18).is_none());
        assert!(CaretRect::from_client(0, 0, 2, 4000).is_none());
    }
}
