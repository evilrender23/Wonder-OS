"""The live link between the desktop and Windows.

`request()` drops a note for the guest agent; `ensure()` makes sure a
supervisor is running. The supervisor owns the FreeRDP process, keeps the app
launchers in step with what Windows has installed, and pauses the machine when
it has been left without windows for a while.
"""

from __future__ import annotations

import fcntl
import json
import os
import signal
import subprocess
import sys
import time
import uuid
from pathlib import Path, PureWindowsPath

from . import apps, config, keyboard, rdp, vm, xserver

AGENT = (
    "conhost.exe",
    "--headless powershell.exe -NoProfile -ExecutionPolicy Bypass -File \\\\tsclient\\wondermirror\\agent.ps1",
)


class SessionError(RuntimeError):
    pass


def requests_dir() -> Path:
    return config.state_dir() / "requests"


def lock_path() -> Path:
    return config.runtime_dir() / "session.lock"


def log_path() -> Path:
    return config.state_dir() / "session.log"


def status_path() -> Path:
    return config.state_dir() / "status.json"


def write_status(phase: str, **extra: object) -> None:
    """For the shell: what Wonder Mirror is doing right now."""
    config.write_atomic(status_path(), json.dumps({"phase": phase, "time": int(time.time()), **extra}) + "\n")


def phase() -> str:
    try:
        return str(json.loads(status_path().read_text()).get("phase", ""))
    except (OSError, ValueError, AttributeError):
        return ""


def windows_path(path: Path, settings: config.Settings) -> str:
    """How Windows sees a file of this computer."""
    resolved = path.expanduser().resolve()
    if not settings.share_home:
        raise SessionError("the home folder is not shared with Windows (setting «share_home»)")
    try:
        relative = resolved.relative_to(Path.home().resolve())
    except ValueError:
        raise SessionError(f"{path}: only files inside your home folder are shared with Windows") from None
    return str(PureWindowsPath("\\\\tsclient\\home", *relative.parts))


def request(app_id: str | None, files: list[str], layout: str = "") -> Path:
    """Ask the agent to start an app or files or, with `layout`, to make
    Windows type with that keyboard layout (a Windows layout identifier)."""
    directory = requests_dir()
    directory.mkdir(parents=True, exist_ok=True)
    path = directory / f"{uuid.uuid4().hex}.json"
    note: dict[str, object] = {"id": app_id or "", "files": files}
    if layout:
        note["keyboard"] = layout
    config.write_atomic(path, json.dumps(note))
    return path


def keyboard_path() -> Path:
    """Where the agent says which layout Windows is typing with."""
    return config.state_dir() / "keyboard.json"


def is_active() -> bool:
    """Whether a supervisor is alive (it holds the lock for its whole life)."""
    try:
        with open(lock_path(), "a") as handle:
            fcntl.flock(handle, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        return True
    except OSError:
        return False
    return False


def ensure(program: list[str]) -> None:
    """Start a supervisor in the background unless one is already running."""
    if is_active():
        return
    config.state_dir().mkdir(parents=True, exist_ok=True)
    with open(log_path(), "ab") as log:
        subprocess.Popen(
            [*program, "supervise"],
            stdin=subprocess.DEVNULL, stdout=log, stderr=log, start_new_session=True,
        )


def wait_for_windows(settings: config.Settings, timeout: float = 300.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if vm.rdp_reachable(settings):
            return
        if vm.pid() is None:
            raise SessionError("Windows stopped while starting")
        time.sleep(1)
    raise SessionError("Windows did not finish starting")


def wake(settings: config.Settings) -> None:
    """Whatever state the machine is in, leave it running and answering."""
    current = vm.state()
    if current in ("absent", "installing"):
        raise SessionError("Windows is not installed yet (wondermirror setup)")
    if current == "stopped":
        write_status("starting")
        vm.start(settings)
    elif current == "paused":
        vm.resume()
    wait_for_windows(settings)


def shares(settings: config.Settings, guest_dir: Path) -> dict[str, Path]:
    out = {"wondermirror": guest_dir, "wmstate": config.state_dir()}
    if settings.share_home:
        out["home"] = Path.home()
    return out


def _leave(number: int, frame: object) -> None:
    write_status("idle")
    raise SystemExit(0)


def supervise(guest_dir: Path) -> int:
    """Run the session until Windows has been idle for long enough."""
    config.runtime_dir().mkdir(parents=True, exist_ok=True)
    lock = open(lock_path(), "a")
    try:
        fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    except BlockingIOError:
        return 0  # another supervisor got there first
    settings = config.load_settings()
    account = config.load_account()
    if account is None:
        raise SessionError("the Windows account is missing; set Wonder Mirror up again")
    try:
        fingerprint = rdp.fingerprint_path().read_text().strip()
    except OSError:
        raise SessionError("Windows' certificate was never recorded; set Wonder Mirror up again") from None
    requests_dir().mkdir(parents=True, exist_ok=True)
    signal.signal(signal.SIGTERM, _leave)
    failures = 0
    while True:
        wake(settings)
        write_status("running")
        # The one setting that changes while a session lives; a connection
        # made again must not bring the old layout back.
        settings.keyboard = config.load_settings().keyboard
        agent = f"{AGENT[1]} -Keyboard {keyboard.effective(settings.keyboard).klid}"
        started = time.monotonic()
        server, display = xserver.start(log_path()) if xserver.available() else (None, None)
        process = None
        try:
            process = rdp.spawn(
                rdp.arguments(settings, account, fingerprint, AGENT[0], agent, shares(settings, guest_dir)),
                log_path(),
                display,
            )
            seen = 0.0
            while process.poll() is None:
                seen = _sync_if_changed(seen)
                if _ignored():
                    # Windows takes a request within a second. A client that
                    # stopped passing them on has hung: connect again, the
                    # apps stay open in Windows meanwhile.
                    print("wondermirror: the connection stopped answering; connecting again",
                          file=open(log_path(), "a"), flush=True)
                    break
                time.sleep(1)
            _sync_if_changed(seen)
        finally:
            # Also on the way out after a signal: neither may outlive us.
            if process is not None and process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:  # FreeRDP can hang on its way out
                    process.kill()
                    process.wait()
            if server is not None:
                xserver.stop(server)
        # A session that dies at once, again and again, is not going to work.
        failures = failures + 1 if time.monotonic() - started < 15 else 0
        if failures >= 3:
            write_status("error", message="the connection to Windows keeps failing")
            for stale in requests_dir().glob("*.json"):
                stale.unlink(missing_ok=True)
            return 1
        if _pending():
            continue
        write_status("idle")
        if _idle(settings):
            return 0


def _ignored(patience: float = 45.0) -> bool:
    now = time.time()
    for note in requests_dir().glob("*.json"):
        try:
            if now - note.stat().st_mtime > patience:
                return True
        except OSError:
            pass  # taken this instant
    return False


def _pending() -> bool:
    return any(requests_dir().glob("*.json"))


def _sync_if_changed(seen: float) -> float:
    try:
        modified = apps.catalogue_path().stat().st_mtime
    except OSError:
        return seen
    if modified != seen:
        apps.sync()
    return modified


def _idle(settings: config.Settings) -> bool:
    """Wait with no windows open. True: time to leave (the machine is paused).
    False: an app was asked for."""
    deadline = time.monotonic() + settings.idle_minutes * 60
    while settings.idle_minutes == 0 or time.monotonic() < deadline:
        if _pending():
            return False
        if vm.pid() is None:
            write_status("stopped")
            return True
        time.sleep(1)
    try:
        vm.pause()
        write_status("paused")
    except vm.VmError as error:
        print(f"wondermirror: {error}", file=sys.stderr)
    return True
