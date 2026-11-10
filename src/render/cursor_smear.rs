//! The pointer's motion blur, drawn just beneath the cursor.
//!
//! The path is [`driftwm::cursor_blur`]'s; this flattens whatever the cursor
//! is right now (a themed image or a client's surface) into a small texture
//! and draws it integrated along that path. Only while the pointer is moving
//! fast enough to leave a smear: at rest nothing is baked or drawn.

use std::borrow::Cow;
use std::collections::HashMap;

use smithay::backend::renderer::Renderer;
use smithay::backend::renderer::element::texture::TextureRenderElement;
use smithay::backend::renderer::element::{Element, Id, Kind, RenderElement, UnderlyingStorage};
use smithay::backend::renderer::gles::{
    GlesError, GlesFrame, GlesRenderer, GlesTexProgram, GlesTexture, Uniform, UniformName,
    UniformType,
};
use smithay::backend::renderer::utils::{CommitCounter, DamageSet, OpaqueRegions};
use smithay::utils::{Buffer, Logical, Physical, Point, Rectangle, Scale, Size, Transform};

use driftwm::cursor_blur::{Smear, Trail, exposure};

use super::elements::OutputRenderElements;
use super::warp::{WarpTarget, bake_elements};

const SHADER_SRC: &str = include_str!("../shaders/cursor_smear.glsl");

/// A path this old was not continued by the frame before: the output was not
/// being drawn, and joining its last point to this one would draw a streak
/// nobody's hand made.
const STALE: f64 = 0.12;

/// The average of a moving picture is faint; real motion blur on a display
/// reads stronger than that because the eye follows the cursor.
const GAIN: f32 = 1.6;

pub fn compile_cursor_smear_shader(renderer: &mut GlesRenderer) -> Option<GlesTexProgram> {
    let uniform = |name: &'static str, type_| UniformName {
        name: Cow::Borrowed(name),
        type_,
    };
    let uniforms = [
        uniform("quad", UniformType::_2f),
        uniform("tile", UniformType::_2f),
        uniform("used", UniformType::_2f),
        uniform("alloc", UniformType::_2f),
        uniform("control", UniformType::_2f),
        uniform("tail", UniformType::_2f),
        uniform("flipped", UniformType::_1f),
        uniform("gain", UniformType::_1f),
    ];
    match renderer.compile_custom_texture_shader(SHADER_SRC, &uniforms) {
        Ok(shader) => Some(shader),
        Err(e) => {
            tracing::error!("Failed to compile cursor smear shader: {e}");
            None
        }
    }
}

struct OutputTrail {
    trail: Trail,
    last_push: f64,
    target: Option<WarpTarget>,
    id: Id,
    commit: CommitCounter,
}

impl Default for OutputTrail {
    fn default() -> Self {
        Self {
            trail: Trail::default(),
            last_push: f64::NEG_INFINITY,
            target: None,
            id: Id::new(),
            commit: CommitCounter::default(),
        }
    }
}

/// Where the pointer has recently been on each output, in that output's own
/// logical pixels: a camera pan moves the canvas under a still cursor and
/// must not read as the cursor moving.
#[derive(Default)]
pub struct CursorBlur {
    outputs: HashMap<String, OutputTrail>,
    /// The time the tests say it is, instead of the wall clock.
    #[cfg(test)]
    pub clock: Option<f64>,
}

impl CursorBlur {
    /// Seconds on the clock the paths are timed against.
    pub fn now(&self, since: std::time::Instant) -> f64 {
        #[cfg(test)]
        if let Some(clock) = self.clock {
            return clock;
        }
        since.elapsed().as_secs_f64()
    }

    /// Forget every path: the pointer was put somewhere, not moved there.
    pub fn reset(&mut self) {
        for entry in self.outputs.values_mut() {
            entry.trail.reset();
        }
    }

    /// Drop everything, textures included.
    pub fn clear(&mut self) {
        self.outputs.clear();
    }

    pub fn forget_output(&mut self, name: &str) {
        self.outputs.remove(name);
    }

    /// Whether some output still has a smear to shrink away at `now`.
    pub fn is_live(&self, now: f64, strength: f64) -> bool {
        let exposure = exposure(strength);
        self.outputs
            .values()
            .any(|o| now - o.last_push < STALE && o.trail.is_live(now, exposure))
    }

    /// Record the pointer at `pos` on `output` at `now` and return the smear
    /// it leaves there, if any.
    fn advance(
        &mut self,
        output: &str,
        now: f64,
        pos: Point<f64, Logical>,
        strength: f64,
    ) -> Option<(Smear, &mut OutputTrail)> {
        let exposure = exposure(strength);
        let entry = self.outputs.entry(output.to_owned()).or_default();
        if now - entry.last_push > STALE {
            entry.trail.reset();
        }
        entry.last_push = now;
        entry.trail.push(now, [pos.x, pos.y], exposure);
        let smear = entry.trail.smear(now, exposure)?;
        Some((smear, entry))
    }
}

/// The smear element for `cursor`, the cursor's own elements as placed on
/// `output` this frame, with the pointer at `pos` (output-logical).
#[allow(clippy::too_many_arguments)]
pub(super) fn build_smear(
    blur: &mut CursorBlur,
    shader: Option<&GlesTexProgram>,
    renderer: &mut GlesRenderer,
    output: &str,
    cursor: &[OutputRenderElements],
    now: f64,
    pos: Point<f64, Logical>,
    strength: f64,
    scale: f64,
    alpha: f32,
) -> Option<OutputRenderElements> {
    let shader = shader?;
    let (smear, entry) = blur.advance(output, now, pos, strength)?;
    let element_scale = Scale::from(scale);
    let tile = cursor
        .iter()
        .map(|e| e.geometry(element_scale))
        .reduce(|a, b| a.merge(b))?;
    let source = bake_elements(renderer, &mut entry.target, cursor, tile, element_scale)?;

    let rel = |p: [f64; 2]| {
        [
            ((p[0] - smear.head[0]) * scale) as f32,
            ((p[1] - smear.head[1]) * scale) as f32,
        ]
    };
    let (control, tail) = (rel(smear.control), rel(smear.tail));
    // The curve lies inside the hull of its three points, the head being the
    // origin; the picture rides along it.
    let min = [
        control[0].min(tail[0]).min(0.0).floor() as i32 - 1,
        control[1].min(tail[1]).min(0.0).floor() as i32 - 1,
    ];
    let max = [
        control[0].max(tail[0]).max(0.0).ceil() as i32 + 1,
        control[1].max(tail[1]).max(0.0).ceil() as i32 + 1,
    ];
    let quad: Rectangle<i32, Physical> = Rectangle::new(
        Point::from((tile.loc.x + min[0], tile.loc.y + min[1])),
        Size::from((tile.size.w + max[0] - min[0], tile.size.h + max[1] - min[1])),
    );
    entry.commit.increment();
    let texture = source.texture.clone();
    let flipped = texture.is_y_inverted();
    let inner = TextureRenderElement::from_static_texture(
        entry.id.clone(),
        renderer.context_id(),
        quad.loc.to_f64(),
        texture,
        1,
        Transform::Normal,
        Some(alpha * smear.opacity),
        None,
        None,
        None,
        Kind::Unspecified,
    );
    Some(OutputRenderElements::CursorSmear(SmearElement {
        inner,
        shader: shader.clone(),
        commit: entry.commit,
        quad,
        uniforms: SmearUniforms {
            tile: [-min[0] as f32, -min[1] as f32],
            used: [source.used.w as f32, source.used.h as f32],
            alloc: [source.alloc.w as f32, source.alloc.h as f32],
            control,
            tail,
            flipped,
        },
    }))
}

#[derive(Debug, Clone, Copy)]
struct SmearUniforms {
    tile: [f32; 2],
    used: [f32; 2],
    alloc: [f32; 2],
    control: [f32; 2],
    tail: [f32; 2],
    flipped: bool,
}

/// The cursor texture stretched over the smear's bounding box and drawn by
/// the smear shader, which ignores the stretch and places the picture itself.
#[derive(Debug)]
pub struct SmearElement {
    inner: TextureRenderElement<GlesTexture>,
    shader: GlesTexProgram,
    commit: CommitCounter,
    quad: Rectangle<i32, Physical>,
    uniforms: SmearUniforms,
}

impl Element for SmearElement {
    fn id(&self) -> &Id {
        self.inner.id()
    }

    fn current_commit(&self) -> CommitCounter {
        self.commit
    }

    fn src(&self) -> Rectangle<f64, Buffer> {
        self.inner.src()
    }

    fn transform(&self) -> Transform {
        Transform::Normal
    }

    fn geometry(&self, _scale: Scale<f64>) -> Rectangle<i32, Physical> {
        self.quad
    }

    fn damage_since(
        &self,
        scale: Scale<f64>,
        commit: Option<CommitCounter>,
    ) -> DamageSet<i32, Physical> {
        if commit == Some(self.commit) {
            DamageSet::default()
        } else {
            DamageSet::from_slice(&[Rectangle::from_size(self.geometry(scale).size)])
        }
    }

    fn opaque_regions(&self, _scale: Scale<f64>) -> OpaqueRegions<i32, Physical> {
        OpaqueRegions::default()
    }

    fn alpha(&self) -> f32 {
        self.inner.alpha()
    }

    fn kind(&self) -> Kind {
        Kind::Unspecified
    }
}

impl RenderElement<GlesRenderer> for SmearElement {
    fn draw(
        &self,
        frame: &mut GlesFrame<'_, '_>,
        src: Rectangle<f64, Buffer>,
        dst: Rectangle<i32, Physical>,
        damage: &[Rectangle<i32, Physical>],
        opaque_regions: &[Rectangle<i32, Physical>],
        user_data: Option<&smithay::utils::user_data::UserDataMap>,
    ) -> Result<(), GlesError> {
        let u = &self.uniforms;
        let pair = |name: &'static str, v: [f32; 2]| Uniform::new(name, (v[0], v[1]));
        let uniforms = vec![
            pair("quad", [self.quad.size.w as f32, self.quad.size.h as f32]),
            pair("tile", u.tile),
            pair("used", u.used),
            pair("alloc", u.alloc),
            pair("control", u.control),
            pair("tail", u.tail),
            Uniform::new("flipped", if u.flipped { 1.0f32 } else { 0.0 }),
            Uniform::new("gain", GAIN),
        ];
        frame.override_default_tex_program(self.shader.clone(), uniforms);
        let result = RenderElement::<GlesRenderer>::draw(
            &self.inner,
            frame,
            src,
            dst,
            damage,
            opaque_regions,
            user_data,
        );
        frame.clear_tex_program_override();
        result
    }

    fn underlying_storage(&self, _renderer: &mut GlesRenderer) -> Option<UnderlyingStorage<'_>> {
        None
    }
}
