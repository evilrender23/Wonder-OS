use driftwm::sync::MutexExt;
use smithay::backend::renderer::{
    element::{
        Kind, memory::MemoryRenderBufferRenderElement, surface::WaylandSurfaceRenderElement,
    },
    gles::GlesRenderer,
};
use smithay::input::pointer::{CursorImageStatus, CursorImageSurfaceData};
use smithay::utils::IsAlive;
use smithay::utils::{Physical, Point, Scale};
use smithay::wayland::compositor::with_states;

use driftwm::canvas::{CanvasPos, canvas_to_screen};

use super::elements::OutputRenderElements;

/// Build the cursor render element(s) for the current frame.
/// `camera` and `zoom` are from the output being rendered; both go unused
/// while the session is locked.
/// Returns `OutputRenderElements` — either xcursor memory buffers or client surface elements,
/// then the drag icon, then the motion-blur smear beneath them all. The smear
/// is its own element so the cursor itself stays one sharp image the KMS
/// cursor plane can take; it follows whatever alpha the cursor has here (the
/// ghost on an inactive output smears as faintly as it is drawn) and is absent
/// whenever the cursor is.
pub fn build_cursor_elements(
    state: &mut crate::state::DriftWm,
    renderer: &mut GlesRenderer,
    output: &smithay::output::Output,
    camera: Point<f64, smithay::utils::Logical>,
    zoom: f64,
    scale: f64,
    alpha: f32,
) -> Vec<OutputRenderElements> {
    if alpha <= 0.0 {
        state.cursor_blur.forget_output(&output.name());
        return vec![];
    }
    // Touch hides the pointer until the next real mouse/trackpad motion. This
    // gate also clears the KMS hardware-cursor plane on udev, since that plane
    // is driven from these same render elements.
    if state.cursor.hidden_by_touch {
        state.cursor_blur.reset();
        return vec![];
    }
    let pointer = state.pointer.clone();
    let pointer_pos = pointer.current_location();
    // For the length of a lock the stored location is already screen-space —
    // see `SessionLockHandler::lock`. Transforming it again would offset the
    // cursor by the camera and scale it by the zoom.
    let screen_pos = if state.session_lock.is_locked() {
        pointer_pos
    } else {
        canvas_to_screen(CanvasPos(pointer_pos), camera, zoom).0
    };
    let physical_pos: Point<f64, Physical> = screen_pos.to_physical_precise_round(scale);

    let status = state.cursor.cursor_status.clone();
    let mut result = match status {
        CursorImageStatus::Hidden => vec![],
        CursorImageStatus::Surface(ref surface) => {
            if !surface.alive() {
                state.cursor.cursor_status = CursorImageStatus::default_named();
                return build_xcursor_elements(
                    state,
                    renderer,
                    physical_pos,
                    "default",
                    scale,
                    alpha,
                );
            }
            let hotspot = with_states(surface, |states| {
                states
                    .data_map
                    .get::<CursorImageSurfaceData>()
                    .map(|d| d.lock_or_recover().hotspot)
                    .unwrap_or_default()
            });
            let pos: Point<i32, Physical> = (
                (physical_pos.x - hotspot.x as f64 * scale) as i32,
                (physical_pos.y - hotspot.y as f64 * scale) as i32,
            )
                .into();
            let elems: Vec<WaylandSurfaceRenderElement<GlesRenderer>> =
                smithay::backend::renderer::element::surface::render_elements_from_surface_tree(
                    renderer,
                    surface,
                    pos,
                    Scale::from(1.0),
                    alpha,
                    Kind::Cursor,
                );
            elems
                .into_iter()
                .map(|e| OutputRenderElements::CursorSurface(e.into()))
                .collect()
        }
        CursorImageStatus::Named(icon) => {
            build_xcursor_elements(state, renderer, physical_pos, icon.name(), scale, alpha)
        }
    };

    let blur = &state.config.effects.cursor_blur;
    let smear = if !blur.active() {
        None
    } else if result.is_empty() {
        // A hidden cursor (a game holding the pointer, say) reappears
        // wherever it likes.
        state.cursor_blur.reset();
        None
    } else {
        let now = state.cursor_blur.now(state.start_time);
        super::cursor_smear::build_smear(
            &mut state.cursor_blur,
            state.render.cursor_smear_shader.as_ref(),
            renderer,
            &output.name(),
            &result,
            now,
            screen_pos,
            blur.strength,
            scale,
            alpha,
        )
    };

    // Drag-and-drop icon. `offset` is the accumulated `wl_surface.attach` delta
    // in the icon's logical pixels — apply it before scale-conversion to physical.
    if let Some(ref icon) = state.dnd_icon
        && icon.surface.alive()
    {
        let pos: Point<i32, Physical> = (
            (physical_pos.x + icon.offset.x as f64 * scale) as i32,
            (physical_pos.y + icon.offset.y as f64 * scale) as i32,
        )
            .into();
        let surface_elems: Vec<WaylandSurfaceRenderElement<GlesRenderer>> =
            smithay::backend::renderer::element::surface::render_elements_from_surface_tree(
                renderer,
                &icon.surface,
                pos,
                Scale::from(1.0),
                alpha,
                Kind::Cursor,
            );
        result.extend(
            surface_elems
                .into_iter()
                .map(|e| OutputRenderElements::CursorSurface(e.into())),
        );
    }

    result.extend(smear);
    result
}

/// Build xcursor memory buffer elements for a named cursor icon.
fn build_xcursor_elements(
    state: &mut crate::state::DriftWm,
    renderer: &mut GlesRenderer,
    physical_pos: Point<f64, Physical>,
    name: &'static str,
    scale: f64,
    alpha: f32,
) -> Vec<OutputRenderElements> {
    let loaded = state.load_xcursor(name).is_some();
    if !loaded && state.load_xcursor("default").is_none() {
        return vec![];
    }
    let key = if loaded { name } else { "default" };
    let Some(cursor_frames) = state.cursor.cursor_buffers.get(key) else {
        return vec![];
    };

    let frame_idx = if cursor_frames.total_duration_ms == 0 {
        0
    } else {
        let elapsed =
            state.start_time.elapsed().as_millis() as u32 % cursor_frames.total_duration_ms;
        let mut acc = 0u32;
        let mut idx = 0;
        for (i, &(_, _, delay)) in cursor_frames.frames.iter().enumerate() {
            acc += delay;
            if elapsed < acc {
                idx = i;
                break;
            }
        }
        idx
    };

    let (buffer, hotspot, _) = &cursor_frames.frames[frame_idx];
    let hotspot = *hotspot;

    let pos = physical_pos - Point::from((hotspot.x as f64 * scale, hotspot.y as f64 * scale));
    match MemoryRenderBufferRenderElement::from_buffer(
        renderer,
        pos,
        buffer,
        Some(alpha),
        None,
        None,
        Kind::Cursor,
    ) {
        Ok(elem) => vec![OutputRenderElements::Cursor(elem)],
        Err(_) => vec![],
    }
}
