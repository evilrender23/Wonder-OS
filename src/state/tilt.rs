//! 3D windows: which windows are leaning right now.
//!
//! The swing is [`driftwm::tilt`]; this is its bookkeeping. A window gets a
//! model when a move grab first displaces it and loses it once it has been let
//! go and is flat again, so a window nobody is moving is drawn by the ordinary
//! path and costs nothing here. A window held still is flat too: its model
//! waits, idle, and asks for no frames.

use std::collections::HashMap;
use std::time::Duration;

use smithay::reexports::wayland_server::Resource;
use smithay::reexports::wayland_server::backend::ObjectId;
use smithay::reexports::wayland_server::protocol::wl_surface::WlSurface;
use smithay::utils::{Logical, Point};
use smithay::wayland::seat::WaylandFocus;

use driftwm::tilt::{Pose, TiltModel};
use driftwm::window_ext::WindowExt;

use crate::render::warp::{WarpStamp, WarpTarget};

use super::{DriftWm, StageWindow};

pub struct TiltedWindow {
    surface: WlSurface,
    model: TiltModel,
    pub stamp: WarpStamp,
    /// The offscreen the window is flattened into before it is turned, one
    /// per output it is drawn on.
    pub targets: HashMap<String, Option<WarpTarget>>,
}

impl TiltedWindow {
    pub fn pose(&self) -> Pose {
        self.model.pose()
    }
}

#[derive(Default)]
pub struct Tilt {
    windows: HashMap<ObjectId, TiltedWindow>,
}

impl Tilt {
    /// Whether any window has a model, leaning or waiting.
    pub fn is_active(&self) -> bool {
        !self.windows.is_empty()
    }

    /// Whether any window is leaning or about to: frames must keep coming.
    pub fn is_animating(&self) -> bool {
        self.windows.values().any(|w| !w.model.is_idle())
    }

    /// The window's model, if it is leaning: a flat one is drawn plain.
    pub fn leaning_mut(&mut self, surface: &WlSurface) -> Option<&mut TiltedWindow> {
        self.windows
            .get_mut(&surface.id())
            .filter(|w| !w.model.pose().is_flat())
    }

    #[cfg(test)]
    pub fn is_leaning(&self, surface: &WlSurface) -> bool {
        self.windows
            .get(&surface.id())
            .is_some_and(|w| !w.model.pose().is_flat())
    }
}

impl DriftWm {
    /// A move grab took `element` from `from` to `to` (canvas positions of its
    /// origin) while holding it at canvas point `held`.
    pub(crate) fn tilt_dragged(
        &mut self,
        element: &StageWindow,
        from: Option<Point<i32, Logical>>,
        to: Point<i32, Logical>,
        held: Point<f64, Logical>,
    ) {
        let config = &self.config.effects.tilt;
        if !config.enabled {
            return;
        }
        let StageWindow::Client(window) = element else {
            return;
        };
        let (Some(from), Some(surface)) = (from, window.wl_surface().map(|s| s.into_owned()))
        else {
            return;
        };
        let delta = to - from;
        if delta == Point::default() && !self.tilt.windows.contains_key(&surface.id()) {
            return;
        }
        // Pinned windows live in screen space, where a canvas delta is not the
        // distance they moved; fullscreen ones are not dragged at all.
        if self.is_pinned(window)
            || self.stage.is_fullscreen(window)
            || window
                .app_id_or_class()
                .is_some_and(|id| config.excludes(&id))
        {
            self.tilt.windows.remove(&surface.id());
            return;
        }
        let geometry = window.geometry();
        let bar = self.title_bar_height_of(&surface) as f64;
        let (w, h) = (
            (geometry.size.w as f64).max(1.0),
            (geometry.size.h as f64 + bar).max(1.0),
        );
        // `held` against the frame as it is after this move.
        let u = (held.x - (to.x + geometry.loc.x) as f64) / w;
        let v = (held.y - ((to.y + geometry.loc.y) as f64 - bar)) / h;
        let entry = self
            .tilt
            .windows
            .entry(surface.id())
            .or_insert_with(|| TiltedWindow {
                surface,
                model: TiltModel::new(u, v),
                stamp: WarpStamp::default(),
                targets: HashMap::new(),
            });
        if !entry.model.is_held() {
            entry.model.grab(u, v);
        }
        entry.model.moved(delta.x as f64, delta.y as f64);
    }

    /// The hand let go of `element`: it swings back flat.
    pub(crate) fn release_tilt(&mut self, element: &StageWindow) {
        if let Some(surface) = element.wl_surface()
            && let Some(entry) = self.tilt.windows.get_mut(&surface.id())
        {
            entry.model.release();
        }
    }

    pub(crate) fn tick_tilt(&mut self, dt: Duration) {
        if !self.tilt.is_active() {
            return;
        }
        let config = &self.config.effects.tilt;
        let (enabled, strength) = (config.enabled, config.strength);
        let dt = dt.as_secs_f64();
        let mut redraw = false;
        let mut frost = false;
        self.tilt.windows.retain(|_, w| {
            let was_flat = w.model.pose().is_flat();
            let was_idle = w.model.is_idle();
            let keep = enabled && w.surface.is_alive() && w.model.step(dt, strength);
            let is_flat = !keep || w.model.pose().is_flat();
            // Also on the tick it goes flat, so the turned picture is replaced
            // by the plain window.
            redraw |= !was_idle || was_flat != is_flat;
            // A leaning window has no frost; one flat again has it back, from
            // a fresh sample.
            frost |= was_flat != is_flat;
            keep
        });
        if frost {
            self.render.blur_geometry_generation += 1;
        }
        if redraw {
            // Everywhere: a leaning window's near side can reach onto an
            // output its rest rectangle does not touch.
            self.mark_all_dirty();
        }
    }
}
