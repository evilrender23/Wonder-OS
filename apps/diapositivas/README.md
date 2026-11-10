# Diapositivas

The presentations of Wonderlands OS: slides made, shown and exported, in `.pptx` and `.odp`. One of
the six apps of the office suite, and the desktop's app for both kinds of file.

- **Slides**: a strip of small ones in the sidebar to add, duplicate, reorder, skip and delete; a
  16:9 or 4:3 sheet with zoom; and a sorter (Ctrl+3) with all of them on a grid.
- **Things on a slide**: rich text, seven shapes, lines and arrows, pictures and a plain table.
  One or many are selected, moved, resized and turned by their handles, with guides that snap to
  the edges and centres of the others and tell when the gaps between three are equal. Align,
  distribute, group, flip, bring forward and send back, copy and paste, undo and redo.
- **Six layouts** (title, title and content, two columns, title only, blank, picture with caption)
  and **eight themes** — Blanco, Negro, Aurora, Papel, Pizarra, Océano, Atardecer, Editorial — each
  with its background, colours and faces. A theme is tried on the whole deck by resting the
  pointer on it, and set with a click. The start window has a template for each.
- **Outline to slides** (Ctrl+2): the deck as Markdown in the sidebar — `# ` the title of a slide,
  `- ` a bullet (two spaces deeper per level), `> ` a note for the speaker, `---` the next slide.
  The slides follow what is written, and what is formatted on a slide is kept where its text is
  not changed.
- **Presenting** (F5): the whole screen, with the keyboard; dissolve, push and fade to black
  between slides, and things that appear or slide in at a key. The presenter's view (Alt+F5) has
  the notes, the next slide, the clock and a stopwatch.
- **Format** in the inspector on the right, by what is selected: fill, border, shadow, opacity,
  face, size, colour, alignment, bullets, position and turn; the slide's layout, background,
  transition and notes; the theme, the footer and the slide numbers.
- **Every order in one list**: Ctrl+K opens it, a few letters find any of them, without minding
  accents. The orders most likely wanted float beside what is selected.
- **Saving**: Ctrl+S. What is not saved is copied to the data directory every little while and
  offered back after a crash. Each save by hand leaves a version (the last twenty of each file)
  that can be looked at by its date and brought back from «Versiones…».
- **Export**: a PDF with a page per slide, or a PNG of each.
- One window per presentation: opening another starts another `wonderland-diapositivas`.
- Light and dark follow the desktop, as does glass under WonderlandWM.

Keys: Ctrl+K the orders, Ctrl+N new, Ctrl+O open, Ctrl+S save, Ctrl+Shift+S save as, F2 rename,
Ctrl+Shift+E a PDF; Ctrl+M a new slide, Ctrl+Shift+D duplicates it, Ctrl+Backspace deletes it,
Ctrl+Shift+↑/↓ move it, PgUp/PgDown the one before and after; Ctrl+T a text box, Ctrl+Shift+I a
picture, Return writes in what is selected, the arrows nudge it; Ctrl+B/I/U, Ctrl++ and Ctrl+- for
the text; Ctrl+G and Ctrl+Shift+G group and ungroup, Ctrl+] and Ctrl+[ forward and back;
Ctrl+1/2/3 slides, outline and sorter, Ctrl+Alt+S and Ctrl+Alt+I the sidebar and the inspector,
Ctrl+Shift+T the themes, Ctrl+Shift+F nothing but the slide (Esc comes back), Ctrl+0 fits it,
Ctrl+/ all the keys. Presenting: → ↓ Space the next, ← ↑ the one before, Home and End, Esc leaves.

Settings, the recent files, the versions and the recovery copies live in
`~/.local/share/wonderland/wonderland-diapositivas/`. Presentations are saved wherever you say.

## Formats

| | Reads | Writes |
|---|---|---|
| `.pptx` (PowerPoint) | yes | yes — the default |
| `.odp` (OpenDocument) | yes | yes |
| PDF, PNG per slide | — | yes |

`.pptx` is the default because it is the one checked the furthest against LibreOffice, and the
only one that keeps the animations. Both are written with every colour and face spelled out, so
they look the same elsewhere, and carry the name of the theme so that Diapositivas gets it back
whole. What was checked: the sample deck saved as each, turned into a PDF by LibreOffice
(`soffice --convert-to pdf`) and compared slide by slide with Diapositivas' own; each turned into
the other by LibreOffice and read back; a `.pptx` made by python-pptx and an `.odp` made by
Impress, kept in `tests/data`.

Limits:

- Charts, SmartArt, video, sound, equations and OLE objects are not read: they are left out.
  Shapes other than the seven come in as rectangles; gradients as the colour they start from.
- A table has one colour for its heading row and plain text in its cells: merged cells, and the
  colours and formats of single cells, are lost. In a deck made elsewhere the cells take the text
  colour of the theme, which may not be the one they had.
- Of the animations, only appearing and sliding in, at a key; of the transitions, dissolve, push
  and fade — any other is read as a dissolve.
- `.odp` does not keep the animations of entrance, written or read. Groups are kept when their
  members lie together in the stack.
- A deck made elsewhere has no theme of Diapositivas: it opens on Blanco with the colours and
  faces it had written on each thing, and a flat background.
- Not tried: PowerPoint itself and Keynote (only LibreOffice was at hand), decks with password.

## Build

Needs Qt ≥ 6.8 (base, declarative, svg) and zlib. On Arch:
`pacman -S cmake ninja qt6-base qt6-declarative qt6-svg zlib`.

```sh
cmake -S apps/diapositivas -B target/apps/diapositivas -G Ninja
cmake --build target/apps/diapositivas
ctest --test-dir target/apps/diapositivas --output-on-failure
cmake --install target/apps/diapositivas --prefix ~/.local
```

## Pictures of the window, without a desktop

```sh
WONDERLAND_DIAPOSITIVAS_DATA=/tmp/dia WONDERLAND_DIAPOSITIVAS_GLASS=0 QT_QPA_PLATFORM=offscreen \
    wonderland-diapositivas --appearance dark --size 1280x800 --shot out.png \
    --do "sample;slide:4;select:2;commands:alin"
```

`--do` takes the id of any order of the list (`bold`, `slideNew`, `alignLeft`, `shape-star`,
`sorter`, `focus`, `present`, `presenter`, `versions`, `keys`…), some of them with a value —
`open:<file>`, `save:<file.pptx|file.odp>`, `exportPdf:<file>`, `exportPng:<file>`,
`commands:<text>` — and a few of its own: `sample` (a deck of five slides, made through the same
calls as by hand), `welcome`, `start:<theme>`, `aspect:<16:9|4:3>`, `slide:<n>`,
`select:<n,n…>`, `text:<text>`, `notes:<text>`, `outline:<markdown, \n for a new line>`,
`theme:<id>`, `try:<id>` (the theme as when the pointer rests on it), `tab:<format|slide|theme>`,
`side:<slides|outline>`, `zoom:<percent>`, `menu:<shapes|export>`, `next` and `prev` (while
presenting). `WONDERLAND_DIAPOSITIVAS_DATA` moves the data directory, so a test does not touch
your files, and `WONDERLAND_DIAPOSITIVAS_GLASS=0` turns the glass off.

Icons: [Lucide](https://lucide.dev) (ISC), see `assets/symbols/LICENSE-lucide`.
