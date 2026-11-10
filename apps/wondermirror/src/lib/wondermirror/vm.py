"""The Windows machine: a QEMU/KVM guest run as the user, with no daemon.

It has no window of its own. Remote Desktop is forwarded to a port on
127.0.0.1 and everything else (power, pause, screenshots) goes through QMP.
"""

from __future__ import annotations

import json
import os
import shutil
import signal
import socket
import subprocess
import time
from pathlib import Path

from . import config, gpu, unattend
from .qmp import Qmp, QmpError

# (code, variables template) of the UEFI firmware, by distribution.
FIRMWARE = (
    ("/usr/share/edk2/x64/OVMF_CODE.4m.fd", "/usr/share/edk2/x64/OVMF_VARS.4m.fd"),
    ("/usr/share/edk2-ovmf/x64/OVMF_CODE.4m.fd", "/usr/share/edk2-ovmf/x64/OVMF_VARS.4m.fd"),
    ("/usr/share/OVMF/OVMF_CODE_4M.fd", "/usr/share/OVMF/OVMF_VARS_4M.fd"),
    ("/usr/share/edk2/ovmf/OVMF_CODE.fd", "/usr/share/edk2/ovmf/OVMF_VARS.fd"),
    ("/usr/share/OVMF/OVMF_CODE.fd", "/usr/share/OVMF/OVMF_VARS.fd"),
)

# Hyper-V enlightenments: Windows runs noticeably better when it knows it is a guest.
HYPERV = "hv_relaxed,hv_vapic,hv_spinlocks=0x1fff,hv_time,hv_vpindex,hv_runtime,hv_synic,hv_stimer,hv_reset,hv_frequencies"

PROCESS_NAME = "wondermirror-vm"


class VmError(RuntimeError):
    pass


def disk_path() -> Path:
    return config.data_dir() / "windows.qcow2"


def vars_path() -> Path:
    return config.data_dir() / "firmware-vars.fd"


def tpm_dir() -> Path:
    return config.data_dir() / "tpm"


def install_path() -> Path:
    """Present while Windows is being installed; holds the image in use."""
    return config.data_dir() / "install.json"


def answers_path() -> Path:
    return config.data_dir() / "answers.iso"


def log_path() -> Path:
    return config.data_dir() / "machine.log"


def qmp_path() -> Path:
    return config.runtime_dir() / "qmp.sock"


def pid_path() -> Path:
    return config.runtime_dir() / "machine.pid"


def firmware() -> tuple[str, str]:
    for code, variables in FIRMWARE:
        if os.path.exists(code) and os.path.exists(variables):
            return code, variables
    raise VmError("no UEFI firmware found (package «edk2-ovmf»)")


def missing_tools() -> list[str]:
    """Programs Wonder Mirror cannot work without, as package names."""
    needs = {
        "qemu-system-x86_64": "qemu-desktop",
        "qemu-img": "qemu-img",
        "swtpm": "swtpm",
        "xorriso": "libisoburn",
    }
    missing = [package for tool, package in needs.items() if not shutil.which(tool)]
    try:
        firmware()
    except VmError:
        missing.append("edk2-ovmf")
    return missing


def kvm_problem() -> str | None:
    if not os.path.exists("/dev/kvm"):
        return "this computer has no KVM (enable virtualisation in the firmware setup)"
    if not os.access("/dev/kvm", os.R_OK | os.W_OK):
        return "no permission to use /dev/kvm (add your user to the «kvm» group)"
    return None


def pid() -> int | None:
    """The machine's process id, if it is alive."""
    try:
        value = int(pid_path().read_text().strip())
        name = Path(f"/proc/{value}/comm").read_text().strip()
    except (OSError, ValueError):
        return None
    # A recycled pid would not carry the machine's process name.
    return value if name == PROCESS_NAME[:15] else None


def is_installed() -> bool:
    return disk_path().exists() and not install_path().exists()


def state() -> str:
    """absent | installing | stopped | running | paused"""
    if not disk_path().exists():
        return "absent"
    if pid() is None:
        return "installing" if install_path().exists() else "stopped"
    if install_path().exists():
        return "installing"
    try:
        with Qmp(qmp_path(), timeout=3) as monitor:
            status = monitor.command("query-status")
    except QmpError:
        return "running"
    return "paused" if isinstance(status, dict) and status.get("status") == "paused" else "running"


def command_line(settings: config.Settings, media: list[Path]) -> list[str]:
    return _machine(settings, media)[0]


def _machine(settings: config.Settings, media: list[Path]) -> tuple[list[str], dict[str, str]]:
    """The command line, and what its environment needs besides ours."""
    code, _ = firmware()
    cpus = settings.effective_cpus
    arguments = [
        "qemu-system-x86_64",
        "-name", f"wondermirror,process={PROCESS_NAME}",
        "-machine", "q35,accel=kvm,hpet=off",
        "-cpu", f"host,{HYPERV}",
        "-smp", f"cpus={cpus},sockets=1,cores={cpus},threads=1",
        "-m", f"{settings.effective_memory_mb}M",
        "-drive", f"if=pflash,format=raw,readonly=on,file={code}",
        "-drive", f"if=pflash,format=raw,file={vars_path()}",
        "-chardev", f"socket,id=tpmchr,path={config.runtime_dir() / 'tpm.sock'}",
        "-tpmdev", "emulator,id=tpm0,chardev=tpmchr",
        "-device", "tpm-tis,tpmdev=tpm0",
        # NVMe and an Intel network card: Windows has both drivers built in.
        "-drive", f"if=none,id=system,format=qcow2,discard=unmap,detect-zeroes=unmap,file={disk_path()}",
        "-device", "nvme,drive=system,serial=wondermirror,bootindex=0",
        "-netdev", f"user,id=net0,hostname=wondermirror,hostfwd=tcp:127.0.0.1:{settings.rdp_port}-:3389",
        "-device", "e1000e,netdev=net0,mac=52:54:00:57:4d:01",
        "-device", "qemu-xhci",
        "-device", "usb-tablet",
        "-rtc", "base=localtime,driftfix=slew",
        "-global", "kvm-pit.lost_tick_policy=delay",
        "-qmp", f"unix:{qmp_path()},server=on,wait=off",
        "-pidfile", str(pid_path()),
        "-D", str(log_path()),
        "-daemonize",
    ]
    environment: dict[str, str] = {}
    if settings.gpu and not media:  # never while installing: Windows has no driver for it yet
        extra, environment = gpu.arguments(settings.effective_memory_mb, settings.gpu_memory_mb, settings.gpu_card)
        arguments += extra
    else:
        arguments += ["-vga", "std", "-display", "none"]
    for index, image in enumerate(media):
        arguments += [
            "-drive", f"if=none,id=cd{index},media=cdrom,readonly=on,format=raw,file={image}",
            "-device", f"ide-cd,bus=ide.{index},drive=cd{index},id=cdrom{index},bootindex={index + 1}",
        ]
    return arguments, environment


def create(image: Path, settings: config.Settings, guest_dir: Path) -> None:
    """Lay out a new machine that will install Windows from `image` on its first start."""
    if disk_path().exists():
        raise VmError("there is already a Windows machine; remove it first")
    if not image.is_file():
        raise VmError(f"{image}: no such image")
    _, variables = firmware()
    data = config.data_dir()
    data.mkdir(parents=True, exist_ok=True)
    data.chmod(0o700)
    account = config.load_account() or config.create_account()
    unattend.build_disc(answers_path(), account, settings.effective_locale, guest_dir)
    shutil.copyfile(variables, vars_path())
    tpm_dir().mkdir(exist_ok=True)
    _no_copy_on_write(data)
    result = subprocess.run(
        ["qemu-img", "create", "-q", "-f", "qcow2", "-o", "lazy_refcounts=on", str(disk_path()), f"{settings.disk_gb}G"],
        capture_output=True, text=True, check=False,
    )
    if result.returncode != 0:
        raise VmError(f"could not create the disk: {result.stderr.strip()}")
    config.write_atomic(install_path(), json.dumps({"image": str(image.resolve()), "started": int(time.time())}) + "\n")


def _no_copy_on_write(directory: Path) -> None:
    """On btrfs, a disk image rewritten in place must not be copy-on-write:
    it fragments without end. New files inherit the flag from the directory."""
    if shutil.which("chattr"):
        subprocess.run(["chattr", "+C", str(directory)], capture_output=True, check=False)


def _start_tpm() -> None:
    run = config.runtime_dir()
    sock = run / "tpm.sock"
    sock.unlink(missing_ok=True)
    result = subprocess.run(
        [
            "swtpm", "socket", "--tpm2", "--terminate", "--daemon",
            "--tpmstate", f"dir={tpm_dir()}",
            "--ctrl", f"type=unixio,path={sock}",
            "--pid", f"file={run / 'tpm.pid'}",
            "--log", f"file={config.data_dir() / 'tpm.log'},level=1",
        ],
        capture_output=True, text=True, check=False,
    )
    if result.returncode != 0:
        raise VmError(f"the virtual TPM did not start: {result.stderr.strip()}")
    for _ in range(50):
        if sock.exists():
            return
        time.sleep(0.1)
    raise VmError("the virtual TPM did not start")


def start(settings: config.Settings) -> bool:
    """Power the machine on. Returns False when it was already running."""
    if not disk_path().exists():
        raise VmError("Windows is not installed yet (wondermirror setup)")
    if pid() is not None:
        return False
    problem = kvm_problem()
    if problem:
        raise VmError(problem)
    run = config.runtime_dir()
    run.mkdir(parents=True, exist_ok=True)
    run.chmod(0o700)
    qmp_path().unlink(missing_ok=True)
    media: list[Path] = []
    installing = install_path().exists()
    if installing:
        media = [Path(json.loads(install_path().read_text())["image"]), answers_path()]
        for image in media:
            if not image.is_file():
                raise VmError(f"{image} is gone; remove the machine and set it up again")
    if settings.gpu and not installing:
        problem = gpu.problem(settings.gpu_card)
        if problem:
            raise VmError(f"the graphics card cannot be shared: {problem} (wondermirror config gpu=false)")
    _start_tpm()
    arguments, environment = _machine(settings, media)
    result = subprocess.run(
        arguments, capture_output=True, text=True, check=False, stdin=subprocess.DEVNULL,
        env={**os.environ, **environment},
    )
    if result.returncode != 0:
        raise VmError(f"the machine did not start: {result.stderr.strip()[-600:]}")
    if installing:
        _boot_from_disc()
    return True


def _boot_from_disc() -> None:
    """Answer the disc's «Press any key to boot from CD» while the disk is empty."""
    if disk_path().stat().st_size > 64 << 20:
        return  # Setup already copied Windows: this boot belongs to the disk.
    try:
        with Qmp(qmp_path()) as monitor:
            for _ in range(20):
                # An arrow key: unlike Enter, it does nothing if Setup is already up.
                monitor.command("send-key", keys=[{"type": "qcode", "data": "down"}])
                time.sleep(0.5)
    except QmpError:
        pass


def finish_install() -> None:
    """Windows answers: drop the discs and forget the answer file."""
    try:
        with Qmp(qmp_path()) as monitor:
            for device in ("cdrom0", "cdrom1"):
                try:
                    monitor.command("eject", id=device, force=True)
                except QmpError:
                    pass
    except QmpError:
        pass
    answers_path().unlink(missing_ok=True)
    install_path().unlink(missing_ok=True)


def monitor_command(name: str, **arguments: object) -> object:
    if pid() is None:
        raise VmError("Windows is not running")
    try:
        with Qmp(qmp_path()) as monitor:
            return monitor.command(name, **arguments)
    except QmpError as error:
        raise VmError(str(error)) from error


def pause() -> None:
    monitor_command("stop")


def resume() -> None:
    monitor_command("cont")


def screenshot(target: Path) -> Path:
    """What the machine's own screen shows (useful while it installs)."""
    target.parent.mkdir(parents=True, exist_ok=True)
    monitor_command("screendump", filename=str(target), format="png")
    return target


def stop(timeout: float = 90.0, force: bool = False) -> None:
    """Ask Windows to shut down; pull the plug if it does not within `timeout`."""
    process = pid()
    if process is None:
        return
    try:
        with Qmp(qmp_path()) as monitor:
            if force:
                monitor.command("quit")
            else:
                monitor.command("cont")
                monitor.command("system_powerdown")
    except QmpError:
        force = True
    deadline = time.monotonic() + (5 if force else timeout)
    while time.monotonic() < deadline:
        if pid() is None:
            return
        time.sleep(0.5)
    if not force:
        return stop(force=True)
    try:
        os.kill(process, signal.SIGKILL)
    except ProcessLookupError:
        pass


def remove() -> None:
    """Delete the machine and everything Windows had on it."""
    stop(force=True)
    for path in (disk_path(), vars_path(), answers_path(), install_path(), log_path(), config.data_dir() / "tpm.log"):
        path.unlink(missing_ok=True)
    shutil.rmtree(tpm_dir(), ignore_errors=True)
    config.account_path().unlink(missing_ok=True)


def rdp_reachable(settings: config.Settings, timeout: float = 2.0) -> bool:
    """Whether something inside the machine answers on the Remote Desktop port.

    QEMU accepts the connection itself, so a successful connect proves nothing:
    wait for the first byte of an RDP negotiation answer.
    """
    request = bytes.fromhex("030000130ee000000000000100080003000000")
    try:
        with socket.create_connection(("127.0.0.1", settings.rdp_port), timeout=timeout) as connection:
            connection.settimeout(timeout)
            connection.sendall(request)
            return connection.recv(4)[:1] == b"\x03"
    except OSError:
        return False
