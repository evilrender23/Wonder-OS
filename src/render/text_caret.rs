//! Draws the text-caret effect (`driftwm::text_cursor`) over the window that
//! owns the caret: the travelling quad, the ripple it leaves and its sparks.

use std::borrow::Cow;

use smithay::backend::renderer::{
    element::{Kind, utils::RescaleRenderElement},
    gles::{
        GlesPixelProgram, GlesRenderer, Uniform, UniformName, UniformType,
        element::PixelShaderElement,
    },
};
use smithay::utils::{Logical, Physical, Point, Rectangle, Size};

use driftwm::text_cursor::TextCursorAnim;

use super::{OutputRenderElements, WindowRenderAnimation, WindowTransformElement};

const SHADER_SRC: &str = include_str!("../shaders/text_caret.glsl");
/// Reach of the halo around the travelling caret, in logical pixels.
const GLOW: f32 = 7.0;
const RIPPLE_STROKE: f32 = 1.5;

pub fn compile_text_caret_shader(renderer: &mut GlesRenderer) -> Option<GlesPixelProgram> {
    let uniform = |name: &'static str, type_| UniformName {
        name: Cow::Borrowed(name),
        type_,
    };
    let uniforms = [
        uniform("u_kind", UniformType::_1f),
        uniform("u_a", UniformType::_4f),
        uniform("u_b", UniformType::_4f),
        uniform("u_params", UniformType::_4f),
        uniform("u_aa_scale", UniformType::_1f),
        uniform("u_color", UniformType::_4f),
    ];
    match renderer.compile_custom_pixel_shader(SHADER_SRC, &uniforms) {
        Ok(shader) => Some(shader),
        Err(e) => {
            tracing::error!("Failed to compile text caret shader: {e}");
            None
        }
    }
}

/// One shape of the effect: where it paints (surface-local logical pixels, as
/// `[x0, y0, x1, y1]`) and how.
struct Shape {
    bounds: [f32; 4],
    kind: f32,
    /// Positions, still surface-local; made element-local when pushed.
    a: [f32; 4],
    b: [f32; 4],
    /// Which components of `a`/`b` are positions (and so get translated).
    points: usize,
    params: [f32; 2],
    alpha: f32,
}

fn shapes(anim: &TextCursorAnim, glow: bool) -> Vec<Shape> {
    let mut out = Vec::new();
    // Sparks and the ripple over the caret, as in wonderterm.
    for s in anim.sparks() {
        let r = s.size * 0.5;
        out.push(Shape {
            bounds: [
                s.center[0] - r,
                s.center[1] - r,
                s.center[0] + r,
                s.center[1] + r,
            ],
            kind: 2.0,
            a: [s.center[0], s.center[1], 0.0, 0.0],
            b: [0.0; 4],
            points: 1,
            params: [r, 0.0],
            alpha: s.alpha,
        });
    }
    for r in anim.ripples() {
        let [x, y, w, h] = r.rect;
        out.push(Shape {
            bounds: [x, y, x + w, y + h],
            kind: 1.0,
            a: [x, y, w, h],
            b: [0.0; 4],
            points: 1,
            params: [r.radius.min(w.min(h) * 0.5), RIPPLE_STROKE],
            alpha: r.alpha,
        });
    }
    let presence = anim.presence();
    if presence > 0.0 {
        let q = anim.quad();
        let fold = |f: fn(f32, f32) -> f32, axis: usize| {
            q.iter().map(|p| p[axis]).reduce(f).unwrap_or_default()
        };
        out.push(Shape {
            bounds: [
                fold(f32::min, 0),
                fold(f32::min, 1),
                fold(f32::max, 0),
                fold(f32::max, 1),
            ],
            kind: 0.0,
            a: [q[0][0], q[0][1], q[1][0], q[1][1]],
            b: [q[2][0], q[2][1], q[3][0], q[3][1]],
            points: 4,
            params: [if glow { GLOW } else { 0.0 }, 0.0],
            alpha: presence,
        });
    }
    out
}

/// Push the effect's elements for the window whose surface origin sits at
/// `surface_loc` (pre-zoom output-logical). Front-most first, like every other
/// element list.
#[allow(clippy::too_many_arguments)]
pub(super) fn push_text_caret_elements(
    target: &mut Vec<OutputRenderElements>,
    shader: &GlesPixelProgram,
    anim: &TextCursorAnim,
    color: [u8; 4],
    glow: bool,
    surface_loc: Point<f64, Logical>,
    opacity: f64,
    px_per_logical: f64,
    zoom: f64,
    animation: Option<WindowRenderAnimation>,
) {
    let color = (
        color[0] as f32 / 255.0,
        color[1] as f32 / 255.0,
        color[2] as f32 / 255.0,
        color[3] as f32 / 255.0,
    );
    for shape in shapes(anim, glow) {
        // Room for the anti-aliased edge, the stroke and the halo.
        let pad = 2.0
            + shape.params[1]
            + if shape.kind == 0.0 {
                shape.params[0]
            } else {
                0.0
            };
        let x0 = (surface_loc.x + (shape.bounds[0] - pad) as f64).floor();
        let y0 = (surface_loc.y + (shape.bounds[1] - pad) as f64).floor();
        let x1 = (surface_loc.x + (shape.bounds[2] + pad) as f64).ceil();
        let y1 = (surface_loc.y + (shape.bounds[3] + pad) as f64).ceil();
        let area = Rectangle::<i32, Logical>::new(
            Point::from((x0 as i32, y0 as i32)),
            Size::from((((x1 - x0) as i32).max(1), ((y1 - y0) as i32).max(1))),
        );
        // Surface-local → element-local.
        let dx = (surface_loc.x - x0) as f32;
        let dy = (surface_loc.y - y0) as f32;
        let local = |v: [f32; 4], first: usize| {
            let mut v = v;
            for i in 0..2 {
                if first + i < shape.points {
                    v[i * 2] += dx;
                    v[i * 2 + 1] += dy;
                }
            }
            (v[0], v[1], v[2], v[3])
        };
        let elem = PixelShaderElement::new(
            shader.clone(),
            area,
            None,
            (shape.alpha as f64 * opacity) as f32,
            vec![
                Uniform::new("u_kind", shape.kind),
                Uniform::new("u_a", local(shape.a, 0)),
                Uniform::new("u_b", local(shape.b, 2)),
                Uniform::new("u_params", (shape.params[0], shape.params[1], 0.0, 0.0)),
                Uniform::new("u_aa_scale", px_per_logical as f32),
                Uniform::new("u_color", color),
            ],
            Kind::Unspecified,
        );
        let elem =
            RescaleRenderElement::from_element(elem, Point::<i32, Physical>::from((0, 0)), zoom);
        target.push(match animation {
            Some(animation) => OutputRenderElements::AnimatedChrome(WindowTransformElement::new(
                elem,
                animation.origin,
                animation.offset,
                animation.scale,
            )),
            None => OutputRenderElements::Background(elem),
        });
    }
}
