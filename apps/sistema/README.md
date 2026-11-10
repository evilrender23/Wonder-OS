# Sistema

The system settings of Wonderlands OS. It edits the compositor's `~/.config/driftwm/config.toml`;
the compositor reloads the file on its own, so a change shows the moment it is made.

- **Every setting of the compositor**, by page: General, Teclado, Ratón y panel táctil, Lienzo y
  zoom, Ventanas, Efectos, Fondo, Pantallas, Gestos y ratón, Avanzado. A control shows the
  compositor's default until you change it; ↺ puts the default back.
- **Buscar**: the field at the top of the sidebar (Ctrl+F) finds a setting by its name, its
  description, an option or its `config.toml` key, whatever the case or the accents («raton»
  finds «Ratón»). The settings found can be changed right there; the heading above them, and the
  pages, tables and keyboard shortcuts that match, lead to where they live.
- **Atajos de teclado**: every keyboard shortcut, built-in and yours. Create one (pick the
  modifiers and the key, or record the keystroke; then an action or a command to run), edit,
  switch a built-in one off, restore it. A combination already in use says what it does now.
- Mouse, trackpad-gesture and touch bindings, and `[env]`, as editable tables.
- **Carpetas**: the colour of the desktop's folders, on the Desktop and in Rabbit. One
  of the usual colours or any `#rrggbb`; a click shows it on the folder of the page and «Aplicar»
  puts it in use. Folders given a colour of their own («Personalizar carpeta…») keep it. This one
  is not a compositor setting: it is kept in `~/.config/wonderland/appearance.json`
  (`{"folder_tint": "#rrggbb"}`), which the file manager and the desktop icons watch.

The file stays yours: comments, order and spacing are kept, and only the line of the setting you
touch changes. Each change is checked by the compositor (`driftwm --check-config`) before it is
written, so a control cannot leave a config the compositor refuses; the message it gives is shown
instead. Ctrl+Z undoes, and the file as it was before the first change of a run is kept beside it as
`config.toml.sistema.bak`.

Not here: `[[outputs]]` (the desktop menu's «Pantallas…» panel does monitors), `[[window_rules]]`,
bookmarks and anchors. «Abrir config.toml» opens the file for those.

## Build

Needs Qt ≥ 6.8 (base, declarative, svg) and libxkbcommon. On Arch:
`pacman -S cmake ninja qt6-base qt6-declarative qt6-svg libxkbcommon`.

```sh
cmake -S apps/sistema -B apps/sistema/build -G Ninja
cmake --build apps/sistema/build
ctest --test-dir apps/sistema/build --output-on-failure
cmake --install apps/sistema/build --prefix ~/.local
```

Once installed, «Ajustes del sistema…» in the desktop menu opens it.

## Run

```sh
wonderland-sistema
wonderland-sistema --page atajos
wonderland-sistema --search "panel táctil"
wonderland-sistema --config /tmp/copy.toml --appearance dark   # try it on a copy
```

The pages are described in `qml/Schema.js`; their defaults are read from the repo's
`config.reference.toml`, built into the binary, so they cannot drift from the compositor.
