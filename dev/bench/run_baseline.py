#!/usr/bin/env python3
"""Phase-0 baseline: nested driftwm (winit) under Tracy with N bench windows.

For each window count and scenario it launches a fresh compositor, opens the
windows with dev/bench/client, drives the camera over IPC, records a Tracy
capture and reports per-frame CPU time (`winit::frame`, `compose_frame`),
RSS and GPU memory.

    dev/bench/run_baseline.py --counts 10 50 200 --out /tmp/baseline
    dev/bench/run_baseline.py --gpu amd ...   # Mesa/radeonsi via DRI_PRIME

Needs: a driftwm built with --features profile-with-tracy, tracy-capture and
tracy-csvexport (v0.13.x), a parent Wayland session.
"""

import argparse
import csv
import io
import json
import math
import os
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


def pct(vals, p):
    if not vals:
        return float("nan")
    s = sorted(vals)
    k = (len(s) - 1) * p / 100
    lo, hi = math.floor(k), math.ceil(k)
    return s[lo] + (s[hi] - s[lo]) * (k - lo)


class Ipc:
    def __init__(self, path):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.sock.connect(path)
        self.f = self.sock.makefile("rw")

    def req(self, obj):
        self.f.write(json.dumps(obj) + "\n")
        self.f.flush()
        return json.loads(self.f.readline())


def proc_mem(pid):
    out = {}
    try:
        for line in open(f"/proc/{pid}/status"):
            if line.startswith(("VmRSS", "VmHWM")):
                k, v = line.split(":")
                out[k] = int(v.split()[0]) // 1024  # MiB
    except FileNotFoundError:
        pass
    # DRM fdinfo (amdgpu/i915/xe expose per-client memory; nvidia does not).
    mem = {}
    seen = set()
    fdinfo = Path(f"/proc/{pid}/fdinfo")
    for fi in fdinfo.glob("*"):
        try:
            txt = fi.read_text()
        except OSError:
            continue
        m = re.search(r"drm-client-id:\s*(\d+)", txt)
        if not m:
            continue
        cid = m.group(1)
        if cid in seen:
            continue
        seen.add(cid)
        for m in re.finditer(r"drm-(?:total|memory)-(\w+):\s*(\d+)\s*(\w+)", txt):
            region, val, unit = m.group(1), int(m.group(2)), m.group(3)
            mult = {"KiB": 1 / 1024, "MiB": 1, "GiB": 1024}.get(unit, 1 / 1048576)
            mem[region] = mem.get(region, 0) + val * mult
    out["drm"] = {k: round(v, 1) for k, v in mem.items()}
    return out


def nvidia_mem(pid):
    try:
        q = subprocess.run(
            ["nvidia-smi", "-q", "-d", "PIDS"], capture_output=True, text=True, timeout=10
        ).stdout
    except (OSError, subprocess.TimeoutExpired):
        return None
    blocks = q.split("Process ID")
    for b in blocks[1:]:
        m = re.match(r"\s*:\s*(\d+)", b)
        if m and int(m.group(1)) == pid:
            u = re.search(r"Used GPU Memory\s*:\s*(\d+)\s*MiB", b)
            return int(u.group(1)) if u else None
    return None


def zone_durations(csvexport, trace, zone):
    out = subprocess.run(
        [csvexport, "-u", "-f", zone, "-c", trace], capture_output=True, text=True
    ).stdout
    rows = csv.DictReader(io.StringIO(out))
    return [
        (int(r["ns_since_start"]), int(r["exec_time_ns"]))
        for r in rows
        if r.get("name") == zone
    ]


def plot_values(csvexport, trace, plot):
    out = subprocess.run([csvexport, "-u", "-p", trace], capture_output=True, text=True).stdout
    rows = csv.DictReader(io.StringIO(out))
    return [(int(r["ns_since_start"]), float(r["value"])) for r in rows if r.get("name") == plot]


def gpu_zone_durations(csvexport, trace, zone):
    """GPU timer-query zones (smithay `tracy_gpu_profiling`): (start_ns, gpu_ns)."""
    out = subprocess.run([csvexport, "-g", trace], capture_output=True, text=True).stdout
    rows = csv.reader(io.StringIO(out))
    next(rows, None)
    return [(int(r[2]), int(r[3])) for r in rows if len(r) >= 4 and r[0] == zone]


LIVE = []  # processes to reap if a case dies half-way


def tracy_port_free(port=8086):
    """Tracy's client listens on 8086; an orphaned instance would steal the capture."""
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    # Without SO_REUSEADDR the previous case's TIME_WAIT reads as busy.
    s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    try:
        s.bind(("0.0.0.0", port))
        return True
    except OSError:
        return False
    finally:
        s.close()


def reap():
    for p in LIVE:
        if p.poll() is None:
            p.kill()
            p.wait()
    LIVE.clear()


def run_case(args, n, scenario, outdir):
    if not tracy_port_free():
        raise RuntimeError("port 8086 busy: another Tracy-enabled process is running")
    tag = f"{args.gpu}-n{n}-{scenario}"
    trace = outdir / f"{tag}.tracy"
    env = dict(os.environ)
    state_home = tempfile.mkdtemp(prefix="drift-bench-state-")
    env.update(
        XDG_STATE_HOME=state_home,
        RUST_LOG="warn,driftwm=info",
        TRACY_NO_INVARIANT_CHECK="1",
    )
    if args.gpu == "amd":
        env["__EGL_VENDOR_LIBRARY_FILENAMES"] = "/usr/share/glvnd/egl_vendor.d/50_mesa.json"
        env["DRI_PRIME"] = args.dri_prime
    if args.ld_path:
        env["LD_LIBRARY_PATH"] = args.ld_path
    log_path = outdir / f"{tag}.log"
    log = open(log_path, "w")
    comp = subprocess.Popen(
        [args.driftwm, "--backend", "winit", "--config", str(args.config)],
        env=env,
        stdout=log,
        stderr=subprocess.STDOUT,
    )
    LIVE.append(comp)
    # A stale socket file from a killed run gets reused under the same name,
    # so read the name the compositor announces instead of diffing the dir.
    sockname = None
    for _ in range(300):
        time.sleep(0.1)
        m = re.search(r"Listening on WAYLAND_DISPLAY=(\S+)", log_path.read_text())
        if m:
            sockname = m.group(1)
            break
    if not sockname:
        comp.kill()
        raise RuntimeError(f"{tag}: compositor socket never appeared (see log)")
    ipc_path = Path(env["XDG_RUNTIME_DIR"]) / "driftwm" / f"ipc-{sockname}.sock"
    for _ in range(50):
        if ipc_path.exists():
            break
        time.sleep(0.1)
    time.sleep(1.0)
    ipc = Ipc(str(ipc_path))
    idle_mem = proc_mem(comp.pid)
    idle_nv = nvidia_mem(comp.pid)

    cenv = dict(env, WAYLAND_DISPLAY=sockname)
    cenv.pop("DRI_PRIME", None)
    mode = "animated" if "animated" in scenario else "static"
    client = subprocess.Popen(
        [args.client, str(n), mode, args.win_size],
        env=cenv,
        stdout=subprocess.DEVNULL,
        stderr=open(outdir / f"{tag}.client.log", "w"),
    )
    LIVE.append(client)
    # Wait until every window is on the stage.
    deadline = time.time() + 60
    count = 0
    while time.time() < deadline:
        st = ipc.req("State")
        count = len(st.get("Ok", {}).get("State", {}).get("windows", []))
        if count >= n:
            break
        time.sleep(0.25)
    time.sleep(2.0)  # let open animations and placement settle
    ipc.req({"Zoom": 1.0})
    ipc.req({"Camera": [0, 0]})
    if scenario.startswith("overview"):
        ipc.req({"Action": "zoom-to-fit"})
        time.sleep(1.5)
    elif "@" in scenario:  # e.g. "pan@0.4": pan at a fixed zoom
        ipc.req({"Zoom": float(scenario.split("@", 1)[1])})
        time.sleep(1.0)
    z = ipc.req({"Zoom": None})["Ok"]["Zoom"]
    cam = ipc.req({"Camera": None})["Ok"]["Camera"]
    cx, cy = cam["x"], cam["y"]

    cap = subprocess.Popen(
        [args.capture, "-o", str(trace), "-f", "-s", str(args.seconds + 2)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    LIVE.append(cap)
    time.sleep(1.0)
    # Drive a continuous pan: a circle so the visible set keeps changing.
    radius = 600 / max(z, 0.05) if scenario.startswith("pan") else 80 / max(z, 0.05)
    t0 = time.time()
    while time.time() - t0 < args.seconds:
        t = time.time() - t0
        a = t * 2 * math.pi / 6.0
        ipc.req({"Camera": [cx + radius * math.cos(a), cy + radius * math.sin(a)]})
        time.sleep(1 / 120)
    loaded_mem = proc_mem(comp.pid)
    loaded_nv = nvidia_mem(comp.pid)
    counters = ipc.req("DebugCounters")
    cap.wait(timeout=60)
    client.terminate()
    comp.send_signal(signal.SIGTERM)
    try:
        comp.wait(timeout=10)
    except subprocess.TimeoutExpired:
        comp.kill()

    frames = zone_durations(args.csvexport, str(trace), "winit::frame")
    compose = zone_durations(args.csvexport, str(trace), "compose_frame")
    gpu = gpu_zone_durations(args.csvexport, str(trace), "render")
    visible = plot_values(args.csvexport, str(trace), "frame.visible_windows")
    # Drop the first second of the capture (connection warm-up).
    if frames:
        start = frames[0][0] + 1_000_000_000
        frames = [f for f in frames if f[0] >= start]
        compose = [c for c in compose if c[0] >= start]
        gpu = [g for g in gpu if g[0] >= start]
        visible = [v for v in visible if v[0] >= start]
    ft = [d / 1e6 for _, d in frames]
    ct = [d / 1e6 for _, d in compose]
    gt = [d / 1e6 for _, d in gpu]
    starts = [s for s, _ in frames]
    intervals = [(b - a) / 1e6 for a, b in zip(starts, starts[1:])]
    res = {
        "tag": tag,
        "gpu": args.gpu,
        "windows": n,
        "windows_seen": count,
        "scenario": scenario,
        "zoom": round(z, 3),
        "frames": len(ft),
        "visible_windows": {"mean": round(statistics.fmean([v for _, v in visible]), 1) if visible else None,
                            "max": max((v for _, v in visible), default=None)},
        "frame_ms": {"p50": pct(ft, 50), "p99": pct(ft, 99), "max": max(ft, default=0)},
        "compose_ms": {"p50": pct(ct, 50), "p99": pct(ct, 99), "max": max(ct, default=0)},
        "gpu_render_ms": {"p50": pct(gt, 50), "p99": pct(gt, 99), "max": max(gt, default=0), "n": len(gt)},
        "interval_ms": {"p50": pct(intervals, 50), "p99": pct(intervals, 99)},
        "mem_idle": idle_mem,
        "mem_loaded": loaded_mem,
        "nvidia_mib_idle": idle_nv,
        "nvidia_mib_loaded": loaded_nv,
        "debug_counters": counters.get("Ok"),
    }
    print(json.dumps({k: res[k] for k in ("tag", "frames", "frame_ms", "compose_ms", "gpu_render_ms", "mem_loaded", "nvidia_mib_loaded")}), flush=True)
    return res


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--counts", type=int, nargs="+", default=[10, 50, 200])
    ap.add_argument("--scenarios", nargs="+", default=["pan", "overview", "pan-animated"])
    ap.add_argument("--seconds", type=int, default=15)
    ap.add_argument("--gpu", choices=["default", "amd"], default="default")
    ap.add_argument("--dri-prime", default="1")
    ap.add_argument("--win-size", default="640x400")
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--config", type=Path, default=REPO / "dev/bench/bench.toml")
    ap.add_argument("--driftwm", default=str(REPO / "target-tracy/release/driftwm"))
    ap.add_argument("--client", default=str(REPO / "dev/bench/client/target/release/drift-bench-client"))
    ap.add_argument("--capture", default=os.environ.get("TRACY_CAPTURE", "tracy-capture"))
    ap.add_argument("--csvexport", default=os.environ.get("TRACY_CSVEXPORT", "tracy-csvexport"))
    ap.add_argument("--ld-path", default=os.environ.get("BENCH_LD_LIBRARY_PATH"))
    args = ap.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    results = []
    for n in args.counts:
        for sc in args.scenarios:
            try:
                results.append(run_case(args, n, sc, args.out))
            except Exception as e:  # keep going; record the failure
                print(f"FAILED {args.gpu} n={n} {sc}: {e}", file=sys.stderr, flush=True)
                results.append({"gpu": args.gpu, "windows": n, "scenario": sc, "error": str(e)})
            finally:
                reap()
            time.sleep(1)
    with open(args.out / f"results-{args.gpu}.json", "w") as f:
        json.dump(results, f, indent=1)


if __name__ == "__main__":
    main()
