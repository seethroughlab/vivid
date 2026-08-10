#!/usr/bin/env python3
"""Operator audit harness (ADR-0042).

Drives the RUNNING app over the control server (127.0.0.1:$VIVID_PORT) and measures every registered
operator against the four-dimension Definition of Done:

  1. thumbnail  — the op's own node-card render is non-blank (path A/B), or a documented exemption
  2. renders    — a minimal per-op test graph produces non-blank output
  3. params     — each numeric param, swept min..max, measurably changes the output (vs an animation floor)
  4. perf       — the op's whole-frame cost (frame_ms) + delta over a trivial baseline

Advisory only: prints a PASS/WARN/FAIL table and writes a JSON report under reports/. Visual ops (gpu=1)
get the full pass; audio ops (gpu=0) are listed with a thumbnail classification (they can't be
GPU-captured — see the ADR).

    uv run tools/operator_audit/audit.py            # audit the whole catalog
    uv run tools/operator_audit/audit.py Shape3D    # audit one operator
"""
import os
import sys
import time
import json
import subprocess

# macOS pauses a fully-occluded window's render loop (App Nap), so the harness keeps Vivid foreground —
# otherwise captures come back blank. Cheap (~50ms) and idempotent.
_FG = ['osascript', '-e',
       'tell application "System Events" to set frontmost of (first process whose name is "vivid") to true']


def foreground():
    try:
        subprocess.run(_FG, capture_output=True, timeout=5)
    except Exception:
        pass

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "examples", "demos"))
sys.path.insert(0, HERE)

from vivid_demo import Vivid            # noqa: E402
import scaffolds                        # noqa: E402

SETTLE = 0.30          # seconds to let the graph render before capturing
PARAM_SETTLE = 0.14    # seconds after a set_node_param before capturing
NOISE_MARGIN = 3       # a param must move the hash more than (animation_floor + this) to count as "affects"
PERF_SLOW_MS = 20.0    # whole-frame ms above this (< 50 fps) is flagged
SKIP = {"Output"}      # the sink node, not a content op

# Audio generators/modulators are EXPECTED to draw a thumbnail from the param snapshot; pure DSP effects
# are exempt (name-only cell OK) — ADR-0042.
AUDIO_THUMB_EXPECTED = {"LFO", "ADSR", "Arp", "Euclid", "Chord", "RandMelody", "Sampler", "Type"}

# Ops that need a LIVE input the scaffold can't synthesize — a played note stream, a loaded asset/mesh, a
# camera/audio device, authored text. A blank render/thumbnail for these is "needs-input", not a defect.
NEEDS_INPUT = {"CustomShader", "MeshLoad", "MeshRender", "MeshDisplace", "Model", "Video", "Webcam",
               "AudioSpectrum", "Notes", "VectorText", "Text"}


def needs_input(op) -> bool:
    if op["name"] in NEEDS_INPUT:
        return True
    ins = {p["name"].lower() for p in op.get("ports", []) if p.get("dir") == "in"}
    return "signal" in ins or "mesh" in ins


def hamming(a: str, b: str) -> int:
    try:
        return bin(int(a, 16) ^ int(b, 16)).count("1")
    except Exception:
        return 0 if a == b else 64


def analyze(v) -> dict:
    return v.call("analyze_frame").get("analysis", {})


def rendered(a: dict) -> bool:
    """Non-blank in the audit sense: ANY real signal. analyze_rgba flags uniform SOLID fills as blank
    (contrast < 0.01), but a solid colour IS output — so accept brightness / colour-spread too."""
    return (not a.get("is_blank", True)
            or a.get("brightness", 0.0) > 0.02
            or a.get("contrast", 0.0) > 0.01
            or a.get("color_spread", 0.0) > 0.02)


def frame_ms(v) -> float:
    return float(v.call("get_perf").get("frame_ms", 0.0))


def find_output(v) -> int:
    for n in v.graph()["nodes"]:
        if (n.get("type") or n.get("op")) == "Output":
            return n["id"]
    raise RuntimeError("no Output node")


def _sig(a: dict):
    """A frame signature robust to small on-screen objects: the 8x8 hash alone is dominated by the
    background, so combine it with brightness / contrast / colour-spread (which do respond)."""
    return (a.get("hash", ""), a.get("brightness", 0.0), a.get("contrast", 0.0), a.get("color_spread", 0.0))


def _dist(x, y) -> float:
    return hamming(x[0], y[0]) / 64.0 + abs(x[1] - y[1]) + abs(x[2] - y[2]) + abs(x[3] - y[3])


def check_param(v, op_node: int, p: dict) -> dict:
    """Sweep one param min..max; a param 'affects' output if the min->max signature distance exceeds the
    animation noise floor (two captures at the same value) plus a small absolute margin."""
    name, typ = p["name"], p.get("type")
    lo, hi, dflt = p.get("min", 0.0), p.get("max", 1.0), p.get("default", 0.0)
    if typ not in ("int", "float", "bool"):
        return {"name": name, "verdict": "skip", "reason": f"type {typ}"}
    if typ == "bool":
        lo, hi = 0.0, 1.0
    if lo == hi:
        return {"name": name, "verdict": "skip", "reason": "flat range"}
    try:
        # Anchor on the DEFAULT (a meaningful value) and compare to BOTH extremes — a pure min<->max sweep
        # can land on two degenerate frames (e.g. Render3D cam_z -50 and +50 are both ~blank) and miss the
        # param's real effect around its default.
        v.set_node_param(op_node, name, float(dflt)); time.sleep(PARAM_SETTLE)
        sd = _sig(analyze(v))
        time.sleep(PARAM_SETTLE)
        sd2 = _sig(analyze(v))                      # animation noise floor at the SAME (default) value
        v.set_node_param(op_node, name, float(lo)); time.sleep(PARAM_SETTLE)
        smin = _sig(analyze(v))
        v.set_node_param(op_node, name, float(hi)); time.sleep(PARAM_SETTLE)
        smax = _sig(analyze(v))
        v.set_node_param(op_node, name, float(dflt))
    except Exception as e:
        return {"name": name, "verdict": "error", "reason": str(e)[:80]}
    noise = _dist(sd, sd2)
    effect = max(_dist(sd, smin), _dist(sd, smax))
    verdict = "affects" if effect > 2.0 * noise + 0.006 else "no-effect"
    return {"name": name, "verdict": verdict, "noise": round(noise, 4), "effect": round(effect, 4)}


def audit_op(v, op: dict, baseline_ms: float) -> dict:
    name = op["name"]
    rec = {"name": name, "gpu": bool(op.get("gpu")), "output_kind": scaffolds.output_kind(op)}

    # Audio ops (gpu=0) can't be GPU-captured; classify thumbnail expectation and move on.
    if not op.get("gpu"):
        rec["verdict"] = "audio"
        rec["thumbnail"] = "expected-manual" if name in AUDIO_THUMB_EXPECTED else "exempt-effect"
        rec["note"] = "audio op — not GPU-audited (render/param/perf via audio-graph contract, out of scope)"
        return rec

    foreground()
    out = find_output(v)
    try:
        op_node, terminal = scaffolds.build_scaffold(v, op, scaffolds.Sources(v))
    except Exception as e:
        rec["verdict"] = "error"; rec["error"] = f"scaffold: {str(e)[:120]}"
        return rec
    if terminal is None:
        rec["verdict"] = "warn"; rec["note"] = f"no sink for output_kind={rec['output_kind']}"
        return rec

    # --- 2. renders + 3. params (terminal wired to Output) ---
    try:
        v.connect(out, terminal, 0, 0)
    except Exception as e:
        rec["verdict"] = "error"; rec["error"] = f"wire terminal: {str(e)[:120]}"
        return rec
    time.sleep(SETTLE)
    base = analyze(v)
    for _ in range(2):                              # a blank frame usually means Vivid lost foreground
        if rendered(base):
            break
        foreground(); time.sleep(SETTLE + 0.2); base = analyze(v)
    rec["render"] = "pass" if rendered(base) else ("needs-input" if needs_input(op) else "fail")
    rec["render_detail"] = {k: round(base.get(k, 0.0), 4) for k in ("brightness", "contrast", "activity")}

    params = [check_param(v, op_node, p) for p in op.get("params", [])]
    swept = [p for p in params if p["verdict"] in ("affects", "no-effect")]
    dead = [p["name"] for p in swept if p["verdict"] == "no-effect"]
    rec["params"] = {"total": len(op.get("params", [])), "swept": len(swept),
                     "affects": len(swept) - len(dead), "no_effect": dead, "detail": params}

    # --- 4. perf ---
    time.sleep(0.2)
    ms = frame_ms(v)
    rec["perf"] = {"frame_ms": round(ms, 2), "delta_ms": round(ms - baseline_ms, 2),
                   "flag": "slow" if ms > PERF_SLOW_MS else "ok"}

    # --- 1. thumbnail (capture the op's OWN render by feeding it into Output) ---
    try:
        v.connect(out, op_node, 0, 0); time.sleep(SETTLE)
        th = analyze(v)
        rec["thumbnail"] = "pass" if rendered(th) else ("needs-input" if needs_input(op) else "fail")
    except Exception:
        rec["thumbnail"] = "unknown"

    # --- overall verdict ---
    if rec["render"] == "fail" or rec["thumbnail"] == "fail":
        rec["verdict"] = "fail"
    elif rec["render"] == "needs-input" or rec["thumbnail"] == "needs-input":
        rec["verdict"] = "needs-input"       # a live input the scaffold can't synthesize — not a defect
    elif dead or rec["perf"]["flag"] == "slow":
        rec["verdict"] = "warn"
    else:
        rec["verdict"] = "pass"
    return rec


def main():
    only = sys.argv[1] if len(sys.argv) > 1 else None
    v = Vivid()
    catalog = [o for o in v.call("list_operators").get("operators", []) if o["name"] not in SKIP]
    ops = catalog
    if only:
        ops = [o for o in ops if o["name"] == only]
        if not ops:
            print(f"no operator '{only}'"); return

    foreground(); time.sleep(0.5)
    # Trivial baseline (a bare texture source -> Output) for the perf delta. This used to name
    # `Gradient`, which is no longer in the catalog — add_node raised and the harness died before
    # auditing ANY operator. Resolve against the live catalog so a future rename degrades to a
    # skipped perf baseline instead of taking the whole run down.
    v.call("new_project"); out = find_output(v)
    baseline_ms = 0.0
    # Resolved against the FULL catalog, not the (possibly single-op) filtered list.
    baseline_op = next((n for n in ("NoiseField", "Lines", "Shape") if any(o["name"] == n for o in catalog)), None)
    if baseline_op is None:
        baseline_op = next((o["name"] for o in catalog
                            if o.get("gpu") and not any(p.get("dir") == "in" for p in o.get("ports", []))), None)
    if baseline_op:
        try:
            v.connect(out, v.add_node(baseline_op), 0, 0); time.sleep(SETTLE + 0.3)
            baseline_ms = frame_ms(v)
            print(f"baseline frame_ms ({baseline_op}->Output): {baseline_ms:.2f}\n")
        except Exception as e:
            print(f"baseline skipped ({baseline_op}: {e}) — perf deltas are absolute, not relative\n")
    else:
        print("baseline skipped (no input-free GPU op in the catalog)\n")

    records = []
    for i, op in enumerate(sorted(ops, key=lambda o: (not o.get("gpu"), o["name"])), 1):
        v.call("new_project")
        try:
            rec = audit_op(v, op, baseline_ms)
        except Exception as e:
            rec = {"name": op["name"], "verdict": "error", "error": str(e)[:160]}
        records.append(rec)
        v_ = rec.get("verdict", "?")
        extra = ""
        if rec.get("gpu"):
            extra = f"render={rec.get('render','-')} thumb={rec.get('thumbnail','-')}"
            pp = rec.get("params", {})
            if pp.get("no_effect"):
                extra += f" no-visible-change={pp['no_effect']}"
            if rec.get("perf", {}).get("flag") == "slow":
                extra += f" SLOW={rec['perf']['frame_ms']}ms"
        else:
            extra = f"thumb={rec.get('thumbnail','-')}"
        mark = {"pass": "PASS", "warn": "WARN", "fail": "FAIL", "audio": "aud ", "error": "ERR ",
                "needs-input": "NEED", "unknown": "??? "}.get(v_, v_)
        print(f"  [{i:>2}/{len(ops)}] {mark}  {op['name']:<22} {extra}")

    # summary
    by = {}
    for r in records:
        by[r["verdict"]] = by.get(r["verdict"], 0) + 1
    print("\n=== summary ===")
    for k in ("pass", "warn", "needs-input", "fail", "error", "audio"):
        if by.get(k):
            print(f"  {k}: {by[k]}")

    ts = time.strftime("%Y%m%d-%H%M%S")
    outdir = os.path.join(HERE, "reports")
    os.makedirs(outdir, exist_ok=True)
    path = os.path.join(outdir, f"audit-{ts}.json")
    with open(path, "w") as f:
        json.dump({"baseline_ms": baseline_ms, "counts": by, "operators": records}, f, indent=2)
    print(f"\nreport -> {path}")


if __name__ == "__main__":
    main()
