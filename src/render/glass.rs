//! Liquid glass (`[effects.glass]`), after React Bits' FluidGlass.
//!
//! Two halves, both riding the frost that already exists:
//!
//! * the **refraction**: where a frosted surface's frost texture is filled
//!   (shared slice or padded crop, in `blur.rs`), `glass.glsl` replaces the
//!   plain copy — inside the rim the frost is taken from further out, red and
//!   blue bent by different amounts. Cached with the frost, so it costs
//!   nothing while the frost holds.
//! * the **lit edge**: `glass_rim.glsl`, an analytic pixel-shader element on
//!   top of the surface, so it shows on opaque windows too. Its uniforms only
//!   change with geometry, so the damage tracker skips it otherwise.

use std::borrow::Cow;
use std::collections::HashMap;
use std::time::{Duration, Instant};

use smithay::backend::renderer::element::Kind;
use smithay::backend::renderer::element::utils::RescaleRenderElement;
use smithay::backend::renderer::gles::element::PixelShaderElement;
use smithay::backend::renderer::gles::{
    GlesPixelProgram, GlesRenderer, GlesTexProgram, Uniform, UniformName, UniformType,
};
use smithay::utils::{Logical, Physical, Point, Rectangle, Scale, Size};

use driftwm::config::GlassConfig;

use super::OutputRenderElements;
use crate::decorations::DecorationKey;

const GLASS_SRC: &str = include_str!("../shaders/glass.glsl");
const GLASS_RIM_SRC: &str = include_str!("../shaders/glass_rim.glsl");

const GLASS_UNIFORMS: &[UniformName<'static>] = &[
    UniformName {
        name: Cow::Borrowed("u_texsize"),
        type_: UniformType::_2f,
    },
    UniformName {
        name: Cow::Borrowed("u_origin"),
        type_: UniformType::_2f,
    },
    UniformName {
        name: Cow::Borrowed("u_size"),
        type_: UniformType::_2f,
    },
    UniformName {
        name: Cow::Borrowed("u_valid"),
        type_: UniformType::_4f,
    },
    UniformName {
        name: Cow::Borrowed("u_shape"),
        type_: UniformType::_4f,
    },
    UniformName {
        name: Cow::Borrowed("u_tint"),
        type_: UniformType::_4f,
    },
];

const GLASS_RIM_UNIFORMS: &[UniformName<'static>] = &[
    UniformName {
        name: Cow::Borrowed("u_radius"),
        type_: UniformType::_1f,
    },
    UniformName {
        name: Cow::Borrowed("u_bevel"),
        type_: UniformType::_1f,
    },
    UniformName {
        name: Cow::Borrowed("u_reflection"),
        type_: UniformType::_1f,
    },
    UniformName {
        name: Cow::Borrowed("u_aa_scale"),
        type_: UniformType::_1f,
    },
];

/// Compile both glass programs. Either failing just leaves that half off.
pub(crate) fn compile_glass_shaders(
    renderer: &mut GlesRenderer,
) -> (Option<GlesTexProgram>, Option<GlesPixelProgram>) {
    let frost = renderer
        .compile_custom_texture_shader(GLASS_SRC, GLASS_UNIFORMS)
        .map_err(|e| tracing::error!("Failed to compile the glass shader: {e:?}"))
        .ok();
    let rim = renderer
        .compile_custom_pixel_shader(GLASS_RIM_SRC, GLASS_RIM_UNIFORMS)
        .map_err(|e| tracing::error!("Failed to compile the glass rim shader: {e:?}"))
        .ok();
    (frost, rim)
}

/// Where one frost copy reads from and writes to, in texels of the source.
#[derive(Clone, Copy, Debug, PartialEq)]
pub(super) struct FrostCopy {
    /// Allocation of the source texture.
    pub tex_size: Size<i32, smithay::utils::Buffer>,
    /// Window's top-left in the source.
    pub origin: Point<f64, smithay::utils::Buffer>,
    /// Window size (the destination).
    pub size: Size<i32, Physical>,
    /// Texels of the source holding real backdrop: `(x0, y0, x1, y1)`.
    pub valid: [f32; 4],
    /// Corner radius in output pixels.
    pub corner_radius: f32,
}

/// Uniforms for `glass.glsl`. `scale` turns the config's logical rim width
/// into output pixels (output scale × zoom for windows, output scale for
/// layers).
pub(super) fn frost_uniforms(
    glass: &GlassConfig,
    copy: &FrostCopy,
    scale: f64,
) -> Vec<Uniform<'static>> {
    let t = glass.tint;
    vec![
        Uniform::new(
            "u_texsize",
            [copy.tex_size.w.max(1) as f32, copy.tex_size.h.max(1) as f32],
        ),
        Uniform::new("u_origin", [copy.origin.x as f32, copy.origin.y as f32]),
        Uniform::new("u_size", [copy.size.w as f32, copy.size.h as f32]),
        Uniform::new("u_valid", copy.valid),
        Uniform::new(
            "u_shape",
            [
                copy.corner_radius.max(0.0),
                rim_px(glass.bevel, scale, copy.size),
                glass.refraction as f32,
                glass.dispersion as f32,
            ],
        ),
        Uniform::new(
            "u_tint",
            [
                t[0] as f32 / 255.0,
                t[1] as f32 / 255.0,
                t[2] as f32 / 255.0,
                glass.tint_strength as f32,
            ],
        ),
    ]
}

/// The rim width in output pixels, never more than a third of the shorter
/// side: a bar 30 px tall with an 18 px rim would be all rim and no glass.
pub(super) fn rim_px(bevel_logical: f64, scale: f64, size: Size<i32, Physical>) -> f32 {
    let short = size.w.min(size.h).max(0) as f64;
    (bevel_logical * scale).min(short / 3.0).max(0.0) as f32
}

/// One cached rim element and what it was built for.
pub struct GlassRimEntry {
    elem: PixelShaderElement,
    key: GlassRimKey,
    used: Instant,
}

/// Everything the rim's uniforms and geometry depend on.
type GlassRimKey = ([i32; 4], [u32; 5]);

/// How long an unused rim survives: windows and layers that went away (or
/// stopped being frosted) drop out of the cache without every teardown path
/// having to know about it.
const RIM_IDLE: Duration = Duration::from_secs(5);

/// Drop rims no frame has used for a while.
pub(crate) fn prune_rims(cache: &mut HashMap<DecorationKey, GlassRimEntry>) {
    cache.retain(|_, e| e.used.elapsed() < RIM_IDLE);
}

/// Build (or reuse) the lit edge for a surface whose body is `inner_logical`
/// (pre-zoom, output-logical) and insert it at `at`, so it sits above the
/// surface's own elements.
#[allow(clippy::too_many_arguments)]
pub(super) fn insert_glass_rim(
    target: &mut Vec<OutputRenderElements>,
    at: usize,
    cache: &mut HashMap<DecorationKey, GlassRimEntry>,
    key: DecorationKey,
    shader: &GlesPixelProgram,
    glass: &GlassConfig,
    inner_logical: Rectangle<f64, Logical>,
    radius_logical: f32,
    opacity: f64,
    output_scale: Scale<f64>,
    zoom: f64,
    animation: Option<super::WindowRenderAnimation>,
) {
    let area: Rectangle<i32, Logical> = Rectangle::new(
        Point::from((
            inner_logical.loc.x.round() as i32,
            inner_logical.loc.y.round() as i32,
        )),
        Size::from((
            inner_logical.size.w.round() as i32,
            inner_logical.size.h.round() as i32,
        )),
    );
    if area.size.w <= 0 || area.size.h <= 0 {
        return;
    }
    let aa_scale = (output_scale.x * zoom) as f32;
    let short = area.size.w.min(area.size.h) as f32;
    let bevel = (glass.bevel as f32).min(short / 3.0);
    let radius = radius_logical.max(0.0).min(short / 2.0);
    let fresh_key: GlassRimKey = (
        [area.loc.x, area.loc.y, area.size.w, area.size.h],
        [
            radius.to_bits(),
            bevel.to_bits(),
            (glass.reflection as f32).to_bits(),
            aa_scale.to_bits(),
            (opacity as f32).to_bits(),
        ],
    );
    let uniforms = || {
        vec![
            Uniform::new("u_radius", radius),
            Uniform::new("u_bevel", bevel),
            Uniform::new("u_reflection", glass.reflection as f32),
            Uniform::new("u_aa_scale", aa_scale),
        ]
    };

    let entry = cache.entry(key).or_insert_with(|| GlassRimEntry {
        elem: PixelShaderElement::new(
            shader.clone(),
            area,
            None,
            opacity as f32,
            uniforms(),
            Kind::Unspecified,
        ),
        key: fresh_key,
        used: Instant::now(),
    });
    entry.used = Instant::now();
    if entry.key != fresh_key {
        // Alpha is fixed at construction; anything else updates in place so
        // the element keeps its id and the damage tracker its history.
        if entry.key.1[4] != fresh_key.1[4] {
            entry.elem = PixelShaderElement::new(
                shader.clone(),
                area,
                None,
                opacity as f32,
                uniforms(),
                Kind::Unspecified,
            );
        } else {
            entry.elem.update_uniforms(uniforms());
            entry.elem.resize(area, None);
        }
        entry.key = fresh_key;
    }

    let elem = RescaleRenderElement::from_element(
        entry.elem.clone(),
        Point::<i32, Physical>::from((0, 0)),
        zoom,
    );
    let elem = match animation {
        Some(a) => OutputRenderElements::AnimatedChrome(super::WindowTransformElement::new(
            elem, a.origin, a.offset, a.scale,
        )),
        None => OutputRenderElements::Background(elem),
    };
    target.insert(at.min(target.len()), elem);
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_rim_never_takes_more_than_a_third_of_the_short_side() {
        let bar: Size<i32, Physical> = Size::from((2560, 30));
        assert_eq!(rim_px(18.0, 1.0, bar), 10.0);
        let window: Size<i32, Physical> = Size::from((800, 600));
        assert_eq!(rim_px(18.0, 1.0, window), 18.0);
        assert_eq!(rim_px(18.0, 2.0, window), 36.0);
    }

    #[test]
    fn an_empty_surface_has_no_rim() {
        assert_eq!(rim_px(18.0, 1.0, Size::from((0, 0))), 0.0);
    }

    #[test]
    fn frost_uniforms_carry_the_config() {
        let glass = GlassConfig {
            enabled: true,
            refraction: 1.5,
            dispersion: 0.5,
            bevel: 20.0,
            reflection: 1.0,
            tint: [255, 0, 0, 255],
            tint_strength: 0.25,
        };
        let copy = FrostCopy {
            tex_size: Size::from((1024, 768)),
            origin: Point::from((96.0, 96.0)),
            size: Size::from((600, 400)),
            valid: [0.0, 0.0, 792.0, 592.0],
            corner_radius: 28.0,
        };
        let u = frost_uniforms(&glass, &copy, 2.0);
        assert_eq!(u.len(), GLASS_UNIFORMS.len());
        let names: Vec<_> = u.iter().map(|u| u.name.to_string()).collect();
        let declared: Vec<_> = GLASS_UNIFORMS.iter().map(|u| u.name.to_string()).collect();
        assert_eq!(
            names, declared,
            "every uniform pushed is declared, in order"
        );
    }
}
