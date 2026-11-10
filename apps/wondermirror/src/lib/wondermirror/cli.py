"""Command line of Wonder Mirror."""

from __future__ import annotations

import argparse
import json
import shutil
import sys
import time
from dataclasses import asdict, fields
from pathlib import Path

from . import __version__, apps, config, keyboard, rdp, session, unattend, vm, wim

SELF = [sys.executable, str(Path(sys.argv[0]).resolve())] if sys.argv and sys.argv[0] else ["wondermirror"]


class Failure(Exception):
    pass


def guest_dir() -> Path:
    here = Path(__file__).resolve()
    for candidate in (here.parents[2] / "guest", here.parents[3] / "guest"):  # installed, checkout
        if (candidate / "agent.ps1").exists():
            return candidate
    raise Failure("the guest scripts are missing; reinstall Wonder Mirror")


def say(text: str) -> None:
    print(text, flush=True)


def check_host() -> None:
    missing = vm.missing_tools()
    try:
        rdp.client()
    except rdp.RdpError:
        missing.append("freerdp")
    if missing:
        raise Failure("missing packages: " + " ".join(sorted(set(missing))))
    problem = vm.kvm_problem()
    if problem:
        raise Failure(problem)


def check_image(image: Path) -> None:
    """Refuse early an image Setup could not install unattended."""
    if not image.is_file():
        raise Failure(f"{image}: no such file")
    bsdtar = shutil.which("bsdtar")
    if not bsdtar:
        return
    import subprocess
    import tempfile

    listing = subprocess.run([bsdtar, "-tf", str(image)], capture_output=True, text=True, check=False).stdout
    names = {line.strip().lower(): line.strip() for line in listing.splitlines()}
    member = names.get("sources/install.wim") or names.get("sources/install.esd")
    if not member:
        raise Failure(f"{image.name} is not a Windows installation image")
    # The catalogue sits at the end of a multi-gigabyte file; extract it to a
    # scratch file next to the machine and read just that.
    config.cache_dir().mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryFile(dir=config.cache_dir()) as scratch:
        subprocess.run([bsdtar, "-xOf", str(image), member], stdout=scratch, check=False)
        scratch.seek(0)
        try:
            names_found = [edition["name"] for edition in wim.editions(scratch)]
        except wim.WimError as error:
            raise Failure(f"{image.name}: {error}") from error
    if unattend.IMAGE_NAME not in names_found:
        raise Failure(
            f"{image.name} has no «{unattend.IMAGE_NAME}» edition (it has: {', '.join(names_found) or 'none'}). "
            "Running apps as windows needs Pro: Home cannot host Remote Desktop."
        )


def cmd_setup(args: argparse.Namespace) -> None:
    settings = config.load_settings()
    current = vm.state()
    if current not in ("absent", "installing"):
        raise Failure("Windows is already installed (wondermirror remove deletes it)")
    check_host()
    if current == "absent":
        if args.image is None:
            raise Failure("which Windows 11 image? wondermirror setup <file.iso>")
        say("Checking the image…")
        check_image(args.image)
        vm.create(args.image, settings, guest_dir())
    say("Installing Windows. It takes a while and needs no answers; this can be interrupted and resumed with «wondermirror setup».")
    session.write_status("installing")
    vm.start(settings)
    started = time.monotonic()
    while not vm.rdp_reachable(settings):
        if vm.pid() is None:
            # Windows Setup powers the machine off and on between its phases.
            vm.start(settings)
        if time.monotonic() - started > 3 * 3600:
            raise Failure("the installation is taking too long; look at it with «wondermirror screenshot <file.png>»")
        time.sleep(5)
    # Remote Desktop answers only after the first-logon script has enabled it.
    rdp.pin_certificate(settings.rdp_port)
    vm.finish_install()
    say("Windows is installed. Reading its apps…")
    cmd_refresh(args)


def cmd_refresh(args: argparse.Namespace) -> None:
    """Open a session with nothing to start: the agent writes the catalogue and leaves."""
    before = _catalogue_time()
    session.wake(config.load_settings())
    session.ensure(SELF)
    deadline = time.monotonic() + 240
    while time.monotonic() < deadline:
        if _catalogue_time() != before:
            written, removed = apps.sync()
            say(f"{len(apps.catalogue())} Windows apps ({written} updated, {removed} removed).")
            return
        if time.monotonic() > deadline - 220 and session.phase() != "running":
            break
        time.sleep(1)
    raise Failure(f"Windows did not report its apps (see {session.log_path()})")


def _catalogue_time() -> float:
    try:
        return apps.catalogue_path().stat().st_mtime
    except OSError:
        return 0.0


def cmd_open(args: argparse.Namespace) -> None:
    settings = config.load_settings()
    app = None
    if args.app:
        app = apps.find(args.app)
        if app is None:
            raise Failure(f"no Windows app called «{args.app}» (wondermirror apps lists them)")
    files = [session.windows_path(Path(name), settings) for name in args.files]
    if app is None and not files:
        raise Failure("nothing to open")
    if vm.state() in ("absent", "installing"):
        raise Failure("Windows is not installed yet (wondermirror setup)")
    note = session.request(app.id if app else None, files)
    # The agent deletes the note when it takes it. Until then keep a session
    # alive: one may be just about to leave.
    deadline = time.monotonic() + 420
    while note.exists():
        session.ensure(SELF)
        if time.monotonic() > deadline:
            note.unlink(missing_ok=True)
            raise Failure(f"Windows did not answer (see {session.log_path()})")
        time.sleep(0.3)


def cmd_apps(args: argparse.Namespace) -> None:
    found = apps.catalogue()
    if args.json:
        say(json.dumps([asdict(app) for app in found], ensure_ascii=False, indent=2))
        return
    for app in found:
        say(f"{app.key}  {app.name}")


def cmd_status(args: argparse.Namespace) -> None:
    settings = config.load_settings()
    current = vm.state()
    info = {
        "state": current,
        "session": session.is_active() and session.phase() == "running",
        "apps": len(apps.catalogue()),
        "cpus": settings.effective_cpus,
        "memory_mb": settings.effective_memory_mb,
    }
    if args.json:
        say(json.dumps(info))
        return
    words = {
        "absent": "Windows is not installed (wondermirror setup <file.iso>)",
        "installing": "Windows is being installed",
        "stopped": "Windows is off",
        "running": "Windows is running",
        "paused": "Windows is paused",
    }
    say(words[current] + (" and showing windows" if info["session"] and current == "running" else ""))
    if current not in ("absent", "installing"):
        say(f"{info['apps']} apps · {info['cpus']} CPUs · {info['memory_mb'] // 1024} GiB of memory")


def cmd_start(args: argparse.Namespace) -> None:
    session.wake(config.load_settings())
    session.write_status("running")


def cmd_stop(args: argparse.Namespace) -> None:
    vm.stop(force=args.force)
    session.write_status("stopped")


def cmd_pause(args: argparse.Namespace) -> None:
    vm.pause()
    session.write_status("paused")


def cmd_remove(args: argparse.Namespace) -> None:
    if vm.state() == "absent" and not config.account_path().exists():
        return
    if not args.yes:
        answer = input("Delete Windows and everything stored inside it? Type «yes»: ")
        if answer.strip().lower() not in ("yes", "sí", "si"):
            raise Failure("nothing was removed")
    vm.remove()
    rdp.fingerprint_path().unlink(missing_ok=True)
    apps.clear()
    session.write_status("absent")


def cmd_screenshot(args: argparse.Namespace) -> None:
    say(str(vm.screenshot(args.target.resolve())))


def cmd_desktop(args: argparse.Namespace) -> None:
    """The whole Windows desktop in one window, for what RemoteApp cannot show."""
    settings = config.load_settings()
    account = config.load_account()
    if account is None or not rdp.fingerprint_path().exists():
        raise Failure("Windows is not installed yet (wondermirror setup)")
    if session.is_active():
        raise Failure("close the Windows apps first: Windows keeps one session per user")
    session.wake(settings)
    arguments = [
        argument
        for argument in rdp.arguments(
            settings, account, rdp.fingerprint_path().read_text().strip(), "x", "",
            session.shares(settings, guest_dir()),
        )
        if not argument.startswith("/app:")
    ]
    arguments += ["/dynamic-resolution", "/size:1600x900", "/t:Windows"]
    raise SystemExit(rdp.spawn(arguments, session.log_path()).wait())


def cmd_config(args: argparse.Namespace) -> None:
    settings = config.load_settings()
    if args.key is None:
        for field in fields(settings):
            say(f"{field.name} = {json.dumps(getattr(settings, field.name))}")
        return
    known = {field.name for field in fields(settings)}
    if args.key not in known:
        raise Failure(f"unknown setting «{args.key}» (there are: {', '.join(sorted(known))})")
    current = getattr(settings, args.key)
    if args.value is None:
        say(json.dumps(current))
        return
    if isinstance(current, bool):
        if args.value.lower() not in ("true", "false", "on", "off", "yes", "no"):
            raise Failure(f"{args.key} is on or off")
        value: object = args.value.lower() in ("true", "on", "yes")
    elif isinstance(current, int):
        try:
            value = int(args.value)
        except ValueError:
            raise Failure(f"{args.key} is a number") from None
    else:
        value = args.value
    setattr(settings, args.key, value)
    config.save_settings(settings)
    if args.key in ("cpus", "memory_mb", "rdp_port", "gpu", "gpu_card", "gpu_memory_mb") and vm.pid() is not None:
        say("It applies the next time Windows starts (wondermirror stop).")


def _keyboard_live(layout: keyboard.Layout) -> bool:
    """Switch the running Windows session to `layout`. False: there is none."""
    if not (session.is_active() and session.phase() == "running" and vm.state() == "running"):
        return False  # not worth waking Windows for: every new session starts with the layout
    session.keyboard_path().unlink(missing_ok=True)
    note = session.request(None, [], layout=layout.klid)
    deadline = time.monotonic() + 20
    while note.exists() or not session.keyboard_path().exists():
        if time.monotonic() > deadline:
            # Left there, the note would hold the session open for nothing.
            note.unlink(missing_ok=True)
            raise Failure(
                f"the setting is saved, but Windows did not switch to {layout.name} now "
                f"(it will with the next app opened; see {session.log_path()})"
            )
        time.sleep(0.2)
    try:
        answer = json.loads(session.keyboard_path().read_text(encoding="utf-8-sig"))
    except (OSError, ValueError):
        answer = {}
    if answer.get("error"):
        raise Failure(f"Windows: {answer['error']}")
    return True


def cmd_keyboard(args: argparse.Namespace) -> None:
    """Which layout the Windows apps type with: show, list, pick or change it."""
    settings = config.load_settings()
    following = settings.keyboard == keyboard.FOLLOW
    current = keyboard.effective(settings.keyboard)
    if args.pick:
        raise SystemExit(keyboard.pick(settings.keyboard, SELF))
    if args.layout == "list":
        if args.json:
            say(json.dumps([asdict(layout) for layout in keyboard.LAYOUTS], ensure_ascii=False, indent=2))
            return
        for layout in keyboard.LAYOUTS:
            say(f"{'*' if layout.key == current.key else ' '} {layout.key:<10} {layout.name}")
        return
    if args.layout is None:
        if args.json:
            say(json.dumps({"keyboard": settings.keyboard, "layout": asdict(current), "host": asdict(keyboard.host())}))
        else:
            say(f"{current.key}  {current.name}" + (" (the keyboard of this computer)" if following else ""))
        return
    if args.layout.lower() in ("auto", "host"):
        settings.keyboard = keyboard.FOLLOW
    else:
        chosen = keyboard.find(args.layout)
        if chosen is None:
            raise Failure(f"no keyboard layout called «{args.layout}» (wondermirror keyboard list shows them)")
        settings.keyboard = chosen.key
    config.save_settings(settings)
    current = keyboard.effective(settings.keyboard)
    live = _keyboard_live(current)
    if args.json:
        say(json.dumps({"keyboard": settings.keyboard, "layout": asdict(current), "live": live}))
    elif live:
        say(f"Windows apps now type with the {current.name} keyboard.")
    else:
        say(f"Windows apps will type with the {current.name} keyboard.")


def cmd_supervise(args: argparse.Namespace) -> None:
    raise SystemExit(session.supervise(guest_dir()))


def parser() -> argparse.ArgumentParser:
    out = argparse.ArgumentParser(
        prog="wondermirror",
        description="Windows apps as windows of the Wonderland desktop.",
    )
    out.add_argument("--version", action="version", version=f"wondermirror {__version__}")
    sub = out.add_subparsers(dest="command", metavar="command")

    def add(name: str, function, help: str) -> argparse.ArgumentParser:
        command = sub.add_parser(name, help=help, description=help)
        command.set_defaults(function=function)
        return command

    p = add("setup", cmd_setup, "install Windows 11 from an ISO image, unattended")
    p.add_argument("image", type=Path, nargs="?")
    p = add("open", cmd_open, "open a Windows app, or files with it")
    p.add_argument("app", help="key, name or Windows id of the app; «-» opens the files with their default app")
    p.add_argument("files", nargs="*")
    p = add("apps", cmd_apps, "list the Windows apps")
    p.add_argument("--json", action="store_true")
    add("refresh", cmd_refresh, "read again which apps Windows has")
    p = add("status", cmd_status, "what Windows is doing")
    p.add_argument("--json", action="store_true")
    add("start", cmd_start, "start or resume Windows in the background")
    add("pause", cmd_pause, "freeze Windows where it is")
    p = add("stop", cmd_stop, "shut Windows down")
    p.add_argument("--force", action="store_true", help="pull the plug")
    add("desktop", cmd_desktop, "show the whole Windows desktop in a window")
    p = add("config", cmd_config, "show or change a setting")
    p.add_argument("key", nargs="?")
    p.add_argument("value", nargs="?")
    p = add("keyboard", cmd_keyboard, "show or change the keyboard layout Windows apps type with")
    p.add_argument("layout", nargs="?", help="a layout (es, us, latam…), «auto» for this computer's, or «list»")
    p.add_argument("--pick", action="store_true", help="choose it in a window")
    p.add_argument("--json", action="store_true")
    p = add("screenshot", cmd_screenshot, "save what the machine's own screen shows")
    p.add_argument("target", type=Path)
    p = add("remove", cmd_remove, "delete Windows and everything in it")
    p.add_argument("--yes", action="store_true")
    command = sub.add_parser("supervise")  # internal
    command.set_defaults(function=cmd_supervise)
    return out


def main(argv: list[str] | None = None) -> int:
    arguments = parser().parse_args(argv)
    if not getattr(arguments, "function", None):
        arguments = parser().parse_args(["status"])
    if getattr(arguments, "app", None) == "-":
        arguments.app = None
    try:
        arguments.function(arguments)
    except (Failure, vm.VmError, rdp.RdpError, session.SessionError, config.SettingsError, unattend.UnattendError) as error:
        print(f"wondermirror: {error}", file=sys.stderr)
        return 1
    except KeyboardInterrupt:
        return 130
    return 0
