//! Where the focused client's text caret is, and the effect that follows it.
//!
//! Clients report their caret through `zwp_text_input_v3.set_cursor_rectangle`
//! so an input method can place its popup. smithay keeps that rectangle to
//! itself, so each text-input object gets a [`CaretTap`] in front of its
//! request handler: the tap reads the caret requests on their way through and
//! hands every message on, untouched, to smithay. `src/tests/text_caret.rs`
//! holds that arrangement to its word against the pinned smithay.
//!
//! The effect itself is [`driftwm::text_cursor`]: an overlay drawn only while
//! the caret travels. Nothing is ever sent to the client on its behalf, so a
//! client's own caret — and any design it has for it — is left as it is.

use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

use smithay::reexports::wayland_server::{
    Resource,
    backend::{
        ClientId, Handle, ObjectData, ObjectId,
        protocol::{Argument, Message},
    },
    protocol::wl_surface::WlSurface,
};
use smithay::wayland::input_method::InputMethodSeat;
use smithay::wayland::text_input::TextInputSeat;

use driftwm::sync::MutexExt;
use driftwm::text_cursor::{CaretRect, TextCursorAnim};
use driftwm::window_ext::WindowExt;

use super::DriftWm;

/// A wheel scroll moves the caret with the page; that is not the caret
/// travelling, and trailing an effect behind every scroll step would be noise.
const SCROLL_QUIET: Duration = Duration::from_millis(250);

// zwp_text_input_v3 request opcodes.
const OP_DESTROY: u16 = 0;
const OP_ENABLE: u16 = 1;
const OP_DISABLE: u16 = 2;
const OP_SET_CURSOR_RECTANGLE: u16 = 6;
const OP_COMMIT: u16 = 7;

#[derive(Default)]
pub struct TextCaret {
    pub anim: TextCursorAnim,
    /// The surface the caret rectangle is relative to.
    pub surface: Option<WlSurface>,
    last_scroll: Option<Instant>,
}

impl TextCaret {
    /// Whether the overlay has something to draw on `surface`.
    pub fn shows_on(&self, surface: &WlSurface) -> bool {
        self.anim.visible() && self.surface.as_ref() == Some(surface)
    }

    fn clear(&mut self) {
        self.anim.reset();
        self.surface = None;
    }
}

/// Double-buffered caret state of one text-input object, applied on `commit`.
#[derive(Default)]
struct Pending {
    enable: Option<bool>,
    rect: Option<(i32, i32, i32, i32)>,
}

/// Sits in front of a `zwp_text_input_v3` object's request handler.
struct CaretTap {
    inner: Arc<dyn ObjectData<DriftWm>>,
    pending: Mutex<Pending>,
}

impl ObjectData<DriftWm> for CaretTap {
    fn request(
        self: Arc<Self>,
        handle: &Handle,
        data: &mut DriftWm,
        client_id: ClientId,
        msg: Message<ObjectId, std::os::fd::OwnedFd>,
    ) -> Option<Arc<dyn ObjectData<DriftWm>>> {
        let committed = {
            let mut pending = self.pending.lock_or_recover();
            match msg.opcode {
                OP_ENABLE => pending.enable = Some(true),
                OP_DISABLE => pending.enable = Some(false),
                OP_SET_CURSOR_RECTANGLE => {
                    if let [
                        Argument::Int(x),
                        Argument::Int(y),
                        Argument::Int(w),
                        Argument::Int(h),
                    ] = msg.args[..]
                    {
                        pending.rect = Some((x, y, w, h));
                    }
                }
                _ => {}
            }
            (msg.opcode == OP_COMMIT).then(|| std::mem::take(&mut *pending))
        };
        if let Some(pending) = committed {
            data.text_caret_committed(&msg.sender_id, pending.enable, pending.rect);
        }
        // smithay's handler finds its own state by downcasting the object's
        // data, so for the length of the call the object carries smithay's
        // data again. A destroyed object keeps it: there is nothing left to tap.
        let id = msg.sender_id.clone();
        let destroy = msg.opcode == OP_DESTROY;
        let inner = self.inner.clone();
        if handle
            .set_object_data::<DriftWm>(id.clone(), inner.clone())
            .is_err()
        {
            return None;
        }
        let new_object = inner.request(handle, data, client_id, msg);
        if !destroy {
            let _ = handle.set_object_data::<DriftWm>(id, self);
        }
        new_object
    }

    fn destroyed(
        self: Arc<Self>,
        handle: &Handle,
        data: &mut DriftWm,
        client_id: ClientId,
        object_id: ObjectId,
    ) {
        self.inner
            .clone()
            .destroyed(handle, data, client_id, object_id);
    }
}

impl DriftWm {
    /// Bring the text-input plumbing in line with the config and tap the
    /// focused client's text-input objects. Cheap and idempotent: called on
    /// focus changes, on config reload and once per event-loop iteration,
    /// because a client may create its text-input object at any point.
    ///
    /// `late` says the focus is already settled: smithay sends `enter` when
    /// the focus arrives, so a text-input object first seen after that was
    /// created too late to hear it, and is told here.
    pub(crate) fn sync_text_caret(&mut self, late: bool) {
        let active = self.config.effects.text_cursor.active();
        let text_input = self.seat.text_input();
        // With a real input method smithay does its own late `enter`.
        let enter_late = late
            && text_input.compositor_input_method()
            && !self.seat.input_method().has_instance();
        // Without an input method smithay never tells clients that a text field
        // has focus, and they in turn never report a caret. Standing in as the
        // input method is what makes them report it; the compositor commits no
        // text of its own.
        text_input.set_compositor_input_method(active);
        if !active {
            self.text_caret.clear();
            return;
        }

        let mut ids = Vec::new();
        text_input.with_focused_text_input(|ti, surface| ids.push((ti.clone(), surface.clone())));
        let handle = self.display_handle.backend_handle();
        for (ti, surface) in ids {
            let id = ti.id();
            let tapped = handle
                .get_object_data_any(id.clone())
                .is_ok_and(|data| data.is::<CaretTap>());
            if tapped {
                continue;
            }
            let Ok(inner) = handle.get_object_data::<DriftWm>(id.clone()) else {
                continue;
            };
            let tap = Arc::new(CaretTap {
                inner,
                pending: Mutex::default(),
            });
            if handle.set_object_data::<DriftWm>(id, tap).is_ok() && enter_late {
                ti.enter(&surface);
            }
        }
    }

    /// The keyboard focus moved: whatever caret was being followed is gone.
    pub(crate) fn text_caret_focus_changed(&mut self, focused: Option<&WlSurface>) {
        if self.text_caret.surface.as_ref() != focused {
            if self.text_caret.anim.visible() {
                self.mark_all_dirty();
            }
            self.text_caret.clear();
        }
        self.sync_text_caret(false);
    }

    pub(crate) fn text_caret_scrolled(&mut self) {
        self.text_caret.last_scroll = Some(Instant::now());
    }

    fn text_caret_committed(
        &mut self,
        text_input: &ObjectId,
        enable: Option<bool>,
        rect: Option<(i32, i32, i32, i32)>,
    ) {
        let cfg = &self.config.effects.text_cursor;
        if !cfg.active() {
            return;
        }
        // Only the focused client's caret counts, as for smithay itself.
        let Some(focus) = self
            .seat
            .text_input()
            .focus()
            .filter(|focus| focus.id().same_client_as(text_input))
        else {
            return;
        };
        if enable == Some(false) {
            if self.text_caret.anim.visible() {
                self.mark_dirty_for_surface(&focus);
            }
            self.text_caret.clear();
            return;
        }
        let Some((x, y, w, h)) = rect else {
            return;
        };
        let Some(rect) = CaretRect::from_client(x, y, w, h) else {
            self.text_caret.clear();
            return;
        };
        let excluded = self
            .window_for_surface(&focus)
            .and_then(|w| w.app_id_or_class())
            .is_some_and(|app_id| cfg.excludes(&app_id));
        if excluded {
            self.text_caret.clear();
            return;
        }

        let cfg = cfg.clone();
        let caret = &mut self.text_caret;
        if caret.surface.as_ref() != Some(&focus) {
            caret.anim.reset();
            caret.surface = Some(focus.clone());
        }
        let scrolling = caret
            .last_scroll
            .is_some_and(|at| at.elapsed() < SCROLL_QUIET);
        if scrolling {
            caret.anim.reset();
        }
        if caret.anim.set_target(rect, &cfg) {
            // The springs integrate from here, not from whenever the last
            // animation frame happened to be.
            self.last_animation_tick = Instant::now();
            self.mark_dirty_for_surface(&focus);
        }
    }

    pub(crate) fn tick_text_caret(&mut self, dt: Duration) {
        if !self.text_caret.anim.visible() {
            return;
        }
        let Some(surface) = self.text_caret.surface.clone().filter(|s| s.is_alive()) else {
            self.text_caret.clear();
            return;
        };
        self.text_caret.anim.step(dt.as_secs_f32());
        // Also on the frame it goes away, so the last picture is erased.
        self.mark_dirty_for_surface(&surface);
    }
}
