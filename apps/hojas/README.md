# Hojas

The spreadsheet of Wonderlands OS: sheets of numbers, tables and charts, without a menu bar. It
opens `.xlsx`, `.ods`, `.csv` and `.tsv` files.

- **The sheet**: a grid that scrolls without end, ranges picked with the keyboard or the mouse, a
  formula bar with a name box, editing in the cell, copy, cut and paste (as text with tabs for other
  apps; formulas keep their relative references), fill, rows and columns inserted, removed, resized
  and hidden, frozen panes, merged cells, undo and redo.
- **Formulas** with about 145 functions, typed in Spanish or English (`SUMA` or `SUM`), arguments
  parted by `;`. While a formula is written each reference has a colour and its range wears the
  same colour on the sheet; under the bar the functions that match what is typed, and the arguments
  of the one being written with the current one marked.
- **Análisis rápido**: a range shows, beside it, what it adds up to, its mean and its count, and
  makes a chart, sorts, filters, colours by a condition or becomes a table with one click.
- **Tables**: a range with a header, banded rows, filters and a row of totals that passes over the
  rows a filter hides.
- **Kinds of column**, drawn in the header (text, number, date, checkbox); cells that hold true or
  false are real checkboxes.
- **Format** in the inspector: number, currency (€), percent, date, time and text; bold, italic,
  underline, strike, size, colour, fill, alignment, wrapping, borders; conditional format (greater,
  less, between, equal, text that contains…).
- **Charts** on the sheet, tied to their range: columns, bars, lines, area, pie and scatter.
- **Sort, filter, find and replace**; the status bar adds up the selection.
- **Sheets** in the sidebar and as tabs below: add, rename, duplicate, remove.
- **Órdenes** (Ctrl+K): everything the app does, a few letters away, with its shortcut.
- **Versions**: every save keeps a copy, up to twenty for each document, to look at and bring back.
  What is not saved yet is kept aside every little while, and offered again after a crash.
- **Concentration** (Ctrl+Shift+F): only the sheet; Esc comes back.
- Templates to start from: blank, a personal budget, an invoice, a list of tasks, a timetable.
- One window for each document: opening another starts another `wonderland-hojas`.
- Light and dark follow the desktop, as does glass under WonderlandWM.

Keys: the arrows, Tab and Intro move; Shift with them selects; Ctrl+arrows jump to the edge of the
data; F2 edits; F4 turns a reference absolute while a formula is written; Space ticks a checkbox;
Delete empties. Ctrl+N, O, S, Shift+S, W for the document; Ctrl+Z / Ctrl+Shift+Z; Ctrl+C, X, V and
Ctrl+Shift+V (values only); Ctrl+D and Ctrl+R fill down and right; Ctrl+B, I, U; Ctrl+L, E; Ctrl+F
find; Ctrl+G go to a cell; Alt+= sum; Ctrl+T table; Ctrl+Shift+L filter; Ctrl+PgUp / PgDown the
previous and the next sheet; Ctrl++, Ctrl+-, Ctrl+0 zoom; Ctrl+Alt+S the sidebar, Ctrl+Alt+I the
inspector.

Documents live where you save them. The settings and the recent files (`settings.json`), the
versions (`versions/`) and what is being recovered (`recovery/`) are in
`~/.local/share/wonderland/wonderland-hojas/`.

## Formats

| Format | Reads | Writes | Notes |
| --- | --- | --- | --- |
| `.xlsx` | yes | yes | The format by default, and the one checked both ways against LibreOffice: values, formulas, number formats, fonts, fills, borders, widths, merged cells, frozen panes, filters, tables, conditional formats and charts. Pivot tables, images, comments, validation and macros are not read, and are lost on saving. |
| `.ods` | yes | no | Values, formulas, number formats, fonts, fills, alignment, widths and merged cells. Charts, conditional formats and filters of an `.ods` are not read. Hojas does not write `.ods` yet: it says so and the document is saved as `.xlsx`. |
| `.csv`, `.tsv` | yes | yes | UTF-8; the separator (`;`, `,` or a tab) is told from the file. One sheet, values only. |
| PDF | – | yes | The current sheet over as many A4 pages as it needs (on its side when the sheet is wide), with its charts. |

## Build

Needs Qt ≥ 6.8 (base, declarative, svg) and zlib. On Arch:
`pacman -S cmake ninja qt6-base qt6-declarative qt6-svg zlib`.

```sh
cmake -S apps/hojas -B target/apps/hojas -G Ninja
cmake --build target/apps/hojas
ctest --test-dir target/apps/hojas --output-on-failure
cmake --install target/apps/hojas --prefix ~/.local
```

## Pictures of the window, without a desktop

```sh
WONDERLAND_HOJAS_DATA=/tmp/hojas WONDERLAND_HOJAS_GLASS=0 QT_QPA_PLATFORM=offscreen \
    wonderland-hojas --appearance dark --size 1280x800 --shot out.png \
    --do "sample;select:C2:D8;inspector-on"
```

`--new <blank|budget|invoice|tasks|schedule>` starts from a template. `--do` takes the id of any
order of the palette (`save`, `bold`, `table`, `chart-column`, `find`, `versions`, `focus`,
`palette`… some take an argument, as `save-as:<file>`, `export-pdf:<file>`, `export-csv:<file>`,
`new:<template>`) and, for tests only: `sample` (a made-up book of sales), `select:<A1|A1:C9>`,
`type:<text>` (types and moves down), `edit:<text>` and `edit-bar:<text>` (leave the editor open;
`¦` stands for `;` inside a formula), `accept`, `complete`, `point:<A1>`, `sheet:<n>`, `dark`,
`light`, `inspector-on`, `sidebar-off`, `chart-select:<n>`, `filter-menu`, `filter-only:<a|b>`,
`context-menu`, `share-menu`, `scroll:<row>,<col>`, `zoom:<n>`, `width:<px>`, `height:<px>`,
`fill:<#rrggbb>`, `color:<#rrggbb>`, `condition:<op>:<value>:<fill>:<ink>`, `replace:<a>=<b>`,
`recovery` and `say:<text>`. `WONDERLAND_HOJAS_DATA` moves the data directory, so a test does not
touch your recent files or versions.

Icons: [Lucide](https://lucide.dev) (ISC), see `assets/symbols/LICENSE-lucide`.
