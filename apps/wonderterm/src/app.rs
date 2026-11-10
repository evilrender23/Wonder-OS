use std::path::PathBuf;
use std::sync::Arc;
use std::sync::atomic::{AtomicBool, Ordering};
use std::time::Duration;

use alacritty_terminal::event::Event as TermEvent;
use alacritty_terminal::grid::{Dimensions, Scroll};
use alacritty_terminal::index::{Column, Line, Point, Side};
use alacritty_terminal::selection::{Selection, SelectionType};
use alacritty_terminal::term::TermMode;
use alacritty_terminal::vte::ansi::{CursorShape, Rgb};
use eframe::egui::{
    self, Color32, Context, CursorIcon, Event, FontFamily, FontId, Key, Modifiers, MouseWheelUnit,
    Painter, PointerButton, Pos2, Rect, ResizeDirection, Shape, Stroke, StrokeKind, Vec2,
    ViewportCommand, pos2, text::LayoutJob, text::TextWrapping, vec2,
};

use crate::config::{self, Config, ThemeMode};
use crate::cursor::{self, CursorAnim};
use crate::grid::{self, Line_, Snapshot};
use crate::session::{GridSize, Launch, Session};
use crate::theme::{Theme, alpha, mix};
use crate::{boxdraw, cut, fonts, input};

pub const HEADER: f32 = 38.0;
pub const STATUS: f32 = 24.0;
const RESIZE_EDGE: f32 = 5.0;
const BELL_SECS: f64 = 0.3;
const MENU_WIDTH: f32 = 216.0;
const MENU_ROW: f32 = 28.0;
const MENU_PAD: f32 = 5.0;

/// The right-click menu's entries, top to bottom.
#[derive(Clone, Copy, PartialEq)]
enum MenuItem {
    Copy,
    Cut,
    Paste,
    SelectAll,
}

const MENU: [MenuItem; 4] = [
    MenuItem::Copy,
    MenuItem::Cut,
    MenuItem::Paste,
    MenuItem::SelectAll,
];

fn spanish() -> bool {
    ["LC_ALL", "LC_MESSAGES", "LANG"]
        .iter()
        .filter_map(|name| std::env::var(name).ok())
        .find(|value| !value.is_empty())
        .is_some_and(|value| value.starts_with("es"))
}

/// The menu's frame and the row of each entry, kept inside the window.
fn menu_rects(at: Pos2, full: Rect) -> (Rect, [Rect; MENU.len()]) {
    let size = vec2(MENU_WIDTH, MENU_ROW * MENU.len() as f32 + MENU_PAD * 2.0);
    let room = (full.max - size - Vec2::splat(4.0)).max(full.min);
    let frame = Rect::from_min_size(at.min(room).max(full.min), size);
    let rows = std::array::from_fn(|i| {
        Rect::from_min_size(
            pos2(
                frame.left() + MENU_PAD,
                frame.top() + MENU_PAD + MENU_ROW * i as f32,
            ),
            vec2(MENU_WIDTH - MENU_PAD * 2.0, MENU_ROW),
        )
    });
    (frame, rows)
}

struct Metrics {
    cell: Vec2,
    text_dy: f32,
    ppp: f32,
    size: f32,
}

enum Action {
    NewTab,
    CloseTab(usize),
    Select(usize),
    Minimize,
    ToggleMaximize,
    CloseWindow,
    Drag,
}

/// What the held primary button started on.
#[derive(PartialEq)]
enum Press {
    Selecting,
    Reporting,
}

pub struct App {
    cfg: Config,
    theme: Theme,
    shell_dark: Arc<AtomicBool>,
    sessions: Vec<Session>,
    active: usize,
    next_id: u64,
    launch: Launch,
    metrics: Option<Metrics>,
    font_size: f32,
    error: Option<String>,
    /// Attempts left at asking for the window that fits the configured grid.
    size_requests: u8,
    title: String,
    press: Option<Press>,
    reported_cell: Option<(usize, usize)>,
    paste_requested: bool,
    /// Where the right-click menu is open.
    menu: Option<Pos2>,
    /// When and where the primary button last went down on the terminal, and
    /// how many presses in a row that made.
    last_press: Option<(f64, Pos2, u8)>,
    spanish: bool,
    shot: Option<(PathBuf, f64)>,
    shot_done: Option<Arc<AtomicBool>>,
    started: Option<f64>,
}

fn shell_theme_is_dark() -> Option<bool> {
    let state = std::env::var_os("XDG_STATE_HOME")
        .map(PathBuf::from)
        .filter(|p| p.is_absolute())
        .unwrap_or_else(|| config::home().join(".local/state"));
    let text = std::fs::read_to_string(state.join("wonderlandwm/bar.json")).ok()?;
    let json: serde_json::Value = serde_json::from_str(&text).ok()?;
    Some(json.get("themeMode")?.as_str()? == "dark")
}

impl App {
    pub fn new(
        cc: &eframe::CreationContext<'_>,
        cfg: Config,
        error: Option<String>,
        fonts: egui::FontDefinitions,
        launch: Launch,
        shot: Option<(PathBuf, f64)>,
    ) -> Self {
        cc.egui_ctx.set_fonts(fonts);

        let dark = match cfg.theme {
            ThemeMode::Dark => true,
            ThemeMode::Light => false,
            ThemeMode::Auto => shell_theme_is_dark().unwrap_or(true),
        };
        let shell_dark = Arc::new(AtomicBool::new(dark));
        if cfg.theme == ThemeMode::Auto {
            let (flag, ctx) = (shell_dark.clone(), cc.egui_ctx.clone());
            std::thread::spawn(move || {
                loop {
                    std::thread::sleep(Duration::from_secs(1));
                    if let Some(dark) = shell_theme_is_dark()
                        && flag.swap(dark, Ordering::Relaxed) != dark
                    {
                        ctx.request_repaint();
                    }
                }
            });
        }

        Self {
            font_size: cfg.font.size,
            theme: Theme::new(dark),
            cfg,
            shell_dark,
            sessions: Vec::new(),
            active: 0,
            next_id: 1,
            launch,
            metrics: None,
            error,
            size_requests: 4,
            title: String::new(),
            press: None,
            reported_cell: None,
            paste_requested: false,
            menu: None,
            last_press: None,
            spanish: spanish(),
            shot,
            shot_done: None,
            started: None,
        }
    }

    fn measure(&mut self, ctx: &Context) {
        let ppp = ctx.pixels_per_point();
        if self
            .metrics
            .as_ref()
            .is_some_and(|m| m.ppp == ppp && m.size == self.font_size)
        {
            return;
        }
        let (cell, text_dy) = cell_metrics(ctx, self.font_size, self.cfg.font.line_height);
        self.metrics = Some(Metrics {
            cell,
            text_dy,
            ppp,
            size: self.font_size,
        });
    }

    fn chrome(&self) -> Vec2 {
        chrome(&self.cfg)
    }

    fn spawn(&mut self, ctx: &Context, size: GridSize, launch: &Launch) {
        let cell = self.metrics.as_ref().map_or((8, 16), |m| {
            ((m.cell.x * m.ppp) as u16, (m.cell.y * m.ppp) as u16)
        });
        match Session::spawn(self.next_id, &self.cfg, launch, size, cell, ctx) {
            Ok(session) => {
                self.next_id += 1;
                self.sessions.push(session);
                self.active = self.sessions.len() - 1;
            }
            Err(e) => self.error = Some(format!("could not start the shell: {e}")),
        }
    }

    fn tab_title(session: &Session) -> String {
        if !session.title.is_empty() {
            return session.title.clone();
        }
        let name = session.foreground_name();
        if name.is_empty() {
            "shell".into()
        } else {
            name
        }
    }

    fn drain_events(&mut self, ctx: &Context, now: f64) {
        let theme = &self.theme;
        self.sessions.retain_mut(|s| {
            let mut alive = true;
            while let Ok(event) = s.events.try_recv() {
                match event {
                    TermEvent::Title(title) => s.title = title,
                    TermEvent::ResetTitle => s.title.clear(),
                    TermEvent::Bell => s.bell_at = Some(now),
                    TermEvent::PtyWrite(text) => s.write(text.into_bytes()),
                    TermEvent::ClipboardStore(_, text) => ctx.copy_text(text),
                    TermEvent::ColorRequest(index, format) => {
                        let c = theme.slot(index);
                        s.write(
                            format(Rgb {
                                r: c.r(),
                                g: c.g(),
                                b: c.b(),
                            })
                            .into_bytes(),
                        );
                    }
                    TermEvent::TextAreaSizeRequest(format) => {
                        s.write(format(s.window_size()).into_bytes());
                    }
                    TermEvent::Exit | TermEvent::ChildExit(_) => alive = false,
                    // Reading the clipboard back to a program is declined:
                    // anything printed to the terminal could ask for it.
                    TermEvent::ClipboardLoad(..) => {}
                    _ => {}
                }
            }
            alive
        });
        self.active = self.active.min(self.sessions.len().saturating_sub(1));
    }

    fn paste(&mut self, text: &str) {
        if let Some(s) = self.sessions.get_mut(self.active) {
            let mode = *s.term.lock().mode();
            s.term.lock().scroll_display(Scroll::Bottom);
            s.write(input::encode_paste(text, mode));
        }
    }

    fn request_paste(&mut self, ctx: &Context) {
        self.paste_requested = true;
        ctx.send_viewport_cmd(ViewportCommand::RequestPaste);
    }

    fn has_selection(&self) -> bool {
        self.sessions.get(self.active).is_some_and(|s| {
            s.term
                .lock()
                .selection_to_string()
                .is_some_and(|text| !text.is_empty())
        })
    }

    /// Copies the selection and lets go of it. False when nothing is selected.
    fn copy_selection(&self, ctx: &Context) -> bool {
        let Some(s) = self.sessions.get(self.active) else {
            return false;
        };
        let mut term = s.term.lock();
        match term.selection_to_string() {
            Some(text) if !text.is_empty() => {
                ctx.copy_text(text);
                term.selection = None;
                true
            }
            _ => false,
        }
    }

    /// Copies the selection and, where it is text the program lets the user
    /// edit, deletes it there. Anything else on screen is only copied.
    fn cut_selection(&mut self, ctx: &Context, now: f64) -> bool {
        let Some(s) = self.sessions.get_mut(self.active) else {
            return false;
        };
        let mut term = s.term.lock();
        let Some(text) = term.selection_to_string().filter(|t| !t.is_empty()) else {
            return false;
        };
        ctx.copy_text(text);
        let plan = cut::plan(&term, s.canonical());
        let mode = *term.mode();
        term.selection = None;
        if let Some(plan) = plan {
            term.scroll_display(Scroll::Bottom);
            drop(term);
            let arrow = if plan.moves < 0 {
                Key::ArrowLeft
            } else {
                Key::ArrowRight
            };
            let mut bytes = input::encode_key(arrow, Modifiers::NONE, mode)
                .unwrap_or_default()
                .repeat(plan.moves.unsigned_abs() as usize);
            bytes.resize(bytes.len() + plan.deletes, 0x7f);
            s.cursor.touch(now);
            s.write(bytes);
        }
        true
    }

    fn select_all(&mut self) {
        if let Some(s) = self.sessions.get(self.active) {
            let mut term = s.term.lock();
            let (top, bottom) = (term.topmost_line(), term.bottommost_line());
            let last = term.last_column();
            let mut selection = Selection::new(
                SelectionType::Simple,
                Point::new(top, Column(0)),
                Side::Left,
            );
            selection.update(Point::new(bottom, last), Side::Right);
            term.selection = Some(selection);
        }
    }

    fn run_menu(&mut self, ctx: &Context, item: MenuItem, now: f64) {
        match item {
            MenuItem::Copy => {
                self.copy_selection(ctx);
            }
            MenuItem::Cut => {
                self.cut_selection(ctx, now);
            }
            MenuItem::Paste => self.request_paste(ctx),
            MenuItem::SelectAll => self.select_all(),
        }
    }

    /// App shortcuts. Returns false when the key belongs to the terminal.
    fn shortcut(
        &mut self,
        ctx: &Context,
        key: Key,
        m: Modifiers,
        now: f64,
        actions: &mut Vec<Action>,
    ) -> bool {
        let count = self.sessions.len();
        let step = |by: usize| Action::Select((self.active + by) % count.max(1));
        let scroll = |scroll: Scroll| {
            if let Some(s) = self.sessions.get(self.active) {
                s.term.lock().scroll_display(scroll);
            }
        };
        match key {
            Key::T if m.ctrl && m.shift => actions.push(Action::NewTab),
            Key::W if m.ctrl && m.shift => actions.push(Action::CloseTab(self.active)),
            Key::N if m.ctrl && m.shift => {
                let cwd = self.sessions.get(self.active).and_then(Session::cwd);
                if let Ok(exe) = std::env::current_exe() {
                    let mut command = std::process::Command::new(exe);
                    if let Some(cwd) = cwd {
                        command.arg("-d").arg(cwd);
                    }
                    if let Err(e) = command.spawn() {
                        self.error = Some(format!("could not open a window: {e}"));
                    }
                }
            }
            Key::C if m.ctrl && m.shift => {
                self.copy_selection(ctx);
            }
            Key::X if m.ctrl && m.shift => {
                self.cut_selection(ctx, now);
            }
            Key::A if m.ctrl && m.shift => self.select_all(),
            Key::V if m.ctrl && m.shift => self.request_paste(ctx),
            Key::Insert if m.shift && !m.ctrl => self.request_paste(ctx),
            Key::Tab if m.ctrl && m.shift => actions.push(step(count.saturating_sub(1))),
            Key::Tab if m.ctrl => actions.push(step(1)),
            Key::PageUp if m.ctrl && !m.shift => actions.push(step(count.saturating_sub(1))),
            Key::PageDown if m.ctrl && !m.shift => actions.push(step(1)),
            Key::PageUp if m.shift => scroll(Scroll::PageUp),
            Key::PageDown if m.shift => scroll(Scroll::PageDown),
            Key::Home if m.shift && !m.ctrl => scroll(Scroll::Top),
            Key::End if m.shift && !m.ctrl => scroll(Scroll::Bottom),
            Key::Plus | Key::Equals if m.ctrl => self.font_size = (self.font_size + 1.0).min(48.0),
            Key::Minus if m.ctrl => self.font_size = (self.font_size - 1.0).max(6.0),
            Key::Num0 if m.ctrl => self.font_size = self.cfg.font.size,
            _ if m.alt && !m.ctrl && !m.shift => {
                let digits = [
                    Key::Num1,
                    Key::Num2,
                    Key::Num3,
                    Key::Num4,
                    Key::Num5,
                    Key::Num6,
                    Key::Num7,
                    Key::Num8,
                    Key::Num9,
                ];
                match digits.iter().position(|&k| k == key) {
                    Some(i) if i < count => actions.push(Action::Select(i)),
                    _ => return false,
                }
            }
            _ => return false,
        }
        true
    }

    fn handle_input(
        &mut self,
        ui: &egui::Ui,
        term_rect: Rect,
        now: f64,
        actions: &mut Vec<Action>,
    ) {
        let ctx = ui.ctx().clone();
        let (events, modifiers) = ui.input(|i| (i.events.clone(), i.modifiers));
        let cell = self.metrics.as_ref().map_or(vec2(8.0, 16.0), |m| m.cell);
        let scroll_lines = self.cfg.terminal.scroll_lines;
        let keys = self.cfg.terminal.clipboard_keys;

        for event in events {
            match event {
                Event::Key {
                    key,
                    pressed: true,
                    modifiers,
                    ..
                } => {
                    if self.shortcut(&ctx, key, modifiers, now, actions) {
                        continue;
                    }
                    let Some(s) = self.sessions.get_mut(self.active) else {
                        continue;
                    };
                    let mode = *s.term.lock().mode();
                    if let Some(bytes) = input::encode_key(key, modifiers, mode) {
                        let mut term = s.term.lock();
                        term.scroll_display(Scroll::Bottom);
                        term.selection = None;
                        drop(term);
                        s.cursor.touch(now);
                        s.write(bytes);
                    }
                }
                Event::Text(text) => {
                    let Some(s) = self.sessions.get_mut(self.active) else {
                        continue;
                    };
                    let mut term = s.term.lock();
                    term.scroll_display(Scroll::Bottom);
                    term.selection = None;
                    drop(term);
                    s.cursor.touch(now);
                    s.write(input::encode_text(&text, modifiers));
                }
                // The windowing layer turns Ctrl+C/X/V into clipboard events.
                // With text selected they copy and cut it; with nothing
                // selected they are the control codes a terminal sends. Ctrl+V
                // pastes, except into a full-screen program, which gets the
                // key (vim's block selection, nano's page down). The
                // Ctrl+Shift forms arrive as plain keys and always act.
                Event::Copy => {
                    if !(keys && self.copy_selection(&ctx)) {
                        self.write_active(b"\x03", now);
                    }
                }
                Event::Cut => {
                    if !(keys && self.cut_selection(&ctx, now)) {
                        self.write_active(b"\x18", now);
                    }
                }
                Event::Paste(text) => {
                    let full_screen = self
                        .sessions
                        .get(self.active)
                        .is_some_and(|s| s.term.lock().mode().contains(TermMode::ALT_SCREEN));
                    if std::mem::take(&mut self.paste_requested) || (keys && !full_screen) {
                        self.paste(&text);
                    } else {
                        self.write_active(b"\x16", now);
                    }
                }
                Event::MiddleClickPaste { pos, text } if term_rect.contains(pos) => {
                    let reporting = self
                        .sessions
                        .get(self.active)
                        .is_some_and(|s| s.term.lock().mode().intersects(TermMode::MOUSE_MODE));
                    if !reporting || modifiers.shift {
                        self.paste(&text);
                    }
                }
                Event::PointerButton {
                    pos,
                    button,
                    pressed,
                    modifiers,
                } => {
                    self.pointer_button(&ctx, term_rect, cell, pos, button, pressed, modifiers);
                }
                Event::PointerMoved(pos) => self.pointer_moved(term_rect, cell, pos, modifiers),
                Event::MouseWheel {
                    unit,
                    delta,
                    modifiers,
                    ..
                } => {
                    let Some(pos) = ui.input(|i| i.pointer.latest_pos()) else {
                        continue;
                    };
                    if !term_rect.contains(pos) {
                        continue;
                    }
                    let Some(s) = self.sessions.get_mut(self.active) else {
                        continue;
                    };
                    s.scroll_remainder += match unit {
                        MouseWheelUnit::Point => delta.y / cell.y,
                        MouseWheelUnit::Line => delta.y * scroll_lines,
                        MouseWheelUnit::Page => delta.y * s.term.lock().screen_lines() as f32,
                    };
                    let lines = s.scroll_remainder.trunc() as i32;
                    if lines == 0 {
                        continue;
                    }
                    s.scroll_remainder -= lines as f32;
                    let mode = *s.term.lock().mode();
                    let (col, row) = cell_at(term_rect, cell, pos, &s.term.lock());
                    if mode.intersects(TermMode::MOUSE_MODE) && !modifiers.shift {
                        let button = if lines > 0 { 64 } else { 65 };
                        for _ in 0..lines.unsigned_abs() {
                            if let Some(b) =
                                input::encode_mouse(button, true, col, row, modifiers, mode)
                            {
                                s.write(b);
                            }
                        }
                    } else if mode.contains(TermMode::ALT_SCREEN | TermMode::ALTERNATE_SCROLL)
                        && !modifiers.shift
                    {
                        let key = if lines > 0 {
                            Key::ArrowUp
                        } else {
                            Key::ArrowDown
                        };
                        if let Some(b) = input::encode_key(key, Modifiers::NONE, mode) {
                            s.write(b.repeat(lines.unsigned_abs() as usize));
                        }
                    } else {
                        s.term.lock().scroll_display(Scroll::Delta(lines));
                    }
                }
                _ => {}
            }
        }
    }

    fn write_active(&mut self, bytes: &[u8], now: f64) {
        if let Some(s) = self.sessions.get_mut(self.active) {
            s.term.lock().scroll_display(Scroll::Bottom);
            s.cursor.touch(now);
            s.write(bytes.to_vec());
        }
    }

    #[allow(clippy::too_many_arguments)]
    fn pointer_button(
        &mut self,
        ctx: &Context,
        term_rect: Rect,
        cell: Vec2,
        pos: Pos2,
        button: PointerButton,
        pressed: bool,
        m: Modifiers,
    ) {
        let Some(s) = self.sessions.get_mut(self.active) else {
            return;
        };
        let code = match button {
            PointerButton::Primary => 0,
            PointerButton::Middle => 1,
            PointerButton::Secondary => 2,
            _ => return,
        };
        let mut term = s.term.lock();
        let mode = *term.mode();
        let (col, row) = cell_at(term_rect, cell, pos, &term);

        if !pressed {
            match self.press.take() {
                Some(Press::Reporting) => {
                    drop(term);
                    if let Some(b) = input::encode_mouse(code, false, col, row, m, mode) {
                        s.write(b);
                    }
                }
                Some(Press::Selecting)
                    if term.selection.as_ref().is_some_and(Selection::is_empty) =>
                {
                    term.selection = None;
                }
                Some(Press::Selecting) | None => {}
            }
            return;
        }
        if !term_rect.contains(pos) {
            return;
        }

        if mode.intersects(TermMode::MOUSE_MODE) && !m.shift {
            drop(term);
            self.press = Some(Press::Reporting);
            self.reported_cell = Some((col, row));
            if let Some(b) = input::encode_mouse(code, true, col, row, m, mode) {
                s.write(b);
            }
            return;
        }
        if button == PointerButton::Secondary {
            drop(term);
            self.menu = Some(pos);
            return;
        }
        if button != PointerButton::Primary {
            return;
        }
        let point = Point::new(
            Line(row as i32 - term.grid().display_offset() as i32),
            Column(col),
        );
        if m.ctrl {
            let chars = grid::row_chars(&term, point.line);
            if let Some(url) = grid::url_at(&chars, col) {
                ctx.open_url(egui::OpenUrl::new_tab(url));
                return;
            }
        }
        // Counted here, at the press: the windowing layer only says a click
        // was double once the button is back up, a frame too late to start a
        // word selection that the same press can then drag out.
        let now = ctx.input(|i| i.time);
        let (delay, reach) = ctx.options(|o| {
            let o = &o.input_options;
            (o.max_double_click_delay, o.max_click_dist)
        });
        let clicks = match self.last_press {
            Some((at, last, count)) if now - at < delay && last.distance(pos) < reach => {
                count % 3 + 1
            }
            _ => 1,
        };
        self.last_press = Some((now, pos, clicks));
        let ty = match clicks {
            3 => SelectionType::Lines,
            2 => SelectionType::Semantic,
            _ if m.alt => SelectionType::Block,
            _ => SelectionType::Simple,
        };
        term.selection = Some(Selection::new(ty, point, side_at(term_rect, cell, pos)));
        self.press = Some(Press::Selecting);
    }

    fn pointer_moved(&mut self, term_rect: Rect, cell: Vec2, pos: Pos2, m: Modifiers) {
        let Some(s) = self.sessions.get_mut(self.active) else {
            return;
        };
        let mut term = s.term.lock();
        let mode = *term.mode();
        let (col, row) = cell_at(term_rect, cell, pos, &term);
        match self.press {
            Some(Press::Selecting) => {
                let offset = term.grid().display_offset() as i32;
                let point = Point::new(Line(row as i32 - offset), Column(col));
                if let Some(selection) = term.selection.as_mut() {
                    selection.update(point, side_at(term_rect, cell, pos));
                }
            }
            Some(Press::Reporting)
                if mode.intersects(TermMode::MOUSE_DRAG | TermMode::MOUSE_MOTION)
                    && self.reported_cell != Some((col, row)) =>
            {
                drop(term);
                self.reported_cell = Some((col, row));
                if let Some(b) = input::encode_mouse(32, true, col, row, m, mode) {
                    s.write(b);
                }
            }
            None if mode.contains(TermMode::MOUSE_MOTION)
                && term_rect.contains(pos)
                && self.reported_cell != Some((col, row)) =>
            {
                drop(term);
                self.reported_cell = Some((col, row));
                if let Some(b) = input::encode_mouse(35, true, col, row, m, mode) {
                    s.write(b);
                }
            }
            _ => {}
        }
    }

    fn paint_terminal(
        &mut self,
        painter: &Painter,
        origin: Pos2,
        snap: &Snapshot,
        focused: bool,
        now: f64,
    ) -> bool {
        let Some(metrics) = &self.metrics else {
            return false;
        };
        let (cell, text_dy, pixel) = (metrics.cell, metrics.text_dy, 1.0 / metrics.ppp);
        let theme = &self.theme;
        let font_size = self.font_size;
        let font = move |style: u8| font_id(font_size, style);
        let px = |v: f32| (v / pixel).round() * pixel;
        let rect_of = |row: usize, col: usize, len: usize| {
            Rect::from_min_max(
                pos2(
                    px(origin.x + col as f32 * cell.x),
                    origin.y + row as f32 * cell.y,
                ),
                pos2(
                    px(origin.x + (col + len) as f32 * cell.x),
                    origin.y + (row + 1) as f32 * cell.y,
                ),
            )
        };

        for span in &snap.backgrounds {
            painter.rect_filled(rect_of(span.row, span.col, span.len), 0.0, span.color);
        }
        let selection = alpha(theme.accent, if theme.dark { 0.38 } else { 0.24 });
        for span in &snap.selection {
            painter.rect_filled(rect_of(span.row, span.col, span.len), 0.0, selection);
        }

        // The cursor's animation state belongs to the tab.
        let cursor_cfg = &self.cfg.cursor;
        let shape_of = |shape: CursorShape| match shape {
            CursorShape::Beam => config::Shape::Beam,
            CursorShape::Underline => config::Shape::Underline,
            _ => config::Shape::Block,
        };
        let anim: Option<&mut CursorAnim> =
            self.sessions.get_mut(self.active).map(|s| &mut s.cursor);
        let mut cursor_paint = None;
        if let (Some(anim), Some(c)) = (anim, &snap.cursor) {
            let hollow = !focused || c.shape == CursorShape::HollowBlock;
            let shape = if hollow {
                config::Shape::Block
            } else {
                shape_of(c.shape)
            };
            let target = cursor::shape_rect(
                shape,
                rect_of(c.row, c.col, if c.wide { 2 } else { 1 }),
                pixel,
            );
            anim.set_target(target, cell, cursor_cfg, now);
            let dt = painter.ctx().input(|i| i.stable_dt).min(0.05);
            let moving = anim.step(dt);
            let (opacity, hold) =
                anim.blink(now, cursor_cfg, c.blinking && cursor_cfg.blink && focused);
            if cursor_cfg.glow && focused && !moving && opacity > 0.0 {
                anim.paint_glow(painter, c.color, opacity);
            }
            cursor_paint = Some((&*anim, c, shape, hollow, opacity, moving, hold));
        }

        for run in &snap.runs {
            let galley = painter.layout_no_wrap(run.text.clone(), font(run.style), run.fg);
            let at = pos2(
                origin.x + run.col as f32 * cell.x,
                origin.y + run.row as f32 * cell.y + text_dy,
            );
            painter.galley(at, galley, run.fg);
        }
        let paint_glyph = |painter: &Painter, g: &grid::Glyph, color: Color32| {
            let rect = rect_of(g.row, g.col, if g.wide { 2 } else { 1 });
            let mut chars = g.text.chars();
            if let (Some(c), None) = (chars.next(), chars.next())
                && boxdraw::paint(painter, c, rect, color, pixel)
            {
                return;
            }
            let galley = painter.layout_no_wrap(g.text.clone(), font(g.style), color);
            let slack = (rect.width() - galley.size().x).max(0.0);
            painter.galley(
                pos2(rect.left() + slack / 2.0, rect.top() + text_dy),
                galley,
                color,
            );
        };
        for glyph in &snap.glyphs {
            paint_glyph(painter, glyph, glyph.fg);
        }

        let thickness = (cell.y / 14.0 / pixel).round().max(1.0) * pixel;
        for d in &snap.decorations {
            let rect = rect_of(d.row, d.col, 1);
            let under = px(rect.bottom() - thickness * 2.0);
            let bar = |y: f32, x0: f32, x1: f32| {
                painter.rect_filled(
                    Rect::from_min_max(pos2(x0, y), pos2(x1, y + thickness)),
                    0.0,
                    d.color,
                );
            };
            match d.kind {
                Line_::Under => bar(under, rect.left(), rect.right()),
                Line_::Strike => bar(px(rect.center().y), rect.left(), rect.right()),
                Line_::Double => {
                    bar(under - thickness, rect.left(), rect.right());
                    bar(under + thickness, rect.left(), rect.right());
                }
                Line_::Dotted => {
                    let step = rect.width() / 4.0;
                    bar(under, rect.left(), rect.left() + step);
                    bar(under, rect.left() + step * 2.0, rect.left() + step * 3.0);
                }
                Line_::Curl => {
                    let points = (0..=8)
                        .map(|i| {
                            let t = i as f32 / 8.0;
                            pos2(
                                rect.left() + rect.width() * t,
                                under + (t * std::f32::consts::TAU).sin() * thickness * 1.5,
                            )
                        })
                        .collect();
                    painter.add(Shape::line(points, Stroke::new(thickness, d.color)));
                }
            }
        }

        let Some((anim, c, shape, hollow, opacity, moving, hold)) = cursor_paint else {
            return false;
        };
        anim.paint_vfx(painter, c.color);
        if opacity > 0.0 {
            anim.paint(painter, c.color, opacity, hollow);
            // The character under a solid block is redrawn in the background
            // colour, clipped to wherever the block currently is.
            if shape == config::Shape::Block && !hollow && !c.text.trim().is_empty() {
                let glyph = grid::Glyph {
                    row: c.row,
                    col: c.col,
                    wide: c.wide,
                    text: c.text.clone(),
                    fg: theme.bg,
                    style: c.style,
                };
                let clipped = painter.with_clip_rect(anim.bounds().intersect(painter.clip_rect()));
                paint_glyph(&clipped, &glyph, alpha(theme.bg, opacity));
            }
        }
        let ctx = painter.ctx();
        match hold {
            _ if moving => ctx.request_repaint(),
            None => ctx.request_repaint(),
            Some(secs) if secs.is_finite() => {
                ctx.request_repaint_after(Duration::from_secs_f64(secs))
            }
            Some(_) => {}
        }
        moving
    }

    fn paint_header(
        &self,
        ui: &egui::Ui,
        rect: Rect,
        focused: bool,
        maximized: bool,
        actions: &mut Vec<Action>,
    ) {
        let theme = &self.theme;
        let painter = ui.painter();
        let opacity = self.cfg.window.opacity.clamp(0.0, 1.0);
        painter.rect_filled(rect, 0.0, alpha(theme.chrome, opacity));

        let (hover, pressed, double, middle) = ui.input(|i| {
            (
                i.pointer.hover_pos(),
                i.pointer.primary_pressed(),
                i.pointer.button_double_clicked(PointerButton::Primary),
                i.pointer.button_pressed(PointerButton::Middle),
            )
        });
        let hovered = |r: Rect| hover.is_some_and(|p| r.contains(p));
        let mut consumed = false;
        let ui_font = |size: f32| FontId::new(size, FontFamily::Proportional);
        let dim = |c: Color32| {
            if focused {
                c
            } else {
                mix(c, theme.chrome, 0.45)
            }
        };

        // Mark: a prompt in the accent colour.
        let mark =
            Rect::from_center_size(pos2(rect.left() + 22.0, rect.center().y), Vec2::splat(20.0));
        painter.rect_filled(mark, 5.0, dim(theme.accent));
        let ink = Stroke::new(1.6, theme.on_accent);
        let c = mark.center();
        painter.add(Shape::line(
            vec![
                pos2(c.x - 5.0, c.y - 4.0),
                pos2(c.x - 1.0, c.y),
                pos2(c.x - 5.0, c.y + 4.0),
            ],
            ink,
        ));
        painter.line_segment(
            [pos2(c.x + 0.5, c.y + 4.0), pos2(c.x + 5.5, c.y + 4.0)],
            ink,
        );

        // Window controls, right to left: close, maximize, minimize.
        let button = vec2(34.0, 26.0);
        let control = |n: f32| {
            Rect::from_center_size(
                pos2(rect.right() - 8.0 - button.x * (n + 0.5), rect.center().y),
                button,
            )
        };
        let icon = Stroke::new(1.2, dim(theme.text2));
        for (n, action) in [
            Action::CloseWindow,
            Action::ToggleMaximize,
            Action::Minimize,
        ]
        .into_iter()
        .enumerate()
        {
            let r = control(n as f32);
            let hot = hovered(r);
            let mut stroke = icon;
            if hot {
                let danger = matches!(action, Action::CloseWindow);
                painter.rect_filled(r, 6.0, if danger { theme.red } else { theme.hover });
                stroke.color = if danger { Color32::WHITE } else { theme.fg };
            }
            let (c, h) = (r.center(), 4.5);
            match action {
                Action::CloseWindow => {
                    painter.line_segment([pos2(c.x - h, c.y - h), pos2(c.x + h, c.y + h)], stroke);
                    painter.line_segment([pos2(c.x - h, c.y + h), pos2(c.x + h, c.y - h)], stroke);
                }
                Action::ToggleMaximize if maximized => {
                    let back = Rect::from_min_size(
                        pos2(c.x - h + 2.5, c.y - h - 0.5),
                        Vec2::splat(h * 2.0 - 2.0),
                    );
                    let front = Rect::from_min_size(
                        pos2(c.x - h, c.y - h + 2.0),
                        Vec2::splat(h * 2.0 - 2.0),
                    );
                    painter.rect_stroke(back, 1.5, stroke, StrokeKind::Middle);
                    painter.rect_filled(front, 1.5, if hot { theme.hover } else { theme.chrome });
                    painter.rect_stroke(front, 1.5, stroke, StrokeKind::Middle);
                }
                Action::ToggleMaximize => {
                    painter.rect_stroke(
                        Rect::from_center_size(c, Vec2::splat(h * 2.0)),
                        1.5,
                        stroke,
                        StrokeKind::Middle,
                    );
                }
                _ => {
                    painter.line_segment([pos2(c.x - h, c.y), pos2(c.x + h, c.y)], stroke);
                }
            }
            if hot && pressed {
                actions.push(action);
                consumed = true;
            }
        }

        // Tabs share the space between the mark and the controls.
        let left = rect.left() + 42.0;
        let right = control(2.0).left() - 8.0;
        let plus = 28.0;
        let count = self.sessions.len().max(1) as f32;
        let width = ((right - left - plus - 6.0) / count).clamp(44.0, 210.0);
        let top = rect.top() + 7.0;
        for (i, session) in self.sessions.iter().enumerate() {
            let tab = Rect::from_min_max(
                pos2(left + width * i as f32, top),
                pos2(left + width * (i + 1) as f32 - 2.0, rect.bottom()),
            );
            let active = i == self.active;
            let hot = hovered(tab);
            let rounding = egui::CornerRadius {
                nw: 7,
                ne: 7,
                sw: 0,
                se: 0,
            };
            if active {
                painter.rect_filled(tab, rounding, alpha(theme.bg, opacity));
                let line = Rect::from_min_max(
                    pos2(tab.left() + 9.0, tab.top()),
                    pos2(tab.right() - 9.0, tab.top() + 2.0),
                );
                painter.rect_filled(line, 1.0, dim(theme.accent));
            } else if hot {
                painter.rect_filled(tab.shrink2(vec2(0.0, 3.0)), 6.0, theme.hover);
            }

            let close =
                Rect::from_center_size(pos2(tab.right() - 15.0, tab.center().y), Vec2::splat(18.0));
            let show_close = (active || hot) && width > 70.0;
            let belled = !active && session.bell_at.is_some();
            let mut text_left = tab.left() + 12.0;
            if belled {
                painter.circle_filled(pos2(text_left + 3.0, tab.center().y), 3.0, theme.yellow);
                text_left += 12.0;
            }
            let color = dim(if active { theme.fg } else { theme.text3 });
            let mut job =
                LayoutJob::simple_singleline(Self::tab_title(session), ui_font(12.5), color);
            let room = (if show_close {
                close.left()
            } else {
                tab.right() - 8.0
            }) - text_left;
            job.wrap = TextWrapping::truncate_at_width(room.max(0.0));
            let galley = painter.layout_job(job);
            painter.galley(
                pos2(text_left, tab.center().y - galley.size().y / 2.0),
                galley,
                color,
            );

            if show_close {
                let over = hovered(close);
                if over {
                    painter.rect_filled(close, 5.0, mix(theme.hover, theme.fg, 0.12));
                }
                let s = Stroke::new(1.2, if over { theme.fg } else { theme.text3 });
                let (c, h) = (close.center(), 3.5);
                painter.line_segment([pos2(c.x - h, c.y - h), pos2(c.x + h, c.y + h)], s);
                painter.line_segment([pos2(c.x - h, c.y + h), pos2(c.x + h, c.y - h)], s);
                if over && pressed {
                    actions.push(Action::CloseTab(i));
                    consumed = true;
                }
            }
            if hot && !consumed {
                if middle {
                    actions.push(Action::CloseTab(i));
                    consumed = true;
                } else if pressed {
                    actions.push(Action::Select(i));
                    consumed = true;
                }
            }
        }

        let add = Rect::from_center_size(
            pos2(
                left + width * self.sessions.len() as f32 + plus / 2.0 + 2.0,
                top + (rect.bottom() - top) / 2.0,
            ),
            Vec2::splat(24.0),
        );
        let hot = hovered(add);
        if hot {
            painter.rect_filled(add, 6.0, theme.hover);
        }
        let s = Stroke::new(1.3, if hot { theme.fg } else { dim(theme.text2) });
        let c = add.center();
        painter.line_segment([pos2(c.x - 4.5, c.y), pos2(c.x + 4.5, c.y)], s);
        painter.line_segment([pos2(c.x, c.y - 4.5), pos2(c.x, c.y + 4.5)], s);
        if hot && pressed {
            actions.push(Action::NewTab);
            consumed = true;
        }

        if !consumed && hovered(rect) {
            if double {
                actions.push(Action::ToggleMaximize);
            } else if pressed {
                actions.push(Action::Drag);
            }
        }
    }

    fn paint_status(&self, ui: &egui::Ui, rect: Rect, snap: &Snapshot, size: GridSize) {
        let theme = &self.theme;
        let painter = ui.painter().with_clip_rect(rect);
        painter.rect_filled(
            rect,
            0.0,
            alpha(theme.chrome, self.cfg.window.opacity.clamp(0.0, 1.0)),
        );
        painter.hline(
            rect.x_range(),
            rect.top(),
            Stroke::new(1.0, alpha(theme.stroke, 0.6)),
        );
        let mono = FontId::new(11.0, FontFamily::Monospace);
        let mid = rect.center().y;
        let text = |x: f32, s: String, color: Color32, right: bool| -> f32 {
            let galley = painter.layout_no_wrap(s, mono.clone(), color);
            let w = galley.size().x;
            let x = if right { x - w } else { x };
            painter.galley(pos2(x, mid - galley.size().y / 2.0), galley, color);
            w
        };

        let mut x = rect.left() + 8.0;
        if let Some(session) = self.sessions.get(self.active) {
            let name = session.foreground_name();
            let name = if name.is_empty() {
                "shell".to_owned()
            } else {
                name
            };
            let galley = painter.layout_no_wrap(name, mono.clone(), theme.on_accent);
            let chip = Rect::from_min_size(pos2(x, mid - 8.0), vec2(galley.size().x + 14.0, 16.0));
            painter.rect_filled(chip, 4.0, theme.accent);
            painter.galley(
                pos2(chip.left() + 7.0, mid - galley.size().y / 2.0),
                galley,
                theme.on_accent,
            );
            x = chip.right() + 10.0;

            if let Some(error) = &self.error {
                text(x, error.clone(), theme.red, false);
            } else if let Some(cwd) = session.cwd() {
                let home = config::home();
                let shown = match cwd.strip_prefix(&home) {
                    Ok(rest) if rest.as_os_str().is_empty() => "~".to_owned(),
                    Ok(rest) => format!("~/{}", rest.display()),
                    Err(_) => cwd.display().to_string(),
                };
                text(x, shown, theme.text2, false);
            }
        }

        let mut x = rect.right() - 10.0;
        x -= text(
            x,
            format!("{}×{}", size.columns, size.lines),
            theme.text3,
            true,
        ) + 14.0;
        if self.font_size != self.cfg.font.size {
            x -= text(x, format!("{}pt", self.font_size), theme.text3, true) + 14.0;
        }
        if snap.display_offset > 0 {
            text(
                x,
                format!("↑ {}/{}", snap.display_offset, snap.history),
                theme.yellow,
                true,
            );
        }
    }

    fn paint_menu(&self, ui: &egui::Ui, at: Pos2, full: Rect) {
        let theme = &self.theme;
        let painter = ui.painter();
        let (frame, rows) = menu_rects(at, full);
        let hover = ui.input(|i| i.pointer.hover_pos());
        let has_selection = self.has_selection();
        let keys = self.cfg.terminal.clipboard_keys;

        painter.rect_filled(
            frame.translate(vec2(0.0, 3.0)).expand(2.0),
            10.0,
            alpha(Color32::BLACK, if theme.dark { 0.35 } else { 0.12 }),
        );
        painter.rect_filled(frame, 8.0, theme.surface);
        painter.rect_stroke(
            frame,
            8.0,
            Stroke::new(1.0, theme.stroke),
            StrokeKind::Inside,
        );
        let font = FontId::new(12.5, FontFamily::Proportional);
        for (item, row) in MENU.into_iter().zip(rows) {
            let (label, key) = match (item, self.spanish) {
                (MenuItem::Copy, true) => ("Copiar", "C"),
                (MenuItem::Copy, false) => ("Copy", "C"),
                (MenuItem::Cut, true) => ("Cortar", "X"),
                (MenuItem::Cut, false) => ("Cut", "X"),
                (MenuItem::Paste, true) => ("Pegar", "V"),
                (MenuItem::Paste, false) => ("Paste", "V"),
                (MenuItem::SelectAll, true) => ("Seleccionar todo", "A"),
                (MenuItem::SelectAll, false) => ("Select all", "A"),
            };
            let short = keys && item != MenuItem::SelectAll;
            let hint = format!("Ctrl+{}{key}", if short { "" } else { "Shift+" });
            let enabled = has_selection || matches!(item, MenuItem::Paste | MenuItem::SelectAll);
            let hot = enabled && hover.is_some_and(|p| row.contains(p));
            if hot {
                painter.rect_filled(row, 5.0, theme.hover);
            }
            let color = if enabled { theme.fg } else { theme.text3 };
            let galley = painter.layout_no_wrap(label.to_owned(), font.clone(), color);
            painter.galley(
                pos2(row.left() + 10.0, row.center().y - galley.size().y / 2.0),
                galley,
                color,
            );
            let dim = if enabled {
                theme.text3
            } else {
                mix(theme.text3, theme.surface, 0.5)
            };
            let galley = painter.layout_no_wrap(hint, font.clone(), dim);
            painter.galley(
                pos2(
                    row.right() - 10.0 - galley.size().x,
                    row.center().y - galley.size().y / 2.0,
                ),
                galley,
                dim,
            );
        }
        if hover.is_some_and(|p| frame.contains(p)) {
            ui.ctx().set_cursor_icon(CursorIcon::Default);
        }
    }

    fn resize_edges(&self, ui: &egui::Ui, full: Rect) -> bool {
        let Some(pos) = ui.input(|i| i.pointer.hover_pos()) else {
            return false;
        };
        let (w, n, e, s) = (
            pos.x < full.left() + RESIZE_EDGE,
            pos.y < full.top() + RESIZE_EDGE,
            pos.x > full.right() - RESIZE_EDGE,
            pos.y > full.bottom() - RESIZE_EDGE,
        );
        use ResizeDirection::*;
        let (direction, icon) = match (w, n, e, s) {
            (true, true, ..) => (NorthWest, CursorIcon::ResizeNorthWest),
            (_, true, true, _) => (NorthEast, CursorIcon::ResizeNorthEast),
            (true, _, _, true) => (SouthWest, CursorIcon::ResizeSouthWest),
            (_, _, true, true) => (SouthEast, CursorIcon::ResizeSouthEast),
            (true, ..) => (West, CursorIcon::ResizeWest),
            (_, true, ..) => (North, CursorIcon::ResizeNorth),
            (_, _, true, _) => (East, CursorIcon::ResizeEast),
            (.., true) => (South, CursorIcon::ResizeSouth),
            _ => return false,
        };
        ui.ctx().set_cursor_icon(icon);
        if ui.input(|i| i.pointer.primary_pressed()) {
            ui.ctx()
                .send_viewport_cmd(ViewportCommand::BeginResize(direction));
        }
        true
    }
}

/// Everything in the window that is not the grid.
fn chrome(cfg: &Config) -> Vec2 {
    let status = if cfg.window.status_bar { STATUS } else { 0.0 };
    vec2(0.0, HEADER + status) + Vec2::splat(cfg.window.padding * 2.0)
}

/// The cell size and the offset that centres a line of text in it.
fn cell_metrics(ctx: &Context, size: f32, line_height: f32) -> (Vec2, f32) {
    // Measured over a long run so the cell width is whatever the text layout
    // really advances by, sub-pixel rounding included.
    const SAMPLE: usize = 64;
    let ppp = ctx.pixels_per_point();
    let galley = ctx.fonts_mut(|f| {
        f.layout_no_wrap(
            "M".repeat(SAMPLE),
            font_id(size, grid::REGULAR),
            Color32::WHITE,
        )
    });
    let row = galley.size().y;
    let height = (row * line_height.max(0.8) * ppp).ceil() / ppp;
    (
        vec2(galley.size().x / SAMPLE as f32, height),
        (height - row) / 2.0,
    )
}

/// The window size that holds the configured grid, worked out before there
/// is a window by laying the font out in a throwaway context.
pub fn window_size(fonts: &egui::FontDefinitions, cfg: &Config) -> Vec2 {
    let ctx = Context::default();
    ctx.set_fonts(fonts.clone());
    let mut cell = Vec2::ZERO;
    let mut output = ctx.run_pass(egui::RawInput::default(), |ctx| {
        cell = cell_metrics(ctx, cfg.font.size, cfg.font.line_height).0;
    });
    // Nothing will ever draw from this context's font atlas.
    output.textures_delta.clear();
    let grid = vec2(
        cfg.window.columns.max(2) as f32,
        cfg.window.rows.max(1) as f32,
    );
    (grid * cell + chrome(cfg)).ceil()
}

fn font_id(size: f32, style: u8) -> FontId {
    let family = match style {
        grid::REGULAR => FontFamily::Monospace,
        grid::BOLD => FontFamily::Name(fonts::BOLD.into()),
        grid::ITALIC => FontFamily::Name(fonts::ITALIC.into()),
        _ => FontFamily::Name(fonts::BOLD_ITALIC.into()),
    };
    FontId::new(size, family)
}

fn cell_at<T>(
    rect: Rect,
    cell: Vec2,
    pos: Pos2,
    term: &alacritty_terminal::Term<T>,
) -> (usize, usize) {
    let col = ((pos.x - rect.left()) / cell.x).floor().max(0.0) as usize;
    let row = ((pos.y - rect.top()) / cell.y).floor().max(0.0) as usize;
    (
        col.min(term.columns() - 1),
        row.min(term.screen_lines() - 1),
    )
}

fn side_at(rect: Rect, cell: Vec2, pos: Pos2) -> Side {
    if ((pos.x - rect.left()) / cell.x).rem_euclid(1.0) < 0.5 {
        Side::Left
    } else {
        Side::Right
    }
}

impl eframe::App for App {
    fn clear_color(&self, _visuals: &egui::Visuals) -> [f32; 4] {
        [0.0; 4]
    }

    fn ui(&mut self, ui: &mut egui::Ui, _frame: &mut eframe::Frame) {
        let ctx = ui.ctx().clone();
        let (now, focused, maximized) = ctx.input(|i| {
            let v = i.viewport();
            (
                i.time,
                i.focused,
                v.maximized.unwrap_or(false) || v.fullscreen.unwrap_or(false),
            )
        });
        let started = *self.started.get_or_insert(now);

        if self.cfg.theme == ThemeMode::Auto {
            let dark = self.shell_dark.load(Ordering::Relaxed);
            if dark != self.theme.dark {
                self.theme = Theme::new(dark);
            }
        }
        self.measure(&ctx);
        let cell = self.metrics.as_ref().map_or(vec2(8.0, 16.0), |m| m.cell);
        let window = self.cfg.window.clone();
        let wanted = GridSize {
            columns: window.columns.max(2),
            lines: window.rows.max(1),
        };

        let full = ui.max_rect();
        let header = Rect::from_min_max(full.min, pos2(full.right(), full.top() + HEADER));
        let status_h = if window.status_bar { STATUS } else { 0.0 };
        let status = Rect::from_min_max(pos2(full.left(), full.bottom() - status_h), full.max);
        let body = Rect::from_min_max(header.left_bottom(), status.right_top());
        let term_rect = body.shrink(window.padding);
        let size = GridSize {
            columns: ((term_rect.width() / cell.x + 0.01).floor() as usize).max(2),
            lines: ((term_rect.height() / cell.y + 0.01).floor() as usize).max(1),
        };
        // The window opens before the font is measured, so its first size is
        // a guess; ask for the one that holds exactly the configured grid. A
        // compositor that sizes windows itself may refuse, hence the limit.
        if self.size_requests > 0 {
            self.size_requests -= 1;
            if size == wanted || maximized {
                self.size_requests = 0;
            } else {
                let fit = vec2(wanted.columns as f32 * cell.x, wanted.lines as f32 * cell.y)
                    + self.chrome();
                ctx.send_viewport_cmd(ViewportCommand::InnerSize(fit.ceil()));
                ctx.request_repaint();
            }
        }
        let cell_px = self.metrics.as_ref().map_or((8, 16), |m| {
            ((m.cell.x * m.ppp) as u16, (m.cell.y * m.ppp) as u16)
        });

        if self.next_id == 1 {
            let launch = Launch {
                command: self.launch.command.clone(),
                cwd: self.launch.cwd.clone(),
            };
            self.spawn(&ctx, size, &launch);
            if self.sessions.is_empty() {
                eprintln!(
                    "wonderterm: {}",
                    self.error.as_deref().unwrap_or("no shell")
                );
                ctx.send_viewport_cmd(ViewportCommand::Close);
                return;
            }
        }
        for s in &mut self.sessions {
            s.resize(size, cell_px);
        }
        self.drain_events(&ctx, now);
        if self.sessions.is_empty() {
            ctx.send_viewport_cmd(ViewportCommand::Close);
            return;
        }

        let mut actions = Vec::new();
        let resizing = !maximized && self.press.is_none() && self.resize_edges(ui, full);

        // An open menu takes the click that picks from it or dismisses it, and
        // Escape; any other key closes it and goes on to the terminal.
        let mut menu_took_input = false;
        if let Some(at) = self.menu {
            let (pressed, primary, secondary, pos, escape, typed) = ctx.input(|i| {
                (
                    i.pointer.any_pressed(),
                    i.pointer.primary_pressed(),
                    i.pointer.secondary_pressed(),
                    i.pointer.interact_pos(),
                    i.key_pressed(Key::Escape),
                    i.events.iter().any(|e| {
                        matches!(
                            e,
                            Event::Key { pressed: true, .. }
                                | Event::Text(_)
                                | Event::Copy
                                | Event::Cut
                                | Event::Paste(_)
                        )
                    }),
                )
            });
            if pressed {
                self.menu = None;
                // A right click somewhere else opens the menu there instead.
                menu_took_input = !secondary;
                let (_, rows) = menu_rects(at, full);
                let picked = pos.and_then(|p| rows.iter().position(|r| r.contains(p)));
                if let (true, Some(i)) = (primary, picked) {
                    self.run_menu(&ctx, MENU[i], now);
                }
            } else if typed || !focused {
                self.menu = None;
                menu_took_input = escape;
            }
        }
        if !resizing && !menu_took_input {
            self.handle_input(ui, term_rect, now, &mut actions);
        }

        let theme = self.theme.clone();
        let snap = {
            let s = &mut self.sessions[self.active];
            if s.bell_at.is_some_and(|t| now - t > BELL_SECS) {
                s.bell_at = None;
            }
            grid::snapshot(&s.term.lock(), &theme)
        };

        let painter = ui.painter().with_clip_rect(body);
        painter.rect_filled(body, 0.0, alpha(theme.bg, window.opacity.clamp(0.0, 1.0)));
        self.paint_terminal(&painter, term_rect.min, &snap, focused, now);

        if snap.display_offset > 0 && snap.history > 0 {
            let track = Rect::from_min_max(
                pos2(body.right() - 5.0, body.top() + 3.0),
                pos2(body.right() - 2.0, body.bottom() - 3.0),
            );
            let total = (snap.history + size.lines) as f32;
            let len = (track.height() * size.lines as f32 / total).max(18.0);
            let at = 1.0 - snap.display_offset as f32 / snap.history as f32;
            let top = track.top() + (track.height() - len) * at;
            let thumb = Rect::from_min_max(pos2(track.left(), top), pos2(track.right(), top + len));
            painter.rect_filled(thumb, 1.5, alpha(theme.text3, 0.7));
        }
        if let Some(t) = self.sessions[self.active].bell_at {
            let fade = 1.0 - ((now - t) / BELL_SECS) as f32;
            painter.rect_stroke(
                body,
                0.0,
                Stroke::new(2.0, alpha(theme.yellow, 0.8 * fade)),
                StrokeKind::Inside,
            );
            ctx.request_repaint();
        }

        self.paint_header(ui, header, focused, maximized, &mut actions);
        if window.status_bar {
            self.paint_status(ui, status, &snap, size);
        }
        if !resizing
            && term_rect.contains(ctx.input(|i| i.pointer.hover_pos()).unwrap_or(Pos2::ZERO))
        {
            let reporting = snap.mode.intersects(TermMode::MOUSE_MODE);
            ctx.set_cursor_icon(if reporting {
                CursorIcon::Default
            } else {
                CursorIcon::Text
            });
        }

        if let Some(at) = self.menu {
            self.paint_menu(ui, at, full);
        }

        for action in actions {
            match action {
                Action::NewTab => {
                    let cwd = self.sessions.get(self.active).and_then(Session::cwd);
                    self.spawn(
                        &ctx,
                        size,
                        &Launch {
                            command: Vec::new(),
                            cwd,
                        },
                    );
                }
                Action::CloseTab(i) if i < self.sessions.len() => {
                    self.sessions.remove(i);
                    if self.active > i || self.active >= self.sessions.len() {
                        self.active = self.active.saturating_sub(1);
                    }
                    if self.sessions.is_empty() {
                        ctx.send_viewport_cmd(ViewportCommand::Close);
                    }
                }
                Action::CloseTab(_) => {}
                Action::Select(i) => self.active = i.min(self.sessions.len().saturating_sub(1)),
                Action::Minimize => ctx.send_viewport_cmd(ViewportCommand::Minimized(true)),
                Action::ToggleMaximize => {
                    ctx.send_viewport_cmd(ViewportCommand::Maximized(!maximized))
                }
                Action::CloseWindow => ctx.send_viewport_cmd(ViewportCommand::Close),
                Action::Drag => ctx.send_viewport_cmd(ViewportCommand::StartDrag),
            }
            ctx.request_repaint();
        }

        if let Some(session) = self.sessions.get(self.active) {
            let title = Self::tab_title(session);
            if title != self.title {
                self.title = title.clone();
                ctx.send_viewport_cmd(ViewportCommand::Title(format!("{title} — wonderterm")));
            }
        }

        if let Some((path, delay)) = &self.shot {
            match &self.shot_done {
                Some(done) if done.load(Ordering::Relaxed) => {
                    ctx.send_viewport_cmd(ViewportCommand::Close)
                }
                Some(_) => {}
                None if now - started >= *delay => {
                    let done = Arc::new(AtomicBool::new(false));
                    self.shot_done = Some(done.clone());
                    let (path, wake) = (path.clone(), ctx.clone());
                    ctx.request_screenshot(move |image| {
                        let bytes: Vec<u8> = image
                            .pixels
                            .iter()
                            .flat_map(|p| p.to_srgba_unmultiplied())
                            .collect();
                        let [w, h] = image.size;
                        let saved = image::RgbaImage::from_raw(w as u32, h as u32, bytes)
                            .ok_or_else(|| "bad frame".to_owned())
                            .and_then(|img| img.save(&path).map_err(|e| e.to_string()));
                        if let Err(e) = saved {
                            eprintln!("wonderterm: {}: {e}", path.display());
                        }
                        done.store(true, Ordering::Relaxed);
                        wake.request_repaint();
                    });
                }
                None => {}
            }
            ctx.request_repaint();
        }
    }
}

/// The app driven the way the window drives it, with a real program on a
/// real PTY and no display.
#[cfg(test)]
mod tests {
    use std::time::Instant;

    use eframe::App as _;
    use eframe::egui::{OutputCommand, RawInput, ViewportId};

    use super::*;

    struct Bench {
        ctx: Context,
        app: App,
        frame: eframe::Frame,
        time: f64,
        dir: tempfile::TempDir,
        copied: Vec<String>,
        paste_requests: usize,
    }

    impl Bench {
        /// Runs `script` under bash; `{out}` in it is a file in a scratch dir.
        fn new(script: &str, keys: bool) -> Self {
            let dir = tempfile::tempdir().unwrap();
            let script = script.replace("{out}", dir.path().join("out").to_str().unwrap());
            let mut cfg = Config {
                theme: ThemeMode::Dark,
                ..Default::default()
            };
            cfg.terminal.clipboard_keys = keys;
            let ctx = Context::default();
            let launch = Launch {
                command: ["bash", "--norc", "--noprofile", "-c", &script]
                    .map(String::from)
                    .to_vec(),
                cwd: None,
            };
            let app = App::new(
                &eframe::CreationContext::_new_kittest(ctx.clone()),
                cfg,
                None,
                fonts::definitions(""),
                launch,
                None,
            );
            Self {
                ctx,
                app,
                frame: eframe::Frame::_new_kittest(),
                time: 0.0,
                dir,
                copied: Vec::new(),
                paste_requests: 0,
            }
        }

        /// One frame, 50 ms after the last: the presses of a double click
        /// land 150 ms apart.
        fn step(&mut self, events: Vec<Event>) {
            self.time += 0.05;
            let input = RawInput {
                screen_rect: Some(Rect::from_min_size(Pos2::ZERO, vec2(960.0, 640.0))),
                time: Some(self.time),
                focused: true,
                events,
                ..Default::default()
            };
            let (app, frame) = (&mut self.app, &mut self.frame);
            let mut out = self.ctx.run_ui(input, |ui| app.ui(ui, frame));
            // Nothing draws here, so the font atlas has nowhere to go.
            out.textures_delta.clear();
            for command in out.platform_output.commands {
                if let OutputCommand::CopyText(text) = command {
                    self.copied.push(text);
                }
            }
            if let Some(viewport) = out.viewport_output.get(&ViewportId::ROOT) {
                self.paste_requests += viewport
                    .commands
                    .iter()
                    .filter(|c| matches!(c, ViewportCommand::RequestPaste))
                    .count();
            }
        }

        fn row(&self, row: i32) -> String {
            let Some(s) = self.app.sessions.first() else {
                return String::new();
            };
            let chars = grid::row_chars(&s.term.lock(), Line(row));
            chars.into_iter().collect::<String>().trim_end().to_owned()
        }

        fn wait(&mut self, what: &str, mut done: impl FnMut(&Self) -> bool) {
            let start = Instant::now();
            while !done(self) {
                assert!(
                    start.elapsed() < Duration::from_secs(10),
                    "timed out waiting for {what}; first row: {:?}",
                    self.row(0)
                );
                std::thread::sleep(Duration::from_millis(10));
                self.step(vec![]);
            }
        }

        fn wait_row(&mut self, text: &str) {
            self.wait(text, |b| b.row(0) == text);
        }

        fn out(&mut self) -> String {
            let path = self.dir.path().join("out");
            self.wait("the program's output file", |_| {
                std::fs::read_to_string(&path).is_ok_and(|t| t.ends_with('\n'))
            });
            std::fs::read_to_string(path).unwrap().trim().to_owned()
        }

        /// The middle of a cell of the terminal.
        fn cell(&self, col: usize, row: usize) -> Pos2 {
            let cell = self.app.metrics.as_ref().unwrap().cell;
            let pad = self.app.cfg.window.padding;
            pos2(
                pad + (col as f32 + 0.5) * cell.x,
                HEADER + pad + (row as f32 + 0.5) * cell.y,
            )
        }

        /// Press and release in separate frames, as a hand does it.
        fn click(&mut self, pos: Pos2, button: PointerButton) {
            self.step(vec![Event::PointerMoved(pos)]);
            for pressed in [true, false] {
                self.step(vec![Event::PointerButton {
                    pos,
                    button,
                    pressed,
                    modifiers: Modifiers::NONE,
                }]);
            }
        }

        fn double_click(&mut self, col: usize) {
            let pos = self.cell(col, 0);
            self.click(pos, PointerButton::Primary);
            self.click(pos, PointerButton::Primary);
        }

        /// Right click away from the text, then pick an entry of the menu.
        fn menu(&mut self, item: MenuItem) {
            let at = pos2(400.0, 300.0);
            self.click(at, PointerButton::Secondary);
            assert_eq!(self.app.menu, Some(at));
            let full = Rect::from_min_size(Pos2::ZERO, vec2(960.0, 640.0));
            let (_, rows) = menu_rects(at, full);
            let row = rows[MENU.iter().position(|i| *i == item).unwrap()];
            self.click(row.center(), PointerButton::Primary);
            assert_eq!(self.app.menu, None);
        }

        fn key(&mut self, key: Key) {
            self.step(vec![Event::Key {
                key,
                physical_key: None,
                pressed: true,
                repeat: false,
                modifiers: Modifiers::NONE,
            }]);
        }
    }

    /// A line edited by readline, the way a shell prompt is.
    const PROMPT: &str =
        r#"read -e -i "git status now" line; printf '[%s]\n' "$line" > {out}; sleep 1"#;
    /// Prints the bytes of the first key it is sent.
    const FIRST_KEY: &str = "stty raw -echo; printf READY; head -c1 | od -An -tx1 > {out}; sleep 1";

    #[test]
    fn ctrl_x_cuts_the_selected_word_out_of_the_prompt_and_ctrl_v_pastes() {
        let mut b = Bench::new(PROMPT, true);
        b.wait_row("git status now");
        b.double_click(6);
        b.step(vec![Event::Cut]);
        assert_eq!(b.copied, ["status"]);
        b.wait_row("git  now");
        b.step(vec![Event::Paste("log".into())]);
        b.wait_row("git log now");
        b.key(Key::Enter);
        assert_eq!(b.out(), "[git log now]");
    }

    #[test]
    fn a_third_click_takes_the_whole_line() {
        let mut b = Bench::new(PROMPT, true);
        b.wait_row("git status now");
        b.double_click(6);
        let pos = b.cell(6, 0);
        b.click(pos, PointerButton::Primary);
        b.step(vec![Event::Cut]);
        assert_eq!(b.copied, ["git status now\n"]);
        b.wait_row("");
        b.key(Key::Enter);
        assert_eq!(b.out(), "[]");
    }

    #[test]
    fn ctrl_c_copies_a_selection_and_leaves_the_program_alone() {
        let mut b = Bench::new(PROMPT, true);
        b.wait_row("git status now");
        b.double_click(1);
        b.step(vec![Event::Copy]);
        assert_eq!(b.copied, ["git"]);
        assert!(!b.app.has_selection());
        b.key(Key::Enter);
        assert_eq!(b.out(), "[git status now]");
    }

    #[test]
    fn the_menu_copies_cuts_and_pastes() {
        let mut b = Bench::new(PROMPT, true);
        b.wait_row("git status now");
        b.double_click(12);
        b.menu(MenuItem::Copy);
        assert_eq!(b.copied, ["now"]);
        b.double_click(1);
        b.menu(MenuItem::Cut);
        assert_eq!(b.copied, ["now", "git"]);
        b.wait_row(" status now");
        b.menu(MenuItem::Paste);
        assert_eq!(b.paste_requests, 1);
        b.step(vec![Event::Paste("hg".into())]);
        b.wait_row("hg status now");
        b.menu(MenuItem::SelectAll);
        assert!(b.app.has_selection());
        b.key(Key::Enter);
        assert_eq!(b.out(), "[hg status now]");
    }

    #[test]
    fn a_right_click_or_a_dismissed_menu_sends_nothing_to_the_program() {
        let mut b = Bench::new(PROMPT, true);
        b.wait_row("git status now");
        b.click(pos2(400.0, 300.0), PointerButton::Secondary);
        assert!(b.app.menu.is_some());
        b.key(Key::Escape);
        assert!(b.app.menu.is_none());
        b.click(pos2(400.0, 300.0), PointerButton::Secondary);
        b.click(pos2(40.0, 500.0), PointerButton::Primary);
        assert!(b.app.menu.is_none());
        assert!(b.copied.is_empty());
        b.key(Key::Enter);
        assert_eq!(b.out(), "[git status now]");
    }

    #[test]
    fn without_a_selection_the_keys_are_the_control_codes() {
        for (event, code) in [(Event::Copy, "03"), (Event::Cut, "18")] {
            let mut b = Bench::new(FIRST_KEY, true);
            b.wait_row("READY");
            b.step(vec![event]);
            assert_eq!(b.out(), code);
            assert!(b.copied.is_empty());
        }
    }

    #[test]
    fn a_full_screen_program_gets_ctrl_v_itself() {
        let script = format!("printf '\\033[?1049h'; {FIRST_KEY}");
        let mut b = Bench::new(&script, true);
        b.wait_row("READY");
        b.step(vec![Event::Paste("clipboard".into())]);
        assert_eq!(b.out(), "16");
    }

    #[test]
    fn with_the_keys_off_only_the_shifted_forms_touch_the_clipboard() {
        let interrupted = format!("trap 'echo INT > {{out}}; exit' INT; {PROMPT}");
        let mut b = Bench::new(&interrupted, false);
        b.wait_row("git status now");
        b.double_click(6);
        b.step(vec![Event::Copy]);
        assert!(b.copied.is_empty());
        assert_eq!(b.out(), "INT");
        let mut b = Bench::new(PROMPT, false);
        b.wait_row("git status now");
        b.double_click(6);
        b.step(vec![Event::Key {
            key: Key::X,
            physical_key: None,
            pressed: true,
            repeat: false,
            modifiers: Modifiers::CTRL | Modifiers::SHIFT,
        }]);
        assert_eq!(b.copied, ["status"]);
        b.wait_row("git  now");

        let mut b = Bench::new(FIRST_KEY, false);
        b.wait_row("READY");
        b.step(vec![Event::Paste("clipboard".into())]);
        assert_eq!(b.out(), "16");
    }

    #[test]
    fn slow_clicks_are_separate_clicks() {
        let mut b = Bench::new(PROMPT, true);
        b.wait_row("git status now");
        let pos = b.cell(6, 0);
        b.click(pos, PointerButton::Primary);
        for _ in 0..8 {
            b.step(vec![]);
        }
        b.click(pos, PointerButton::Primary);
        assert!(!b.app.has_selection());
    }
}
