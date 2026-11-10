use alacritty_terminal::Term;
use alacritty_terminal::grid::Dimensions;
use alacritty_terminal::index::{Column, Line, Point};
use alacritty_terminal::term::TermMode;
use alacritty_terminal::term::cell::Flags;

/// The keys that delete the selection from the line being edited.
#[derive(Debug, PartialEq)]
pub struct Plan {
    /// Arrow presses that take the cursor to the end of the selection:
    /// right when positive, left when negative.
    pub moves: i32,
    /// Backspaces to send once it is there.
    pub deletes: usize,
}

/// A terminal cannot remove text from the screen; what it can do is ask the
/// program for it, the way the user would: walk to the end of the selection
/// and backspace over it. That only makes sense on the line that holds the
/// cursor (wrapped rows included), so a selection that reaches anywhere else,
/// a block selection or a full-screen program gives `None` and the cut is
/// just a copy.
///
/// `canonical`: the kernel edits the line, so there is no moving about in it
/// and only its tail can be deleted.
pub fn plan<T>(term: &Term<T>, canonical: bool) -> Option<Plan> {
    if term.mode().contains(TermMode::ALT_SCREEN) {
        return None;
    }
    let range = term.selection.as_ref()?.to_range(term)?;
    if range.is_block {
        return None;
    }
    let grid = term.grid();
    let columns = grid.columns();
    let wraps = |line: Line| {
        grid[line][Column(columns - 1)]
            .flags
            .contains(Flags::WRAPLINE)
    };

    let cursor = grid.cursor.point;
    let mut first = cursor.line;
    while first > grid.topmost_line() && wraps(Line(first.0 - 1)) {
        first = Line(first.0 - 1);
    }
    let mut last = cursor.line;
    while last < grid.bottommost_line() && wraps(last) {
        last = Line(last.0 + 1);
    }
    if range.start.line < first || range.end.line > last {
        return None;
    }

    // Cells of the logical line, counted from its first row.
    let index = |p: Point| (p.line.0 - first.0) as usize * columns + p.column.0;
    let cell = |i: usize| &grid[Line(first.0 + (i / columns) as i32)][Column(i % columns)];
    let chars = |from: usize, to: usize| {
        (from..to)
            .filter(|&i| {
                !cell(i)
                    .flags
                    .intersects(Flags::WIDE_CHAR_SPACER | Flags::LEADING_WIDE_CHAR_SPACER)
            })
            .count()
    };

    let cursor = index(cursor) + grid.cursor.input_needs_wrap as usize;
    // A selection made by triple click runs to the edge of the window; the
    // text ends at its last character, or at the cursor after typed spaces.
    let cells = (last.0 - first.0 + 1) as usize * columns;
    let text_end = (0..cells)
        .rev()
        .find(|&i| !matches!(cell(i).c, ' ' | '\0'))
        .map_or(0, |i| i + 1)
        .max(cursor);
    let start = index(range.start);
    let end = (index(range.end) + 1).min(text_end);
    if end <= start {
        return None;
    }

    let moves = if end >= cursor {
        chars(cursor, end) as i32
    } else {
        -(chars(end, cursor) as i32)
    };
    if canonical && moves != 0 {
        return None;
    }
    Some(Plan {
        moves,
        deletes: chars(start, end),
    })
}

#[cfg(test)]
mod tests {
    use alacritty_terminal::event::VoidListener;
    use alacritty_terminal::index::Side;
    use alacritty_terminal::selection::{Selection, SelectionType};
    use alacritty_terminal::term::Config;
    use alacritty_terminal::vte::ansi::Processor;

    use super::*;
    use crate::session::GridSize;

    fn term(columns: usize, bytes: &[u8]) -> Term<VoidListener> {
        let size = GridSize { columns, lines: 6 };
        let mut term = Term::new(Config::default(), &size, VoidListener);
        let mut parser: Processor = Processor::new();
        parser.advance(&mut term, bytes);
        term
    }

    fn select(
        term: &mut Term<VoidListener>,
        ty: SelectionType,
        from: (i32, usize),
        to: (i32, usize),
    ) {
        let mut selection =
            Selection::new(ty, Point::new(Line(from.0), Column(from.1)), Side::Left);
        selection.update(Point::new(Line(to.0), Column(to.1)), Side::Right);
        term.selection = Some(selection);
    }

    fn simple(term: &mut Term<VoidListener>, from: (i32, usize), to: (i32, usize)) {
        select(term, SelectionType::Simple, from, to);
    }

    #[test]
    fn word_before_the_cursor_is_walked_back_to_and_erased() {
        // "$ git status now", cursor after "now"; "status" is selected.
        let mut t = term(40, b"$ git status now");
        simple(&mut t, (0, 6), (0, 11));
        assert_eq!(
            plan(&t, false),
            Some(Plan {
                moves: -4,
                deletes: 6
            })
        );
    }

    #[test]
    fn selection_after_the_cursor_is_reached_with_right_arrows() {
        // Cursor moved back to the start of "git".
        let mut t = term(40, b"$ git status\x1b[10D");
        simple(&mut t, (0, 6), (0, 11));
        assert_eq!(
            plan(&t, false),
            Some(Plan {
                moves: 10,
                deletes: 6
            })
        );
    }

    #[test]
    fn whole_line_selection_stops_at_the_text() {
        let mut t = term(40, b"$ ls -la");
        select(&mut t, SelectionType::Lines, (0, 3), (0, 3));
        assert_eq!(
            plan(&t, false),
            Some(Plan {
                moves: 0,
                deletes: 8
            })
        );
    }

    #[test]
    fn trailing_typed_spaces_belong_to_the_line() {
        let mut t = term(40, b"$ ls  ");
        simple(&mut t, (0, 2), (0, 5));
        assert_eq!(
            plan(&t, false),
            Some(Plan {
                moves: 0,
                deletes: 4
            })
        );
    }

    #[test]
    fn wrapped_rows_are_one_line() {
        // 10 columns: "0123456789" wraps into "abcd".
        let mut t = term(10, b"0123456789abcd");
        simple(&mut t, (0, 8), (1, 1));
        assert_eq!(
            plan(&t, false),
            Some(Plan {
                moves: -2,
                deletes: 4
            })
        );
    }

    #[test]
    fn cursor_waiting_to_wrap_counts_as_past_the_last_cell() {
        let mut t = term(10, b"0123456789");
        simple(&mut t, (0, 8), (0, 9));
        assert_eq!(
            plan(&t, false),
            Some(Plan {
                moves: 0,
                deletes: 2
            })
        );
    }

    #[test]
    fn wide_characters_are_one_key_each() {
        let mut t = term(40, "$ 日本 ok".as_bytes());
        simple(&mut t, (0, 2), (0, 5));
        assert_eq!(
            plan(&t, false),
            Some(Plan {
                moves: -3,
                deletes: 2
            })
        );
    }

    #[test]
    fn output_above_the_prompt_is_only_copied() {
        let mut t = term(40, b"some output\r\n$ ls");
        simple(&mut t, (0, 0), (0, 3));
        assert_eq!(plan(&t, false), None);
        simple(&mut t, (0, 5), (1, 3));
        assert_eq!(plan(&t, false), None);
    }

    #[test]
    fn block_selections_and_full_screen_programs_are_only_copied() {
        let mut t = term(40, b"$ ls -la");
        select(&mut t, SelectionType::Block, (0, 2), (0, 3));
        assert_eq!(plan(&t, false), None);
        let mut t = term(40, b"\x1b[?1049hhello");
        simple(&mut t, (0, 0), (0, 4));
        assert_eq!(plan(&t, false), None);
    }

    #[test]
    fn kernel_edited_lines_only_lose_their_tail() {
        let mut t = term(40, b"hello world");
        simple(&mut t, (0, 6), (0, 10));
        assert_eq!(
            plan(&t, true),
            Some(Plan {
                moves: 0,
                deletes: 5
            })
        );
        simple(&mut t, (0, 0), (0, 4));
        assert_eq!(plan(&t, true), None);
    }

    #[test]
    fn nothing_selected_past_the_text_is_nothing_to_cut() {
        let mut t = term(40, b"$ ls");
        simple(&mut t, (0, 10), (0, 14));
        assert_eq!(plan(&t, false), None);
    }
}
