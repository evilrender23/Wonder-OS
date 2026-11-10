//! Wobbly windows: which windows are jelly right now.
//!
//! The physics is [`driftwm::wobbly`]; this is its bookkeeping. A window gets
//! a spring grid the moment a move grab displaces it and loses it the moment
//! the grid has come to rest, so a window nobody is shaking is drawn by the
//! ordinary path and costs nothing here.

use std::collections::HashMap;
use std::time::Duration;

use smithay::reexports::wayland_server::Resource;
use smithay::reexports::wayland_server::backend::ObjectId;
use smithay::reexports::wayland_server::protocol::wl_surface::WlSurface;
use smithay::utils::{Logical, Point};
use smithay::wayland::seat::WaylandFocus;

use driftwm::window_ext::WindowExt;
use driftwm::wobbly::{Offsets, WobblyModel, WobblyParams};

use crate::render::warp::{WarpStamp, WarpTarget};

use super::{DriftWm, StageWindow};

pub struct WobblyWindow {
    surface: WlSurface,
    model: WobblyModel,
    pub stamp: WarpStamp,
    /// The offscreen the window is flattened into before it is bent, one per
    /// output it is drawn on.
    pub targets: HashMap<String, Option<WarpTarget>>,
}

impl WobblyWindow {
    pub fn offsets(&self) -> Offsets {
        self.model.offsets()
    }

    /// How far, in logical pixels, any point of the window can be from where
    /// it rests.
    pub fn reach(&self) -> f64 {
        self.model.max_stretch()
    }
}

#[derive(Default)]
pub struct Wobbly {
    windows: HashMap<ObjectId, WobblyWindow>,
}

impl Wobbly {
    pub fn is_active(&self) -> bool {
        !self.windows.is_empty()
    }

    pub fn get_mut(&mut self, surface: &WlSurface) -> Option<&mut WobblyWindow> {
        self.windows.get_mut(&surface.id())
    }

    pub fn contains(&self, surface: &WlSurface) -> bool {
        self.windows.contains_key(&surface.id())
    }
}

impl DriftWm {
    /// A move grab took `element` from `from` to `to` (canvas positions of its
    /// origin) while holding it at canvas point `held`. The grid's mass under
    /// the hand goes with the window; the rest are left where they were and
    /// catch up on their springs.
    pub(crate) fn wobble_dragged(
        &mut self,
        element: &StageWindow,
        from: Option<Point<i32, Logical>>,
        to: Point<i32, Logical>,
        held: Point<f64, Logical>,
    ) {
        let config = &self.config.effects.wobbly;
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
        if delta == Point::default() && !self.wobbly.contains(&surface) {
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
            self.wobbly.windows.remove(&surface.id());
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
        let extent = w.max(h);
        let entry = self
            .wobbly
            .windows
            .entry(surface.id())
            .or_insert_with(|| WobblyWindow {
                surface,
                model: WobblyModel::new(u, v, extent),
                stamp: WarpStamp::default(),
                targets: HashMap::new(),
            });
        if !entry.model.is_grabbed() {
            entry.model.grab(u, v, extent);
        }
        entry.model.moved(delta.x as f64, delta.y as f64);
    }

    /// The hand let go of `element`: its grid swings free and settles.
    pub(crate) fn release_wobble(&mut self, element: &StageWindow) {
        if let Some(surface) = element.wl_surface()
            && let Some(entry) = self.wobbly.get_mut(&surface)
        {
            entry.model.release();
        }
    }

    /// Height of the server-side title bar drawn above `surface`, 0 without.
    pub(super) fn title_bar_height_of(&self, surface: &WlSurface) -> i32 {
        if self
            .decorations
            .contains_key(&crate::decorations::DecorationKey::Surface(surface.id()))
        {
            self.config.decorations.title_bar_height
        } else {
            0
        }
    }

    pub(crate) fn tick_wobbly(&mut self, dt: Duration) {
        if !self.wobbly.is_active() {
            return;
        }
        let config = &self.config.effects.wobbly;
        let enabled = config.enabled;
        let params = WobblyParams {
            stiffness: config.stiffness,
            friction: config.friction,
        };
        let dt = dt.as_secs_f64();
        let before = self.wobbly.windows.len();
        self.wobbly
            .windows
            .retain(|_, w| enabled && w.surface.is_alive() && w.model.step(dt, params));
        if self.wobbly.windows.len() != before {
            // A window back at rest is frosted again, from a fresh sample.
            self.render.blur_geometry_generation += 1;
        }
        // Also on the tick the last grid settles, so the bent picture is
        // replaced by the plain window. Everywhere: a stretched window can
        // reach onto an output its rest rectangle does not touch.
        self.mark_all_dirty();
    }

    /// Whether the pointer's motion blur still has a smear to shrink away.
    pub(crate) fn cursor_smear_is_live(&self) -> bool {
        let blur = &self.config.effects.cursor_blur;
        blur.active()
            && self
                .cursor_blur
                .is_live(self.cursor_blur.now(self.start_time), blur.strength)
    }

    /// Keep frames coming while a smear is on screen, the one that erases it
    /// included: the pointer has stopped, so nothing else asks for them.
    pub(crate) fn tick_cursor_blur(&mut self) {
        let live = self.cursor_smear_is_live();
        if live || self.cursor_smear_shown {
            self.mark_all_dirty();
        }
        self.cursor_smear_shown = live;
        if !self.config.effects.cursor_blur.active() {
            self.cursor_blur.clear();
        }
    }
}
