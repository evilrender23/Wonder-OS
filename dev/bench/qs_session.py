#!/usr/bin/env python3
"""Run the extras Quickshell config inside a nested WonderlandWM and capture it.

    WAYLAND_DISPLAY=wayland-1 dev/bench/qs_session.py OUTDIR [--theme light|dark]
        [--windows N] [--qs-ipc "bar setTheme dark" ...] [--hold SECONDS]

Writes OUTDIR/qs.log (Quickshell's log, where QML errors land),
OUTDIR/compositor.log and OUTDIR/screen-*.png from the compositor's own
screenshot, which includes layer-shell surfaces such as the bar. State and
config are private temp dirs, so the real session is never touched.
"""
import argparse
import json
import os
import re
import shlex
import shutil
import socket
import subprocess
import tempfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
QS_DIR = REPO / "extras/quickshell"


def ipc(path, obj):
    s = socket.socket(socket.AF_UNIX)
    s.connect(path)
    f = s.makefile("rw")
    f.write(json.dumps(obj) + "\n")
    f.flush()
    reply = json.loads(f.readline())
    s.close()
    return reply


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("out", type=Path)
    ap.add_argument("--theme", default="light")
    ap.add_argument("--windows", type=int, default=3)
    ap.add_argument("--qs-ipc", action="append", default=[])
    ap.add_argument("--bookmarks", default="1:0:0,2:3000:0,3:0:-2500,dev:-3000:1200")
    ap.add_argument("--hold", type=float, default=0)
    ap.add_argument("--notify", action="append", default=[],
                    help="run qs on a private D-Bus and send 'summary|body|urgency' notifications")
    ap.add_argument("--bar-json", default="{}", help="initial bar.json state (JSON object)")
    ap.add_argument("--ai-sample", action="store_true", help="plant sample AI quota caches")
    ap.add_argument("--then", action="append", default=[],
                    help="after the first shot: 'qs:<target fn args>', then another shot")
    ap.add_argument("--driftwm", default=str(REPO / "target/release/driftwm"))
    ap.add_argument("--client", default=str(REPO / "dev/bench/client/target/release/drift-bench-client"))
    args = ap.parse_args()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)

    theme = (REPO / f"extras/themes/hermes/hermes-{args.theme}.toml").read_text().replace(
        "~/.config/driftwm/themes/hermes/", str(REPO / "extras/themes/hermes") + "/")
    cfg = ('autostart = []\n' + theme +
           '\n[session]\nsuspend_on_close = false\nrestore_windows = false\n'
           'restore_camera = false\nrestore_bookmarks = false\n[xwayland]\nenabled = false\n'
           '[[window_rules]]\napp_id = "drift-bench"\ndecoration = "server"\n')
    (out / "config.toml").write_text(cfg)

    tmp = Path(tempfile.mkdtemp(prefix="qs-session-"))
    env = dict(os.environ, XDG_STATE_HOME=str(tmp / "state"), XDG_CACHE_HOME=str(tmp / "cache"),
               RUST_LOG="warn,driftwm=info")
    (tmp / "state").mkdir()
    procs = []
    clog = out / "compositor.log"
    comp = subprocess.Popen([args.driftwm, "--backend", "winit", "--config", str(out / "config.toml")],
                            env=env, stdout=open(clog, "w"), stderr=subprocess.STDOUT)
    procs.append(comp)
    try:
        sock = None
        for _ in range(200):
            time.sleep(0.1)
            m = re.search(r"Listening on WAYLAND_DISPLAY=(\S+)", clog.read_text())
            if m:
                sock = m.group(1)
                break
        assert sock, "compositor did not start"
        ipc_path = str(Path(env["XDG_RUNTIME_DIR"]) / "driftwm" / f"ipc-{sock}.sock")
        time.sleep(1.0)
        for spec in filter(None, args.bookmarks.split(",")):
            name, x, y = spec.split(":")
            ipc(ipc_path, {"Bookmark": {"name": name, "to": [float(x), float(y)]}})
        cenv = dict(env, WAYLAND_DISPLAY=sock)
        if args.windows:
            procs.append(subprocess.Popen([args.client, str(args.windows), "static", "520x320"],
                                          env=cenv, stderr=subprocess.DEVNULL))
        if args.ai_sample:
            (tmp / "cache").mkdir(exist_ok=True)
            now = int(time.time())
            (tmp / "cache/claude-usage.json").write_text(json.dumps({
                "5h-utilization": "0.4200", "5h-reset": str(now + 7300), "7d-utilization": "0.7800",
                "7d-reset": str(now + 3 * 86400), "updated_at": "18:00:00", "_source": "oauth",
                "_today_tokens": 812000, "_rate_per_hour": 64000, "status": "allowed"}))
            (tmp / "cache/codex-usage.json").write_text(json.dumps({
                "5h-utilization": "0.93", "5h-reset": str(now + 1800), "7d-utilization": "0.31",
                "7d-reset": str(now + 5 * 86400), "updated_at": "18:00:00", "_plan": "plus"}))
        bar_state = json.loads(args.bar_json)
        if args.theme == "dark":
            bar_state["themeMode"] = "dark"
        if bar_state:
            (tmp / "state/wonderlandwm").mkdir(parents=True, exist_ok=True)
            (tmp / "state/wonderlandwm/bar.json").write_text(json.dumps(bar_state))
        qlog = out / "qs.log"
        qs_cmd = ["qs", "-p", str(QS_DIR)]
        if args.notify:
            sends = "; ".join("notify-send -a WonderlandWM -u {2} {0} {1}".format(
                *[shlex.quote(x) for x in (n.split("|") + ["normal"])[:3]]) for n in args.notify)
            qs_cmd = ["dbus-run-session", "--", "sh", "-c",
                      f"qs -p {shlex.quote(str(QS_DIR))} & sleep 3; {sends}; wait"]
        qs = subprocess.Popen(qs_cmd, env=cenv,
                              stdout=open(qlog, "w"), stderr=subprocess.STDOUT)
        procs.append(qs)
        time.sleep(4.0)
        for call in args.qs_ipc:
            subprocess.run(["qs", "-p", str(QS_DIR), "ipc", "call", *call.split()], env=cenv,
                           capture_output=True, timeout=10)
            time.sleep(0.6)
        ipc(ipc_path, {"Camera": [0, 0]})
        time.sleep(1.0)
        shot = REPO / "dev/bench/client/target/release/drift-shot"
        r = subprocess.run([str(shot), str(out / "screen-1.png"), "--flip"], env=cenv, capture_output=True, text=True)
        print("screenshot:", r.stdout.strip() or r.stderr.strip())
        for i, call in enumerate(args.then):
            kind, _, rest = call.partition(":")
            if kind == "qs":
                subprocess.run(["qs", "-p", str(QS_DIR), "ipc", "call", *rest.split()], env=cenv,
                               capture_output=True, timeout=10)
            elif kind == "click":
                # x,y in output pixels, through a virtual pointer is not available;
                # use the bar's own IPC to open panels instead.
                pass
            time.sleep(0.8)
            r = subprocess.run([str(shot), str(out / f"screen-{i + 2}.png"), "--flip"], env=cenv,
                               capture_output=True, text=True)
            print(f"screenshot {i + 2} after {call}:", r.stdout.strip() or r.stderr.strip())
        if args.hold:
            time.sleep(args.hold)
        print("qs alive:", qs.poll() is None)
    finally:
        for p in reversed(procs):
            if p.poll() is None:
                p.terminate()
                try:
                    p.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    p.kill()
        shutil.rmtree(tmp, ignore_errors=True)
    log = (out / "qs.log").read_text(errors="replace")
    issues = [l for l in log.splitlines() if re.search(r"WARN|ERROR|error|Error|failed|not a type|unavailable|undefined", l)]
    print(f"qs log: {len(log.splitlines())} lines, {len(issues)} issue lines")
    for l in issues[:60]:
        print("  ", l[:220])


if __name__ == "__main__":
    main()
