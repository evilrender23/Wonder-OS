#!/usr/bin/env python3
"""Screenshot a theme in a nested session: a few windows, several zoom levels.

    dev/bench/theme_shots.py THEME.toml OUTDIR [--driftwm PATH]

Shader paths in the theme may start with ~/.config/driftwm/themes/hermes/;
they are rewritten to the in-repo extras/themes/hermes/ copies. Uses the
compositor's own canvas screenshot (IPC), so nothing on the host screen is read.
"""
import argparse
import json
import os
import re
import socket
import subprocess
import tempfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
HERMETIC = """
autostart = []
[session]
suspend_on_close = false
restore_windows = false
restore_camera = false
restore_bookmarks = false
[xwayland]
enabled = false
"""


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
    ap.add_argument("theme", type=Path)
    ap.add_argument("out", type=Path)
    ap.add_argument("--driftwm", default=str(REPO / "target/release/driftwm"))
    ap.add_argument("--client", default=str(REPO / "dev/bench/client/target/release/drift-bench-client"))
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)

    theme = args.theme.read_text().replace(
        "~/.config/driftwm/themes/hermes/", str(REPO / "extras/themes/hermes") + "/")
    # Top-level keys must precede the first table, so prepend the hermetic ones
    # and give the bench client a server title bar to show the header styling.
    cfg = HERMETIC.split("[session]")[0] + theme + "\n[session]" + HERMETIC.split("[session]")[1]
    cfg += '\n[[window_rules]]\napp_id = "drift-bench"\ndecoration = "server"\n'
    cfg_path = args.out / "config.toml"
    cfg_path.write_text(cfg)

    env = dict(os.environ, XDG_STATE_HOME=tempfile.mkdtemp(prefix="theme-state-"),
               RUST_LOG="warn,driftwm=info")
    log_path = args.out / "compositor.log"
    procs = []
    comp = subprocess.Popen([args.driftwm, "--backend", "winit", "--config", str(cfg_path)],
                            env=env, stdout=open(log_path, "w"), stderr=subprocess.STDOUT)
    procs.append(comp)
    try:
        sock = None
        for _ in range(200):
            time.sleep(0.1)
            m = re.search(r"Listening on WAYLAND_DISPLAY=(\S+)", log_path.read_text())
            if m:
                sock = m.group(1)
                break
        assert sock, "compositor did not start"
        ipc_path = str(Path(env["XDG_RUNTIME_DIR"]) / "driftwm" / f"ipc-{sock}.sock")
        time.sleep(1.0)
        cenv = dict(env, WAYLAND_DISPLAY=sock)
        procs.append(subprocess.Popen([args.client, "5", "static", "560x340"], env=cenv,
                                      stderr=subprocess.DEVNULL))
        if subprocess.run(["which", "alacritty"], capture_output=True).returncode == 0:
            procs.append(subprocess.Popen(["alacritty", "-e", "sh", "-c", "echo WonderlandWM; sleep 60"],
                                          env=cenv, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        time.sleep(4.0)
        shots = []
        for name, zoom in (("z100", 1.0), ("z050", 0.5), ("z020", 0.2), ("z006", 0.06)):
            ipc(ipc_path, {"Zoom": zoom})
            time.sleep(0.8)
            path = str((args.out / f"{name}.png").resolve())
            # scale = zoom renders at on-screen resolution, i.e. what the user sees
            r = ipc(ipc_path, {"Screenshot": {"target": "Viewport", "scale": zoom, "path": path}})
            shots.append((name, r))
        ipc(ipc_path, {"Zoom": 1.0})
        ipc(ipc_path, {"Camera": [0, 0]})
        time.sleep(0.8)
        path = str((args.out / "origin.png").resolve())
        shots.append(("origin", ipc(ipc_path, {"Screenshot": {"target": "Viewport", "scale": 1.0, "path": path}})))
        state = ipc(ipc_path, "State")
        print(json.dumps({"shots": shots, "windows": [
            (w.get("app_id"), w.get("title")) for w in state["Ok"]["State"]["windows"]]}, indent=1))
    finally:
        for p in reversed(procs):
            if p.poll() is None:
                p.terminate()
                try:
                    p.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    p.kill()
    errors = [l for l in log_path.read_text().splitlines() if "shader" in l.lower() or "ERROR driftwm" in l]
    print("log errors:", errors[:10])


if __name__ == "__main__":
    main()
