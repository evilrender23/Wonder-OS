//! The fallback context menu: a plain right-click the app under it did not
//! answer runs `[mouse] context_menu`, so a shell can draw a menu there.
//!
//! Wayland has no way to ask a client whether it shows a menu, so the click is
//! always forwarded untouched and the client is watched instead. It answered if
//! it opened a popup or a window, or repainted the clicked surface (a menu it
//! draws inside its own buffer). Only silence past the delay runs the command.
//! Empty canvas and an SSD title bar have no client to wait for.

use std::time::Duration;

use driftwm::canvas::{CanvasPos, canvas_to_screen};
use driftwm::window_ext::WindowExt;
use smithay::desktop::Window;
use smithay::reexports::calloop::RegistrationToken;
use smithay::reexports::calloop::timer::{TimeoutAction, Timer};
use smithay::reexports::wayland_server::Resource;
use smithay::reexports::wayland_server::protocol::wl_surface::WlSurface;
use smithay::utils::{IsAlive, Logical, Point};
use smithay::wayland::compositor::get_parent;

use crate::state::DriftWm;

/// A longer press is a hold, not a click.
const CLICK_MAX_MS: u32 = 500;
/// Screen px the pointer may travel between press and release.
const CLICK_SLOP: f64 = 8.0;

pub struct PendingContextMenu {
    press_pos: Point<f64, Logical>,
    press_time: u32,
    window: Option<Window>,
    /// Root of the surface that received the click; `None` when no client did.
    client_root: Option<WlSurface>,
    timer: Option<RegistrationToken>,
}

fn root_of(surface: &WlSurface) -> WlSurface {
    let mut root = surface.clone();
    while let Some(parent) = get_parent(&root) {
        root = parent;
    }
    root
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
        let client_root = pointer.current_focus().map(|focus| root_of(&focus.0));
        // A client took the click but it is no window: a canvas layer or
        // widget, which owns its own right-click.
        if client_root.is_some() && window.as_ref().is_none_or(|w| w.is_widget()) {
            return;
        }
        self.pending_context_menu = Some(PendingContextMenu {
            press_pos: pointer.current_location(),
            press_time: time,
            window,
            client_root,
            timer: None,
        });
    }

    pub(crate) fn cancel_context_menu(&mut self) {
        if let Some(pending) = self.pending_context_menu.take()
            && let Some(token) = pending.timer
        {
            self.loop_handle.remove(token);
        }
    }

    /// Called once the right release has been forwarded.
    pub(super) fn release_context_menu(&mut self, time: u32) {
        let slop = CLICK_SLOP / self.zoom();
        let Some(pending) = self.pending_context_menu.as_ref() else {
            return;
        };
        if pending.timer.is_some() {
            return;
        }
        let travelled = self.pointer.current_location() - pending.press_pos;
        if time.wrapping_sub(pending.press_time) > CLICK_MAX_MS
            || travelled.x.abs() > slop
            || travelled.y.abs() > slop
        {
            self.pending_context_menu = None;
            return;
        }
        if pending.client_root.is_none() {
            self.fire_context_menu();
            return;
        }
        let timer = Timer::from_duration(Duration::from_millis(self.config.context_menu_delay));
        let inserted = self
            .loop_handle
            .insert_source(timer, |_, _, data: &mut DriftWm| {
                data.fire_context_menu();
                TimeoutAction::Drop
            });
        match inserted {
            Ok(token) => {
                if let Some(pending) = self.pending_context_menu.as_mut() {
                    pending.timer = Some(token);
                }
            }
            Err(_) => self.pending_context_menu = None,
        }
    }

    /// The client that got the click opened a popup or a window of its own.
    pub(crate) fn context_menu_client_answered(&mut self, surface: &WlSurface) {
        if self
            .pending_context_menu
            .as_ref()
            .and_then(|p| p.client_root.as_ref())
            .is_some_and(|root| root.id().same_client_as(&surface.id()))
        {
            self.cancel_context_menu();
        }
    }

    /// `surface` attached a new buffer: a repaint of the clicked surface tree
    /// is taken as the app drawing its own menu in place.
    pub(crate) fn context_menu_surface_repainted(&mut self, surface: &WlSurface) {
        let Some(root) = self
            .pending_context_menu
            .as_ref()
            .and_then(|p| p.client_root.as_ref())
        else {
            return;
        };
        if *root == root_of(surface) {
            self.cancel_context_menu();
        }
    }

    fn fire_context_menu(&mut self) {
        let Some(pending) = self.pending_context_menu.take() else {
            return;
        };
        let Some(command) = self.config.context_menu.clone() else {
            return;
        };
        if pending.window.as_ref().is_some_and(|w| !w.alive()) {
            return;
        }
        let screen = canvas_to_screen(CanvasPos(pending.press_pos), self.camera(), self.zoom()).0;
        let target = match (&pending.window, &pending.client_root) {
            (None, _) => "canvas",
            (Some(_), None) => "titlebar",
            (Some(_), Some(_)) => "window",
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
