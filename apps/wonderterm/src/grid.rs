//! A frame's worth of terminal content, copied out from under the terminal's
//! lock so painting never holds up the PTY reader.

use alacritty_terminal::grid::Dimensions;
use alacritty_terminal::index::{Column, Line, Point};
use alacritty_terminal::term::cell::{Cell, Flags};
use alacritty_terminal::term::color::Colors;
use alacritty_terminal::term::{Term, TermMode};
use alacritty_terminal::vte::ansi::{Color, CursorShape, NamedColor};
use eframe::egui::Color32;

use crate::theme::{Theme, mix};

pub const REGULAR: u8 = 0;
pub const BOLD: u8 = 1;
pub const ITALIC: u8 = 2;

pub struct Span {
    pub row: usize,
    pub col: usize,
    pub len: usize,
    pub color: Color32,
}

/// Consecutive cells in one style whose glyphs all advance by exactly one
/// cell in the primary face, laid out in a single pass.
pub struct Run {
    pub row: usize,
    pub col: usize,
    pub text: String,
    pub fg: Color32,
    pub style: u8,
}

/// A character positioned on its own: wide, from a fallback font, or drawn
/// as geometry.
pub struct Glyph {
    pub row: usize,
    pub col: usize,
    pub wide: bool,
    pub text: String,
    pub fg: Color32,
    pub style: u8,
}

#[derive(Clone, Copy, PartialEq)]
pub enum Line_ {
    Under,
    Double,
    Curl,
    Dotted,
    Strike,
}

pub struct Decoration {
    pub row: usize,
    pub col: usize,
    pub kind: Line_,
    pub color: Color32,
}

pub struct CursorCell {
    pub row: usize,
    pub col: usize,
    pub wide: bool,
    pub shape: CursorShape,
    pub blinking: bool,
    pub text: String,
    pub style: u8,
    pub color: Color32,
}

#[derive(Default)]
pub struct Snapshot {
    pub backgrounds: Vec<Span>,
    pub selection: Vec<Span>,
    pub runs: Vec<Run>,
    pub glyphs: Vec<Glyph>,
    pub decorations: Vec<Decoration>,
    pub cursor: Option<CursorCell>,
    pub mode: TermMode,
    pub display_offset: usize,
    pub history: usize,
}

fn rgb(c: alacritty_terminal::vte::ansi::Rgb) -> Color32 {
    Color32::from_rgb(c.r, c.g, c.b)
}

pub fn resolve(color: Color, colors: &Colors, theme: &Theme) -> Color32 {
    match color {
        Color::Spec(c) => rgb(c),
        Color::Named(n) => colors[n].map(rgb).unwrap_or_else(|| theme.slot(n as usize)),
        Color::Indexed(i) => colors[i as usize]
            .map(rgb)
            .unwrap_or_else(|| theme.indexed(i)),
    }
}

fn style_of(flags: Flags) -> u8 {
    (if flags.contains(Flags::BOLD) { BOLD } else { 0 })
        | (if flags.contains(Flags::ITALIC) {
            ITALIC
        } else {
            0
        })
}

fn cell_text(cell: &Cell) -> String {
    let mut text = String::from(cell.c);
    if let Some(extra) = cell.zerowidth() {
        text.extend(extra);
    }
    text
}

/// Latin letters and punctuation: one cell each in any monospace face.
fn fits_a_run(cell: &Cell) -> bool {
    (cell.c as u32) < 0x250 && cell.zerowidth().is_none()
}

fn push_span(spans: &mut Vec<Span>, row: usize, col: usize, len: usize, color: Color32) {
    if let Some(last) = spans.last_mut()
        && last.row == row
        && last.col + last.len == col
        && last.color == color
    {
        last.len += len;
        return;
    }
    spans.push(Span {
        row,
        col,
        len,
        color,
    });
}

pub fn snapshot<T: alacritty_terminal::event::EventListener>(
    term: &Term<T>,
    theme: &Theme,
) -> Snapshot {
    let content = term.renderable_content();
    let colors = content.colors;
    let offset = content.display_offset as i32;
    let rows = term.screen_lines() as i32;
    let mut snap = Snapshot {
        mode: content.mode,
        display_offset: content.display_offset,
        history: term.grid().history_size(),
        ..Default::default()
    };

    for indexed in content.display_iter {
        let cell = indexed.cell;
        let flags = cell.flags;
        if flags.intersects(Flags::WIDE_CHAR_SPACER | Flags::LEADING_WIDE_CHAR_SPACER) {
            continue;
        }
        let row = indexed.point.line.0 + offset;
        if row < 0 || row >= rows {
            continue;
        }
        let (row, col) = (row as usize, indexed.point.column.0);
        let wide = flags.contains(Flags::WIDE_CHAR);
        let width = if wide { 2 } else { 1 };

        let mut fg = resolve(cell.fg, colors, theme);
        let mut bg = resolve(cell.bg, colors, theme);
        let default_bg = cell.bg == Color::Named(NamedColor::Background);
        if flags.contains(Flags::INVERSE) {
            std::mem::swap(&mut fg, &mut bg);
        }
        if flags.contains(Flags::DIM) {
            fg = mix(fg, bg, 0.4);
        }
        if !default_bg || flags.contains(Flags::INVERSE) {
            push_span(&mut snap.backgrounds, row, col, width, bg);
        }
        if content.selection.is_some_and(|s| s.contains(indexed.point)) {
            push_span(&mut snap.selection, row, col, width, Color32::TRANSPARENT);
        }

        let kind = if flags.contains(Flags::UNDERCURL) {
            Some(Line_::Curl)
        } else if flags.contains(Flags::DOUBLE_UNDERLINE) {
            Some(Line_::Double)
        } else if flags.intersects(Flags::DOTTED_UNDERLINE | Flags::DASHED_UNDERLINE) {
            Some(Line_::Dotted)
        } else if flags.contains(Flags::UNDERLINE) {
            Some(Line_::Under)
        } else if cell.hyperlink().is_some() {
            Some(Line_::Dotted)
        } else {
            None
        };
        let line_color = cell
            .underline_color()
            .map(|c| resolve(c, colors, theme))
            .unwrap_or(fg);
        for c in col..col + width {
            if let Some(kind) = kind {
                snap.decorations.push(Decoration {
                    row,
                    col: c,
                    kind,
                    color: line_color,
                });
            }
            if flags.contains(Flags::STRIKEOUT) {
                snap.decorations.push(Decoration {
                    row,
                    col: c,
                    kind: Line_::Strike,
                    color: fg,
                });
            }
        }

        if flags.contains(Flags::HIDDEN) || (cell.c == ' ' && cell.zerowidth().is_none()) {
            continue;
        }
        let style = style_of(flags);
        if fits_a_run(cell) {
            match snap.runs.last_mut() {
                Some(run)
                    if run.row == row
                        && run.col + run.text.chars().count() == col
                        && run.fg == fg
                        && run.style == style =>
                {
                    run.text.push(cell.c);
                }
                _ => snap.runs.push(Run {
                    row,
                    col,
                    text: String::from(cell.c),
                    fg,
                    style,
                }),
            }
        } else {
            snap.glyphs.push(Glyph {
                row,
                col,
                wide,
                text: cell_text(cell),
                fg,
                style,
            });
        }
    }

    let cursor = content.cursor;
    let row = cursor.point.line.0 + offset;
    if cursor.shape != CursorShape::Hidden && row >= 0 && row < rows {
        let cell = &term.grid()[cursor.point];
        snap.cursor = Some(CursorCell {
            row: row as usize,
            col: cursor.point.column.0,
            wide: cell.flags.contains(Flags::WIDE_CHAR),
            shape: cursor.shape,
            blinking: term.cursor_style().blinking,
            text: if cell.flags.contains(Flags::HIDDEN) {
                String::new()
            } else {
                cell_text(cell)
            },
            style: style_of(cell.flags),
            color: colors[NamedColor::Cursor].map(rgb).unwrap_or(theme.accent),
        });
    }
    snap
}

/// The characters of one visible row, for looking up what was clicked.
pub fn row_chars<T>(term: &Term<T>, line: Line) -> Vec<char> {
    (0..term.columns())
        .map(|c| term.grid()[Point::new(line, Column(c))].c)
        .collect()
}

/// The http(s) URL under column `col`, if any. Trailing punctuation that
/// usually belongs to the sentence, not the link, is dropped.
pub fn url_at(row: &[char], col: usize) -> Option<String> {
    let stop = |c: char| c.is_whitespace() || "\"'<>`|\0".contains(c);
    if col >= row.len() || stop(row[col]) {
        return None;
    }
    let start = row[..col]
        .iter()
        .rposition(|&c| stop(c))
        .map_or(0, |i| i + 1);
    let end = row[col..]
        .iter()
        .position(|&c| stop(c))
        .map_or(row.len(), |i| col + i);
    let token: String = row[start..end].iter().collect();
    let begin = token.find("https://").or_else(|| token.find("http://"))?;
    let url = token[begin..].trim_end_matches(['.', ',', ';', ':', ')', ']', '}', '!', '?']);
    (token[..begin].chars().count() <= col - start && url.len() > "https://".len())
        .then(|| url.to_owned())
}

#[cfg(test)]
mod tests {
    use super::*;
    use alacritty_terminal::event::VoidListener;
    use alacritty_terminal::term::Config;
    use alacritty_terminal::vte::ansi::{Processor, StdSyncHandler};

    use crate::session::GridSize;

    fn term(text: &str) -> Term<VoidListener> {
        let size = GridSize {
            columns: 20,
            lines: 4,
        };
        let mut term = Term::new(Config::default(), &size, VoidListener);
        let mut parser: Processor<StdSyncHandler> = Processor::new();
        parser.advance(&mut term, text.as_bytes());
        term
    }

    #[test]
    fn plain_text_is_one_run_per_style() {
        let theme = Theme::new(true);
        let snap = snapshot(&term("ab \x1b[1;31mcd\x1b[0m"), &theme);
        assert_eq!(snap.runs.len(), 2);
        assert_eq!((snap.runs[0].text.as_str(), snap.runs[0].col), ("ab", 0));
        assert_eq!((snap.runs[1].text.as_str(), snap.runs[1].col), ("cd", 3));
        assert_eq!(snap.runs[1].style, BOLD);
        assert_eq!(snap.runs[1].fg, theme.ansi[1]);
        assert!(snap.backgrounds.is_empty());
    }

    #[test]
    fn coloured_and_inverse_cells_get_a_background_span() {
        let theme = Theme::new(true);
        let snap = snapshot(&term("\x1b[44mxy\x1b[0m \x1b[7mz"), &theme);
        assert_eq!(snap.backgrounds.len(), 2);
        assert_eq!(snap.backgrounds[0].len, 2);
        assert_eq!(snap.backgrounds[0].color, theme.ansi[4]);
        assert_eq!(snap.backgrounds[1].color, theme.fg);
    }

    #[test]
    fn wide_and_box_characters_are_placed_individually() {
        let theme = Theme::new(true);
        let snap = snapshot(&term("a日─"), &theme);
        assert_eq!(snap.runs.len(), 1);
        assert_eq!(snap.glyphs.len(), 2);
        assert!(snap.glyphs[0].wide);
        assert_eq!(snap.glyphs[1].col, 3);
    }

    #[test]
    fn the_cursor_follows_the_text_and_hides_on_request() {
        let theme = Theme::new(true);
        let snap = snapshot(&term("abc"), &theme);
        let cursor = snap.cursor.unwrap();
        assert_eq!((cursor.row, cursor.col), (0, 3));
        assert!(snapshot(&term("abc\x1b[?25l"), &theme).cursor.is_none());
    }

    #[test]
    fn urls_are_found_under_the_pointer() {
        let row: Vec<char> = "see (https://example.org/a?b=1). ok".chars().collect();
        assert_eq!(
            url_at(&row, 10).as_deref(),
            Some("https://example.org/a?b=1")
        );
        assert_eq!(url_at(&row, 1), None);
        assert_eq!(url_at(&row, 33), None);
        assert_eq!(
            url_at(&row, 4),
            None,
            "the bracket before the link is not the link"
        );
    }
}
