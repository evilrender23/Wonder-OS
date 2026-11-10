//! The slide between two spaces: both are rendered once into textures, and
//! while the slide runs the output draws those side by side instead of the
//! scene. See `state/spaces.rs`.

use smithay::backend::allocator::Fourcc;
use smithay::backend::renderer::damage::OutputDamageTracker;
use smithay::backend::renderer::element::Kind;
use smithay::backend::renderer::element::texture::{TextureBuffer, TextureRenderElement};
use smithay::backend::renderer::gles::{GlesRenderer, GlesTexture};
use smithay::backend::renderer::{Bind as _, Offscreen};
use smithay::output::Output;
use smithay::utils::{Physical, Point, Size, Transform};

use super::OutputRenderElements;

/// One space as it looked on its output, cursor excluded.
pub(crate) struct SpaceSnapshot {
    buffer: TextureBuffer<GlesTexture>,
    texels: Size<i32, Physical>,
}

/// Render what `output` shows right now into a texture. `None` when the
/// output has no mode or the render fails; the caller then switches without a
/// slide.
pub(crate) fn snapshot_space(
    state: &mut crate::state::DriftWm,
    renderer: &mut GlesRenderer,
    output: &Output,
) -> Option<SpaceSnapshot> {
    // Rendered and later drawn in the output's untransformed space: the real
    // frame applies the output transform to the pictures like to any element.
    let texels = output
        .current_transform()
        .transform_size(output.current_mode()?.size);
    if texels.w <= 0 || texels.h <= 0 {
        return None;
    }
    let scale = output.current_scale().fractional_scale();

    // The cached background follows the camera, which a switch has just moved.
    let (camera, zoom) = state.world_view(output);
    let (last_camera, last_zoom) = {
        let os = crate::state::output_state(output);
        (os.last_rendered_camera, os.last_rendered_zoom)
    };
    super::update_background_element(state, output, camera, zoom, last_camera, last_zoom);
    let elements = super::compose_frame(state, renderer, output, Vec::new());

    let buffer_size = texels.to_logical(1).to_buffer(1, Transform::Normal);
    let mut texture =
        Offscreen::<GlesTexture>::create_buffer(renderer, Fourcc::Abgr8888, buffer_size).ok()?;
    {
        let mut target = renderer.bind(&mut texture).ok()?;
        let mut tracker = OutputDamageTracker::new(texels, scale, Transform::Normal);
        if let Err(e) =
            tracker.render_output(renderer, &mut target, 0, &elements, [0.0, 0.0, 0.0, 1.0])
        {
            tracing::debug!("space slide dropped: rendering a snapshot failed: {e:?}");
            return None;
        }
    }
    let buffer = TextureBuffer::from_texture(renderer, texture, 1, Transform::Normal, None);
    Some(SpaceSnapshot { buffer, texels })
}

/// The two pictures of a slide at their current offsets, front to back.
pub(super) fn slide_elements(
    slide: &crate::state::SpaceSlide,
    output: &Output,
) -> Vec<OutputRenderElements> {
    let logical = crate::state::output_logical_size(output);
    let width = slide.from.texels.w as f64;
    let travelled = (slide.progress() * width).round();
    // Going right, the pictures move left: the old one leaves by the left
    // edge and the new one follows it in from the right.
    let (from_x, to_x) = if slide.rightward {
        (-travelled, width - travelled)
    } else {
        (travelled, travelled - width)
    };
    [(&slide.to, to_x), (&slide.from, from_x)]
        .into_iter()
        .map(|(snapshot, x)| {
            OutputRenderElements::Blur(TextureRenderElement::from_texture_buffer(
                Point::<f64, Physical>::from((x, 0.0)),
                &snapshot.buffer,
                None,
                Some(super::texel_src(snapshot.texels)),
                Some(logical),
                Kind::Unspecified,
            ))
        })
        .collect()
}
