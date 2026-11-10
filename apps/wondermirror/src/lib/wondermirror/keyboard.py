"""Which keyboard layout the Windows apps type with.

FreeRDP sends Windows the position of each key, not its character, so what a
key writes is decided inside Windows. Left alone, Windows takes the layout the
Remote Desktop client announces, and that one comes from the private X server
the windows live in, not from the keyboard the user really has. So the layout
is chosen here: by default the one of this computer, or the one picked with
`wondermirror keyboard`. It is announced when connecting (rdp.py) and the guest
agent switches every running app to it (guest/keyboard.ps1).
"""

from __future__ import annotations

import json
import os
import re
import shutil
import subprocess
import tomllib
from dataclasses import dataclass
from pathlib import Path

FOLLOW = ""  # the setting's value for «the layout of this computer»


@dataclass(frozen=True)
class Layout:
    key: str  # what the user types: wondermirror keyboard es
    name: str
    klid: str  # Windows' keyboard layout identifier
    xkb: tuple[str, ...] = ()  # the host layouts it stands for: «layout» or «layout:variant»


LAYOUTS = (
    Layout("es", "Spanish", "0000040A", ("es",)),
    Layout("latam", "Spanish (Latin America)", "0000080A", ("latam",)),
    Layout("us", "English (US)", "00000409", ("us",)),
    Layout("us-intl", "English (US, international)", "00020409", ("us:intl",)),
    Layout("us-dvorak", "English (US, Dvorak)", "00010409", ("us:dvorak",)),
    Layout("gb", "English (UK)", "00000809", ("gb",)),
    Layout("ie", "Irish", "00001809", ("ie",)),
    Layout("fr", "French", "0000040C", ("fr",)),
    Layout("be", "Belgian", "00000813", ("be",)),
    Layout("ca", "Canadian French", "00001009", ("ca",)),
    Layout("ca-multi", "Canadian (multilingual)", "00011009", ("ca:multix",)),
    Layout("ch", "Swiss German", "00000807", ("ch", "ch:de")),
    Layout("ch-fr", "Swiss French", "0000100C", ("ch:fr",)),
    Layout("de", "German", "00000407", ("de", "at")),
    Layout("it", "Italian", "00000410", ("it",)),
    Layout("pt", "Portuguese", "00000816", ("pt",)),
    Layout("br", "Portuguese (Brazil)", "00000416", ("br",)),
    Layout("nl", "Dutch", "00000413", ("nl",)),
    Layout("dk", "Danish", "00000406", ("dk",)),
    Layout("no", "Norwegian", "00000414", ("no",)),
    Layout("se", "Swedish", "0000041D", ("se",)),
    Layout("fi", "Finnish", "0000040B", ("fi",)),
    Layout("is", "Icelandic", "0000040F", ("is",)),
    Layout("pl", "Polish", "00000415", ("pl",)),
    Layout("cz", "Czech", "00000405", ("cz",)),
    Layout("sk", "Slovak", "0000041B", ("sk",)),
    Layout("hu", "Hungarian", "0000040E", ("hu",)),
    Layout("ro", "Romanian", "00010418", ("ro",)),
    Layout("hr", "Croatian", "0000041A", ("hr",)),
    Layout("si", "Slovenian", "00000424", ("si",)),
    Layout("rs", "Serbian (Cyrillic)", "00000C1A", ("rs",)),
    Layout("rs-latin", "Serbian (Latin)", "0000081A", ("rs:latin",)),
    Layout("bg", "Bulgarian", "00030402", ("bg",)),
    Layout("gr", "Greek", "00000408", ("gr",)),
    Layout("tr", "Turkish (Q)", "0000041F", ("tr",)),
    Layout("tr-f", "Turkish (F)", "0001041F", ("tr:f",)),
    Layout("ru", "Russian", "00000419", ("ru",)),
    Layout("ua", "Ukrainian", "00020422", ("ua",)),
    Layout("ee", "Estonian", "00000425", ("ee",)),
    Layout("lv", "Latvian", "00020426", ("lv",)),
    Layout("lt", "Lithuanian", "00010427", ("lt",)),
    Layout("il", "Hebrew", "0002040D", ("il",)),
    Layout("ara", "Arabic", "00000401", ("ara",)),
)

_KLID = re.compile(r"[0-9a-fA-F]{8}")


def find(name: str) -> Layout | None:
    """The layout a user means: its key, its name, or any Windows identifier."""
    wanted = name.strip().lower()
    for layout in LAYOUTS:
        if wanted in (layout.key, layout.name.lower(), layout.klid.lower()):
            return layout
    if _KLID.fullmatch(wanted):
        # Windows has a couple of hundred layouts; any of them can be named.
        return Layout(wanted, f"Windows layout {wanted.upper()}", wanted.upper())
    return None


def _run(*command: str) -> str:
    if not shutil.which(command[0]):
        return ""
    try:
        return subprocess.run(command, capture_output=True, text=True, timeout=3, check=False).stdout
    except (OSError, subprocess.SubprocessError):
        return ""


def _compositor_layout() -> str:
    """The layout in use this instant, when the desktop has several."""
    for program in ("wonderlandwm", "driftwm"):
        try:
            return str(json.loads(_run(program, "msg", "state", "--json"))["Ok"]["State"]["layout_short"])
        except (ValueError, KeyError, TypeError):
            continue
    return ""


def _configured() -> list[tuple[str, str]]:
    """Every place a layout is written down, as (layouts, variants), best first."""
    found = []
    home = Path(os.environ.get("XDG_CONFIG_HOME") or Path.home() / ".config")
    for name in ("wonderlandwm", "driftwm"):
        try:
            keyboard = tomllib.loads((home / name / "config.toml").read_text(encoding="utf-8"))["input"]["keyboard"]
            found.append((str(keyboard.get("layout", "")), str(keyboard.get("variant", ""))))
        except (OSError, ValueError, KeyError, TypeError, AttributeError):
            pass
    found.append((os.environ.get("XKB_DEFAULT_LAYOUT", ""), os.environ.get("XKB_DEFAULT_VARIANT", "")))
    system = dict(
        (key.strip(), value.strip())
        for key, _, value in (line.partition(":") for line in _run("localectl", "status").splitlines())
    )
    found.append((system.get("X11 Layout", ""), system.get("X11 Variant", "")))
    return found


def host_xkb() -> tuple[str, str]:
    """This computer's layout and variant, in XKB's names; ("", "") if unknown."""
    active = _compositor_layout()
    first = None
    for layouts, variants in _configured():
        names = [name.strip() for name in layouts.split(",") if name.strip()]
        if not names:
            continue
        kinds = [kind.strip() for kind in variants.split(",")]
        index = names.index(active) if active in names else 0
        answer = (names[index], kinds[index] if index < len(kinds) else "")
        if not active or active in names:
            return answer
        first = first or answer
    return (active, "") if active else first or ("", "")


def host() -> Layout:
    """The Windows layout that matches this computer's keyboard."""
    name, variant = host_xkb()
    for wanted in (f"{name}:{variant}", name):
        for layout in LAYOUTS:
            if wanted in layout.xkb:
                return layout
    return LAYOUTS[2]  # nothing known about it: the US layout, as everywhere else


def effective(setting: str) -> Layout:
    """The layout a `keyboard` setting stands for."""
    return (find(setting) if setting else None) or host()


def valid(setting: str) -> bool:
    return setting == FOLLOW or find(setting) is not None


# The picker ---------------------------------------------------------------

def _dark() -> bool:
    """Whether the shell is in its dark theme, so that the picker matches it."""
    state = Path(os.environ.get("XDG_STATE_HOME") or Path.home() / ".local/state")
    try:
        return json.loads((state / "wonderlandwm" / "bar.json").read_text(encoding="utf-8")).get("themeMode") == "dark"
    except (OSError, ValueError, AttributeError):
        return False


def pick(setting: str, program: list[str]) -> int:
    """Show the picker. It runs `program keyboard <key>` for the layout chosen."""
    spanish = (os.environ.get("LC_ALL") or os.environ.get("LC_MESSAGES") or os.environ.get("LANG") or "").startswith("es")
    current = effective(setting)
    here = host()
    scene = Path(__file__).with_name("keyboard.qml")
    shell = shutil.which("quickshell") or shutil.which("qs")
    if shell and scene.exists():
        environment = dict(os.environ)
        environment["WONDERMIRROR_KEYBOARD"] = json.dumps({
            "layouts": [{"key": layout.key, "name": layout.name} for layout in LAYOUTS],
            "current": FOLLOW if setting == FOLLOW else current.key,
            "host": here.name,
            "command": [*program, "keyboard"],
            "dark": _dark(),
            "spanish": spanish,
        })
        return subprocess.run([shell, "-p", str(scene)], env=environment, check=False).returncode
    # Without the desktop's shell, a plain list from whatever dialog tool there is.
    title = "Teclado de Wonder Mirror" if spanish else "Wonder Mirror Keyboard"
    text = "Teclado de las apps de Windows" if spanish else "Keyboard of the Windows apps"
    rows = [("auto", f"{'Automático' if spanish else 'Automatic'} ({here.name})")]
    rows += [(layout.key, layout.name) for layout in LAYOUTS]
    marked = "auto" if setting == FOLLOW else current.key
    if shutil.which("zenity"):
        command = ["zenity", "--list", "--radiolist", f"--title={title}", f"--text={text}", "--height=520",
                   "--hide-header", "--column=", "--column=key", "--column=name", "--hide-column=2", "--print-column=2"]
        for key, name in rows:
            command += ["TRUE" if key == marked else "FALSE", key, name]
    elif shutil.which("kdialog"):
        command = ["kdialog", "--title", title, "--radiolist", text]
        for key, name in rows:
            command += [key, name, "on" if key == marked else "off"]
    else:
        return 1
    chosen = subprocess.run(command, capture_output=True, text=True, check=False).stdout.strip()
    if not chosen:
        return 0
    return subprocess.run([*program, "keyboard", chosen], check=False).returncode
