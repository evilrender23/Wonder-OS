# Textos

The word processor of Wonder OS: letters, reports and anything else that ends up on a sheet of
paper. It is the desktop's app for `.odt`, `.docx`, `.rtf`, `.md` and `.txt` files.

- **Sheets**: the document is shown as its pages (A4, Letter and others, upright or on its side,
  with margins), centred on the desk, at any zoom or fitted to the window. A header, a footer and
  the page number can go on every sheet.
- **Characters and paragraphs**: font, size, bold, italic, underline, strike, super and subscript,
  colour and highlight; alignment, line spacing, indents, bulleted and numbered lists. Paragraph
  styles (Título, Encabezado 1–3, Cuerpo, Cita, Código) from the bar or the inspector.
- **One bar, no menus**: undo, the style, the most used formats and what can be inserted. Everything
  else is in **Órdenes** (Ctrl+K), which finds any action by a few letters of its name, accents or
  not, and shows its keys.
- **A pill by the selection** with what is most likely wanted of it; the whole of the format lives
  in the inspector on the right (Formato and Documento), which can be folded away.
- **Markdown as you type**: `# `, `## ` and `### ` make headings, `- ` a list, `1. ` a numbered
  one, `> ` a quote, ` ``` ` a block of code and `**so**` bold. It can be switched off.
- **«/» on an empty line** offers the blocks that can go there: headings, lists, a task, a quote,
  code, a table, a picture, a page break, a separator, today's date.
- **Outline**: the sidebar lists the title and headings as they are written; a click goes there.
- **Concentration** (Ctrl+Shift+F): nothing but the sheet, the paragraph being written lit and kept
  at mid height like the line of a typewriter. Esc comes back.
- **A goal of words**, with its progress in the line at the bottom, next to the count of words and
  characters (of the selection, when there is one) and the reading time.
- **Tables** (rows and columns added and removed), **pictures**, **links**, page breaks.
- **Find and replace** (Ctrl+F, Ctrl+H), minding case and whole words or not.
- **Templates** to start from: blank, letter, résumé, report, minutes.
- **Versions**: every Ctrl+S keeps a copy (the last 20 of each document), which the Versiones panel
  lists by date, previews and restores. What is being written is also saved aside every few
  seconds, and offered back if Textos did not close well.
- One document a window: opening another starts another Textos.
- Light and dark follow the desktop, as does glass under WonderlandWM.

Keys: Ctrl+K orders, Ctrl+N new, Ctrl+O open, Ctrl+S save, Ctrl+Shift+S save as, Ctrl+Shift+E
export a PDF, F2 rename, Ctrl+W close the document; Ctrl+B / I / U bold, italic and underline,
Ctrl+Shift+X strike, Ctrl+. and Ctrl+, super and subscript, Ctrl+Shift+H highlight, Ctrl+] and
Ctrl+[ a bigger and a smaller letter; Ctrl+Alt+0 body, Ctrl+Alt+T title, Ctrl+Alt+1 … 3 headings,
Ctrl+Alt+Q quote, Ctrl+Alt+C code; Ctrl+L / E / R / J align; Ctrl+Shift+8 and Ctrl+Shift+7 lists,
Ctrl+M and Ctrl+Shift+M indent; Ctrl+Return a page break, Ctrl+Shift+K a link, Ctrl+Shift+I a
picture, Ctrl+Shift+D the date; Ctrl+F find, Ctrl+H replace, F3 / Shift+F3 the next and the
previous; Ctrl+Alt+S the outline, Ctrl+Alt+I the inspector, Ctrl+Shift+F concentration; Ctrl++,
Ctrl+-, Ctrl+1 and Ctrl+0 zoom in, out, to 100 % and to fit.

Settings, recovery copies (`recovery/`) and versions (`versions/`) live in
`~/.local/share/wonderland/wonderland-textos/`. Documents are wherever you save them.

## Formats

The default is **OpenDocument Text (`.odt`)**, the one best checked against LibreOffice.

| Format | Reads | Writes | Checked |
| --- | --- | --- | --- |
| `.odt` | yes | yes | tests round-trip it; LibreOffice opens what Textos writes and Textos reads what LibreOffice writes back (test `odt`) |
| `.docx` | yes | yes | the same, against LibreOffice (test `docx`); not tried in Microsoft Word |
| `.rtf` | yes | yes | round trip in tests; LibreOffice reads it and Textos reads LibreOffice's, compared as text by hand |
| `.md` | yes | yes | round trip in tests; keeps headings, lists, quotes, code, tables, links, bold, italic and strike, not fonts, colours or the page |
| `.html` | yes | yes | round trip in tests |
| `.txt` | yes | yes | the text and nothing else |
| `.pdf` | no | export | page count and size looked at with `pdfinfo` |

What is kept between `.odt` and `.docx`: paragraph styles, character formats, alignment, spacing
and indents, nested bulleted and numbered lists, plain tables, pictures in the line of text,
links, page breaks, the paper, its margins, one header and one footer and the page number.
What is not: footnotes, comments, tracked changes, fields other than the page number, columns,
sections with different pages, text boxes, pictures the text flows around, merged cells and table
styles. A document that has them opens, without them. LibreOffice does not break lines exactly
where Textos does, so a document may have a page more or less there. A window that shows a format
that keeps less than the document has says so in the line at the bottom.

A task («/» → Lista de tareas) is a paragraph that starts with «☐»: it is text, not a box that
a click ticks.

## Build

Needs Qt ≥ 6.8 (base, declarative, svg) and zlib. On Arch:
`pacman -S cmake ninja qt6-base qt6-declarative qt6-svg zlib`.

```sh
cmake -S apps/textos -B target/apps/textos -G Ninja
cmake --build target/apps/textos
ctest --test-dir target/apps/textos --output-on-failure
cmake --install target/apps/textos --prefix ~/.local
```

The tests that talk to LibreOffice are skipped where `soffice` is not installed.

## Pictures of the window, without a desktop

```sh
WONDERLAND_TEXTOS_DATA=/var/tmp/textos WONDERLAND_TEXTOS_GLASS=0 QT_QPA_PLATFORM=offscreen \
    wonderland-textos --appearance dark --size 1180x760 --shot out.png --do "sample;select:40-90"
```

`--do` takes the id of any action of the palette, with an argument after a colon where it has one:
`sample`, `new`, `newLetter`, `newResume`, `newReport`, `newMinutes`, `open:<file>`, `save`,
`saveAs:<file>`, `exportPdf:<file>` (and `exportDocx`, `exportOdt`, `exportRtf`, `exportHtml`,
`exportMd`, `exportTxt`), `rename:<name>`, `versions`, `close`; `bold`, `italic`, `underline`,
`strike`, `superscript`, `subscript`, `highlight[:rrggbb]`, `color:<rrggbb>`, `family:<name>`,
`size:<pt>`; `styleTitle`, `styleH1` … `styleH3`, `styleBody`, `styleQuote`, `styleCode`,
`alignLeft`, `alignCenter`, `alignRight`, `alignJustify`, `bullets`, `numbers`, `indentMore`,
`indentLess`, `spacing100` … `spacing200`; `table[:ROWSxCOLUMNS]`, `image[:<file>]`,
`link[:<address>]`, `pageBreak`, `separator`, `date`, `task`, `rowBelow`, `columnAfter`,
`removeRow`, `removeColumn`, `removeTable`; `find:<text>`, `replace:<text>`, `findNext`;
`palette[:<text>]`, `sidebar`, `inspector[:format|document]`, `focus`, `zoom:<percent>`,
`zoomFit`, `landscape`, `portrait`, `pageNumbers`, `header:<text>`, `goal:<words>`, `markdown`,
`about`. And, for what a hand would do: `type:<text>` (¶ is Return), `key:<ctrl+shift+left>`,
`select:<from>-<to>`, `goto:<position>`, `end`, `slash[:<text>]`, `context`, `exportMenu`,
`asksave`, `welcome` and `say:<text>`. Files named on the command line are opened first.
`WONDERLAND_TEXTOS_DATA` moves the data directory, so a test does not touch your versions.

Icons: [Lucide](https://lucide.dev) (ISC), see `assets/symbols/LICENSE-lucide`.
