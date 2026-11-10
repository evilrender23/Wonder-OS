# Dibujo

The drawing app of Wonderlands OS: vectors and diagrams on a canvas with no end, the way the desktop
itself is one. It is the app for `.wdib` drawings, and it opens `.svg` files without becoming their
default.

- **An endless canvas**: the wheel scrolls, Ctrl + wheel zooms on the pointer, Space + drag (or the
  hand) carries the view. `1` is 100 %, `0` fits everything, Shift+1 fits the selection. A minimap in
  the corner shows where you are and moves the view.
- **Frames**: areas with a name and the size of a page (A4, 16:9, square, or drawn by hand). They are
  what is exported, and they are listed in the sidebar above the layers.
- **Tools**, in a bar floating at the bottom, each with a letter: select (V), hand (H), rectangle (R),
  ellipse (O), line (L), arrow (A), connector (C), polygon (G), star (S), pen with Bézier handles (P),
  pencil with smoothing (D), text (T), sticky note (N), frame (F) and image (I).
- **Connectors** stick to the shapes they join and follow them: straight, elbowed or curved, with a
  label of their own.
- **Editing**: one, several or a marquee; move, resize and turn by the grips; smart guides with a
  magnet and equal distances; an optional grid; align and distribute; group; order; lock; duplicate;
  copy and paste, also between windows.
- **Formato**, the inspector on the right: solid or gradient fill, stroke (width, dashes, arrow
  heads), corners, shadow, opacity, type. With several selected, what they do not share says so.
  The drawing keeps a palette of its own colours.
- **A library of shapes** in the sidebar: basic, flowchart, arrows and speech bubbles.
- **Órdenes** (Ctrl+K): everything the app does, a few letters away. There is no menu bar.
- **A floating bar** beside the selection with what is most likely wanted next.
- **Templates**: blank, flowchart, mind map, wireframe and poster.
- **Versions**: every Ctrl+S keeps a copy, twenty per drawing, to look at by date and bring back.
  What is not saved yet is kept aside every few seconds and offered again after a crash.
- **Concentration** (Ctrl+Shift+F) leaves the drawing alone on the window; Esc comes back.
- One window per drawing: opening another starts another `wonderland-dibujo`.
- Light and dark follow the desktop, as does glass under WonderlandWM.

Keys: Ctrl+N, Ctrl+O, Ctrl+S, Ctrl+Shift+S, Ctrl+E export, Ctrl+W close; Ctrl+Z / Ctrl+Shift+Z;
Ctrl+X/C/V, Ctrl+D duplicate, Ctrl+A, Delete; Ctrl+G / Ctrl+Shift+G group and ungroup; Ctrl+] and
Ctrl+[ one level up and down, with Shift to the front and to the back; Ctrl+L lock; the arrows move
by 1, with Shift by 10; F2 or Enter edits the text; Ctrl+' the grid, Ctrl+Shift+' the magnet;
Ctrl+Alt+S the sidebar, Ctrl+Alt+I the inspector, Ctrl+M the minimap. While drawing, Shift keeps
proportions and angles, and Esc lets go of what is in hand.

Settings, the list of recent drawings, the versions (`versions/`) and the unsaved work (`recovery/`)
live in `~/.local/share/wonderland/wonderland-dibujo/`. Drawings live wherever you save them.

## Formats

| Format | Reads | Writes | Notes |
| --- | --- | --- | --- |
| `.wdib` | yes | yes | The default. A ZIP with `document.json`, the images under `resources/` and a `preview.png`. MIME `application/x-wonderland-dibujo`. |
| SVG | yes | yes | See below. |
| PNG | as an image | yes | A frame, the selection or everything, at ×1 to ×4, with or without background. |
| PDF | no | yes | Vector, one page per frame, each of its own size; text stays text. |
| ODG | no | no | Not there yet. |

SVG out: shapes, paths, lines with their heads, gradients, dashes, opacity, rotation, text and
embedded images. Checked with `xmllint`, `rsvg-convert` and LibreOffice (`soffice --convert-to png`
draws the sample with its text, arrows, dashes and gradient). A shadow is written as a filter.

SVG in: `rect`, `circle`, `ellipse`, `line`, `polyline`, `polygon`, `path`, `text` (with `tspan`,
also the nested ones LibreOffice writes), `image`, `g` and `use`, with transforms, `style`
attributes, CSS classes from a `<style>` block, and two-colour gradients. Boxes and ellipses stay
shapes that can be edited, also when turned; everything else comes in as paths. Filters, masks, clip
paths, patterns and markers are skipped, and a gradient keeps only its first and last colour and
loses its direction. Checked with icons from `/usr/share/icons/hicolor/scalable/apps` and with an
SVG written by LibreOffice Draw (there, a gradient fill arrives as a clipped picture and so as a
plain box).

## Build

Needs Qt ≥ 6.8 (base, declarative, svg) and zlib. On Arch:
`pacman -S cmake ninja qt6-base qt6-declarative qt6-svg zlib`.

```sh
cmake -S apps/dibujo -B target/apps/dibujo -G Ninja
cmake --build target/apps/dibujo
ctest --test-dir target/apps/dibujo --output-on-failure
cmake --install target/apps/dibujo --prefix ~/.local
```

## Layout

- `src/model.*` — a drawing: shapes with geometry, rotation, order, style and groups; JSON.
- `src/geometry.*` — outlines of the forms, hit tests, line routes, connectors.
- `src/history.*` — undo and redo by commands.
- `src/render.*` — painting with QPainter, for the canvas, the exports and the thumbnails.
- `src/editor.*` — selection, view, tools and gestures; everything the window does goes through it.
- `src/io.*`, `src/zip.*` — `.wdib`, versions and recovery.
- `src/svg.*`, `src/exporter.*` — SVG in and out, PNG and PDF.
- `src/templates.*` — the drawings to start from.
- `src/canvas.*`, `src/app.*`, `qml/` — the window.
- `tests/` — QtTest, without a window.

## Checking without a screen

```sh
QT_QPA_PLATFORM=offscreen WONDERLAND_DIBUJO_DATA=/some/empty/dir WONDERLAND_DIBUJO_GLASS=0 \
  wonderland-dibujo --shot out.png --size 1360x820 --appearance dark --do "sample;select:Rombo 1"
```

`--do` takes actions split by `;`. Every id of the palette is one (`new`, `save`, `undo`, `group`,
`align-left`, `zoom-fit`, `concentrate`, `versions`, `tool-rect`…), and so are these:

| Action | What it does |
| --- | --- |
| `sample` | A drawing with a bit of everything, made through the editor. |
| `template:<id>` | `blank`, `flowchart`, `mindmap`, `wireframe`, `poster`. |
| `open:<path>`, `save:<path>`, `import:<svg>`, `picture:<image>` | Files. |
| `export:<png\|svg\|pdf>:<path>[:<frame>]` | Writes without asking. |
| `select:<name>[,<name>]` | By name; `all` and `none` too. |
| `set:<key>=<value>` | A property of the selection, as the inspector sets it. |
| `tool:<id>`, `form:<form>`, `text:<words>`, `note:<words>`, `addframe:<preset>` | Adding things. |
| `press:x,y`, `move:x,y`, `release:x,y`, `drag:x1,y1,x2,y2`, `click:x,y`, `double:x,y` | The pointer, in drawing coordinates. |
| `type:<words>`, `done` | Text being edited. |
| `zoom:<percent>`, `pan:x,y`, `reveal:<name>` | The view. |
| `tab:<layers\|shapes>`, `panels:<on\|off>` | The sidebar and the panels. |
| `find:<letters>`, `menu:<frame\|align\|zoom\|context>`, `exportas:<format>`, `color`, `confirm` | The palette, the menus and the dialogs. |
| `title:<name>`, `say:<words>`, `welcome` | The rest. |
