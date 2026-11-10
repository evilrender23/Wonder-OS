"""The Remote Desktop side: one FreeRDP process shows every Windows window.

Windows keeps a single session per user, so all apps share one connection. Its
program is the guest agent, which starts the apps asked for through the shared
state folder. FreeRDP's RemoteApp mode makes each Windows window a window of
the Wonderland desktop.
"""

from __future__ import annotations

import hashlib
import os
import shutil
import socket
import ssl
import subprocess
from pathlib import Path

from . import config, keyboard

CLIENTS = ("xfreerdp3", "xfreerdp")
WM_CLASS = "wondermirror-windows"
HOST = "127.0.0.1"

# X.224 connection request offering TLS and NLA.
_NEGOTIATE = bytes.fromhex("030000130ee000000000000100080003000000")


class RdpError(RuntimeError):
    pass


# A right click in a Windows window did not show its menu with stock FreeRDP
# 3.32, for three reasons patches/freerdp-remoteapp-menus.patch fixes:
#   - it ignores the size Windows gives a window within a second of the
#     previous one. Menus are created empty and sized right after, so they
#     stayed one pixel wide: nothing appeared.
#   - it never tells Windows which window got the focus here. All of them sit
#     at the same place of the Windows desktop, so the click went to whichever
#     Windows had on top, and the menu belonged to (and opened over) that one.
#   - Windows takes a few hundred ms to open the menu: a compositor that draws
#     a menu of its own on right clicks the app leaves unanswered drew it too.
# The client built with that patch by patches/build-freerdp.sh (`make
# install-freerdp`) is preferred over the system's. It runs on the system's
# FreeRDP libraries, so it is only used while their version matches: build it
# again after a FreeRDP update.
PRIVATE_BUILDS = (
    Path.home() / ".local/lib/wondermirror/xfreerdp3",
    Path("/usr/local/lib/wondermirror/xfreerdp3"),
    Path("/usr/lib/wondermirror/xfreerdp3"),
)


def _version(program: str) -> str:
    try:
        return subprocess.run(
            [program, "--version"], capture_output=True, text=True, timeout=10
        ).stdout.strip()
    except (OSError, subprocess.SubprocessError):
        return ""


def client() -> str:
    system = next((path for path in map(shutil.which, CLIENTS) if path), None)
    for build in PRIVATE_BUILDS:
        if os.access(build, os.X_OK):
            version = _version(str(build))
            if version and (system is None or version == _version(system)):
                return str(build)
    for name in CLIENTS:
        path = shutil.which(name)
        if path:
            return path
    raise RdpError("FreeRDP is missing (package «freerdp»)")


def fingerprint_path() -> Path:
    return config.data_dir() / "certificate.sha256"


def read_fingerprint(port: int, timeout: float = 5.0) -> str:
    """SHA-256 of the certificate Windows presents on its Remote Desktop port."""
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    context.check_hostname = False
    context.verify_mode = ssl.CERT_NONE  # self-signed: we pin it instead
    try:
        with socket.create_connection((HOST, port), timeout=timeout) as plain:
            plain.sendall(_NEGOTIATE)
            answer = plain.recv(19)
            if len(answer) < 12 or answer[11] != 0x02:
                raise RdpError("Windows refused to negotiate a secure connection")
            with context.wrap_socket(plain) as secure:
                certificate = secure.getpeercert(binary_form=True)
    except (OSError, ssl.SSLError) as error:
        raise RdpError(f"cannot read Windows' certificate: {error}") from error
    if not certificate:
        raise RdpError("Windows presented no certificate")
    return hashlib.sha256(certificate).hexdigest()


def pin_certificate(port: int) -> str:
    """Remember the certificate of the machine we have just installed.

    The connection never leaves this computer (QEMU forwards a loopback port
    to the guest), and from now on only this certificate is accepted.
    """
    value = read_fingerprint(port)
    config.write_atomic(fingerprint_path(), value + "\n", mode=0o600)
    return value


def arguments(
    settings: config.Settings,
    account: config.Account,
    fingerprint: str,
    program: str,
    command: str,
    shares: dict[str, Path],
) -> list[str]:
    """FreeRDP's command line. It carries the password: pass it through a pipe."""
    # Commas separate the fields of /app.
    if "," in program or "," in command:
        raise RdpError("a program or its arguments cannot contain a comma")
    app = f"/app:program:{program},name:Wonder Mirror"
    if command:
        app += f",cmd:{command}"
    out = [
        f"/v:{HOST}:{settings.rdp_port}",
        f"/u:{account.user}",
        f"/p:{account.password}",
        "/sec:nla",
        "/auth-pkg-list:ntlm,!kerberos",  # a local account: there is no domain to ask
        f"/cert:fingerprint:sha256:{fingerprint}",
        app,
        f"/wm-class:{WM_CLASS}",
        "/network:lan",
        # Not the graphics pipeline: FreeRDP draws it from a second thread, and
        # that one and the thread that follows the windows end up waiting for
        # each other for ever (seen with 3.32) once a few apps are open.
        "-gfx",
        "+fonts",
        "+aero",
        "-grab-keyboard",  # Super and the desktop's shortcuts stay with Wonderland
        # Else FreeRDP announces the layout of the X server it runs in, and
        # Windows types with that one whatever keyboard the user has.
        f"/kbd:layout:0x{keyboard.effective(settings.keyboard).klid}",
        "+auto-reconnect",
        "/auto-reconnect-max-retries:5",
        f"/scale:{settings.scale}",
        "/log-level:DEBUG" if os.environ.get("WONDERMIRROR_DEBUG") else "/log-level:WARN",
        "+clipboard" if settings.clipboard else "-clipboard",
    ]
    if settings.sound:
        out += ["/sound", "/microphone"]
    for name, path in shares.items():
        if "," in str(path):
            raise RdpError(f"{path}: a shared folder's path cannot contain a comma")
        out.append(f"/drive:{name},{path}")
    return out


def spawn(args: list[str], log: Path, display: str | None = None) -> subprocess.Popen[bytes]:
    """Start FreeRDP with `args` read from a pipe, so that the password never
    shows in the process list."""
    read_end, write_end = os.pipe()
    payload = ("\n".join(args) + "\n").encode()
    log.parent.mkdir(parents=True, exist_ok=True)
    environment = dict(os.environ)
    if display:
        environment["DISPLAY"] = display
    try:
        with open(log, "ab") as output:
            process = subprocess.Popen(
                [client(), f"/args-from:fd:{read_end}"],
                pass_fds=(read_end,),
                stdin=subprocess.DEVNULL,
                stdout=output,
                stderr=output,
                env=environment,
                start_new_session=True,
            )
    finally:
        os.close(read_end)
    with os.fdopen(write_end, "wb") as pipe:
        pipe.write(payload)
    return process
