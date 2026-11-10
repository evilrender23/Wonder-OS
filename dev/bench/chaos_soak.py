#!/usr/bin/env python3
"""Chaos soak: a nested WonderlandWM under random abuse for hours.

    WAYLAND_DISPLAY=wayland-1 dev/bench/chaos_soak.py --hours 8 --out DIR [--gpu amd]

Chaos, every 20–200 ms, weighted at random:
  * clients: spawn drift-bench-client with 1–8 windows (static or animated),
    SIGKILL one (the crash path), SIGTERM one;
  * IPC: move, resize, focus, close, suspend windows; jump/zoom the camera;
    run actions (zoom-to-fit, fit-window, fullscreen, cycle-windows,
    bookmarks, …);
  * keyboard: chords typed through zwp_virtual_keyboard_v1 (drift-vkbd), so
    the compositor's own keybinding path runs (focus-nearest, zoom, close…).

Every --quiesce-min minutes it reaches the same rest state (no clients,
camera home, zoom 1, 20 s settle) and samples RSS, DRM memory (VRAM/GTT),
open fds and the compositor's debug counters. Leak verdict: the median of the
last three rest samples against the median of the first hour's (the first
one is warm-up and skipped). Growth above --max-growth (5 %) fails, as does
any compositor exit before the end.

Writes DIR/samples.jsonl (every 30 s), DIR/rest.jsonl, DIR/compositor.log,
DIR/summary.json; exit status 0 = pass.
"""
import argparse
import json
import os
import random
import re
import signal
import socket
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
CLIENT_DIR = REPO / "dev/bench/client/target/release"

ACTIONS = [
    "zoom-in", "zoom-out", "zoom-reset", "zoom-to-fit", "zoom-to-fit-snapped",
    "home-toggle", "center-window", "center-nearest up", "focus-nearest left",
    "focus-center", "cycle-windows forward", "cycle-windows backward",
    "fit-window", "fit-window-snapped", "fill-window", "resize-to-snap",
    "shrink-window", "toggle-fullscreen", "close-window", "set-bookmark a",
    "set-bookmark b", "go-to-bookmark a", "go-to-bookmark b", "move-to-bookmark a",
    "pan-viewport 300 -200", "switch-layout next",
]
CHORDS = [
    "super+Left", "super+Right", "super+Up", "super+Down", "super+equal",
    "super+minus", "super+0", "super+w", "super+c", "super+f", "super+q",
    "super+shift+Left", "super+shift+Right", "alt+Tab", "super+Tab", "Escape",
]


class Ipc:
    def __init__(self, path):
        self.path = path
        self.f = None

    def req(self, obj):
        for attempt in range(2):
            try:
                if self.f is None:
                    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                    s.settimeout(10)
                    s.connect(self.path)
                    self.f = s.makefile("rw")
                self.f.write(json.dumps(obj) + "\n")
                self.f.flush()
                line = self.f.readline()
                if not line:
                    raise OSError("ipc closed")
                return json.loads(line)
            except (OSError, ValueError):
                self.f = None
                if attempt:
                    raise
        return None


def mem(pid):
    out = {}
    try:
        for line in open(f"/proc/{pid}/status"):
            if line.startswith(("VmRSS", "VmHWM")):
                k, v = line.split(":")
                out[k] = int(v.split()[0])  # kB
        out["fds"] = len(os.listdir(f"/proc/{pid}/fd"))
    except FileNotFoundError:
        return out
    drm, seen = {}, set()
    for fi in Path(f"/proc/{pid}/fdinfo").glob("*"):
        try:
            txt = fi.read_text()
        except OSError:
            continue
        m = re.search(r"drm-client-id:\s*(\d+)", txt)
        if not m or m.group(1) in seen:
            continue
        seen.add(m.group(1))
        for m in re.finditer(r"drm-(?:total|memory)-(\w+):\s*(\d+)\s*(\w+)", txt):
            mult = {"KiB": 1, "MiB": 1024, "GiB": 1048576}.get(m.group(3), 1 / 1024)
            drm[m.group(1)] = drm.get(m.group(1), 0) + int(m.group(2)) * mult
    out["drm_kb"] = {k: round(v) for k, v in drm.items()}
    return out


class Soak:
    def __init__(self, a):
        self.a = a
        self.rng = random.Random(a.seed)
        self.out = a.out
        self.clients = []
        self.windows = []
        self.ops = {}
        self.ipc_errors = 0
        self.stop = False
        self.t0 = time.time()

    # --- processes -------------------------------------------------------
    def start(self):
        env = dict(os.environ)
        self.state_home = tempfile.mkdtemp(prefix="chaos-soak-state-")
        env.update(XDG_STATE_HOME=self.state_home, RUST_LOG="warn,driftwm=info",
                   RUST_BACKTRACE="1")
        if self.a.gpu == "amd":
            env["__EGL_VENDOR_LIBRARY_FILENAMES"] = "/usr/share/glvnd/egl_vendor.d/50_mesa.json"
            env["DRI_PRIME"] = self.a.dri_prime
        if self.a.ld_path:
            env["LD_LIBRARY_PATH"] = self.a.ld_path
        self.log_path = self.out / "compositor.log"
        self.comp = subprocess.Popen(
            [self.a.driftwm, "--backend", "winit", "--config", str(self.a.config)],
            env=env, stdout=open(self.log_path, "w"), stderr=subprocess.STDOUT)
        sock = None
        for _ in range(300):
            time.sleep(0.1)
            m = re.search(r"Listening on WAYLAND_DISPLAY=(\S+)", self.log_path.read_text())
            if m:
                sock = m.group(1)
                break
        if not sock:
            raise RuntimeError("compositor socket never appeared")
        ipc_path = Path(env["XDG_RUNTIME_DIR"]) / "driftwm" / f"ipc-{sock}.sock"
        for _ in range(50):
            if ipc_path.exists():
                break
            time.sleep(0.1)
        time.sleep(1.0)
        self.ipc = Ipc(str(ipc_path))
        self.cenv = dict(env, WAYLAND_DISPLAY=sock)
        self.cenv.pop("DRI_PRIME", None)
        self.vkbd = None
        self.restart_vkbd()

    def restart_vkbd(self):
        if self.vkbd and self.vkbd.poll() is None:
            return
        self.vkbd = subprocess.Popen([str(CLIENT_DIR / "drift-vkbd")], env=self.cenv,
                                     stdin=subprocess.PIPE, stdout=subprocess.DEVNULL,
                                     stderr=subprocess.DEVNULL, text=True)

    def reap_clients(self):
        self.clients = [c for c in self.clients if c.poll() is None]

    def kill_all_clients(self):
        for c in self.clients:
            if c.poll() is None:
                c.kill()
        for c in self.clients:
            try:
                c.wait(timeout=5)
            except subprocess.TimeoutExpired:
                pass
        self.clients = []

    # --- chaos -----------------------------------------------------------
    def refresh_windows(self):
        r = self.ipc.req("State")
        if r and "Ok" in r:
            st = r["Ok"]["State"]
            self.windows = [w for w in st.get("windows", []) if not w.get("is_widget")]
            self.camera = st.get("camera", [0, 0])
            return st
        return None

    def pick(self):
        return self.rng.choice(self.windows)["id"] if self.windows else self.rng.randrange(0, 50)

    def ipc_op(self, obj):
        r = self.ipc.req(obj)
        if r is not None and "Err" in r:
            self.ipc_errors += 1

    def chaos_step(self):
        r = self.rng
        n_windows = len(self.windows)
        cam = getattr(self, "camera", [0, 0])
        near = lambda: [int(cam[0] + r.uniform(-1500, 1500)), int(cam[1] + r.uniform(-1000, 1000))]
        ops = [
            ("spawn", 6 if n_windows < self.a.max_windows else 0),
            ("kill", 2), ("term", 2), ("close", 3), ("move", 10), ("resize", 6),
            ("focus", 6), ("suspend", 1), ("camera", 6), ("zoom", 5), ("action", 10),
            ("chord", 10), ("bookmark", 1),
        ]
        names, weights = zip(*ops)
        op = r.choices(names, weights)[0]
        self.ops[op] = self.ops.get(op, 0) + 1
        if op == "spawn":
            n = r.randint(1, 8)
            size = f"{r.randint(160, 900)}x{r.randint(120, 700)}"
            mode = r.choice(["static", "static", "animated"])
            self.clients.append(subprocess.Popen(
                [str(CLIENT_DIR / "drift-bench-client"), str(n), mode, size], env=self.cenv,
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL))
        elif op in ("kill", "term"):
            self.reap_clients()
            if self.clients:
                c = r.choice(self.clients)
                c.kill() if op == "kill" else c.terminate()
        elif op == "close":
            self.ipc_op({"Close": self.pick()})
        elif op == "move":
            self.ipc_op({"Move": {"window": self.pick(), "to": near()}})
        elif op == "resize":
            self.ipc_op({"Resize": {"window": self.pick(),
                                    "to": [r.randint(-50, 2500), r.randint(-50, 1800)]}})
        elif op == "focus":
            self.ipc_op({"Focus": self.pick()})
        elif op == "suspend":
            self.ipc_op({"Suspend": self.pick()})
        elif op == "camera":
            self.ipc_op({"Camera": [r.uniform(-20000, 20000), r.uniform(-20000, 20000)]})
        elif op == "zoom":
            self.ipc_op({"Zoom": r.choice([r.uniform(0.05, 1.0), 1.0, 0.5, 0.25])})
        elif op == "action":
            self.ipc_op({"Action": r.choice(ACTIONS)})
        elif op == "chord":
            self.restart_vkbd()
            try:
                assert self.vkbd is not None and self.vkbd.stdin is not None
                self.vkbd.stdin.write(r.choice(CHORDS) + "\n")
                self.vkbd.stdin.flush()
            except (BrokenPipeError, OSError):
                self.vkbd = None
                self.restart_vkbd()
        elif op == "bookmark":
            self.ipc_op({"Bookmark": {"name": r.choice(["a", "b", "c"]), "to": near()}})

    # --- sampling --------------------------------------------------------
    def counters(self):
        try:
            r = self.ipc.req("DebugCounters")
            return r["Ok"]["DebugCounters"] if r and "Ok" in r else None
        except (OSError, KeyError, TypeError):
            return None

    def sample(self, kind):
        s = {"t": round(time.time() - self.t0, 1), "kind": kind, "windows": len(self.windows),
             "clients": len(self.clients), **mem(self.comp.pid)}
        if kind == "rest":
            s["counters"] = self.counters()
        return s

    def rest(self):
        """Same state every time: no clients, camera home, zoom 1, settled."""
        self.kill_all_clients()
        deadline = time.time() + 30
        while time.time() < deadline:
            self.refresh_windows()
            # Suspended stand-ins aren't clients; close them too.
            for w in self.windows:
                self.ipc_op({"Close": w["id"]})
            if not self.windows:
                break
            time.sleep(0.5)
        self.ipc_op({"Zoom": 1.0})
        self.ipc_op({"Camera": [0.0, 0.0]})
        time.sleep(self.a.settle)
        samples = []
        for _ in range(3):
            samples.append(self.sample("rest"))
            time.sleep(2)
        s = samples[-1]
        s["VmRSS_med"] = statistics.median(x.get("VmRSS", 0) for x in samples)
        return s

    def alive(self):
        return self.comp.poll() is None

    def run(self):
        end = self.t0 + self.a.hours * 3600
        next_sample = time.time()
        next_rest = time.time() + self.a.quiesce_min * 60
        next_refresh = 0.0
        samples = open(self.out / "samples.jsonl", "a")
        rests = open(self.out / "rest.jsonl", "a")
        first_rest = self.rest()
        rests.write(json.dumps(first_rest) + "\n")
        rests.flush()
        while not self.stop and time.time() < end and self.alive():
            now = time.time()
            try:
                if now >= next_refresh:
                    self.refresh_windows()
                    self.reap_clients()
                    next_refresh = now + 1.0
                self.chaos_step()
                if now >= next_sample:
                    samples.write(json.dumps(self.sample("chaos")) + "\n")
                    samples.flush()
                    next_sample = now + 30
                if now >= next_rest:
                    s = self.rest()
                    rests.write(json.dumps(s) + "\n")
                    rests.flush()
                    print(f"[{s['t'] / 3600:5.2f} h] rest RSS {s['VmRSS_med'] / 1024:.1f} MiB "
                          f"fds {s.get('fds')} drm {s.get('drm_kb')}", flush=True)
                    next_rest = time.time() + self.a.quiesce_min * 60
            except (OSError, ValueError) as e:
                if not self.alive():
                    break
                print(f"ipc hiccup: {e}", flush=True)
                time.sleep(0.5)
            time.sleep(self.rng.uniform(0.02, 0.2))
        crashed = not self.alive()
        final = None if crashed else self.rest()
        if final:
            rests.write(json.dumps(final) + "\n")
        rests.close()
        samples.close()
        return crashed

    def shutdown(self):
        self.kill_all_clients()
        if self.vkbd and self.vkbd.poll() is None:
            self.vkbd.kill()
        if self.comp.poll() is None:
            self.comp.terminate()
            try:
                self.comp.wait(timeout=10)
            except subprocess.TimeoutExpired:
                self.comp.kill()


def verdict(out, crashed, exit_code, max_growth, soak):
    rests = [json.loads(l) for l in open(out / "rest.jsonl")]
    log = (out / "compositor.log").read_text(errors="replace")
    panics = [l for l in log.splitlines() if "panicked at" in l]
    res = {"crashed": crashed, "exit_code": exit_code, "panics": panics[:5],
           "hours": round((time.time() - soak.t0) / 3600, 2), "ops": soak.ops,
           "ipc_errors": soak.ipc_errors, "rest_samples": len(rests)}
    usable = rests[1:]  # the first rest is warm-up
    if len(usable) >= 2:
        hour1 = [r for r in usable if r["t"] <= 3600 + rests[0]["t"]] or usable[:1]
        base = statistics.median(r["VmRSS_med"] for r in hour1)
        last = statistics.median(r["VmRSS_med"] for r in usable[-3:])
        res["rss_base_mib"] = round(base / 1024, 1)
        res["rss_last_mib"] = round(last / 1024, 1)
        res["rss_growth_pct"] = round((last - base) / base * 100, 2)
        res["rss_hwm_mib"] = round(max(r.get("VmHWM", 0) for r in rests) / 1024, 1)
        for region in sorted({k for r in usable for k in r.get("drm_kb", {})}):
            b = statistics.median(r.get("drm_kb", {}).get(region, 0) for r in hour1)
            l = statistics.median(r.get("drm_kb", {}).get(region, 0) for r in usable[-3:])
            res[f"drm_{region}_base_mib"] = round(b / 1024, 1)
            res[f"drm_{region}_last_mib"] = round(l / 1024, 1)
            if b > 0:
                res[f"drm_{region}_growth_pct"] = round((l - b) / b * 100, 2)
        res["fds_base"] = hour1[0].get("fds")
        res["fds_last"] = usable[-1].get("fds")
        c0, c1 = usable[0].get("counters"), usable[-1].get("counters")
        if c0 and c1:
            res["counters_changed"] = {k: [c0.get(k), c1.get(k)] for k in sorted(set(c0) | set(c1))
                                       if c0.get(k) != c1.get(k)}
    leak = any(v > max_growth for k, v in res.items() if k.endswith("_growth_pct"))
    res["pass"] = not crashed and not panics and not leak and len(usable) >= 2
    (out / "summary.json").write_text(json.dumps(res, indent=1))
    return res


def main():
    ap = argparse.ArgumentParser(description=(__doc__ or "").split("\n")[0])
    ap.add_argument("--hours", type=float, default=8)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--gpu", choices=["default", "amd"], default="amd")
    ap.add_argument("--dri-prime", default="1")
    ap.add_argument("--seed", type=int, default=None)
    ap.add_argument("--max-windows", type=int, default=60)
    ap.add_argument("--quiesce-min", type=float, default=15)
    ap.add_argument("--settle", type=float, default=20)
    ap.add_argument("--max-growth", type=float, default=5.0)
    ap.add_argument("--config", type=Path, default=REPO / "dev/bench/bench.toml")
    ap.add_argument("--driftwm", default=str(REPO / "target/release/driftwm"))
    ap.add_argument("--ld-path", default=os.environ.get("BENCH_LD_LIBRARY_PATH", ""))
    a = ap.parse_args()
    a.seed = a.seed if a.seed is not None else random.randrange(1 << 30)
    a.out.mkdir(parents=True, exist_ok=True)
    (a.out / "args.json").write_text(json.dumps({k: str(v) for k, v in vars(a).items()}, indent=1))
    soak = Soak(a)
    signal.signal(signal.SIGTERM, lambda *_: setattr(soak, "stop", True))
    signal.signal(signal.SIGINT, lambda *_: setattr(soak, "stop", True))
    soak.start()
    print(f"chaos soak: {a.hours} h, seed {a.seed}, compositor pid {soak.comp.pid}", flush=True)
    crashed = True
    try:
        crashed = soak.run()
    finally:
        code = soak.comp.poll()
        soak.shutdown()
        res = verdict(a.out, crashed, code, a.max_growth, soak)
        print(json.dumps(res, indent=1))
    sys.exit(0 if res["pass"] else 1)


if __name__ == "__main__":
    main()
