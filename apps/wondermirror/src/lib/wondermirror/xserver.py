"""A private X server for the Windows windows.

FreeRDP's RemoteApp client is an X11 program. On Wonderland, X11 programs
reach the compositor through xwayland-satellite, and the desktop's own
instance is shared by every X11 app: if the unusual windows Windows creates
ever upset it, they would all go down together. So Wonder Mirror runs its own
instance; whatever happens there stays there, and it is started afresh with
the next session.
"""

from __future__ import annotations

import os
import shutil
import subprocess
import time
from pathlib import Path

SOCKETS = Path("/tmp/.X11-unix")

# xwayland-satellite 0.8.3 stops with a protocol error when a popup hangs from
# a window that is one pixel wide on a display scaled by a fraction, and that
# is how Windows' shell starts. The fix came a day after that release, so a
# newer build installed for Wonder Mirror is preferred over the system's.
PRIVATE_BUILDS = (
    Path.home() / ".local/lib/wondermirror/xwayland-satellite",
    Path("/usr/local/lib/wondermirror/xwayland-satellite"),
    Path("/usr/lib/wondermirror/xwayland-satellite"),
)


class XServerError(RuntimeError):
    pass


def program() -> str | None:
    for path in PRIVATE_BUILDS:
        if os.access(path, os.X_OK):
            return str(path)
    return shutil.which("xwayland-satellite")


def available() -> bool:
    return program() is not None and bool(os.environ.get("WAYLAND_DISPLAY"))


def _free_display() -> int:
    for number in range(40, 100):
        if not (SOCKETS / f"X{number}").exists() and not Path(f"/tmp/.X{number}-lock").exists():
            return number
    raise XServerError("no free X display")


def start(log: Path, timeout: float = 10.0) -> tuple[subprocess.Popen[bytes], str]:
    """Start an instance and return it with its DISPLAY."""
    number = _free_display()
    log.parent.mkdir(parents=True, exist_ok=True)
    with open(log, "ab") as output:
        process = subprocess.Popen(
            [program() or "xwayland-satellite", f":{number}"],
            stdin=subprocess.DEVNULL, stdout=output, stderr=output, start_new_session=True,
        )
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise XServerError(f"xwayland-satellite stopped at once (see {log})")
        if (SOCKETS / f"X{number}").exists():
            return process, f":{number}"
        time.sleep(0.05)
    stop(process)
    raise XServerError("xwayland-satellite did not come up")


def stop(process: subprocess.Popen[bytes]) -> None:
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
