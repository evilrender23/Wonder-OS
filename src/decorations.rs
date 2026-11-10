use smithay::backend::allocator::Fourcc;
use smithay::backend::renderer::element::memory::MemoryRenderBuffer;
use smithay::reexports::wayland_protocols::xdg::shell::server::xdg_toplevel;
use smithay::utils::{Logical, Point, Rectangle, Size, Transform};

use driftwm::app_icon::{AppIcon, IconImage};
use driftwm::config::{DecorationConfig, TitleAlign};

/// A window-control button on the title bar.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Control {
    Close,
    Maximize,
    Minimize,
}

impl Control {
    /// Position counted from the bar's right edge.
    fn slot(self) -> i32 {
        match self {
            Control::Close => 0,
            Control::Maximize => 1,
            Control::Minimize => 2,
        }
    }
}

/// Everything a title bar is rasterised from, besides the config.
#[derive(Debug, Clone, Copy)]
pub struct BarState<'a> {
    pub width: i32,
    pub focused: bool,
    pub hovered: Option<Control>,
    pub scale: i32,
    pub title: &'a str,
    /// Draw the screen-pinned indicator dot ahead of the title.
    pub pinned: bool,
    /// Draw the maximize and minimize buttons. Off for a stand-in, which has
    /// no client to resize or put away.
    pub minimizable: bool,
    /// Draw the maximize button as "restore".
    pub maximized: bool,
    /// Logical px — the caller's decided radius, which is not always the
    /// configured one.
    pub corner_radius: i32,
    /// The app's icon, drawn in the mark slot. `None` draws the fallback
    /// prompt mark — unless `icon_pending`, which leaves the slot empty so a
    /// lookup in flight doesn't flash the fallback for a frame.
    pub icon: Option<&'a IconImage>,
    pub icon_pending: bool,
    /// One label per window sharing this frame, in tab order. Empty for a
    /// window on its own, whose single tab carries `title`.
    pub tabs: &'a [TabLabel<'a>],
}

/// One tab of a frame shared by several windows.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct TabLabel<'a> {
    pub title: &'a str,
    /// The tab whose window is the one on the canvas.
    pub active: bool,
    pub hovered: bool,
}

/// Per-window SSD decoration state.
pub struct WindowDecoration {
    pub title_bar: MemoryRenderBuffer,
    pub width: i32,
    pub focused: bool,
    pub hovered: Option<Control>,
    pub scale: i32,
    pub title: String,
    pub pinned: bool,
    pub minimizable: bool,
    pub maximized: bool,
    /// Corner radius the bar was last rasterised with. Tracked per decoration
    /// because the drawn radius is a render-pass decision (a window covering the
    /// output draws square), not the config value.
    pub corner_radius: i32,
    /// Font-load state at last render: flips a textless bar to re-render once
    /// the background font scan lands.
    fonts_ready: bool,
    /// Icon lookup key (`app_id`, or X11 class); empty draws the fallback mark.
    app_id: String,
    icon: AppIcon,
    /// Titles of the windows sharing this frame, in tab order; empty for a
    /// window on its own.
    tabs: Vec<String>,
    active_tab: usize,
    hovered_tab: Option<usize>,
    /// `set_tabs` changed something the next `update` has to redraw.
    tabs_dirty: bool,
}

/// What the pointer is over in SSD decoration space.
#[derive(Debug, Clone, Copy)]
pub enum DecorationHit {
    TitleBar,
    CloseButton,
    /// The "□" between minimize and close on a client's bar: toggle fit.
    MaximizeButton,
    /// The "–" leftmost of a client's window controls: minimize.
    MinimizeButton,
    ResizeBorder(xdg_toplevel::ResizeEdge),
    /// Suspended-window body outside the centered label — focus + raise.
    Body,
    /// Suspended-window centered label — relaunch the app.
    Label,
    /// An inactive tab of a shared frame, by tab index: switch to it. The
    /// active tab stays `TitleBar`, so it drags the frame like any bar.
    Tab(usize),
}

impl DecorationHit {
    pub fn control(self) -> Option<Control> {
        match self {
            DecorationHit::CloseButton => Some(Control::Close),
            DecorationHit::MaximizeButton => Some(Control::Maximize),
            DecorationHit::MinimizeButton => Some(Control::Minimize),
            _ => None,
        }
    }
}

/// Key for the SSD decoration + border + shadow caches. A client window keys on
/// its surface id; a suspended window keys on its durable id (no surface). One
/// map serves both so the chrome lifecycle stays single-path.
#[derive(Debug, Clone, PartialEq, Eq, Hash)]
pub enum DecorationKey {
    Surface(smithay::reexports::wayland_server::backend::ObjectId),
    Suspended(crate::state::SuspendedId),
}

impl From<smithay::reexports::wayland_server::backend::ObjectId> for DecorationKey {
    fn from(id: smithay::reexports::wayland_server::backend::ObjectId) -> Self {
        DecorationKey::Surface(id)
    }
}

impl From<crate::state::SuspendedId> for DecorationKey {
    fn from(id: crate::state::SuspendedId) -> Self {
        DecorationKey::Suspended(id)
    }
}

impl WindowDecoration {
    pub fn new(width: i32, focused: bool, minimizable: bool, config: &DecorationConfig) -> Self {
        // Placeholder scale + empty title; the first-frame `update()` re-renders
        // at the real output scale and window title.
        let mut deco = Self {
            title_bar: MemoryRenderBuffer::default(),
            width,
            focused,
            hovered: None,
            scale: 1,
            title: String::new(),
            pinned: false,
            minimizable,
            maximized: false,
            corner_radius: config.corner_radius,
            fonts_ready: driftwm::text::fonts_ready(),
            app_id: String::new(),
            icon: AppIcon::Missing,
            tabs: Vec::new(),
            active_tab: 0,
            hovered_tab: None,
            tabs_dirty: false,
        };
        deco.rerender(config);
        deco
    }

    fn rerender(&mut self, config: &DecorationConfig) {
        let tabs: Vec<TabLabel> = self
            .tabs
            .iter()
            .enumerate()
            .map(|(i, title)| TabLabel {
                title,
                active: i == self.active_tab,
                hovered: self.hovered_tab == Some(i),
            })
            .collect();
        self.title_bar = render_title_bar(
            &BarState {
                width: self.width,
                focused: self.focused,
                hovered: self.hovered,
                scale: self.scale,
                title: &self.title,
                pinned: self.pinned,
                minimizable: self.minimizable,
                maximized: self.maximized,
                corner_radius: self.corner_radius,
                icon: match &self.icon {
                    AppIcon::Image(img) => Some(img),
                    _ => None,
                },
                icon_pending: self.icon == AppIcon::Pending,
                tabs: &tabs,
            },
            config,
        );
    }

    /// The tabs this frame shows: one title per window sharing it, or none for
    /// a window on its own. Takes effect on the next `update`, which every
    /// frame's render pass issues right after.
    pub fn set_tabs<S: AsRef<str>>(&mut self, titles: &[S], active: usize) {
        let same = self.active_tab == active
            && self.tabs.len() == titles.len()
            && self.tabs.iter().zip(titles).all(|(a, b)| a == b.as_ref());
        if same {
            return;
        }
        self.tabs = titles.iter().map(|t| t.as_ref().to_string()).collect();
        self.active_tab = active;
        if self.hovered_tab.is_some_and(|i| i >= self.tabs.len()) {
            self.hovered_tab = None;
        }
        self.tabs_dirty = true;
    }

    #[cfg(test)]
    pub fn tab_count(&self) -> usize {
        self.tabs.len().max(1)
    }

    #[cfg(test)]
    pub fn active_tab(&self) -> usize {
        self.active_tab
    }

    pub fn set_hovered_tab(&mut self, hovered: Option<usize>, config: &DecorationConfig) {
        let hovered = hovered.filter(|i| *i < self.tabs.len());
        if self.hovered_tab != hovered {
            self.hovered_tab = hovered;
            self.rerender(config);
        }
    }

    /// Set whose icon the bar shows; picked up by the next [`Self::update`].
    pub fn set_app_id(&mut self, app_id: &str) {
        if self.app_id != app_id {
            self.app_id.clear();
            self.app_id.push_str(app_id);
        }
    }

    fn current_icon(&self, scale: i32, config: &DecorationConfig) -> AppIcon {
        if self.app_id.is_empty() {
            return AppIcon::Missing;
        }
        driftwm::app_icon::lookup(&self.app_id, icon_px(scale, config.title_bar_height))
    }

    /// Re-render if width, focus, pinned, maximized, scale, title, or corner
    /// radius changed. Returns true if rebuilt.
    #[allow(clippy::too_many_arguments)]
    pub fn update(
        &mut self,
        width: i32,
        focused: bool,
        pinned: bool,
        maximized: bool,
        scale: i32,
        title: &str,
        corner_radius: i32,
        config: &DecorationConfig,
    ) -> bool {
        let fonts_ready = driftwm::text::fonts_ready();
        let icon = self.current_icon(scale, config);
        if icon == self.icon
            && width == self.width
            && focused == self.focused
            && pinned == self.pinned
            && maximized == self.maximized
            && scale == self.scale
            && title == self.title
            && corner_radius == self.corner_radius
            && fonts_ready == self.fonts_ready
            && !self.tabs_dirty
        {
            return false;
        }
        self.tabs_dirty = false;
        self.width = width;
        self.focused = focused;
        self.pinned = pinned;
        self.maximized = maximized;
        self.scale = scale;
        self.corner_radius = corner_radius;
        self.fonts_ready = fonts_ready;
        self.icon = icon;
        self.title.clear();
        self.title.push_str(title);
        self.rerender(config);
        true
    }

    /// Move the hover highlight to `hovered`, re-rendering if it changed.
    pub fn set_hovered(&mut self, hovered: Option<Control>, config: &DecorationConfig) {
        if self.hovered != hovered {
            self.hovered = hovered;
            self.rerender(config);
        }
    }
}

/// Bar height the chrome is laid out for. A shorter bar scales the layout
/// down; a taller one keeps it and centres it.
const DESIGN_HEIGHT: f64 = 38.0;
const CONTROL_WIDTH: f64 = 34.0;
const CONTROL_HEIGHT: f64 = 26.0;
/// Gap between the close button and the bar's right edge.
const CONTROLS_RIGHT_PAD: i32 = 8;
/// Where the title tab starts, clear of the mark.
const TAB_LEFT: f64 = 42.0;
const TAB_MIN_WIDTH: f64 = 44.0;
const TAB_MAX_WIDTH: f64 = 210.0;
/// Room kept free right of the tab before the window controls.
const TAB_TRAILING: f64 = 42.0;
/// Points-to-logical-pixels factor (96 dpi reference). `font_size` is
/// configured in points to match GTK/pango font specs.
const PT_TO_PX: f32 = 4.0 / 3.0;
/// Largest side, in buffer pixels, of any chrome buffer rasterized from a
/// window or stand-in size. A client can size its surface without limit, so
/// past this its chrome renders clamped and stretches to fit.
pub const MAX_BUFFER_EXTENT: i32 = 16384;
/// Most pixels a stand-in's body fill is rasterized with (64 MiB). The fill
/// spans both of the stand-in's sides, so a per-side cap alone would still
/// allow a gigabyte; past this it renders at a lower resolution instead.
const MAX_BODY_FILL_PIXELS: f64 = 4096.0 * 4096.0;

fn buffer_extent(logical: i32, s: i32) -> i32 {
    logical.max(1).saturating_mul(s).min(MAX_BUFFER_EXTENT)
}

fn rgba_len(w: i32, h: i32) -> usize {
    usize::try_from(w)
        .ok()
        .zip(usize::try_from(h).ok())
        .and_then(|(w, h)| w.checked_mul(h)?.checked_mul(4))
        // Sides are clamped to MAX_BUFFER_EXTENT, so this can't overflow; a
        // negative side yields an empty buffer.
        .unwrap_or(0)
}

fn design_scale(bar_height: i32) -> f64 {
    (bar_height as f64 / DESIGN_HEIGHT).min(1.0)
}

/// Side, in design units, of the app icon centred on the mark slot — a touch
/// larger than the drawn mark since icon artwork carries its own padding.
const ICON_UNITS: f64 = 24.0;
/// Opacity of an unfocused window's icon, matching the dimmed mark.
const UNFOCUSED_ICON_ALPHA: u32 = 190;

/// Buffer-pixel side of the title-bar icon at `scale`.
pub fn icon_px(scale: i32, bar_height: i32) -> u32 {
    let u = scale.max(1) as f64 * design_scale(bar_height);
    (ICON_UNITS * u).round().clamp(1.0, 1024.0) as u32
}

/// Src-over composite a premultiplied square `icon` centred on `(cx, cy)`,
/// scaled by `alpha` / 255.
fn blit_icon(canvas: &mut Canvas, icon: &IconImage, cx: f64, cy: f64, alpha: u32) {
    let side = icon.size as i32;
    let x0 = (cx - side as f64 / 2.0).round() as i32;
    let y0 = (cy - side as f64 / 2.0).round() as i32;
    for iy in 0..side {
        let y = y0 + iy;
        if y < 0 || y >= canvas.h {
            continue;
        }
        for ix in 0..side {
            let x = x0 + ix;
            if x < 0 || x >= canvas.w {
                continue;
            }
            let si = ((iy * side + ix) * 4) as usize;
            let Some(src) = icon.rgba.get(si..si + 4) else {
                return;
            };
            let sa = src[3] as u32 * alpha / 255;
            if sa == 0 {
                continue;
            }
            let di = ((y * canvas.w + x) * 4) as usize;
            let dst = &mut canvas.pixels[di..di + 4];
            for c in 0..4 {
                let s = if c == 3 {
                    sa
                } else {
                    src[c] as u32 * alpha / 255
                };
                dst[c] = (s + (dst[c] as u32 * (255 - sa) + 127) / 255).min(255) as u8;
            }
        }
    }
}

fn control_size(bar_height: i32) -> Size<i32, Logical> {
    let k = design_scale(bar_height);
    Size::from((
        ((CONTROL_WIDTH * k).round() as i32).max(1),
        ((CONTROL_HEIGHT * k).round() as i32).max(1),
    ))
}

/// A window control's button: vertically centred in the bar, stacked leftwards
/// from the right edge in `Control::slot` order.
pub fn control_rect(
    control: Control,
    window_loc: Point<i32, Logical>,
    width: i32,
    bar_height: i32,
) -> Rectangle<i32, Logical> {
    let size = control_size(bar_height);
    Rectangle::new(
        Point::from((
            window_loc
                .x
                .saturating_add(width)
                .saturating_sub(CONTROLS_RIGHT_PAD + size.w * (control.slot() + 1)),
            window_loc.y - bar_height + (bar_height - size.h) / 2,
        )),
        size,
    )
}

pub fn close_button_rect(
    window_loc: Point<i32, Logical>,
    width: i32,
    bar_height: i32,
) -> Rectangle<i32, Logical> {
    control_rect(Control::Close, window_loc, width, bar_height)
}

pub fn maximize_button_rect(
    window_loc: Point<i32, Logical>,
    width: i32,
    bar_height: i32,
) -> Rectangle<i32, Logical> {
    control_rect(Control::Maximize, window_loc, width, bar_height)
}

pub fn minimize_button_rect(
    window_loc: Point<i32, Logical>,
    width: i32,
    bar_height: i32,
) -> Rectangle<i32, Logical> {
    control_rect(Control::Minimize, window_loc, width, bar_height)
}

/// Whether a bar `width` wide has room for maximize and minimize beside close:
/// the mark and the narrowest tab must still fit, so a narrow window keeps
/// most of its bar to drag by.
pub fn window_controls_fit(width: i32, bar_height: i32) -> bool {
    let k = design_scale(bar_height);
    let lead = ((TAB_LEFT + TAB_MIN_WIDTH + 8.0) * k).ceil() as i32;
    width >= lead + 3 * control_size(bar_height).w + CONTROLS_RIGHT_PAD
}

/// How far an inactive tab's title fades toward the chrome.
const INACTIVE_TAB_DIM: f64 = 0.4;
/// Narrowest a tab gets when several share a bar too tight for
/// `TAB_MIN_WIDTH` each; past this the overflow tabs are not drawn.
const TAB_COMPACT_WIDTH: f64 = 28.0;
/// Gap left between a tab's right edge and the next slot.
const TAB_GAP: f64 = 2.0;
/// How far above the bar's middle a tab starts, in design units.
const TAB_RISE: f64 = 12.0;

/// Horizontal extent of one drawn tab, in the units `tab_slots` was asked in.
#[derive(Debug, Clone, Copy, PartialEq)]
pub struct TabSlot {
    /// Index into the frame's tab order.
    pub index: usize,
    pub left: f64,
    pub right: f64,
}

/// Lay `count` tabs out between the mark and the window controls. The single
/// source of the tab geometry: rendering asks in buffer px (`unit` = scale ×
/// design scale), hit-testing in logical px (`unit` = design scale).
///
/// The room is split equally, each tab capped at `TAB_MAX_WIDTH`. Tabs shrink
/// down to `TAB_COMPACT_WIDTH`; when even that doesn't fit all of them, only a
/// run of tabs containing `active` is returned. A lone tab keeps the stricter
/// `TAB_MIN_WIDTH` threshold, so a narrow bar stays all drag zone.
pub fn tab_slots(controls_left: f64, unit: f64, count: usize, active: usize) -> Vec<TabSlot> {
    let tab_left = TAB_LEFT * unit;
    let room = controls_left - tab_left - TAB_TRAILING * unit;
    if count <= 1 {
        if count == 0 || room < TAB_MIN_WIDTH * unit {
            return Vec::new();
        }
        return vec![TabSlot {
            index: 0,
            left: tab_left,
            right: tab_left + room.min(TAB_MAX_WIDTH * unit) - TAB_GAP * unit,
        }];
    }
    let compact = TAB_COMPACT_WIDTH * unit;
    if unit <= 0.0 || room < compact {
        return Vec::new();
    }
    let shown = count.min((room / compact).floor() as usize).max(1);
    let active = active.min(count - 1);
    let first = (active + 1).saturating_sub(shown).min(count - shown);
    let width = (room / shown as f64).min(TAB_MAX_WIDTH * unit);
    (0..shown)
        .map(|slot| {
            let left = tab_left + width * slot as f64;
            TabSlot {
                index: first + slot,
                left,
                right: left + width - TAB_GAP * unit,
            }
        })
        .collect()
}

/// Left edge of the window controls in bar-local logical px: where the tabs'
/// room ends.
fn controls_left_edge(width: i32, bar_height: i32, minimizable: bool) -> i32 {
    let leftmost = if minimizable && window_controls_fit(width, bar_height) {
        Control::Minimize
    } else {
        Control::Close
    };
    control_rect(leftmost, Point::from((0, bar_height)), width, bar_height)
        .loc
        .x
}

/// The tabs of a client's bar as bar-local logical rects (origin at the bar's
/// top-left), paired with their tab index.
pub fn tab_rects(
    width: i32,
    bar_height: i32,
    count: usize,
    active: usize,
) -> Vec<(usize, Rectangle<f64, Logical>)> {
    let unit = design_scale(bar_height);
    let top = (bar_height as f64 / 2.0 - TAB_RISE * unit).max(0.0);
    let controls_left = controls_left_edge(width, bar_height, true) as f64;
    tab_slots(controls_left, unit, count, active)
        .into_iter()
        .map(|slot| {
            (
                slot.index,
                Rectangle::new(
                    Point::from((slot.left, top)),
                    Size::from((slot.right - slot.left, bar_height as f64 - top)),
                ),
            )
        })
        .collect()
}

/// The tab of a client's bar under `pos`, by tab index. `window_loc` is the
/// content's top-left, as for the control rects.
pub fn tab_at(
    pos: Point<f64, Logical>,
    window_loc: Point<i32, Logical>,
    width: i32,
    bar_height: i32,
    count: usize,
    active: usize,
) -> Option<usize> {
    let local = Point::<f64, Logical>::from((
        pos.x - window_loc.x as f64,
        pos.y - (window_loc.y - bar_height) as f64,
    ));
    tab_rects(width, bar_height, count, active)
        .into_iter()
        .find(|(_, rect)| {
            local.x >= rect.loc.x
                && local.x < rect.loc.x + rect.size.w
                && local.y >= rect.loc.y
                && local.y < rect.loc.y + rect.size.h
        })
        .map(|(index, _)| index)
}

fn rect_contains(rect: Rectangle<i32, Logical>, pos: Point<f64, Logical>) -> bool {
    pos.x >= rect.loc.x as f64
        && pos.x < (rect.loc.x + rect.size.w) as f64
        && pos.y >= rect.loc.y as f64
        && pos.y < (rect.loc.y + rect.size.h) as f64
}

/// Check if a canvas position is within the minimize button. Only a client's
/// bar draws one, so callers ask only for clients.
pub fn minimize_button_contains(
    pos: Point<f64, Logical>,
    window_loc: Point<i32, Logical>,
    width: i32,
    bar_height: i32,
) -> bool {
    window_controls_fit(width, bar_height)
        && rect_contains(minimize_button_rect(window_loc, width, bar_height), pos)
}

/// Check if a canvas position is within the maximize button. Only a client's
/// bar draws one, so callers ask only for clients.
pub fn maximize_button_contains(
    pos: Point<f64, Logical>,
    window_loc: Point<i32, Logical>,
    width: i32,
    bar_height: i32,
) -> bool {
    window_controls_fit(width, bar_height)
        && rect_contains(maximize_button_rect(window_loc, width, bar_height), pos)
}

/// Check if a canvas position is within the title bar (excluding close button).
/// The band runs the window's full width, padding strip and all, because the bar
/// paints across all of it — stopping short would leave a sliver that falls
/// through to whatever is drawn beneath.
pub fn title_bar_contains(
    pos: Point<f64, Logical>,
    window_loc: Point<i32, Logical>,
    width: i32,
    bar_height: i32,
) -> bool {
    let x = pos.x;
    let y = pos.y;
    let bar_top = window_loc.y as f64 - bar_height as f64;
    let bar_bottom = window_loc.y as f64;
    let bar_left = window_loc.x as f64;
    let bar_right = bar_left + width as f64;
    x >= bar_left
        && x < bar_right
        && y >= bar_top
        && y < bar_bottom
        && !close_button_contains(pos, window_loc, width, bar_height)
}

/// Check if a canvas position is within the close button.
pub fn close_button_contains(
    pos: Point<f64, Logical>,
    window_loc: Point<i32, Logical>,
    width: i32,
    bar_height: i32,
) -> bool {
    rect_contains(close_button_rect(window_loc, width, bar_height), pos)
}

/// Hit-test invisible resize borders around the window + title bar.
/// Returns the resize edge if the position is within the border zone.
pub fn resize_edge_at(
    pos: Point<f64, Logical>,
    window_loc: Point<i32, Logical>,
    window_size: Size<i32, Logical>,
    bar_height: i32,
    border_width: i32,
) -> Option<xdg_toplevel::ResizeEdge> {
    let bw = border_width as f64;
    let left = window_loc.x as f64 - bw;
    let right = (window_loc.x + window_size.w) as f64 + bw;
    let top = (window_loc.y - bar_height) as f64 - bw;
    let bottom = (window_loc.y + window_size.h) as f64 + bw;

    if pos.x < left || pos.x >= right || pos.y < top || pos.y >= bottom {
        return None;
    }

    let inner_left = window_loc.x as f64;
    let inner_right = (window_loc.x + window_size.w) as f64;
    let inner_top = (window_loc.y - bar_height) as f64;
    let inner_bottom = (window_loc.y + window_size.h) as f64;

    // Already inside the window+titlebar area — not a resize border
    if pos.x >= inner_left && pos.x < inner_right && pos.y >= inner_top && pos.y < inner_bottom {
        return None;
    }

    let in_left = pos.x < inner_left;
    let in_right = pos.x >= inner_right;
    let in_top = pos.y < inner_top;
    let in_bottom = pos.y >= inner_bottom;

    Some(match (in_left, in_right, in_top, in_bottom) {
        (true, _, true, _) => xdg_toplevel::ResizeEdge::TopLeft,
        (_, true, true, _) => xdg_toplevel::ResizeEdge::TopRight,
        (true, _, _, true) => xdg_toplevel::ResizeEdge::BottomLeft,
        (_, true, _, true) => xdg_toplevel::ResizeEdge::BottomRight,
        (true, _, _, _) => xdg_toplevel::ResizeEdge::Left,
        (_, true, _, _) => xdg_toplevel::ResizeEdge::Right,
        (_, _, true, _) => xdg_toplevel::ResizeEdge::Top,
        (_, _, _, true) => xdg_toplevel::ResizeEdge::Bottom,
        _ => return None,
    })
}

/// The bar's colours. The bar and text colours are the configured ones and the
/// accent is the focused border's; the rest are the shell palette's shades for
/// whichever of light or dark the bar colour is.
struct Palette {
    chrome: [u8; 4],
    tab: [u8; 4],
    title: [u8; 4],
    icon: [u8; 4],
    icon_hovered: [u8; 4],
    accent: [u8; 4],
    on_accent: [u8; 4],
    hover: [u8; 4],
    danger: [u8; 4],
}

const fn rgb(v: u32) -> [u8; 4] {
    [(v >> 16) as u8, (v >> 8) as u8, v as u8, 0xFF]
}

fn mix(a: [u8; 4], b: [u8; 4], t: f64) -> [u8; 4] {
    let l = |x: u8, y: u8| (x as f64 + (y as f64 - x as f64) * t).round() as u8;
    [l(a[0], b[0]), l(a[1], b[1]), l(a[2], b[2]), a[3]]
}

impl Palette {
    fn new(config: &DecorationConfig, focused: bool) -> Self {
        let chrome = config.bg_color;
        let dark = config.is_dark();
        let pick = |dark_shade: u32, light_shade: u32| {
            let mut c = rgb(if dark { dark_shade } else { light_shade });
            c[3] = chrome[3];
            c
        };
        let dim = |c: [u8; 4]| {
            if focused { c } else { mix(c, chrome, 0.45) }
        };
        Self {
            chrome,
            tab: pick(0x0d1117, 0xfdfdfd),
            title: dim(config.fg_color),
            icon: dim(rgb(if dark { 0xb1bac4 } else { 0x515255 })),
            icon_hovered: config.fg_color,
            accent: dim(config.border_color_focused),
            on_accent: rgb(if dark { 0x0d1117 } else { 0xfcfcfc }),
            hover: rgb(if dark { 0x17243a } else { 0xe7ecf7 }),
            danger: rgb(if dark { 0xf85149 } else { 0xcf2d56 }),
        }
    }
}

/// Anti-aliased shape rasteriser over a premultiplied `Abgr8888` buffer.
/// Coordinates are buffer pixels; shapes are signed-distance functions.
struct Canvas<'a> {
    pixels: &'a mut [u8],
    w: i32,
    h: i32,
}

/// Axis-aligned box in buffer pixels: left, top, right, bottom.
#[derive(Clone, Copy)]
struct Bounds(f64, f64, f64, f64);

impl Bounds {
    fn centered(cx: f64, cy: f64, half: f64) -> Self {
        Bounds(cx - half, cy - half, cx + half, cy + half)
    }

    fn center(self) -> (f64, f64) {
        ((self.0 + self.2) / 2.0, (self.1 + self.3) / 2.0)
    }

    /// Distance to a rounded box's edge, negative inside. `radii` runs
    /// clockwise from the top-left corner.
    fn distance(self, radii: [f64; 4], px: f64, py: f64) -> f64 {
        let (cx, cy) = self.center();
        let (hx, hy) = ((self.2 - self.0) / 2.0, (self.3 - self.1) / 2.0);
        let r = match (px < cx, py < cy) {
            (true, true) => radii[0],
            (false, true) => radii[1],
            (false, false) => radii[2],
            (true, false) => radii[3],
        }
        .min(hx)
        .min(hy);
        let qx = (px - cx).abs() - hx + r;
        let qy = (py - cy).abs() - hy + r;
        qx.max(0.0).hypot(qy.max(0.0)) + qx.max(qy).min(0.0) - r
    }
}

impl Canvas<'_> {
    fn clear(&mut self, color: [u8; 4]) {
        let a = color[3] as f64 / 255.0;
        let premultiplied = [
            (color[0] as f64 * a) as u8,
            (color[1] as f64 * a) as u8,
            (color[2] as f64 * a) as u8,
            color[3],
        ];
        for px in self.pixels.as_chunks_mut::<4>().0 {
            *px = premultiplied;
        }
    }

    /// Blend `color` over every pixel in `bounds` by its coverage of the shape
    /// whose signed distance (negative inside) is `distance`.
    fn shape(&mut self, bounds: Bounds, color: [u8; 4], distance: impl Fn(f64, f64) -> f64) {
        let x0 = (bounds.0 - 1.0).floor().max(0.0) as i32;
        let y0 = (bounds.1 - 1.0).floor().max(0.0) as i32;
        let x1 = (bounds.2 + 1.0).ceil().min(self.w as f64) as i32;
        let y1 = (bounds.3 + 1.0).ceil().min(self.h as f64) as i32;
        let alpha = color[3] as f64 / 255.0;
        for y in y0..y1 {
            for x in x0..x1 {
                let coverage = (0.5 - distance(x as f64 + 0.5, y as f64 + 0.5)).clamp(0.0, 1.0);
                let a = alpha * coverage;
                if a <= 0.0 {
                    continue;
                }
                let idx = ((y * self.w + x) * 4) as usize;
                let Some(px) = self.pixels.get_mut(idx..idx + 4) else {
                    continue;
                };
                for (dst, src) in px.iter_mut().zip([color[0], color[1], color[2], 255]) {
                    *dst = (src as f64 * a + *dst as f64 * (1.0 - a)).round() as u8;
                }
            }
        }
    }

    fn fill(&mut self, bounds: Bounds, radii: [f64; 4], color: [u8; 4]) {
        self.shape(bounds, color, |x, y| bounds.distance(radii, x, y));
    }

    /// Outline centred on the box's edge.
    fn stroke(&mut self, bounds: Bounds, radius: f64, width: f64, color: [u8; 4]) {
        let half = width / 2.0;
        let outer = Bounds(
            bounds.0 - half,
            bounds.1 - half,
            bounds.2 + half,
            bounds.3 + half,
        );
        self.shape(outer, color, |x, y| {
            bounds.distance([radius; 4], x, y).abs() - half
        });
    }

    /// Polyline with square ends; inner joints are rounded.
    fn line(&mut self, points: &[(f64, f64)], width: f64, color: [u8; 4]) {
        let half = width / 2.0;
        let xs = points.iter().map(|p| p.0);
        let ys = points.iter().map(|p| p.1);
        let bounds = Bounds(
            xs.clone().fold(f64::INFINITY, f64::min) - half,
            ys.clone().fold(f64::INFINITY, f64::min) - half,
            xs.fold(f64::NEG_INFINITY, f64::max) + half,
            ys.fold(f64::NEG_INFINITY, f64::max) + half,
        );
        let last = points.len().saturating_sub(2);
        self.shape(bounds, color, |x, y| {
            let mut nearest = f64::INFINITY;
            for (i, pair) in points.windows(2).enumerate() {
                let (a, b) = (pair[0], pair[1]);
                let (dx, dy) = (b.0 - a.0, b.1 - a.1);
                let len = dx.hypot(dy);
                if len < 1e-6 {
                    continue;
                }
                let along = ((x - a.0) * dx + (y - a.1) * dy) / len;
                let across = ((x - a.0) * dy - (y - a.1) * dx).abs() / len;
                let before = if i == 0 { -along } else { f64::NEG_INFINITY };
                let after = if i == last {
                    along - len
                } else {
                    f64::NEG_INFINITY
                };
                let d = if before > 0.0 || after > 0.0 {
                    (across - half).max(before.max(after))
                } else {
                    let t = along.clamp(0.0, len);
                    (x - a.0 - dx * t / len).hypot(y - a.1 - dy * t / len) - half
                };
                nearest = nearest.min(d);
            }
            nearest
        });
    }

    fn circle(&mut self, cx: f64, cy: f64, r: f64, color: [u8; 4]) {
        self.shape(Bounds::centered(cx, cy, r), color, |x, y| {
            (x - cx).hypot(y - cy) - r
        });
    }
}

/// CPU-render the title bar: the accent prompt mark, the window title on a
/// tab, and the window controls, over the bar colour with rounded top corners.
/// `scale` supersamples the buffer (buffer scale = `scale`) so the bar stays
/// crisp on HiDPI outputs; all buffer-space geometry below is in physical
/// pixels, laid out in units of `u`.
pub fn render_title_bar(bar: &BarState, config: &DecorationConfig) -> MemoryRenderBuffer {
    let s = bar.scale.max(1);
    let bar_height = config.title_bar_height;
    let h = buffer_extent(bar_height, s);
    let w = buffer_extent(bar.width, s);
    let palette = Palette::new(config, bar.focused);
    let u = s as f64 * design_scale(bar_height);
    let mid = h as f64 / 2.0;

    let mut pixels = vec![0u8; rgba_len(w, h)];
    let mut canvas = Canvas {
        pixels: &mut pixels,
        w,
        h,
    };
    canvas.clear(palette.chrome);

    let all_controls = bar.minimizable && window_controls_fit(bar.width, bar_height);
    let controls: &[Control] = if all_controls {
        &[Control::Close, Control::Maximize, Control::Minimize]
    } else {
        &[Control::Close]
    };
    let mut controls_left = w as f64;
    for &control in controls {
        let rect = control_rect(control, Point::from((0, bar_height)), w / s, bar_height);
        let button = Bounds(
            (rect.loc.x * s) as f64,
            (rect.loc.y * s) as f64,
            ((rect.loc.x + rect.size.w) * s) as f64,
            ((rect.loc.y + rect.size.h) * s) as f64,
        );
        controls_left = controls_left.min(button.0);
        let hot = bar.hovered == Some(control);
        let mut ink = palette.icon;
        let mut ground = palette.chrome;
        if hot {
            let danger = control == Control::Close;
            ground = if danger {
                palette.danger
            } else {
                palette.hover
            };
            ink = if danger {
                rgb(0xffffff)
            } else {
                palette.icon_hovered
            };
            canvas.fill(button, [6.0 * u; 4], ground);
        }
        let (cx, cy) = button.center();
        let (half, pen) = (4.5 * u, 1.2 * u);
        match control {
            Control::Close => {
                canvas.line(&[(cx - half, cy - half), (cx + half, cy + half)], pen, ink);
                canvas.line(&[(cx - half, cy + half), (cx + half, cy - half)], pen, ink);
            }
            Control::Maximize if bar.maximized => {
                let side = half * 2.0 - 2.0 * u;
                let (bx, by) = (cx - half + 2.5 * u, cy - half - 0.5 * u);
                let (fx, fy) = (cx - half, cy - half + 2.0 * u);
                let front = Bounds(fx, fy, fx + side, fy + side);
                canvas.stroke(Bounds(bx, by, bx + side, by + side), 1.5 * u, pen, ink);
                canvas.fill(front, [1.5 * u; 4], ground);
                canvas.stroke(front, 1.5 * u, pen, ink);
            }
            Control::Maximize => {
                canvas.stroke(Bounds::centered(cx, cy, half), 2.0 * u, pen, ink);
            }
            Control::Minimize => {
                // On the pixel boundary the stroke would smear over two rows;
                // the row's centre keeps it one crisp line.
                let y = cy.floor() + 0.5;
                canvas.line(&[(cx - half, y), (cx + half, y)], pen, ink);
            }
        }
    }

    // The mark: the app's icon, else a shell prompt on the accent colour.
    // Dropped once the controls reach it, so a sliver of a window keeps a
    // clean bar.
    if controls_left >= TAB_LEFT * u
        && let Some(icon) = bar.icon
    {
        let alpha = if bar.focused {
            255
        } else {
            UNFOCUSED_ICON_ALPHA
        };
        blit_icon(&mut canvas, icon, 22.0 * u, mid, alpha);
    } else if controls_left >= TAB_LEFT * u && !bar.icon_pending {
        let (cx, cy) = (22.0 * u, mid);
        canvas.fill(
            Bounds::centered(cx, cy, 10.0 * u),
            [5.0 * u; 4],
            palette.accent,
        );
        let pen = 1.6 * u;
        canvas.line(
            &[
                (cx - 5.0 * u, cy - 4.0 * u),
                (cx - 1.0 * u, cy),
                (cx - 5.0 * u, cy + 4.0 * u),
            ],
            pen,
            palette.on_accent,
        );
        canvas.line(
            &[(cx + 0.5 * u, cy + 4.0 * u), (cx + 5.5 * u, cy + 4.0 * u)],
            pen,
            palette.on_accent,
        );
    }

    // Each title sits on a tab; the active one runs into the window below it.
    let lone = [TabLabel {
        title: bar.title,
        active: true,
        hovered: false,
    }];
    let labels: &[TabLabel] = if bar.tabs.is_empty() { &lone } else { bar.tabs };
    let active = labels.iter().position(|t| t.active).unwrap_or(0);
    let slots = tab_slots(controls_left, u, labels.len(), active);
    let tab_top = mid - TAB_RISE * u;
    let top = 7.0 * u;
    // `font_size` is in points; convert to logical px (96 dpi) then to buffer
    // px. Not clamped — vertical centering plus the buffer bounds absorb an
    // oversized font.
    let font_px = (config.font_size * PT_TO_PX * s as f32).max(1.0);
    for (at, slot) in slots.iter().enumerate() {
        let Some(label) = labels.get(slot.index) else {
            continue;
        };
        if labels.len() == 1 && label.title.is_empty() {
            continue;
        }
        let tab = Bounds(slot.left, tab_top, slot.right, h as f64);
        let ink = if label.active {
            canvas.fill(tab, [top, top, 0.0, 0.0], palette.tab);
            canvas.fill(
                Bounds(tab.0 + 9.0 * u, tab.1, tab.2 - 9.0 * u, tab.1 + 2.0 * u),
                [0.0; 4],
                palette.accent,
            );
            palette.title
        } else {
            if label.hovered {
                canvas.fill(tab, [top, top, 0.0, 0.0], palette.hover);
            }
            // A hairline keeps two neighbouring inactive tabs apart; next to
            // the active tab its fill already does.
            let next_inactive = slots
                .get(at + 1)
                .and_then(|next| labels.get(next.index))
                .is_some_and(|next| !next.active);
            if next_inactive {
                let x = slot.right + (TAB_GAP * u - u) / 2.0;
                let tab_mid = (tab.1 + tab.3) / 2.0;
                canvas.fill(
                    Bounds(x, tab_mid - 6.0 * u, x + u, tab_mid + 6.0 * u),
                    [0.0; 4],
                    mix(palette.chrome, palette.title, 0.22),
                );
            }
            mix(palette.title, palette.chrome, INACTIVE_TAB_DIM)
        };

        let tab_mid = (tab.1 + tab.3) / 2.0;
        let mut text_left = tab.0 + 12.0 * u;
        if bar.pinned && label.active {
            canvas.circle(text_left + 3.0 * u, tab_mid, 3.0 * u, palette.accent);
            text_left += 12.0 * u;
        }
        let room = (tab.2 - 8.0 * u - text_left).floor() as i32;
        if room > 0 {
            let (text, text_w) = driftwm::text::fit_text(
                label.title,
                &config.font,
                font_px,
                config.font_weight,
                room,
            );
            let origin_x = text_left.round() as i32
                + match config.title_align {
                    TitleAlign::Left => 0,
                    TitleAlign::Center => (room - text_w).max(0) / 2,
                };
            driftwm::text::rasterize_into(
                canvas.pixels,
                w,
                h,
                &text,
                &config.font,
                font_px,
                config.font_weight,
                ink,
                origin_x,
                tab_mid as f32,
            );
        }
    }

    let cr = (bar.corner_radius as f64 * s as f64)
        .min(w as f64 / 2.0)
        .min(h as f64);
    let corner = cr.ceil() as i32;
    for y in 0..corner.min(h) {
        for x in (0..corner.min(w)).chain((w - corner).max(0)..w) {
            let idx = ((y * w + x) * 4) as usize;
            let alpha = corner_alpha_at(x, y, w, cr);
            for channel in &mut pixels[idx..idx + 4] {
                *channel = (*channel as f64 * alpha) as u8;
            }
        }
    }

    MemoryRenderBuffer::from_slice(
        &pixels,
        Fourcc::Abgr8888,
        (w, h),
        s,
        Transform::Normal,
        None,
    )
}

/// CPU-render a suspended window's body fill: the SSD background color with
/// rounded corners so it tucks inside the border arc instead of poking past it.
/// Only the bottom pair rounds — the title bar always covers the top corners.
/// `scale` supersamples like `render_title_bar`; the fill is premultiplied.
pub fn render_body_fill(
    width: i32,
    height: i32,
    scale: i32,
    config: &DecorationConfig,
) -> MemoryRenderBuffer {
    let s = scale.max(1);
    let full_w = width.max(1) as f64 * s as f64;
    let full_h = height.max(1) as f64 * s as f64;
    // One factor for both sides, so the corners are still round once the
    // buffer is stretched back over the body.
    let k = (MAX_BUFFER_EXTENT as f64 / full_w.max(full_h))
        .min((MAX_BODY_FILL_PIXELS / (full_w * full_h)).sqrt())
        .min(1.0);
    let w = ((full_w * k) as i32).max(1);
    let h = ((full_h * k) as i32).max(1);
    let bg = config.bg_color;
    let ba = bg[3] as f64 / 255.0;
    let cr = (config.corner_radius as f64 * s as f64 * k)
        .min(w as f64 / 2.0)
        .min(h as f64 / 2.0);

    let mut pixels = vec![0u8; rgba_len(w, h)];
    for y in 0..h {
        for x in 0..w {
            let idx = ((y * w + x) * 4) as usize;
            let a = body_corner_alpha_at(x, y, w, h, cr) * ba;
            pixels[idx] = (bg[0] as f64 * a) as u8;
            pixels[idx + 1] = (bg[1] as f64 * a) as u8;
            pixels[idx + 2] = (bg[2] as f64 * a) as u8;
            pixels[idx + 3] = (255.0 * a) as u8;
        }
    }

    MemoryRenderBuffer::from_slice(
        &pixels,
        Fourcc::Abgr8888,
        (w, h),
        s,
        Transform::Normal,
        None,
    )
}

/// Anti-aliased coverage (1 inside → 0 outside) for a body pixel, rounding only
/// the bottom corners (the title bar covers the top pair). Mirrors
/// `corner_alpha_at`'s smoothstep falloff.
fn body_corner_alpha_at(x: i32, y: i32, w: i32, h: i32, r: f64) -> f64 {
    if r <= 0.0 {
        return 1.0;
    }
    let px = x as f64 + 0.5;
    let py = y as f64 + 0.5;

    let cx = if px < r {
        r
    } else if px > w as f64 - r {
        w as f64 - r
    } else {
        return 1.0;
    };
    let cy = if py > h as f64 - r {
        h as f64 - r
    } else {
        return 1.0;
    };

    let dx = px - cx;
    let dy = py - cy;
    let dist = (dx * dx + dy * dy).sqrt();
    let t = (dist - r + 0.5).clamp(0.0, 1.0);
    1.0 - t * t * (3.0 - 2.0 * t)
}

/// Anti-aliased alpha for top-left and top-right corner rounding.
fn corner_alpha_at(x: i32, y: i32, w: i32, r: f64) -> f64 {
    if r <= 0.0 {
        return 1.0;
    }
    let px = x as f64 + 0.5;
    let py = y as f64 + 0.5;

    // Top-left corner
    if px < r && py < r {
        let dx = r - px;
        let dy = r - py;
        let dist = (dx * dx + dy * dy).sqrt();
        let t = (dist - r + 0.5).clamp(0.0, 1.0);
        return 1.0 - t * t * (3.0 - 2.0 * t);
    }
    // Top-right corner
    let right_edge = w as f64;
    if px > right_edge - r && py < r {
        let dx = px - (right_edge - r);
        let dy = r - py;
        let dist = (dx * dx + dy * dy).sqrt();
        let t = (dist - r + 0.5).clamp(0.0, 1.0);
        return 1.0 - t * t * (3.0 - 2.0 * t);
    }
    1.0
}

#[cfg(test)]
mod tests {
    use super::*;
    use xdg_toplevel::ResizeEdge;

    fn loc(x: i32, y: i32) -> Point<i32, Logical> {
        Point::from((x, y))
    }
    fn pt(x: f64, y: f64) -> Point<f64, Logical> {
        Point::from((x, y))
    }
    fn sz(w: i32, h: i32) -> Size<i32, Logical> {
        Size::from((w, h))
    }

    const PAD: i32 = CONTROLS_RIGHT_PAD;

    /// Read one pixel's RGBA out of a rasterised bar. `MemoryRenderBuffer`
    /// exposes its bytes only through a render context, so borrow one.
    fn pixel_at(buf: &mut MemoryRenderBuffer, x: i32, y: i32, stride_px: i32) -> [u8; 4] {
        let mut out = [0u8; 4];
        buf.render()
            .draw(|pixels| {
                let idx = ((y * stride_px + x) * 4) as usize;
                out.copy_from_slice(&pixels[idx..idx + 4]);
                Ok::<_, std::convert::Infallible>(Vec::new())
            })
            .unwrap();
        out
    }

    fn bar_state(width: i32, scale: i32, title: &str, corner_radius: i32) -> BarState<'_> {
        BarState {
            width,
            focused: true,
            hovered: None,
            scale,
            title,
            pinned: false,
            minimizable: true,
            maximized: false,
            corner_radius,
            icon: None,
            icon_pending: false,
            tabs: &[],
        }
    }

    fn byte_len(buf: &mut MemoryRenderBuffer) -> usize {
        let mut len = 0;
        buf.render()
            .draw(|pixels| {
                len = pixels.len();
                Ok::<_, std::convert::Infallible>(Vec::new())
            })
            .unwrap();
        len
    }

    #[test]
    fn tabs_split_the_room_between_the_mark_and_the_controls() {
        let bar = DESIGN_HEIGHT as i32;
        let width = 800;
        let controls = controls_left_edge(width, bar, true) as f64;
        for count in 2..=12 {
            for active in 0..count {
                let rects = tab_rects(width, bar, count, active);
                assert!(
                    rects.iter().any(|(index, _)| *index == active),
                    "{count} tabs: the active one ({active}) is always laid out"
                );
                let mut edge = TAB_LEFT;
                for (_, rect) in &rects {
                    assert!(rect.loc.x >= edge, "tabs don't overlap");
                    assert!(rect.size.w >= TAB_COMPACT_WIDTH - TAB_GAP);
                    assert!(rect.size.w <= TAB_MAX_WIDTH);
                    assert!(rect.loc.y >= 0.0 && rect.loc.y + rect.size.h <= bar as f64);
                    edge = rect.loc.x + rect.size.w;
                }
                assert!(edge <= controls - TAB_TRAILING, "clear of the controls");
            }
        }
        // Few tabs in a wide bar stop at the cap instead of stretching.
        assert_eq!(
            tab_rects(1600, bar, 2, 0)[1].1.size.w,
            TAB_MAX_WIDTH - TAB_GAP
        );
        // All of them fit here, in order.
        let indices: Vec<usize> = tab_rects(width, bar, 4, 2).iter().map(|t| t.0).collect();
        assert_eq!(indices, [0, 1, 2, 3]);
    }

    #[test]
    fn overflowing_tabs_keep_a_run_around_the_active_one() {
        let bar = DESIGN_HEIGHT as i32;
        let all = tab_rects(400, bar, 40, 39);
        assert!(all.len() < 40 && !all.is_empty());
        assert_eq!(all.last().unwrap().0, 39, "the run ends at the active tab");
        let first = tab_rects(400, bar, 40, 0);
        assert_eq!(first[0].0, 0);
        assert_eq!(first.len(), all.len());
    }

    #[test]
    fn a_narrow_bar_lays_out_no_tabs_and_stays_a_drag_zone() {
        let bar = DESIGN_HEIGHT as i32;
        assert!(tab_rects(100, bar, 2, 0).is_empty());
        assert!(tab_rects(100, bar, 5, 3).is_empty());
        for x in 0..100 {
            assert_eq!(tab_at(pt(x as f64, -10.0), loc(0, 0), 100, bar, 3, 1), None);
        }
    }

    #[test]
    fn tab_at_names_the_tab_under_the_point() {
        let bar = DESIGN_HEIGHT as i32;
        let origin = loc(100, 200);
        for (index, rect) in tab_rects(600, bar, 3, 1) {
            let centre = pt(
                origin.x as f64 + rect.loc.x + rect.size.w / 2.0,
                (origin.y - bar) as f64 + rect.loc.y + rect.size.h / 2.0,
            );
            assert_eq!(tab_at(centre, origin, 600, bar, 3, 1), Some(index));
        }
        // Past the last tab the bar is plain drag zone, as are the controls.
        let close = close_button_rect(origin, 600, bar);
        let on_close = pt(close.loc.x as f64 + 2.0, close.loc.y as f64 + 2.0);
        assert_eq!(tab_at(on_close, origin, 600, bar, 3, 1), None);
        assert_eq!(tab_at(pt(110.0, 190.0), origin, 600, bar, 3, 1), None);
        assert_eq!(tab_at(pt(300.0, 210.0), origin, 600, bar, 3, 1), None);
    }

    #[test]
    fn a_two_tab_bar_fills_the_active_tab_and_leaves_the_other_bare() {
        let config = DecorationConfig::default();
        let bar_h = config.title_bar_height;
        let width = 600;
        let tabs = |active: usize, hovered: Option<usize>| {
            [0, 1].map(|i| TabLabel {
                title: "",
                active: i == active,
                hovered: hovered == Some(i),
            })
        };
        let render = |labels: &[TabLabel]| {
            let mut state = bar_state(width, 1, "", 0);
            state.tabs = labels;
            render_title_bar(&state, &config)
        };
        let rects = tab_rects(width, bar_h, 2, 1);
        let probe = |buf: &mut MemoryRenderBuffer, tab: usize| {
            let rect = rects[tab].1;
            pixel_at(
                buf,
                (rect.loc.x + rect.size.w / 2.0) as i32,
                (rect.loc.y + rect.size.h - 3.0) as i32,
                width,
            )
        };

        let mut second_active = render(&tabs(1, None));
        let chrome = probe(&mut second_active, 0);
        let filled = probe(&mut second_active, 1);
        assert_ne!(chrome, filled, "the active tab stands out of the chrome");

        let mut first_active = render(&tabs(0, None));
        assert_eq!(probe(&mut first_active, 0), filled);
        assert_eq!(probe(&mut first_active, 1), chrome);

        let mut hovered = render(&tabs(1, Some(0)));
        let hover = probe(&mut hovered, 0);
        assert_ne!(hover, chrome, "a hovered inactive tab lights up");
        assert_ne!(hover, filled);
        assert_eq!(probe(&mut hovered, 1), filled);
    }

    #[test]
    fn a_decoration_tracks_its_tabs() {
        let config = DecorationConfig::default();
        let mut deco = WindowDecoration::new(600, true, true, &config);
        assert_eq!(deco.tab_count(), 1);
        deco.set_tabs(&["one", "two"], 1);
        assert_eq!((deco.tab_count(), deco.active_tab()), (2, 1));
        deco.set_tabs::<&str>(&[], 0);
        assert_eq!((deco.tab_count(), deco.active_tab()), (1, 0));
    }

    #[test]
    fn title_bar_for_a_window_past_the_buffer_cap_stays_within_it() {
        let config = DecorationConfig::default();
        let s = 2;
        let mut bar = render_title_bar(&bar_state(30_000_000, s, "title", 8), &config);
        let h = (config.title_bar_height * s) as usize;
        assert_eq!(byte_len(&mut bar), MAX_BUFFER_EXTENT as usize * h * 4);
    }

    #[test]
    fn body_fill_for_a_stand_in_past_the_buffer_cap_stays_within_it() {
        let config = DecorationConfig::default();
        let cap = MAX_BUFFER_EXTENT as usize;
        let mut wide = render_body_fill(20_000, 100, 1, &config);
        assert!(byte_len(&mut wide) <= cap * 100 * 4);
        let mut tall = render_body_fill(100, 20_000, 1, &config);
        assert!(byte_len(&mut tall) <= 100 * cap * 4);
        // Sides that overflow i32 once scaled.
        for (w, h) in [(i32::MAX, 10), (10, i32::MAX)] {
            let mut fill = render_body_fill(w, h, 2, &config);
            assert!(byte_len(&mut fill) <= cap * 20 * 4);
        }
    }

    #[test]
    fn body_fill_past_the_pixel_budget_is_rasterized_within_it() {
        let config = DecorationConfig::default();
        let mut fill = render_body_fill(4500, 4000, 1, &config);
        assert!(byte_len(&mut fill) <= MAX_BODY_FILL_PIXELS as usize * 4);
    }

    #[test]
    fn title_bar_with_zero_corner_radius_has_opaque_top_left_pixel() {
        let config = DecorationConfig::default();
        let mut bar = render_title_bar(&bar_state(200, 1, "", 0), &config);
        assert_eq!(pixel_at(&mut bar, 0, 0, 200)[3], config.bg_color[3]);
    }

    #[test]
    fn title_bar_with_rounded_corners_has_transparent_top_left_pixel() {
        let config = DecorationConfig::default();
        let mut bar = render_title_bar(&bar_state(200, 1, "", 16), &config);
        assert_eq!(pixel_at(&mut bar, 0, 0, 200)[3], 0);
    }

    #[test]
    fn decoration_update_rebuilds_when_only_corner_radius_changes() {
        let config = DecorationConfig::default();
        let mut deco = WindowDecoration::new(200, true, true, &config);
        // Settle first: `new` renders at a placeholder scale, and the font scan
        // may land between calls.
        deco.update(
            200,
            true,
            false,
            false,
            1,
            "",
            config.corner_radius,
            &config,
        );
        assert!(!deco.update(
            200,
            true,
            false,
            false,
            1,
            "",
            config.corner_radius,
            &config
        ));
        assert!(deco.update(200, true, false, false, 1, "", 0, &config));
        assert_eq!(deco.corner_radius, 0);
    }

    #[test]
    fn decoration_update_rebuilds_when_only_maximized_changes() {
        let config = DecorationConfig::default();
        let mut deco = WindowDecoration::new(400, true, true, &config);
        deco.update(
            400,
            true,
            false,
            false,
            1,
            "",
            config.corner_radius,
            &config,
        );
        assert!(deco.update(400, true, false, true, 1, "", config.corner_radius, &config));
        assert!(deco.maximized);
    }

    #[test]
    fn controls_at_the_design_height_are_34_by_26_centred_in_the_bar() {
        for control in [Control::Close, Control::Maximize, Control::Minimize] {
            let r = control_rect(control, loc(0, 100), 400, 38);
            assert_eq!((r.size.w, r.size.h), (34, 26));
            assert_eq!(r.loc.y, 100 - 38 + 6);
        }
    }

    #[test]
    fn controls_stack_leftwards_from_the_pad_close_maximize_minimize() {
        let close = close_button_rect(loc(50, 200), 400, 38);
        let maximize = maximize_button_rect(loc(50, 200), 400, 38);
        let minimize = minimize_button_rect(loc(50, 200), 400, 38);
        assert_eq!(close.loc.x + close.size.w, 50 + 400 - PAD);
        assert_eq!(maximize.loc.x + maximize.size.w, close.loc.x);
        assert_eq!(minimize.loc.x + minimize.size.w, maximize.loc.x);
    }

    #[test]
    fn controls_shrink_with_a_bar_shorter_than_the_design_height() {
        let r = close_button_rect(loc(0, 100), 400, 19);
        assert_eq!((r.size.w, r.size.h), (17, 13));
        assert_eq!(r.loc.y, 100 - 19 + 3);
    }

    #[test]
    fn controls_keep_their_size_in_a_bar_taller_than_the_design_height() {
        let r = close_button_rect(loc(0, 100), 400, 60);
        assert_eq!((r.size.w, r.size.h), (34, 26));
        assert_eq!(r.loc.y, 100 - 60 + 17);
    }

    #[test]
    fn close_button_rect_works_with_negative_canvas_coords() {
        let r = close_button_rect(loc(-300, -100), 200, 38);
        assert_eq!(r.loc.y, -100 - 38 + 6);
        assert_eq!(r.loc.x + r.size.w, -300 + 200 - PAD);
    }

    #[test]
    fn maximize_button_contains_its_centre_only_where_the_controls_fit() {
        let at = |width| {
            let r = maximize_button_rect(loc(0, 100), width, 38);
            pt(
                r.loc.x as f64 + r.size.w as f64 / 2.0,
                r.loc.y as f64 + r.size.h as f64 / 2.0,
            )
        };
        assert!(maximize_button_contains(at(400), loc(0, 100), 400, 38));
        assert!(!maximize_button_contains(at(150), loc(0, 100), 150, 38));
    }

    const RED: [u8; 4] = [0xcf, 0x2d, 0x56, 0xff];

    fn control_corner_pixel(hovered: Option<Control>, control: Control) -> [u8; 4] {
        let config = DecorationConfig {
            title_bar_height: 38,
            ..DecorationConfig::default()
        };
        let state = BarState {
            hovered,
            ..bar_state(400, 1, "", 0)
        };
        let mut bar = render_title_bar(&state, &config);
        // Just inside the button, clear of its rounded corner and of the icon.
        let r = control_rect(control, loc(0, 38), 400, 38);
        pixel_at(&mut bar, r.loc.x + 8, r.loc.y + 3, 400)
    }

    #[test]
    fn hovering_close_paints_it_red_and_leaves_the_others_bare() {
        let bg = DecorationConfig::default().bg_color;
        assert_eq!(control_corner_pixel(None, Control::Close), bg);
        assert_eq!(
            control_corner_pixel(Some(Control::Close), Control::Close),
            RED
        );
        assert_eq!(
            control_corner_pixel(Some(Control::Close), Control::Maximize),
            bg
        );
    }

    #[test]
    fn hovering_maximize_or_minimize_paints_the_hover_shade() {
        let hover = [0xe7, 0xec, 0xf7, 0xff];
        for control in [Control::Maximize, Control::Minimize] {
            assert_eq!(control_corner_pixel(Some(control), control), hover);
        }
    }

    #[test]
    fn a_stand_in_bar_draws_no_maximize_or_minimize() {
        let config = DecorationConfig {
            title_bar_height: 38,
            ..DecorationConfig::default()
        };
        let state = BarState {
            minimizable: false,
            hovered: Some(Control::Maximize),
            ..bar_state(400, 1, "", 0)
        };
        let mut bar = render_title_bar(&state, &config);
        let r = maximize_button_rect(loc(0, 38), 400, 38);
        for x in r.loc.x..r.loc.x + r.size.w {
            assert_eq!(pixel_at(&mut bar, x, 19, 400), config.bg_color);
        }
    }

    #[test]
    fn without_an_icon_the_fallback_mark_is_the_focused_border_colour_dimmed_when_unfocused() {
        let config = DecorationConfig {
            title_bar_height: 38,
            border_color_focused: [0x00, 0x53, 0xfd, 0xff],
            ..DecorationConfig::default()
        };
        let mut focused = render_title_bar(&bar_state(400, 1, "", 0), &config);
        assert_eq!(
            pixel_at(&mut focused, 22, 12, 400),
            [0x00, 0x53, 0xfd, 0xff]
        );
        let state = BarState {
            focused: false,
            ..bar_state(400, 1, "", 0)
        };
        let mut unfocused = render_title_bar(&state, &config);
        assert_eq!(
            pixel_at(&mut unfocused, 22, 12, 400),
            [0x73, 0xa0, 0xfe, 0xff]
        );
    }

    #[test]
    fn an_app_icon_replaces_the_mark_and_dims_when_unfocused() {
        let config = DecorationConfig {
            title_bar_height: 38,
            ..DecorationConfig::default()
        };
        let side = icon_px(1, 38);
        assert_eq!(side, 24);
        let icon = IconImage {
            size: side,
            rgba: [0xff, 0x00, 0x00, 0xff].repeat((side * side) as usize),
        };
        let state = BarState {
            icon: Some(&icon),
            ..bar_state(400, 1, "", 0)
        };
        let mut bar = render_title_bar(&state, &config);
        assert_eq!(pixel_at(&mut bar, 22, 19, 400), [0xff, 0x00, 0x00, 0xff]);
        assert_eq!(pixel_at(&mut bar, 12, 19, 400), [0xff, 0x00, 0x00, 0xff]);

        let unfocused = BarState {
            focused: false,
            ..state
        };
        let mut dim = render_title_bar(&unfocused, &config);
        let px = pixel_at(&mut dim, 22, 19, 400);
        assert!(px[1] > 0 && px[1] < 0x80 && px[3] == 0xff, "{px:?}");
    }

    #[test]
    fn a_pending_icon_leaves_the_mark_slot_empty() {
        let config = DecorationConfig {
            title_bar_height: 38,
            ..DecorationConfig::default()
        };
        let state = BarState {
            icon_pending: true,
            ..bar_state(400, 1, "", 0)
        };
        let mut bar = render_title_bar(&state, &config);
        let mut empty = render_title_bar(&bar_state(400, 1, "", 0), &config);
        assert_ne!(
            pixel_at(&mut bar, 22, 12, 400),
            pixel_at(&mut empty, 22, 12, 400)
        );
        assert_eq!(
            pixel_at(&mut bar, 22, 12, 400),
            pixel_at(&mut bar, 60, 3, 400)
        );
    }

    #[test]
    fn title_bar_contains_point_in_bar_body_returns_true() {
        assert!(title_bar_contains(pt(100.0, 80.0), loc(0, 100), 400, 25));
    }

    #[test]
    fn title_bar_contains_point_in_close_button_area_returns_false() {
        // title_bar_contains explicitly excludes the close button zone
        assert!(!title_bar_contains(pt(370.0, 80.0), loc(0, 100), 400, 25));
    }

    #[test]
    fn title_bar_contains_point_above_bar_returns_false() {
        assert!(!title_bar_contains(pt(100.0, 74.0), loc(0, 100), 400, 25));
    }

    #[test]
    fn title_bar_contains_point_at_bar_top_boundary_returns_true() {
        // half-open interval: y >= bar_top is included
        assert!(title_bar_contains(pt(100.0, 75.0), loc(0, 100), 400, 25));
    }

    #[test]
    fn title_bar_contains_point_at_bar_bottom_boundary_returns_false() {
        // half-open interval: y >= window_loc.y is excluded (client area)
        assert!(!title_bar_contains(pt(100.0, 100.0), loc(0, 100), 400, 25));
    }

    #[test]
    fn title_bar_contains_point_left_of_window_returns_false() {
        assert!(!title_bar_contains(pt(-1.0, 80.0), loc(0, 100), 400, 25));
    }

    #[test]
    fn title_bar_contains_at_left_boundary_returns_true() {
        assert!(title_bar_contains(pt(0.0, 80.0), loc(0, 100), 400, 25));
    }

    #[test]
    fn title_bar_contains_point_at_bar_right_edge_returns_false() {
        // Half-open band: x == bar_left + width is the first excluded column —
        // the bar spans the window's whole width, so this is the right boundary.
        assert!(!title_bar_contains(pt(400.0, 80.0), loc(0, 100), 400, 25));
    }

    #[test]
    fn title_bar_contains_point_in_right_pad_strip_returns_true() {
        // The strip between the close button's right edge and the bar's: inside
        // the full-width band, outside the button — the title bar, not a hole.
        let strip = 400.0 - PAD as f64 / 2.0;
        assert!(title_bar_contains(pt(strip, 80.0), loc(0, 100), 400, 25));
    }

    #[test]
    fn close_button_contains_point_inside_returns_true() {
        assert!(close_button_contains(pt(370.0, 80.0), loc(0, 100), 400, 25));
    }

    #[test]
    fn close_button_contains_point_in_bar_body_returns_false() {
        assert!(!close_button_contains(
            pt(100.0, 80.0),
            loc(0, 100),
            400,
            25
        ));
    }

    #[test]
    fn close_button_contains_point_above_bar_returns_false() {
        assert!(!close_button_contains(
            pt(370.0, 74.0),
            loc(0, 100),
            400,
            25
        ));
    }

    #[test]
    fn close_button_contains_point_below_bar_returns_false() {
        assert!(!close_button_contains(
            pt(370.0, 100.0),
            loc(0, 100),
            400,
            25
        ));
    }

    #[test]
    fn title_bar_and_close_button_cover_disjoint_regions() {
        let in_close = pt(370.0, 80.0);
        assert!(close_button_contains(in_close, loc(0, 100), 400, 25));
        assert!(!title_bar_contains(in_close, loc(0, 100), 400, 25));

        let in_body = pt(100.0, 80.0);
        assert!(title_bar_contains(in_body, loc(0, 100), 400, 25));
        assert!(!close_button_contains(in_body, loc(0, 100), 400, 25));
    }

    // Layout: window_loc=(100,200), size=300x400, bar=25, border=8
    // Outer bbox: x=[92,408), y=[167,608); interior: x=[100,400), y=[175,600)
    fn edge_at(px: f64, py: f64) -> Option<ResizeEdge> {
        resize_edge_at(pt(px, py), loc(100, 200), sz(300, 400), 25, 8)
    }

    #[test]
    fn resize_edge_inside_window_body_returns_none() {
        assert_eq!(edge_at(200.0, 300.0), None);
    }

    #[test]
    fn resize_edge_inside_title_bar_returns_none() {
        assert_eq!(edge_at(200.0, 185.0), None);
    }

    #[test]
    fn resize_edge_outside_outer_bbox_returns_none() {
        assert_eq!(edge_at(200.0, 100.0), None);
        assert_eq!(edge_at(500.0, 300.0), None);
    }

    #[test]
    fn resize_edge_top_border_above_title_bar_returns_top() {
        // outer_top = 167, inner_top = 175 — point in [167,175) is the top strip
        assert_eq!(edge_at(200.0, 170.0), Some(ResizeEdge::Top));
    }

    #[test]
    fn resize_edge_bottom_border_returns_bottom() {
        assert_eq!(edge_at(200.0, 603.0), Some(ResizeEdge::Bottom));
    }

    #[test]
    fn resize_edge_left_border_returns_left() {
        assert_eq!(edge_at(95.0, 300.0), Some(ResizeEdge::Left));
    }

    #[test]
    fn resize_edge_right_border_returns_right() {
        assert_eq!(edge_at(403.0, 300.0), Some(ResizeEdge::Right));
    }

    #[test]
    fn resize_edge_top_left_corner_returns_top_left() {
        assert_eq!(edge_at(95.0, 170.0), Some(ResizeEdge::TopLeft));
    }

    #[test]
    fn resize_edge_top_right_corner_returns_top_right() {
        assert_eq!(edge_at(403.0, 170.0), Some(ResizeEdge::TopRight));
    }

    #[test]
    fn resize_edge_bottom_left_corner_returns_bottom_left() {
        assert_eq!(edge_at(95.0, 603.0), Some(ResizeEdge::BottomLeft));
    }

    #[test]
    fn resize_edge_bottom_right_corner_returns_bottom_right() {
        assert_eq!(edge_at(403.0, 603.0), Some(ResizeEdge::BottomRight));
    }
}
