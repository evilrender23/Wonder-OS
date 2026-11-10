"""Windows apps as desktop entries, so the launcher, Spotlight and the dock
treat them like any other app."""

from __future__ import annotations

import hashlib
import json
import re
from dataclasses import dataclass
from pathlib import Path

from . import config

PREFIX = "wondermirror-app-"
MARK = "X-WonderMirror-App"


@dataclass(frozen=True)
class App:
    key: str  # stable, safe to put in a file name or a command line
    id: str  # what Windows calls it (an AppUserModelID)
    name: str
    icon: str  # absolute path of a PNG, or ""


def key_of(app_id: str) -> str:
    return hashlib.sha1(app_id.encode("utf-8")).hexdigest()[:16]


def catalogue_path() -> Path:
    return config.state_dir() / "apps.json"


def icons_dir() -> Path:
    return config.state_dir() / "icons"


def catalogue() -> list[App]:
    """The apps Windows last reported. The file is written by the guest:
    nothing in it is trusted beyond being text."""
    try:
        raw = json.loads(catalogue_path().read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        return []
    if isinstance(raw, dict):  # PowerShell unwraps one-element lists
        raw = [raw]
    if not isinstance(raw, list):
        return []
    apps: dict[str, App] = {}
    for entry in raw:
        if not isinstance(entry, dict):
            continue
        app_id, name, icon = entry.get("id"), entry.get("name"), entry.get("icon") or ""
        if not isinstance(app_id, str) or not isinstance(name, str) or not app_id or not name.strip():
            continue
        if any(ord(c) < 32 for c in app_id):
            continue
        icon_path = ""
        if isinstance(icon, str) and re.fullmatch(r"[0-9a-f]{40}\.png", icon):
            candidate = icons_dir() / icon
            if candidate.is_file():
                icon_path = str(candidate)
        key = key_of(app_id)
        apps[key] = App(key, app_id, _one_line(name)[:120], icon_path)
    return sorted(apps.values(), key=lambda app: app.name.casefold())


def find(reference: str) -> App | None:
    """An app by key, by Windows id or by (case-insensitive) name."""
    apps = catalogue()
    for test in (
        lambda app: app.key == reference,
        lambda app: app.id == reference,
        lambda app: app.name.casefold() == reference.casefold(),
    ):
        for app in apps:
            if test(app):
                return app
    return None


def _one_line(text: str) -> str:
    return " ".join("".join(c if ord(c) >= 32 else " " for c in text).split())


def desktop_entry(app: App) -> str:
    # Values of a desktop entry are one line; backslash is its escape character.
    name = app.name.replace("\\", "\\\\")
    lines = [
        "[Desktop Entry]",
        "Type=Application",
        f"Name={name}",
        "GenericName=Windows app",
        "GenericName[es]=Aplicación de Windows",
        "Comment=Runs in Windows through Wonder Mirror",
        "Comment[es]=Se ejecuta en Windows mediante Wonder Mirror",
        f"Exec=wondermirror open {app.key} %F",
        f"Icon={app.icon or 'wondermirror'}",
        "Terminal=false",
        "Categories=Windows;",
        "Keywords=windows;wonder mirror;",
        "StartupNotify=false",
        f"{MARK}={app.key}",
        "",
    ]
    return "\n".join(lines)


def sync() -> tuple[int, int]:
    """Make the desktop entries match the catalogue. Returns (written, removed)."""
    directory = config.applications_dir()
    wanted = {f"{PREFIX}{app.key}.desktop": desktop_entry(app) for app in catalogue()}
    written = removed = 0
    for name, text in wanted.items():
        path = directory / name
        try:
            if path.read_text(encoding="utf-8") == text:
                continue
        except OSError:
            pass
        config.write_atomic(path, text)
        written += 1
    if directory.is_dir():
        for path in directory.glob(f"{PREFIX}*.desktop"):
            if path.name not in wanted and _is_ours(path):
                path.unlink(missing_ok=True)
                removed += 1
    return written, removed


def _is_ours(path: Path) -> bool:
    try:
        return f"\n{MARK}=" in path.read_text(encoding="utf-8")
    except (OSError, UnicodeDecodeError):
        return False


def clear() -> None:
    """Forget every Windows app (the machine was removed)."""
    directory = config.applications_dir()
    if directory.is_dir():
        for path in directory.glob(f"{PREFIX}*.desktop"):
            if _is_ours(path):
                path.unlink(missing_ok=True)
    catalogue_path().unlink(missing_ok=True)
    if icons_dir().is_dir():
        for icon in icons_dir().glob("*.png"):
            icon.unlink(missing_ok=True)
