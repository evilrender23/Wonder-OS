# Wonderland's login screen

`wonderland-greeter` is the distro's own login manager: [greetd] asks for the
password, the compositor draws the screen and a small Quickshell UI is the only
thing on it. Same compositor, same typeface and colours as the desktop, and the
live wallpaper «Rayos de luz» behind — rendered by the compositor itself, with
the settings you use for it on the desktop (light or dark included).

- clock and date, your avatar and name, the password;
- other accounts as small chips under the field, or a name typed by hand;
- the session to start (bottom left; remembered per machine) and suspend,
  restart, power off (bottom right);
- whatever PAM asks beyond a password (a code, a new password) is asked in the
  same field; its messages show under it.

Keyboard only is enough: type, `Enter`; `Esc` clears or goes back.

## Install

The distro's `./install.sh` installs it and makes it the login manager
(`--no-greeter` skips that). By hand:

    extras/greeter/install.sh            # install; your login manager stays
    extras/greeter/install.sh --enable   # ...and make it the login manager
    extras/greeter/install.sh --disable  # back to the one you had
    extras/greeter/install.sh --uninstall

Run it as yourself from the desktop (`-n` shows what it would do). It reads
`~/.config/driftwm/config.toml` so the login screen has your monitors, keyboard
layout and pointer settings; run it again after changing those. `--enable`
takes effect at the next boot and stops nothing that is running; the login
manager it replaces is written to `/etc/wonderland-greeter/previous-dm`, which
is what `--disable` brings back.

If the screen ever fails to come up, greetd is still there on VT 1 and any
other VT (`Ctrl+Alt+F3`) gives a text login: run `--disable` from it. The log
is `/var/lib/wonderland-greeter/greeter.log`.

| Where | What |
|---|---|
| `/usr/local/bin/wonderland-greeter` | what greetd runs, as the `greeter` user |
| `/usr/local/share/wonderland-greeter/` | the UI and the rendered wallpaper |
| `/etc/wonderland-greeter/driftwm.toml` | the compositor's config: no bindings, no X11 |
| `/etc/wonderland-greeter/greeter.json` | `theme` (`dark`/`light`), `lang` (`es`/`en`), `screen` (output for the form; empty = the largest), `clock24` |
| `/etc/greetd/config.toml` | greetd's (a previous one is kept as `.pre-wonderland`) |

## Try it without touching the login

    extras/greeter/test/try-greeter /tmp/greeter --hold 60

opens it in a window against a stand-in for greetd (password `wonderland`).
With `--step` it types and takes screenshots; see the top of the script.

[greetd]: https://sr.ht/~kennylevinsen/greetd/
