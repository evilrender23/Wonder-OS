# wonderterm

WonderlandWM's terminal. A real VT emulator (the `alacritty_terminal` core)
under a window it draws itself: tabs in the header, a status bar, the Hermes
palette, and a cursor that moves instead of teleporting.

## The cursor

The cursor is four corners on four springs. The corners facing the direction
of travel arrive first, so the block stretches along its path and snaps back
into shape when it lands. Changing shape (block to beam in an editor's insert
mode) is animated the same way.

| `[cursor]` key | Values | |
|---|---|---|
| `animation` | `smear` · `glide` · `none` | `glide` moves the block rigidly |
| `duration_ms` | `150` | a jump across the screen |
| `typing_duration_ms` | `50` | a step of a cell or two on the same line |
| `trail` | `0`–`1` | how far the tail lags (smear only) |
| `vfx` | `ripple` · `sparkle` · `none` | played where a jump lands |
| `glow`, `blink` | `true` / `false` | blinking stops after 20 s idle |

## Build and install

```bash
cd apps/wonderterm
make build
sudo -S -p '' make install        # binary, .desktop entry, icon
```

It is a separate crate with its own `Cargo.lock`, so building the compositor
does not build it.

It is the distro's default terminal: `extras/config.toml` sets
`TERMINAL = "wonderterm"` in `[env]`, which `mod+return` and the bar follow.
Without that variable the compositor still picks it first when it is on
`PATH`.

## Configuration

`~/.config/wonderterm/config.toml`. `wonderterm --print-default-config` prints
every option with its default; an unknown key is an error, shown in the status
bar, and the defaults are used.

`theme = "auto"` follows the shell's light/dark switch live.

The background is translucent (`[window] opacity`). The rule in
`extras/config.toml` has the compositor frost, round and shade the window:

```toml
[[window_rules]]
app_id = "wonderterm"
blur = true
corner_radius = 12
shadow = true
```

## Keys

| | |
|---|---|
| `ctrl+shift+t` / `ctrl+shift+w` | new tab / close tab |
| `ctrl+tab`, `ctrl+shift+tab`, `ctrl+pageup/pagedown` | switch tab |
| `ctrl+shift+n` | new window in the same directory |
| `ctrl+c` / `ctrl+x` with text selected | copy / cut |
| `ctrl+v` | paste (a full-screen program such as vim gets the key instead) |
| `ctrl+shift+c` / `ctrl+shift+x` / `ctrl+shift+v`, `shift+insert` | copy / cut / paste, always |
| `ctrl+shift+a` | select everything, scrollback included |
| `shift+pageup/pagedown` | scroll back |
| `ctrl` `+` / `-` / `0` | font size |

Drag selects, double click selects a word, triple a line; middle click
pastes and right click opens a menu with copy, cut, paste and select all.

With nothing selected `ctrl+c` and `ctrl+x` are the usual control codes, so
`ctrl+c` still interrupts. Cut copies the selection and, when it lies on the
line being typed, deletes it there by moving the cursor and backspacing, as
you would; text a program already printed can only be copied.
`clipboard_keys = false` under `[terminal]` leaves `ctrl+c/x/v` to the
programs and keeps only the `ctrl+shift` forms.

Drag the header to move the window, double click it to maximise.

## Command line

```
wonderterm [-e COMMAND...] [-d DIR] [-c FILE]
wonderterm --screenshot out.png [--screenshot-delay MS]
```

`--screenshot` saves the window to a PNG and exits, for checking rendering
changes without a human in the loop.
