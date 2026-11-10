//! Spaces: a maximized or fullscreened window gets a screen of its own beside
//! the canvas, macOS-style, and `space-left` / `space-right` slide between
//! them.
//!
//! Each output has a row of spaces. The first is the Desktop — the canvas
//! itself — and every further one holds a single window filling the output, in
//! the order the windows were sent there. The space on screen *is* that
//! output's fullscreen entry, so the viewport park, the render path and every
//! exit route are the fullscreen ones, untouched. A space that is not on
//! screen is *parked*: its window is taken out of the fullscreen entry and
//! keeps its stage entry at full-output size, but is drawn nowhere and kept
//! out of every canvas relation through [`DriftWm::hidden_from_canvas`], the
//! same gate that hides a minimized window. What the fullscreen entry
//! remembered — where the window came from, and its pin — is carried in the
//! slot meanwhile, so ending a parked space restores the window exactly as a
//! fullscreen exit would have.
//!
//! Slots exist only while `[spaces] maximize_to_space` is on; with it off no
//! slot is ever made and everything here is a no-op.

use std::time::{Duration, Instant};

use smithay::desktop::{PopupManager, Window};
use smithay::output::Output;
use smithay::reexports::wayland_server::protocol::wl_surface::WlSurface;
use smithay::utils::{IsAlive, Logical, Point, SERIAL_COUNTER, Size};
use smithay::wayland::seat::WaylandFocus;

use driftwm::stage::PinnedSite;
use driftwm::window_ext::{WindowExt, set_space_maximized, space_maximized};

use super::{
    DriftWm, FocusTarget, FullscreenReturn, StageWindow, output_logical_size, output_state,
};

/// How long the slide between two spaces takes.
const SLIDE_DURATION: Duration = Duration::from_millis(350);

/// One space past the Desktop: a window that fills its output.
pub(crate) struct SpaceSlot {
    pub(crate) window: Window,
    /// `None` while this space is on screen, when the stage's fullscreen entry
    /// holds the same facts.
    pub(crate) parked: Option<ParkedSpace>,
}

/// What a fullscreen entry remembers, kept while its space is off screen.
pub(crate) struct ParkedSpace {
    saved_location: Point<i32, Logical>,
    saved_size: Size<i32, Logical>,
    pinned: Option<PinnedSite>,
}

/// A slide between two spaces under way on one output: the picture being
/// left and the one arriving, drawn side by side in place of the scene.
pub(crate) struct SpaceSlide {
    pub(crate) from: crate::render::SpaceSnapshot,
    pub(crate) to: crate::render::SpaceSnapshot,
    /// The arriving space lies to the right, so the pictures move left.
    pub(crate) rightward: bool,
    start: Instant,
}

impl SpaceSlide {
    /// Eased progress, 0 at the start and 1 once the arriving picture has
    /// landed.
    pub(crate) fn progress(&self) -> f64 {
        let t = (self.start.elapsed().as_secs_f64() / SLIDE_DURATION.as_secs_f64()).min(1.0);
        1.0 - (1.0 - t).powi(3)
    }

    fn is_done(&self) -> bool {
        self.start.elapsed() >= SLIDE_DURATION
    }
}

impl DriftWm {
    /// Whether the window rooted at `surface` sits in a space that is not on
    /// screen.
    pub(crate) fn root_space_parked(&self, surface: &WlSurface) -> bool {
        self.spaces.values().flatten().any(|slot| {
            slot.parked.is_some() && slot.window.wl_surface().as_deref() == Some(surface)
        })
    }

    /// [`Self::root_space_parked`] for callers holding an element. Guarded on
    /// the (usually empty) map first, as `is_minimized` is.
    pub(crate) fn is_space_parked<Q: WaylandFocus>(&self, element: &Q) -> bool {
        !self.spaces.is_empty()
            && element
                .wl_surface()
                .is_some_and(|s| self.root_space_parked(&s))
    }

    /// The canvas rect the window rooted at `surface` returns to when its
    /// parked space ends.
    pub(crate) fn parked_space_rect(
        &self,
        surface: &WlSurface,
    ) -> Option<smithay::utils::Rectangle<i32, Logical>> {
        self.spaces.values().flatten().find_map(|slot| {
            let parked = slot.parked.as_ref()?;
            (slot.window.wl_surface().as_deref() == Some(surface))
                .then(|| smithay::utils::Rectangle::new(parked.saved_location, parked.saved_size))
        })
    }

    /// Whether `window` has a space, on screen or parked.
    pub(crate) fn window_in_space(&self, window: &Window) -> bool {
        self.spaces
            .values()
            .flatten()
            .any(|slot| &slot.window == window)
    }

    /// How many spaces `output` has, the Desktop included.
    pub fn space_count_on(&self, output: &Output) -> usize {
        1 + self.spaces.get(&output.name()).map_or(0, Vec::len)
    }

    /// The space on screen on `output`: 0 is the Desktop.
    pub fn active_space_on(&self, output: &Output) -> usize {
        self.spaces
            .get(&output.name())
            .and_then(|slots| slots.iter().position(|slot| slot.parked.is_none()))
            .map_or(0, |i| i + 1)
    }

    /// Whether the active output has any space besides the Desktop.
    pub(crate) fn active_output_has_spaces(&self) -> bool {
        self.active_output()
            .is_some_and(|o| self.space_count_on(&o) > 1)
    }

    /// `window` just became `output`'s fullscreen entry: give it a space at
    /// the right end of the row. Called by `enter_fullscreen`.
    pub(crate) fn note_space_entered(&mut self, output: &Output, window: &Window) {
        if !self.config.maximize_to_space {
            return;
        }
        self.purge_stale_spaces();
        let slots = self.spaces.entry(output.name()).or_default();
        if !slots.iter().any(|slot| &slot.window == window) {
            slots.push(SpaceSlot {
                window: window.clone(),
                parked: None,
            });
        }
    }

    /// `output`'s fullscreen entry is gone: so is the space it was. Called by
    /// every route that takes the entry.
    pub(crate) fn forget_active_space(&mut self, output_name: &str) {
        if let Some(slots) = self.spaces.get_mut(output_name) {
            slots.retain(|slot| slot.parked.is_some());
            if slots.is_empty() {
                self.spaces.remove(output_name);
            }
        }
    }

    /// Drop the slot of the window rooted at `surface`, whatever its state,
    /// restoring nothing: the window is going away.
    pub(crate) fn forget_space_of_surface(&mut self, surface: &WlSurface) {
        if self.spaces.is_empty() {
            return;
        }
        for slots in self.spaces.values_mut() {
            slots.retain(|slot| slot.window.wl_surface().as_deref() != Some(surface));
        }
        self.spaces.retain(|_, slots| !slots.is_empty());
    }

    /// Drop slots whose window died, and on-screen slots the fullscreen entry
    /// no longer backs.
    pub(crate) fn purge_stale_spaces(&mut self) {
        if self.spaces.is_empty() {
            return;
        }
        let stage = &self.stage;
        for (name, slots) in &mut self.spaces {
            slots.retain(|slot| {
                slot.window.alive()
                    && (slot.parked.is_some()
                        || stage
                            .fullscreen_on(name)
                            .is_some_and(|fs| fs.window == slot.window))
            });
        }
        self.spaces.retain(|_, slots| !slots.is_empty());
    }

    /// Take the space on screen on `output` off it, back to the canvas view,
    /// keeping the space. Returns its window. Focus is the caller's.
    fn park_active_space(&mut self, output: &Output) -> Option<Window> {
        let name = output.name();
        let index = self
            .spaces
            .get(&name)?
            .iter()
            .position(|slot| slot.parked.is_none())?;
        let ret = output_state(output).fullscreen_return.take();
        let entry = self.stage.take_fullscreen(&name);
        let (Some(ret), Some(entry)) = (ret, entry) else {
            self.forget_active_space(&name);
            return None;
        };
        let window = self.spaces.get(&name)?.get(index)?.window.clone();
        if let Some(slot) = self.spaces.get_mut(&name).and_then(|s| s.get_mut(index)) {
            slot.parked = Some(ParkedSpace {
                saved_location: entry.saved_location,
                saved_size: entry.saved_size,
                pinned: ret.pinned,
            });
        }
        // An entry still growing into the output has nowhere left to grow.
        self.cancel_window_animation(&window);
        if let Some(surface) = window.wl_surface() {
            for (popup, _) in PopupManager::popups_for_surface(&surface) {
                let _ = PopupManager::dismiss_popup(&surface, &popup);
            }
        }
        // Out of the focus cycle while it cannot be seen.
        self.stage.drop_from_focus_history(&window);
        self.restore_fullscreen_view(output, ret.camera, ret.zoom);
        Some(window)
    }

    /// Put parked slot `index` (0-based) of `output` on screen: the fullscreen
    /// park, minus the configure and the grow — the window never stopped
    /// filling the output.
    fn unpark_space(&mut self, output: &Output, index: usize) {
        let name = output.name();
        let Some(slot) = self.spaces.get_mut(&name).and_then(|s| s.get_mut(index)) else {
            return;
        };
        let Some(parked) = slot.parked.take() else {
            return;
        };
        let window = slot.window.clone();
        let viewport = output_logical_size(output);
        let (saved_camera, saved_zoom) = {
            let os = output_state(output);
            (os.camera, os.zoom)
        };
        self.stage.set_fullscreen(
            &name,
            window.clone(),
            parked.saved_location,
            parked.saved_size,
        );
        let camera_i32 = {
            let mut os = output_state(output);
            os.fullscreen_return = Some(FullscreenReturn {
                camera: saved_camera,
                zoom: saved_zoom,
                pinned: parked.pinned,
            });
            os.zoom = 1.0;
            os.zoom_target = None;
            os.zoom_animation_anchor = None;
            os.camera_target = None;
            os.momentum.stop();
            os.overview_return = None;
            let camera_i32 = os.camera.to_i32_round();
            os.camera = Point::from((camera_i32.x as f64, camera_i32.y as f64));
            camera_i32
        };
        // The output may have changed mode while the space was away.
        if super::configured_window_size(&window) != viewport {
            window.enter_fullscreen_configure(viewport);
        }
        self.map_window(window.clone(), camera_i32, false);
        self.recentre_fullscreen_window(&window);
        self.raise_window(&window, false);
        self.enforce_below_windows();
        self.update_output_from_camera();

        let serial = SERIAL_COUNTER.next_serial();
        let focus = window.wl_surface().map(|s| FocusTarget(s.into_owned()));
        self.set_window_focus(focus, serial);

        // Keep the cursor at the same on-screen spot across the zoom park, as
        // `enter_fullscreen` does.
        if self.active_output().as_ref() == Some(output) && !self.session_lock.is_locked() {
            let pos = self.pointer.current_location();
            let in_view = pos.x >= saved_camera.x
                && pos.x < saved_camera.x + viewport.w as f64 / saved_zoom
                && pos.y >= saved_camera.y
                && pos.y < saved_camera.y + viewport.h as f64 / saved_zoom;
            if in_view {
                let new_pos = Point::from((
                    (pos.x - saved_camera.x) * saved_zoom + camera_i32.x as f64,
                    (pos.y - saved_camera.y) * saved_zoom + camera_i32.y as f64,
                ));
                if new_pos != pos {
                    self.warp_pointer(new_pos);
                }
            }
        }
    }

    /// Show space `target` (0 = the Desktop) on `output`, sliding there when
    /// `animate` and animations are on. Returns whether anything changed.
    pub fn switch_space(&mut self, output: &Output, target: usize, animate: bool) -> bool {
        self.purge_stale_spaces();
        let active = self.active_space_on(output);
        if target >= self.space_count_on(output) || target == active {
            return false;
        }
        // A fullscreen that is no space (made while the option was off) has no
        // slot to wait in.
        if active == 0 && self.is_output_fullscreen(output) {
            self.exit_fullscreen_on(output);
        }
        let name = output.name();
        self.space_slides.remove(&name);
        let from = if animate && self.config.effects.animation_speed < 1.0 {
            self.snapshot_space(output)
        } else {
            None
        };

        let departed = if active > 0 {
            self.park_active_space(output)
        } else {
            None
        };
        if target > 0 {
            self.unpark_space(output, target - 1);
        } else if let Some(departed) = departed.as_ref() {
            self.focus_after_minimize(departed);
        }
        self.refresh_pointer_focus();

        if let Some(from) = from
            && let Some(to) = self.snapshot_space(output)
        {
            self.space_slides.insert(
                name,
                SpaceSlide {
                    from,
                    to,
                    rightward: target > active,
                    start: Instant::now(),
                },
            );
        }
        self.render.blur_geometry_generation += 1;
        self.mark_all_dirty();
        true
    }

    /// The picture `output` shows right now, for a slide. `None` without a
    /// backend (headless) or when the render fails, which makes the switch
    /// instant.
    fn snapshot_space(&mut self, output: &Output) -> Option<crate::render::SpaceSnapshot> {
        let mut backend = self.backend.take()?;
        let snapshot = crate::render::snapshot_space(self, backend.renderer(), output);
        self.backend = Some(backend);
        snapshot
    }

    /// `space-left` / `space-right` on the active output. No wrap-around.
    pub fn step_space(&mut self, rightward: bool) {
        let Some(output) = self.active_output() else {
            return;
        };
        let active = self.active_space_on(&output);
        let target = if rightward {
            active + 1
        } else if let Some(target) = active.checked_sub(1) {
            target
        } else {
            return;
        };
        self.switch_space(&output, target, true);
    }

    /// Get the fullscreen window on the active output out of the way for
    /// something that wants the canvas: a space steps aside to the Desktop
    /// and stays, a plain fullscreen exits.
    pub fn leave_fullscreen(&mut self) {
        if let Some(output) = self.active_output() {
            self.leave_fullscreen_on(&output);
        }
    }

    /// [`Self::leave_fullscreen`] for a specific output.
    pub fn leave_fullscreen_on(&mut self, output: &Output) {
        if self.active_space_on(output) > 0 {
            self.switch_space(output, 0, false);
        } else {
            self.exit_fullscreen_on(output);
        }
    }

    /// Clear `output`'s fullscreen entry for another window about to take it:
    /// a space is parked and keeps its place in the row, a plain fullscreen
    /// exits.
    pub(crate) fn displace_fullscreen_on(&mut self, output: &Output) {
        if self.active_space_on(output) > 0 && self.park_active_space(output).is_some() {
            return;
        }
        self.exit_fullscreen_on(output);
    }

    /// Bring `window`'s parked space on screen. Returns whether it had one.
    pub(crate) fn activate_parked_space(&mut self, window: &Window) -> bool {
        let found = self.spaces.iter().find_map(|(name, slots)| {
            slots
                .iter()
                .position(|slot| slot.parked.is_some() && &slot.window == window)
                .map(|i| (name.clone(), i))
        });
        let Some((name, index)) = found else {
            return false;
        };
        let Some(output) = self.output_by_name(&name) else {
            return false;
        };
        self.switch_space(&output, index + 1, true);
        true
    }

    /// End `window`'s parked space. With `restore` the window goes back on
    /// the canvas where it came from, as a fullscreen exit puts it; without,
    /// the caller is disposing of the window. Returns whether it had one.
    pub(crate) fn release_parked_space(&mut self, window: &Window, restore: bool) -> bool {
        let mut released = None;
        for slots in self.spaces.values_mut() {
            if let Some(i) = slots
                .iter()
                .position(|slot| slot.parked.is_some() && &slot.window == window)
            {
                released = Some(slots.remove(i));
                break;
            }
        }
        self.spaces.retain(|_, slots| !slots.is_empty());
        let Some(SpaceSlot {
            window,
            parked: Some(parked),
        }) = released
        else {
            return false;
        };
        if !restore {
            set_space_maximized(&window, false);
            return true;
        }
        window.exit_fullscreen_configure(parked.saved_size);
        let bar = self.window_ssd_bar(&window) as f64;
        let center = super::visual_frame_center(parked.saved_location, parked.saved_size, bar);
        self.establish_exit_placement(
            &StageWindow::Client(window.clone()),
            parked.saved_location,
            parked.saved_size,
            center,
            false,
        );
        if let Some(site) = parked.pinned {
            self.stage.drop_from_focus_history(&window);
            self.stage.set_pin(&window, site);
            self.sync_pinned_locs();
        }
        self.render.blur_geometry_generation += 1;
        self.mark_all_dirty();
        true
    }

    /// Restore every parked space of `output` to the canvas: the output is
    /// going away. Its on-screen space is the caller's fullscreen exit.
    pub(crate) fn release_spaces_on(&mut self, output: &Output) {
        let name = output.name();
        self.space_slides.remove(&name);
        let parked: Vec<Window> = self
            .spaces
            .get(&name)
            .into_iter()
            .flatten()
            .filter(|slot| slot.parked.is_some())
            .map(|slot| slot.window.clone())
            .collect();
        for window in &parked {
            self.release_parked_space(window, true);
        }
    }

    /// A maximize of `window`: send it to a space of its own. Returns `false`
    /// when spaces are off or the window can have none, leaving the caller to
    /// fit it on the canvas.
    pub(crate) fn space_maximize(&mut self, window: &Window) -> bool {
        if !self.config.maximize_to_space || window.is_widget() {
            return false;
        }
        if self.window_in_space(window) {
            self.activate_parked_space(window);
            return true;
        }
        // Told it is maximized rather than fullscreen, so it keeps its own
        // toolbars — unless it already holds that state for a fit or fill the
        // exit must leave standing.
        if !self.stage.is_fit(window) && !self.stage.is_fill(window) {
            set_space_maximized(window, true);
        }
        let target = window
            .wl_surface()
            .and_then(|s| self.resolve_fullscreen_output(&s, None));
        self.enter_fullscreen(window, target);
        if !self.window_in_space(window) {
            set_space_maximized(window, false);
        }
        true
    }

    /// An unmaximize of `window`: end its space and put it back on the
    /// canvas. Returns `false` when it has none.
    pub(crate) fn space_unmaximize(&mut self, window: &Window) -> bool {
        if !self.window_in_space(window) {
            return false;
        }
        if self.release_parked_space(window, true) {
            return true;
        }
        if let Some(output) = window
            .wl_surface()
            .and_then(|s| self.find_fullscreen_output_for_surface(&s))
        {
            self.exit_fullscreen_on(&output);
        }
        true
    }

    /// The unmaximize a client or taskbar asks for. Only a window that was
    /// told it is maximized answers; one in real fullscreen ignores it, as it
    /// always has.
    pub(crate) fn space_unmaximize_request(&mut self, window: &Window) -> bool {
        if !self.window_in_space(window) {
            return false;
        }
        if space_maximized(window) {
            self.space_unmaximize(window);
        }
        true
    }

    /// A maximize toggle of `window`. Returns `false` when spaces are off and
    /// it has no space to end.
    pub(crate) fn space_toggle_maximize(&mut self, window: &Window) -> bool {
        if self.space_unmaximize(window) {
            return true;
        }
        // A window already fitted on the canvas toggles back out of the fit.
        !self.stage.is_fit(window) && self.space_maximize(window)
    }

    /// A client asked for real fullscreen: a window maximized into a space
    /// trades the maximized state for it and stays where it is.
    pub(crate) fn upgrade_space_to_fullscreen(&mut self, window: &Window) {
        if !space_maximized(window) || !self.window_in_space(window) {
            return;
        }
        set_space_maximized(window, false);
        if let Some(toplevel) = window.toplevel() {
            use smithay::reexports::wayland_protocols::xdg::shell::server::xdg_toplevel;
            toplevel.with_pending_state(|state| {
                state.states.unset(xdg_toplevel::State::Maximized);
                state.states.set(xdg_toplevel::State::Fullscreen);
            });
            toplevel.send_pending_configure();
        }
    }

    /// Whether a slide is under way on `output`.
    pub(crate) fn space_slide_active(&self, output: &Output) -> bool {
        !self.space_slides.is_empty() && self.space_slides.contains_key(&output.name())
    }

    /// Drop finished slides and ask for the frame that shows the scene again.
    pub(crate) fn tick_space_slides(&mut self) {
        if self.space_slides.is_empty() {
            return;
        }
        let done: Vec<String> = self
            .space_slides
            .iter()
            .filter(|(_, slide)| slide.is_done())
            .map(|(name, _)| name.clone())
            .collect();
        for name in done {
            self.space_slides.remove(&name);
            if let Some(output) = self.output_by_name(&name) {
                self.redraws_needed.insert(output);
            }
        }
    }
}
