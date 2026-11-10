//! Minimized windows: put away without closing, macOS-style, and brought back
//! from the dock, a taskbar, an activation or a keybinding.
//!
//! A minimized window keeps its stage entry — position, id, fit/fill state —
//! so restoring it is the same window at the same place. While minimized it is
//! drawn nowhere and kept out of every canvas relation (hit-testing, snapping,
//! placement, navigation, keyboard focus) through the same gates that hide a
//! window awaiting a deferred adopt ([`DriftWm::hidden_from_canvas`]), and it
//! leaves the focus cycle. The client is told with the xdg-toplevel
//! `suspended` state, so it can stop drawing until it comes back.
//!
//! A window minimizes together with its dialogs: the group is the topmost
//! ancestor plus every descendant, so a modal never floats alone over the
//! canvas while its parent is in the dock.

use smithay::desktop::{PopupManager, Window};
use smithay::output::Output;
use smithay::reexports::wayland_protocols::xdg::shell::server::xdg_toplevel;
use smithay::reexports::wayland_server::Resource as _;
use smithay::reexports::wayland_server::protocol::wl_surface::WlSurface;
use smithay::utils::{Logical, Point, SERIAL_COUNTER};
use smithay::wayland::seat::WaylandFocus;

use driftwm::stage::StageElement;
use driftwm::window_ext::WindowExt;

use super::{DriftWm, output_logical_size, output_state};
use crate::surface_tree::focus_belongs_to_toplevel;

/// How small the picture gets on its way into the dock, as a fraction of the
/// window: about an icon's worth of a typical window.
const MINIMIZE_SCALE: f64 = 0.08;

/// How far above the output's bottom edge the minimize flight lands — the
/// middle of a bottom dock.
const DOCK_TARGET_ABOVE_BOTTOM: f64 = 40.0;

/// A parent chain longer than this is a cycle a client built on purpose.
const MAX_PARENT_DEPTH: usize = 32;

impl DriftWm {
    /// Whether the window rooted at `surface` is minimized.
    pub(crate) fn root_minimized(&self, surface: &WlSurface) -> bool {
        self.minimized.iter().any(|s| s == surface)
    }

    /// [`Self::root_minimized`] for callers holding an element. Guarded on the
    /// (usually empty) list first, so the per-motion hit-test walks pay nothing.
    pub(crate) fn is_minimized<Q: WaylandFocus>(&self, element: &Q) -> bool {
        !self.minimized.is_empty()
            && element
                .wl_surface()
                .is_some_and(|s| self.root_minimized(&s))
    }

    /// Placed on the stage but not on the canvas: held back for a deferred
    /// adopt, minimized, or waiting in a space that is not on screen. Either
    /// way nothing is drawn where it sits, so nothing may hit-test, snap to,
    /// place against, navigate to, or focus it.
    pub(crate) fn hidden_from_canvas<Q: WaylandFocus>(&self, element: &Q) -> bool {
        self.hidden_by_deferred_adopt(element)
            || self.is_minimized(element)
            || self.is_space_parked(element)
            || self.hidden_in_tab_group(element)
    }

    /// [`Self::hidden_from_canvas`] for a root surface.
    pub(crate) fn root_hidden_from_canvas(&self, surface: &WlSurface) -> bool {
        self.root_hidden_by_deferred_adopt(surface)
            || self.root_minimized(surface)
            || self.root_space_parked(surface)
            || self.root_hidden_in_tab_group(surface)
    }

    /// `window`'s topmost ancestor followed by all of its descendants,
    /// breadth-first: what minimizes and restores as one.
    pub(crate) fn minimize_group(&self, window: &Window) -> Vec<Window> {
        let mut root = window.clone();
        for _ in 0..MAX_PARENT_DEPTH {
            let Some(parent) = root
                .parent_surface()
                .and_then(|p| self.window_for_surface(&p))
            else {
                break;
            };
            root = parent;
        }
        let mut group = vec![root];
        let mut i = 0;
        while i < group.len() {
            let parent = group[i].clone();
            for w in self.stage.windows().filter_map(|w| w.client()) {
                if !group.contains(w) && w.is_child_of(&parent) {
                    group.push(w.clone());
                }
            }
            i += 1;
        }
        group
    }

    /// Minimize `window` together with its dialogs. Fullscreen ends first and
    /// a screen pin returns to the canvas, so what comes back is a plain canvas
    /// window the dock can list. Focus moves to the next window in view.
    pub fn minimize_window(&mut self, window: &Window) -> Result<(), String> {
        let group = self.minimize_group(window);
        let root = group[0].clone();
        if root.is_widget() {
            return Err("widgets can't be minimized".to_string());
        }
        if self.hidden_by_deferred_adopt(&root) {
            return Err("this window is not on screen yet".to_string());
        }
        if self.is_minimized(&root) {
            return Ok(());
        }

        let focus = self.keyboard.current_focus();
        let had_focus = focus.as_ref().is_some_and(|f| {
            group.iter().any(|w| {
                w.wl_surface()
                    .is_some_and(|s| focus_belongs_to_toplevel(&f.0, &s))
            })
        });

        // The flight is taken from the picture as it stands, before the
        // fullscreen exit or unpin below reshapes it.
        for w in &group {
            self.snapshot_minimizing_window(w);
        }
        for w in &group {
            // A space ends with its window going to the dock.
            self.release_parked_space(w, true);
            if let Some(output) = w
                .wl_surface()
                .and_then(|s| self.find_fullscreen_output_for_surface(&s))
            {
                self.exit_fullscreen_on(&output);
            }
            if self.is_pinned(w) {
                self.unpin_window(w, false);
            }
        }
        for w in &group {
            let Some(surface) = w.wl_surface().map(|s| s.into_owned()) else {
                continue;
            };
            self.cancel_window_animation(w);
            for (popup, _) in PopupManager::popups_for_surface(&surface) {
                let _ = PopupManager::dismiss_popup(&surface, &popup);
            }
            self.stage.drop_from_focus_history(w);
            if !self.root_minimized(&surface) {
                self.minimized.push(surface);
            }
            set_client_hidden(w, true);
        }

        if had_focus {
            self.focus_after_minimize(&root);
        }
        self.render.blur_geometry_generation += 1;
        self.mark_all_dirty();
        Ok(())
    }

    /// Bring `window`'s group back where it was, fading in. No focus and no
    /// pan — [`Self::restore_and_activate`] is the user-facing form. Returns
    /// whether anything was minimized.
    pub fn unminimize_window(&mut self, window: &Window) -> bool {
        let group = self.minimize_group(window);
        if !group.iter().any(|w| self.is_minimized(w)) {
            return false;
        }
        for w in &group {
            if let Some(surface) = w.wl_surface() {
                self.minimized.retain(|m| *m != *surface);
            }
            set_client_hidden(w, false);
            // Out of the lamp the window is handed over whole; without one
            // it fades in like a new window.
            if !self.pour_restoring_window(w) {
                self.start_window_open_animation(w);
            }
            // A fullscreen or maximize the client asked for while it was in
            // the dock was queued; it applies now that there is a window to
            // apply it to.
            if let Some(surface) = w.wl_surface().map(|s| s.into_owned()) {
                self.apply_queued_geometry_request(&surface);
                // The lamp was poured from the picture the window had in
                // the dock; one that comes back fullscreen is another shape.
                if self.find_fullscreen_output_for_surface(&surface).is_some() {
                    let id = surface.id();
                    self.closing_snapshots.retain(|s| !s.restores(&id));
                }
            }
        }
        self.render.blur_geometry_generation += 1;
        self.mark_all_dirty();
        true
    }

    /// Restore `window` if it is minimized and activate it: focus, and a pan
    /// when it is not on screen, exactly like an activation request.
    pub fn restore_and_activate(&mut self, window: &Window) {
        let root = self.minimize_group(window).swap_remove(0);
        self.unminimize_window(&root);
        self.activate_window_output_local(&root);
    }

    /// Restore the most recently minimized window.
    pub fn restore_last_minimized(&mut self) {
        let window = self
            .minimized
            .iter()
            .rev()
            .find_map(|s| self.window_for_surface(s));
        if let Some(window) = window {
            self.restore_and_activate(&window);
        }
    }

    /// Hand the keyboard to the window the user is most likely to want once
    /// `departed` has gone into the dock: the most recent one still in view on
    /// its output, else the nearest visible one, else nothing.
    pub(crate) fn focus_after_minimize(&mut self, departed: &Window) {
        let home = self
            .output_for_window(departed)
            .or_else(|| self.active_output());
        let from = self.window_visual_center(departed);
        let target = home.as_ref().and_then(|out| {
            let mru = self
                .stage
                .focus_history()
                .iter()
                .filter_map(|w| w.client())
                .find(|w| !self.hidden_from_canvas(*w))
                .cloned();
            mru.filter(|m| self.window_intersects_viewport_on(m, out))
                .or_else(|| from.and_then(|c| self.nearest_visible_window_on(c, out, None)))
        });
        let serial = SERIAL_COUNTER.next_serial();
        match target {
            Some(target) => {
                self.raise_and_focus(&target, serial);
                // As in `navigate_to_window_on`: this sets focus on purpose.
                self.hover_follows_camera = false;
            }
            None => self.set_window_focus(None, serial),
        }
    }

    /// The magic lamp's duration in seconds, or `None` when minimize should
    /// not use it: switched off, or animations are instant.
    fn lamp_duration(&self) -> Option<f64> {
        let effects = &self.config.effects;
        (effects.magic_lamp.enabled && effects.animation_speed < 1.0)
            .then_some(effects.magic_lamp.duration_ms / 1000.0)
    }

    /// Whether `window` is still inside the lamp on its way back from the
    /// dock. Until that flight lands the live window is not drawn: the
    /// flight's picture is the window.
    pub(crate) fn poured_by_lamp<Q: WaylandFocus>(&self, element: &Q) -> bool {
        !self.closing_snapshots.is_empty()
            && element.wl_surface().is_some_and(|s| {
                let id = s.id();
                self.closing_snapshots.iter().any(|snap| snap.restores(&id))
            })
    }

    /// Bring `window` back out of the dock through the lamp. A minimize still
    /// in flight is turned around where it is; otherwise the window's picture
    /// is taken now and poured backwards. Returns whether a flight carries
    /// the window, in which case it must not also fade in.
    fn pour_restoring_window(&mut self, window: &Window) -> bool {
        let Some(duration) = self.lamp_duration() else {
            return false;
        };
        let Some(surface) = window.wl_surface().map(|s| s.into_owned()) else {
            return false;
        };
        let id = surface.id();
        if let Some(flight) = self.closing_snapshots.iter_mut().find(|s| s.pours(&id)) {
            flight.set_restoring(true);
            return true;
        }
        let Some(output) = self.output_for_window(window) else {
            return false;
        };
        let target = dock_target(&output, false);
        let before = self.closing_snapshots.len();
        self.snapshot_closing_window(window, &surface, None, Point::default(), false);
        if self.closing_snapshots.len() > before
            && let Some(snapshot) = self.closing_snapshots.last_mut()
        {
            snapshot.pour_to(target, id, duration, true);
            return true;
        }
        false
    }

    /// Fade `window`'s current picture down into the dock: poured through the
    /// magic lamp, or with it off the close fade, shrunk to icon size and
    /// flown to the bottom middle of its output.
    fn snapshot_minimizing_window(&mut self, window: &Window) {
        let Some(surface) = window.wl_surface().map(|s| s.into_owned()) else {
            return;
        };
        // Minimized again on its way out of the dock: the same flight turns
        // back, from where it is.
        let id = surface.id();
        if let Some(flight) = self.closing_snapshots.iter_mut().find(|s| s.pours(&id)) {
            flight.set_restoring(false);
            return;
        }
        let fs_output = self.find_fullscreen_output_for_surface(&surface);
        let pin_output = self
            .stage
            .pin_of(window)
            .and_then(|site| self.output_by_name(&site.output));
        let screen_space = fs_output.is_some() || pin_output.is_some();
        let Some(output) = fs_output
            .clone()
            .or(pin_output)
            .or_else(|| self.output_for_window(window))
        else {
            return;
        };
        let target = dock_target(&output, screen_space);
        let centre = self.fullscreen_centre_of(fs_output.as_ref());
        let lamp_duration = self.lamp_duration();
        let before = self.closing_snapshots.len();
        self.snapshot_closing_window(window, &surface, fs_output.as_ref(), centre, false);
        if self.closing_snapshots.len() > before
            && let Some(snapshot) = self.closing_snapshots.last_mut()
        {
            match lamp_duration {
                Some(duration) => snapshot.pour_to(target, id, duration, false),
                None => snapshot.fly_to(target, MINIMIZE_SCALE),
            }
        }
    }
}

/// Where a minimize flight on `output` lands: the middle of its bottom edge,
/// in that output's screen space or, for a canvas window, on the canvas under
/// its current camera.
fn dock_target(output: &Output, screen_space: bool) -> Point<f64, Logical> {
    let size = output_logical_size(output);
    let screen = Point::from((
        size.w as f64 / 2.0,
        size.h as f64 - DOCK_TARGET_ABOVE_BOTTOM,
    ));
    if screen_space {
        return screen;
    }
    let (camera, zoom) = {
        let os = output_state(output);
        (os.camera, os.zoom)
    };
    driftwm::canvas::screen_to_canvas(driftwm::canvas::ScreenPos(screen), camera, zoom).0
}

/// Tell the client whether it is out of sight. Hiding also drops the
/// activated hint: a toolkit that minimized itself only clears its own
/// minimized flag on a configure carrying `Activated`, which the restore's
/// focus then sends as a real change.
pub(super) fn set_client_hidden(window: &Window, hidden: bool) {
    let Some(toplevel) = window.toplevel() else {
        return;
    };
    let changed = toplevel.with_pending_state(|state| {
        if hidden {
            let suspended = state.states.set(xdg_toplevel::State::Suspended);
            let deactivated = state.states.unset(xdg_toplevel::State::Activated);
            suspended || deactivated
        } else {
            state.states.unset(xdg_toplevel::State::Suspended)
        }
    });
    if changed && toplevel.is_initial_configure_sent() {
        toplevel.send_pending_configure();
    }
}
