"""Where Wonder Mirror keeps its things, and the user's settings."""

from __future__ import annotations

import json
import os
import secrets
import string
import tempfile
from dataclasses import asdict, dataclass, fields
from pathlib import Path


def _xdg(variable: str, default: str) -> Path:
    value = os.environ.get(variable)
    return Path(value) if value else Path.home() / default


def data_dir() -> Path:
    """The machine itself: disk, firmware variables, TPM state, credentials."""
    return _xdg("XDG_DATA_HOME", ".local/share") / "wondermirror"


def config_dir() -> Path:
    return _xdg("XDG_CONFIG_HOME", ".config") / "wondermirror"


def cache_dir() -> Path:
    return _xdg("XDG_CACHE_HOME", ".cache") / "wondermirror"


def state_dir() -> Path:
    """What the shell reads: status.json, the app catalogue and its icons."""
    return _xdg("XDG_STATE_HOME", ".local/state") / "wondermirror"


def runtime_dir() -> Path:
    base = os.environ.get("XDG_RUNTIME_DIR") or tempfile.gettempdir()
    return Path(base) / "wondermirror"


def applications_dir() -> Path:
    return _xdg("XDG_DATA_HOME", ".local/share") / "applications"


def write_atomic(path: Path, text: str, mode: int = 0o644) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    handle, temporary = tempfile.mkstemp(dir=path.parent, prefix=f".{path.name}.")
    try:
        os.fchmod(handle, mode)
        with os.fdopen(handle, "w", encoding="utf-8") as out:
            out.write(text)
        os.replace(temporary, path)
    except BaseException:
        Path(temporary).unlink(missing_ok=True)
        raise


def _default_cpus() -> int:
    return max(2, min(8, (os.cpu_count() or 4) // 2))


def _default_memory() -> int:
    """A quarter of the host's memory, between 4 and 16 GiB."""
    try:
        total = os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES") // (1 << 20)
    except (ValueError, OSError):
        total = 16384
    return max(4096, min(16384, total // 4 // 1024 * 1024))


@dataclass
class Settings:
    cpus: int = 0  # 0: half of the host's threads, up to 8
    memory_mb: int = 0  # 0: a quarter of the host's memory, 4 to 16 GiB
    disk_gb: int = 128  # the disk only takes the space Windows really uses
    rdp_port: int = 33890  # on 127.0.0.1 only
    locale: str = ""  # "" follows the system language
    share_home: bool = True  # the home folder appears in Windows as \\tsclient\home
    clipboard: bool = True
    sound: bool = True
    scale: int = 100  # 100, 140 or 180
    idle_minutes: int = 10  # pause Windows this long after its last window closes; 0 never
    gpu: bool = False  # experimental: share the host's graphics card with Windows (see gpu.py)
    gpu_card: str = ""  # "" picks one; else a render node or kernel driver: renderD128, amdgpu…
    gpu_memory_mb: int = 4096  # how much of the card's memory Windows may map
    keyboard: str = ""  # "" types with this computer's layout; else one of `wondermirror keyboard list`

    @property
    def effective_cpus(self) -> int:
        return self.cpus if self.cpus > 0 else _default_cpus()

    @property
    def effective_memory_mb(self) -> int:
        return self.memory_mb if self.memory_mb > 0 else _default_memory()

    @property
    def effective_locale(self) -> str:
        if self.locale:
            return self.locale
        language = (os.environ.get("LC_ALL") or os.environ.get("LANG") or "en_US").split(".")[0]
        return language.replace("_", "-") if "_" in language else "en-US"


class SettingsError(ValueError):
    pass


def validate(settings: Settings) -> Settings:
    if not 0 <= settings.cpus <= 256:
        raise SettingsError("cpus must be between 1 and 256 (0 = automatic)")
    if settings.memory_mb and not 2048 <= settings.memory_mb <= 1 << 20:
        raise SettingsError("memory_mb must be at least 2048 (0 = automatic)")
    if not 64 <= settings.disk_gb <= 4096:
        raise SettingsError("disk_gb must be between 64 and 4096")
    if not 1024 <= settings.rdp_port <= 65535:
        raise SettingsError("rdp_port must be between 1024 and 65535")
    if settings.scale not in (100, 140, 180):
        raise SettingsError("scale must be 100, 140 or 180")
    if not 256 <= settings.gpu_memory_mb <= 65536:
        raise SettingsError("gpu_memory_mb must be between 256 and 65536")
    if settings.idle_minutes < 0:
        raise SettingsError("idle_minutes cannot be negative")
    from . import keyboard  # here: nothing else in this module needs it

    if not keyboard.valid(settings.keyboard):
        raise SettingsError("keyboard must be one of «wondermirror keyboard list» (empty = this computer's)")
    return settings


def settings_path() -> Path:
    return config_dir() / "settings.json"


def load_settings() -> Settings:
    try:
        raw = json.loads(settings_path().read_text(encoding="utf-8"))
    except FileNotFoundError:
        return Settings()
    except (OSError, ValueError) as error:
        raise SettingsError(f"{settings_path()}: {error}") from error
    if not isinstance(raw, dict):
        raise SettingsError(f"{settings_path()}: expected an object")
    known = {f.name: f.type for f in fields(Settings)}
    values = {}
    for key, value in raw.items():
        if key not in known:
            continue
        default = getattr(Settings, key)
        if type(value) is not type(default):
            raise SettingsError(f"{settings_path()}: «{key}» has the wrong type")
        values[key] = value
    return validate(Settings(**values))


def save_settings(settings: Settings) -> None:
    validate(settings)
    write_atomic(settings_path(), json.dumps(asdict(settings), indent=2) + "\n")


@dataclass
class Account:
    """The Windows account every Wonder Mirror window logs in with."""

    user: str
    password: str


def account_path() -> Path:
    return data_dir() / "account.json"


def windows_user_name(name: str) -> str:
    """A name Windows accepts for a local account."""
    cleaned = "".join(c for c in name if c.isalnum() or c in "-_")[:20]
    reserved = {"administrator", "administrador", "guest", "invitado", "system", "defaultaccount"}
    return cleaned if cleaned and cleaned.lower() not in reserved else "wonder"


def load_account() -> Account | None:
    try:
        raw = json.loads(account_path().read_text(encoding="utf-8"))
        return Account(str(raw["user"]), str(raw["password"]))
    except (OSError, ValueError, KeyError):
        return None


def create_account() -> Account:
    """A new account with a random password, readable only by the user."""
    alphabet = string.ascii_letters + string.digits
    # Upper, lower and digit so it passes Windows' complexity policy.
    password = "Wm1-" + "".join(secrets.choice(alphabet) for _ in range(24))
    account = Account(windows_user_name(os.environ.get("USER") or "wonder"), password)
    write_atomic(account_path(), json.dumps(asdict(account)) + "\n", mode=0o600)
    return account
