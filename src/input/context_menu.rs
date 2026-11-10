//! The desktop context menu: a plain right-click on empty canvas or on one of
//! our title bars runs `[mouse] context_menu`, so a shell can draw a menu there.
//!
//! Inside a window the right-click is the app's alone. Wayland has no way to
//! ask a client whether it shows a menu of its own, and guessing from its
//! silence put ours on top of apps that answer late or draw theirs in place.

use driftwm::canvas::{CanvasPos, canvas_to_screen};
use driftwm::window_ext::WindowExt;
use smithay::desktop::Window;
use smithay::utils::{IsAlive, Logical, Point};

use crate::state::DriftWm;

/// A longer press is a hold, not a click.
const CLICK_MAX_MS: u32 = 500;
/// Screen px the pointer may travel between press and release.
const CLICK_SLOP: f64 = 8.0;

pub struct PendingContextMenu {
    press_pos: Point<f64, Logical>,
    press_time: u32,
    /// The window whose title bar was pressed; `None` on empty canvas.
    window: Option<Window>,
}

impl DriftWm {
    /// Called for a right press about to be forwarded as an ordinary click.
    pub(super) fn arm_context_menu(&mut self, window: Option<Window>, time: u32) {
        self.cancel_context_menu();
        if self.config.context_menu.is_none() {
            return;
        }
        let pointer = self.pointer.clone();
        // Another held button, an open menu this press dismisses, or a session
        // lock: none of them is a fresh click on the thing under the pointer.
        if pointer.is_grabbed() || self.popup_grab.is_some() || self.session_lock.is_locked() {
            return;
        }
        // A client took the click (a window's content, a canvas layer, a
        // widget): it owns its right-click.
        if pointer.current_focus().is_some() {
            return;
        }
        self.pending_context_menu = Some(PendingContextMenu {
            press_pos: pointer.current_location(),
            press_time: time,
            window,
        });
    }

    pub(crate) fn cancel_context_menu(&mut self) {
        self.pending_context_menu = None;
    }

    /// Called once the right release has been forwarded.
    pub(super) fn release_context_menu(&mut self, time: u32) {
        let slop = CLICK_SLOP / self.zoom();
        let Some(pending) = self.pending_context_menu.take() else {
            return;
        };
        let travelled = self.pointer.current_location() - pending.press_pos;
        if time.wrapping_sub(pending.press_time) > CLICK_MAX_MS
            || travelled.x.abs() > slop
            || travelled.y.abs() > slop
        {
            return;
        }
        let Some(command) = self.config.context_menu.clone() else {
            return;
        };
        if pending.window.as_ref().is_some_and(|w| !w.alive()) {
            return;
        }
        let screen = canvas_to_screen(CanvasPos(pending.press_pos), self.camera(), self.zoom()).0;
        let target = if pending.window.is_some() {
            "titlebar"
        } else {
            "canvas"
        };
        let window_id = pending
            .window
            .as_ref()
            .and_then(|w| self.stage.id_of(w))
            .map(|id| id.0.to_string())
            .unwrap_or_default();
        let app_id = pending
            .window
            .as_ref()
            .and_then(|w| w.app_id_or_class())
            .unwrap_or_default();
        let output = self.active_output().map(|o| o.name()).unwrap_or_default();

        let mut env = self.config.child_env.clone();
        env.extend(
            [
                ("DRIFTWM_MENU_TARGET", target.to_string()),
                ("DRIFTWM_MENU_X", (screen.x.round() as i32).to_string()),
                ("DRIFTWM_MENU_Y", (screen.y.round() as i32).to_string()),
                ("DRIFTWM_MENU_OUTPUT", output),
                ("DRIFTWM_MENU_WINDOW", window_id),
                ("DRIFTWM_MENU_APP_ID", app_id),
            ]
            .map(|(key, value)| (key.to_string(), value)),
        );
        crate::state::spawn_command(&command, &env);
    }
}
