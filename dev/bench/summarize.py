#!/usr/bin/env python3
"""Render run_baseline.py result JSONs as Markdown tables."""
import json
import sys
from pathlib import Path

GPU_NAME = {"amd": "Radeon 610M (iGPU, Mesa radeonsi)", "default": "RTX 4090 (NVIDIA propietario)"}


def f(x, d=2):
    return "—" if x is None or x != x else f"{x:.{d}f}"


def main(paths):
    rows = []
    for p in paths:
        rows += [r for r in json.load(open(p)) if "error" not in r]
    for gpu in ("amd", "default"):
        sel = [r for r in rows if r["gpu"] == gpu]
        if not sel:
            continue
        print(f"\n**{GPU_NAME[gpu]}**\n")
        print("| ventanas | escenario | zoom | visibles (media/máx) | CPU frame p50 / p99 / máx (ms) | compose p50 / p99 (ms) | GPU render p50 / p99 (ms) | intervalo p50 / p99 (ms) | RSS reposo → carga (MiB) | VRAM / GTT (MiB) |")
        print("|---:|---|---:|---:|---|---|---|---|---|---|")
        order = {"pan": 0, "pan@0.4": 1, "overview": 2, "pan-animated": 3, "pan-animated@0.4": 4}
        for r in sorted(sel, key=lambda r: (r["windows"], order.get(r["scenario"], 9))):
            fm, cm, gm, im = r["frame_ms"], r["compose_ms"], r["gpu_render_ms"], r["interval_ms"]
            vis = r.get("visible_windows") or {}
            drm = r["mem_loaded"].get("drm") or {}
            vram = f"{drm.get('vram', 0):.0f} / {drm.get('gtt', 0):.0f}" if drm else (
                f"{r['nvidia_mib_loaded']} (nvidia-smi)" if r.get("nvidia_mib_loaded") else "—")
            vis_s = f"{vis['mean']} / {vis['max']:.0f}" if vis.get("mean") is not None else "—"
            print(
                f"| {r['windows']} | {r['scenario']} | {r['zoom']:.3f} | {vis_s} | "
                f"{f(fm['p50'])} / {f(fm['p99'])} / {f(fm['max'], 1)} | {f(cm['p50'], 3)} / {f(cm['p99'])} | "
                f"{f(gm['p50'])} / {f(gm['p99'])} | {f(im['p50'], 1)} / {f(im['p99'], 1)} | "
                f"{r['mem_idle'].get('VmRSS')} → {r['mem_loaded'].get('VmRSS')} | {vram} |"
            )


if __name__ == "__main__":
    main([Path(p) for p in sys.argv[1:]])
