//! Tab groups: server-decorated windows of one app sharing a single frame.
//!
//! Exactly one member of a group — the active tab — is on the canvas. The
//! others keep their stage entry but are hidden through the same gate
//! minimized windows use (`hidden_from_canvas`), so render, hit-testing, focus,
//! snapping and navigation all skip them without knowing about tabs.

use smithay::desktop::{PopupManager, Window};
use smithay::reexports::wayland_protocols::xdg::shell::server::xdg_toplevel;
use smithay::reexports::wayland_server::Resource;
use smithay::reexports::wayland_server::protocol::wl_surface::WlSurface;
use smithay::utils::{Logical, Point, SERIAL_COUNTER, Size};
use smithay::wayland::seat::WaylandFocus;

use driftwm::window_ext::WindowExt;

use super::minimize::set_client_hidden;
use super::{DriftWm, StageWindow};
use crate::decorations::DecorationKey;
use crate::surface_tree::focus_belongs_to_toplevel;

/// Windows sharing one frame, in tab order.
#[derive(Debug, Clone)]
pub(crate) struct TabGroup {
    /// Stable for the group's lifetime; what IPC reports as `tab_group`.
    pub id: u64,
    pub members: Vec<WlSurface>,
    /// Index into `members` of the one window on the canvas.
    pub active: usize,
}

impl DriftWm {
    /// Whether `surface` is put away behind another tab of its group — as a
    /// hidden member, or as a dialog of one.
    pub(crate) fn root_hidden_in_tab_group(&self, surface: &WlSurface) -> bool {
        self.tab_hidden.iter().any(|s| s == surface)
    }

    pub(crate) fn hidden_in_tab_group<Q: WaylandFocus>(&self, element: &Q) -> bool {
        !self.tab_hidden.is_empty()
            && element
                .wl_surface()
                .is_some_and(|s| self.root_hidden_in_tab_group(&s))
    }

    /// `(group index, member index)` of a grouped surface.
    pub(crate) fn tab_group_index_of(&self, surface: &WlSurface) -> Option<(usize, usize)> {
        self.tab_groups.iter().enumerate().find_map(|(gi, group)| {
            group
                .members
                .iter()
                .position(|m| m == surface)
                .map(|mi| (gi, mi))
        })
    }

    /// `(group id, is the active tab)` of a grouped surface, for IPC.
    pub(crate) fn tab_membership_of(&self, surface: &WlSurface) -> Option<(u64, bool)> {
        let (gi, mi) = self.tab_group_index_of(surface)?;
        let group = &self.tab_groups[gi];
        Some((group.id, group.active == mi))
    }

    /// `(tab count, active index)` of the frame `surface` fronts. `None` for a
    /// window on its own and for a hidden member, which fronts nothing.
    pub(crate) fn tab_bar_of(&self, surface: &WlSurface) -> Option<(usize, usize)> {
        let (gi, mi) = self.tab_group_index_of(surface)?;
        let group = &self.tab_groups[gi];
        (group.active == mi).then_some((group.members.len(), group.active))
    }

    /// The tab titles of the frame `surface` fronts, with the active index.
    pub(crate) fn tab_titles_of(&self, surface: &WlSurface) -> Option<(Vec<String>, usize)> {
        let (gi, mi) = self.tab_group_index_of(surface)?;
        let group = &self.tab_groups[gi];
        if group.active != mi {
            return None;
        }
        let titles = group
            .members
            .iter()
            .map(|member| {
                self.window_for_surface(member)
                    .and_then(|w| w.window_title().or_else(|| w.app_id_or_class()))
                    .unwrap_or_default()
            })
            .collect();
        Some((titles, group.active))
    }

    /// Whether `window` may sit in a shared frame: a root window wearing our
    /// title bar, on the canvas in the plain sense.
    fn tab_eligible(&self, window: &Window) -> bool {
        let Some(surface) = window.wl_surface() else {
            return false;
        };
        self.decorations
            .contains_key(&DecorationKey::Surface(surface.id()))
            && window.parent_surface().is_none()
            && !window.is_widget()
            && !self.is_pinned(window)
            && self.find_fullscreen_output_for_surface(&surface).is_none()
            && window.app_id_or_class().is_some_and(|id| !id.is_empty())
    }

    /// Put a freshly placed `window` into the frame of an already open window
    /// of the same app, as a new active tab at the end. Returns whether it
    /// joined one.
    pub(crate) fn auto_group_new_window(&mut self, window: &Window) -> bool {
        if !self.config.decorations.tabs || !self.tab_eligible(window) {
            return false;
        }
        let Some(surface) = window.wl_surface().map(|s| s.into_owned()) else {
            return false;
        };
        if self.tab_group_index_of(&surface).is_some() {
            return false;
        }
        let Some(app_id) = window.app_id_or_class() else {
            return false;
        };

        let group = self
            .tab_groups
            .iter()
            .position(|group| self.group_takes_new_tab(group, &app_id));
        let gi = match group {
            Some(gi) => gi,
            None => {
                let Some(host) = self.standalone_tab_host(window, &app_id) else {
                    return false;
                };
                let Some(host_surface) = host.wl_surface().map(|s| s.into_owned()) else {
                    return false;
                };
                let id = self.next_tab_group_id;
                self.next_tab_group_id += 1;
                self.tab_groups.push(TabGroup {
                    id,
                    members: vec![host_surface],
                    active: 0,
                });
                self.tab_groups.len() - 1
            }
        };
        self.tab_groups[gi].members.push(surface);
        let index = self.tab_groups[gi].members.len() - 1;
        // The commit that maps the window decides about focus right after.
        self.activate_tab(gi, index, false);
        true
    }

    /// A group of `app_id` whose frame is on the canvas in the plain sense. A
    /// minimized, pinned or fullscreen frame is left alone: a new window
    /// swapping into it would open unseen, or steal the screen.
    fn group_takes_new_tab(&self, group: &TabGroup, app_id: &str) -> bool {
        let Some(front) = group
            .members
            .get(group.active)
            .and_then(|s| self.window_for_surface(s))
        else {
            return false;
        };
        self.tab_eligible(&front)
            && !self.is_minimized(&front)
            && !self.hidden_by_deferred_adopt(&front)
            && group.members.iter().any(|member| {
                self.window_for_surface(member)
                    .and_then(|w| w.app_id_or_class())
                    .is_some_and(|id| id == app_id)
            })
    }

    /// The window on its own that `newcomer` shares a frame with: the most
    /// recently focused eligible one of the same app, else the topmost.
    fn standalone_tab_host(&self, newcomer: &Window, app_id: &str) -> Option<Window> {
        let hosts = |w: &Window| {
            w != newcomer
                && self.tab_eligible(w)
                && !self.hidden_from_canvas(w)
                && w.app_id_or_class().is_some_and(|id| id == app_id)
                && w.wl_surface().is_some_and(|s| {
                    !self.tab_detached.contains(&*s) && self.tab_group_index_of(&s).is_none()
                })
        };
        self.stage
            .focus_history()
            .iter()
            .filter_map(|w| w.client())
            .find(|w| hosts(w))
            .or_else(|| {
                self.stage
                    .windows()
                    .rev()
                    .filter_map(|w| w.client())
                    .find(|w| hosts(w))
            })
            .cloned()
    }

    /// Switch the frame `window` fronts to tab `index`.
    pub(crate) fn activate_tab_of(&mut self, window: &Window, index: usize) {
        let Some(surface) = window.wl_surface() else {
            return;
        };
        if let Some((gi, _)) = self.tab_group_index_of(&surface) {
            self.activate_tab(gi, index, true);
        }
    }

    /// If `window` (or the window its dialog chain hangs off) is a hidden tab,
    /// bring that tab to the front of its frame. Returns whether it switched.
    pub(crate) fn reveal_tab_member(&mut self, window: &Window) -> bool {
        let root = self.minimize_group(window).swap_remove(0);
        let Some(surface) = root.wl_surface() else {
            return false;
        };
        let Some((gi, mi)) = self.tab_group_index_of(&surface) else {
            return false;
        };
        if self.tab_groups[gi].active == mi {
            return false;
        }
        self.activate_tab(gi, mi, true);
        true
    }

    /// Step the focused window's frame to its next or previous tab, wrapping.
    pub(crate) fn cycle_tab(&mut self, backward: bool) {
        let Some(focused) = self.focused_window() else {
            return;
        };
        let root = self.minimize_group(&focused).swap_remove(0);
        let Some((gi, _)) = root.wl_surface().and_then(|s| self.tab_group_index_of(&s)) else {
            return;
        };
        let group = &self.tab_groups[gi];
        let count = group.members.len();
        let next = if backward {
            (group.active + count - 1) % count
        } else {
            (group.active + 1) % count
        };
        self.activate_tab(gi, next, true);
    }

    /// Make member `index` of group `gi` the one on the canvas: it takes the
    /// outgoing member's place and size, and the outgoing one is put away.
    /// `focus` raises and focuses the frame, as a click on a tab does; without
    /// it the frame keeps focus only if it already had it.
    pub(crate) fn activate_tab(&mut self, gi: usize, index: usize, focus: bool) {
        let Some(group) = self.tab_groups.get(gi) else {
            return;
        };
        if index >= group.members.len() || index == group.active {
            return;
        }
        let outgoing_surface = group.members[group.active].clone();
        let incoming_surface = group.members[index].clone();
        let Some(incoming) = self.window_for_surface(&incoming_surface) else {
            return;
        };
        let Some(outgoing) = self.window_for_surface(&outgoing_surface) else {
            self.tab_groups[gi].active = index;
            self.reveal_tab_window(&incoming, None);
            return;
        };

        // The frame goes back to being a plain canvas window first: a hidden
        // member holding a pin or a fullscreen would keep an output hostage.
        if let Some(output) = self.find_fullscreen_output_for_surface(&outgoing_surface) {
            self.exit_fullscreen_on(&output);
        }
        if self.is_pinned(&outgoing) {
            self.unpin_window(&outgoing, false);
        }

        let had_focus = self.keyboard.current_focus().is_some_and(|f| {
            self.minimize_group(&outgoing).iter().any(|w| {
                w.wl_surface()
                    .is_some_and(|s| focus_belongs_to_toplevel(&f.0, &s))
            })
        });
        let frame = self.stage.position_of(&outgoing).map(|pos| {
            let size = self.requested_element_size(&StageWindow::Client(outgoing.clone()));
            (pos, size)
        });
        let fit = self
            .stage
            .fit_saved_size(&outgoing)
            .map(|saved| (saved, self.stage.fit_push(&outgoing)));

        self.tab_groups[gi].active = index;
        // Switching away from a minimized frame brings the frame back with the
        // new tab in it; neither member may stay behind in the dock.
        for member in [&outgoing, &incoming] {
            for w in self.minimize_group(member) {
                if let Some(s) = w.wl_surface() {
                    self.minimized.retain(|m| *m != *s);
                }
            }
        }
        self.put_tab_window_away(&outgoing);
        // The fit moves with the frame, so the outgoing member must not also
        // claim it — nor the cluster it pushed aside.
        self.stage.clear_fit(&outgoing);
        self.rebuild_tab_hidden();

        self.reveal_tab_window(&incoming, frame);
        match fit {
            Some((saved, push)) => {
                self.stage.set_fit(&incoming, saved);
                if let Some(push) = push {
                    self.stage.set_fit_push(&incoming, push);
                }
                if let Some(toplevel) = incoming.toplevel() {
                    toplevel.with_pending_state(|state| {
                        state.states.set(xdg_toplevel::State::Maximized);
                    });
                    toplevel.send_pending_configure();
                }
            }
            None => self.stage.clear_fit(&incoming),
        }

        self.raise_with_children(&StageWindow::Client(incoming.clone()));
        self.enforce_below_windows();
        if focus || had_focus {
            let serial = SERIAL_COUNTER.next_serial();
            self.raise_and_focus(&incoming, serial);
        }
        self.render.blur_geometry_generation += 1;
        self.session_store_mark_dirty();
        self.mark_all_dirty();
    }

    /// Hide `window` and its dialogs behind another tab. The caller rebuilds
    /// `tab_hidden` once the group says who is active.
    fn put_tab_window_away(&mut self, window: &Window) {
        for w in self.minimize_group(window) {
            let Some(surface) = w.wl_surface().map(|s| s.into_owned()) else {
                continue;
            };
            self.cancel_window_animation(&w);
            for (popup, _) in PopupManager::popups_for_surface(&surface) {
                let _ = PopupManager::dismiss_popup(&surface, &popup);
            }
            self.stage.drop_from_focus_history(&w);
            set_client_hidden(&w, true);
        }
    }

    /// Bring `window` and its dialogs back onto the canvas, at `frame` (content
    /// position and size) when the frame it joins has one.
    fn reveal_tab_window(
        &mut self,
        window: &Window,
        frame: Option<(Point<i32, Logical>, Size<i32, Logical>)>,
    ) {
        let family = self.minimize_group(window);
        if let Some((pos, size)) = frame {
            let old = self.stage.position_of(window).unwrap_or(pos);
            let delta = pos - old;
            for w in &family {
                if w == window {
                    continue;
                }
                if let Some(at) = self.stage.position_of(w) {
                    self.stage.set_position(w, at + delta);
                }
            }
            self.cancel_window_animation(window);
            self.drop_owed_recenter(window);
            self.stage.clear_fill(window);
            self.stage.set_position(window, pos);
            if size.w > 0 && size.h > 0 {
                if window.geometry().size != size {
                    self.send_size_configure(window, size);
                }
                // As in `resize_element_to`: `geometry()` is the pre-ack size
                // until the client answers, so the settled footprint and the
                // restore size are written from what was asked for.
                self.cache_stable_snap_rect(window, pos, size);
                self.stage.set_restore_size(window, size);
            }
        }
        for w in &family {
            set_client_hidden(w, false);
        }
    }

    /// Rebuild the derived hidden list from the groups: every inactive member
    /// plus the dialogs hanging off it.
    pub(crate) fn rebuild_tab_hidden(&mut self) {
        let mut hidden = Vec::new();
        for group in &self.tab_groups {
            for (mi, member) in group.members.iter().enumerate() {
                if mi == group.active {
                    continue;
                }
                match self.window_for_surface(member) {
                    Some(window) => hidden.extend(
                        self.minimize_group(&window)
                            .iter()
                            .filter_map(|w| w.wl_surface().map(|s| s.into_owned())),
                    ),
                    None => hidden.push(member.clone()),
                }
            }
        }
        self.tab_hidden = hidden;
    }

    /// Take `surface` out of its group's bookkeeping, dissolving a group left
    /// with a single member. Returns the group's index if it survived.
    fn remove_tab_member(&mut self, gi: usize, mi: usize) {
        let group = &mut self.tab_groups[gi];
        group.members.remove(mi);
        if group.active > mi {
            group.active -= 1;
        }
        if group.members.len() < 2 {
            self.tab_groups.remove(gi);
        } else if group.active >= group.members.len() {
            group.active = group.members.len() - 1;
        }
    }

    /// A grouped window is closing or unmapping. If it fronted the frame, the
    /// next tab (the previous one for the last) takes its place first. Returns
    /// whether the window was in a group.
    ///
    /// A hidden member stays in `tab_hidden` until `cleanup_surface_state`, so
    /// the teardown that follows never fades out a window nobody was looking at.
    pub(crate) fn tab_member_closing(&mut self, surface: &WlSurface) -> bool {
        self.tab_detached.retain(|s| s != surface);
        let Some((gi, mi)) = self.tab_group_index_of(surface) else {
            return false;
        };
        let group = &self.tab_groups[gi];
        let was_active = group.active == mi;
        if was_active {
            let next = if mi + 1 < group.members.len() {
                mi + 1
            } else {
                mi - 1
            };
            self.activate_tab(gi, next, false);
        }
        self.remove_tab_member(gi, mi);
        let closing_hidden = !was_active;
        self.rebuild_tab_hidden();
        if closing_hidden && !self.root_hidden_in_tab_group(surface) {
            self.tab_hidden.push(surface.clone());
        }
        self.mark_all_dirty();
        true
    }

    /// Drop the last traces of a destroyed surface.
    pub(crate) fn forget_tab_surface(&mut self, surface: &WlSurface) {
        if self.tab_group_index_of(surface).is_some() {
            self.tab_member_closing(surface);
        }
        self.tab_detached.retain(|s| s != surface);
        self.tab_hidden.retain(|s| s != surface);
    }

    /// One title-bar height down and right: where a window leaving a frame
    /// reappears relative to it, so both bars stay reachable.
    fn tab_cascade_step(&self) -> Point<i32, Logical> {
        let step = self.config.decorations.title_bar_height.max(1);
        Point::from((step, step))
    }

    /// `window` leaves its group and reappears on its own, one cascade step
    /// off the frame. Returns whether it was grouped.
    pub(crate) fn leave_tab_group(&mut self, window: &Window, focus: bool) -> bool {
        let Some(surface) = window.wl_surface().map(|s| s.into_owned()) else {
            return false;
        };
        let Some((gi, mi)) = self.tab_group_index_of(&surface) else {
            return false;
        };
        let group = &self.tab_groups[gi];
        if group.active == mi {
            let next = if mi + 1 < group.members.len() {
                mi + 1
            } else {
                mi - 1
            };
            self.activate_tab(gi, next, false);
        }
        let front = self.tab_groups[gi]
            .members
            .get(self.tab_groups[gi].active)
            .and_then(|s| self.window_for_surface(s));
        let frame = front.as_ref().and_then(|front| {
            let pos = self.stage.position_of(front)?;
            let size = self.requested_element_size(&StageWindow::Client(front.clone()));
            Some((pos + self.tab_cascade_step(), size))
        });
        self.remove_tab_member(gi, mi);
        self.rebuild_tab_hidden();
        self.stage.clear_fit(window);
        self.reveal_tab_window(window, frame);
        self.raise_with_children(&StageWindow::Client(window.clone()));
        self.enforce_below_windows();
        if focus {
            let serial = SERIAL_COUNTER.next_serial();
            self.raise_and_focus(window, serial);
        }
        self.render.blur_geometry_generation += 1;
        self.session_store_mark_dirty();
        self.mark_all_dirty();
        true
    }

    /// `detach-tab`: the focused window leaves its frame for good — it won't
    /// be picked as a host again, though windows opened later still join the
    /// frame it left.
    pub(crate) fn detach_focused_tab(&mut self) {
        let Some(focused) = self.focused_window() else {
            return;
        };
        let root = self.minimize_group(&focused).swap_remove(0);
        let Some(surface) = root.wl_surface().map(|s| s.into_owned()) else {
            return;
        };
        if self.leave_tab_group(&root, true) && !self.tab_detached.contains(&surface) {
            self.tab_detached.push(surface);
        }
    }

    /// Break every group up, each hidden member reappearing one more cascade
    /// step off its frame. For `tabs = false`.
    pub(crate) fn dissolve_all_tab_groups(&mut self) {
        if self.tab_groups.is_empty() {
            return;
        }
        let groups = std::mem::take(&mut self.tab_groups);
        self.rebuild_tab_hidden();
        let step = self.tab_cascade_step();
        for group in groups {
            let front = group
                .members
                .get(group.active)
                .and_then(|s| self.window_for_surface(s));
            let frame = front.as_ref().and_then(|front| {
                let pos = self.stage.position_of(front)?;
                let size = self.requested_element_size(&StageWindow::Client(front.clone()));
                Some((pos, size))
            });
            let mut offset = Point::<i32, Logical>::default();
            for (mi, member) in group.members.iter().enumerate() {
                if mi == group.active {
                    continue;
                }
                let Some(window) = self.window_for_surface(member) else {
                    continue;
                };
                offset += step;
                self.reveal_tab_window(&window, frame.map(|(pos, size)| (pos + offset, size)));
                self.raise_with_children(&StageWindow::Client(window));
            }
        }
        self.enforce_below_windows();
        self.render.blur_geometry_generation += 1;
        self.session_store_mark_dirty();
        self.mark_all_dirty();
    }
}
