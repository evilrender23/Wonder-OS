# Fórmulas

The formula editor of Wonderlands OS: mathematics written in LaTeX and drawn as it is typed. It is
the desktop's app for `.wform` files and opens LibreOffice Math's `.odf` and fragments of `.tex`.

- **Code and picture**: the LaTeX below, coloured as it is written, with the pair of the brace under
  the cursor in view; the formula above, large and sharp. A click on a part of the formula takes the
  cursor to its code. A backslash offers the orders that begin that way, each with its symbol.
- **Its own typesetting**: boxes the way TeX makes them — display, text and script styles, baseline,
  height and depth, the space between atoms — drawn with the OpenType math font the system has
  (Latin Modern Math, STIX Two Math, Libertinus Math, TeX Gyre, DejaVu Math TeX Gyre…), with its
  constants, its tall delimiters, its assembled braces and its script forms. Without one, a serif
  with the Unicode symbols.
- **What it understands**: `\frac`, `\binom`, `\sqrt[n]{}`, `^` and `_` (primes, prescripts),
  `\sum \prod \int \oint \lim` and the rest with their limits (`\limits`, `\nolimits`),
  `\left … \middle … \right`, `matrix`, `pmatrix`, `bmatrix`, `vmatrix`, `cases`, `aligned`, Greek,
  operators, relations and arrows, `\mathbb \mathbf \mathrm \mathcal \mathfrak \text
  \operatorname`, the accents (`\hat \bar \vec \dot \ddot \tilde \overline \underline \widehat
  \overrightarrow \overbrace \underbrace`), spaces, `\color`, `\textcolor` and `\boxed`.
- **Mistakes do not stop it**: what is valid is drawn, what is not is marked in red in the code and
  in the formula, and the inspector says which column and why.
- **Symbols**: the sidebar holds the structures and symbols by category, with a search by their
  Spanish name («integral», «flecha», «alfa»). A click writes one at the cursor.
- **Library**: formulas kept with a name and a miniature, and a collection that comes with the app
  (algebra, calculus, physics, statistics), also on the start screen.
- **Copiar como…**: a PNG with a transparent background at the scale chosen, SVG, LaTeX or MathML,
  on the clipboard, to paste into Textos and Diapositivas.
- **A document is a numbered list of equations**, with one colour, size and font, that exports to
  PDF as a page.
- **Versions**: every save by hand leaves a copy (the last 20 of each document) that can be looked
  at and brought back. Unsaved work is kept aside every few seconds and offered again after a crash.
- **Órdenes** (Ctrl+K): every action of the app, found by typing, without minding accents.
- Opening another document starts another window. Light and dark follow the desktop, as does glass
  under WonderlandWM.

Keys: Ctrl+K the orders, Ctrl+N new, Ctrl+O open, Ctrl+S save, Ctrl+Shift+S save as, Ctrl+E export,
Ctrl+P PDF, F2 rename, Ctrl+W close; Ctrl+Return a new equation, Ctrl+D duplicate it,
Ctrl+Shift+Backspace delete it, Ctrl+PgUp / Ctrl+PgDown the previous and the next, Alt+↑ / Alt+↓
move it; Ctrl+/ a fraction, Ctrl+R a root, Ctrl+↑ / Ctrl+↓ super- and subscript, Ctrl+M a matrix,
Ctrl+9 growing parentheses, Ctrl+T text, Ctrl+Shift+B bold, Ctrl+Space the orders that fit;
Ctrl+Shift+C / M / V / I copy as LaTeX, MathML, SVG and PNG; Ctrl+B keep in the library, Ctrl+1 …
Ctrl+3 equations, symbols and library, Ctrl+F find a symbol; Ctrl+Shift+. and Ctrl+Shift+, the size
of the letter, Ctrl++ / Ctrl+- / Ctrl+0 the zoom, Ctrl+Alt+S the sidebar, Ctrl+Alt+I the inspector,
Ctrl+Shift+F nothing but the formula and its code (Esc comes back).

Settings, the library, the versions and the recovery copies live in
`~/.local/share/wonderland/wonderland-formulas/`. Documents live where you save them.

## Formats

| Format | Reads | Writes | Notes |
|---|---|---|---|
| `.wform` (`application/x-wonderland-formula`) | yes | yes | The default: JSON with the LaTeX of each equation, title, colour, size and font. Nothing is lost. |
| `.odf` (LibreOffice Math) | yes | yes | A ZIP with `content.xml` in presentation MathML and a StarMath annotation. Checked against LibreOffice 26: it opens what Fórmulas writes (five equations, matrices and cases among them, with no error marks) and Fórmulas reads what LibreOffice writes. |
| `.tex` | yes | yes | A fragment: one `\[ … \]` (or `$$ … $$`, `equation`) per equation. Not a whole LaTeX document: preamble and macros are not interpreted. |
| MathML (`.mml`) | — | yes | Presentation MathML with the LaTeX as annotation. Also on the clipboard. |
| SVG, PNG | — | yes | The equation in hand, as outlines; PNG at 1× to 8×, transparent or white. |
| PDF | — | yes | The whole document: title and numbered equations, A4. |

`.wform` is the default because it is the one that comes back exactly. An `.odf` written by Fórmulas
also carries the document as written, so it reopens here unchanged; one written by LibreOffice is
read from its MathML, which keeps the mathematics but not the way the LaTeX was typed (`^{2}` for
`^2`), and not colours or anything MathML has no element for. Going out, LibreOffice draws the
StarMath annotation: several equations become lines of one formula, and what StarMath has no word
for (`\mathcal`, `\boxed`, some arrows) goes as the nearest thing it has. MS Office's equation
format (OMML) is not read or written.

## Build

Needs Qt ≥ 6.8 (base, declarative, svg) and zlib. On Arch:
`pacman -S cmake ninja qt6-base qt6-declarative qt6-svg zlib`.

```sh
cmake -S apps/formulas -B target/apps/formulas -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build target/apps/formulas
ctest --test-dir target/apps/formulas --output-on-failure
cmake --install target/apps/formulas --prefix ~/.local
```

The engine (`latex`, `layout`, `mathfont`, `mathml`, `formats`, `symbols`, `library`, `session`,
`io`) has no window in it and is what the tests exercise.

## Pictures of the window, without a desktop

```sh
QT_QPA_PLATFORM=offscreen WONDERLAND_FORMULAS_DATA=/tmp/formulas WONDERLAND_FORMULAS_GLASS=0 \
    wonderland-formulas --appearance dark --size 1240x780 --shot out.png \
    --do "sample;equation:3;tab:symbols"
```

`--do` takes the id of any order of the palette (`add`, `duplicate`, `frac`, `copy-latex`,
`versions`, `concentrate`, `palette`…) and, besides: `sample` (five made-up equations),
`template:<key>`, `open:<path>`, `save:<path>`, `export:<png|svg|pdf|mml|odf|tex>:<path>[:<scale>]`,
`welcome`, `scroll:<y>`, `equation:<n>`, `new-equation:<latex>`, `latex:<latex>`, `type:<text>`,
`accept`, `insert:<snippet>`, `cursor:<n>`, `select:<from>,<to>`, `tab:<equations|symbols|library>`,
`search:<text>`, `keep:<name>`, `title:<text>`, `size:<pt>`, `color:<#rrggbb>`, `font:<family>`,
`zoom:<factor>`, `copy:<latex|mathml|svg|png>`, `copy-menu`, `export-dialog:<format>`,
`palette-query:<text>`, `restore:<n>`, `recover` and `say:<text>`. Actions cannot contain `;`.
`WONDERLAND_FORMULAS_DATA` moves the data directory, so a test does not touch your library.

Icons: [Lucide](https://lucide.dev) (ISC), see `assets/symbols/LICENSE-lucide`.
