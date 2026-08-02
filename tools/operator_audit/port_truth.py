#!/usr/bin/env python3
"""ADR-0047 port-truth inventory — the "inventory the lies" advisory check.

Several audio operators declare a fake audio-buffer output port even though their real stream is a note
or control signal (it leaves through a hidden context channel, e.g. c->note_out / c->control_out). An
Audio wire off an LFO is silence. This makes the catalog lie about what a node produces, which hurts
users, agents, and connection validation.

This script drives the RUNNING app over the control server and reports:
  * catalog-visible note/control ops (kind modulator / note_effect) whose output port does NOT declare
    its real stream via semantic_shape (note_stream / control_signal) — an unlabeled shim ("LIE"); and
  * note generators (Euclid/Chord/RandMelody), which are enumerated by name only via list_generators
    and whose port descriptors the catalog does not expose at all — a deeper introspection gap ("GAP").

Advisory only (per ADR-0042): prints a table + writes reports/port_truth.json and always exits 0.

    uv run tools/operator_audit/port_truth.py
"""
import os
import sys
import json
import subprocess

# macOS pauses an occluded window's render loop; keep Vivid foreground so control calls stay responsive.
_FG = ['osascript', '-e',
       'tell application "System Events" to set frontmost of (first process whose name is "vivid") to true']


def foreground():
    try:
        subprocess.run(_FG, capture_output=True, timeout=5)
    except Exception:
        pass


HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "examples", "demos"))
from vivid_demo import Vivid            # noqa: E402

# The catalog `kind` of an op whose real stream is NOT audio -> the semantic_shape its output should carry.
KIND_STREAM = {"modulator": "control_signal", "note_effect": "note_stream", "generator": "note_stream"}
HONEST_SHAPES = {"note_stream", "control_signal"}

# Ops intentionally exposing an audio-buffer output despite a note/control kind (there are none today).
ACCEPTED_SHIMS: set[str] = set()


def main():
    v = Vivid()
    foreground()

    cat = v.call("list_operator_catalog", domain="audio", kind="all", detail="full")
    ops = cat.get("operators", cat.get("catalog", []))

    labeled, flagged = [], []
    for op in ops:
        kind = op.get("kind")
        if kind not in KIND_STREAM:
            continue                                   # instruments / audio effects: really are audio
        expected = KIND_STREAM[kind]
        outs = [p for p in op.get("ports", []) if p.get("dir") == "out"]
        rec = {"name": op["name"], "kind": kind, "expected": expected,
               "out_ports": [{"name": p.get("name"), "semantic_shape": p.get("semantic_shape")} for p in outs]}
        honest = any(p.get("semantic_shape") in HONEST_SHAPES for p in outs)
        if honest or op["name"] in ACCEPTED_SHIMS:
            labeled.append(rec)
        else:
            rec["issue"] = (f"is a {kind} but its output declares no note/control stream; "
                            f"expected semantic_shape '{expected}'")
            flagged.append(rec)

    # Every note generator (list_generators) should now also appear in the catalog with its descriptor,
    # so its ports are introspectable. Flag any that are still name-only (registered but not catalogued).
    catalog_names = {op.get("name") for op in ops}
    gens = v.call("list_generators").get("generators", [])
    gen_gap = [{"name": g,
                "issue": "note generator enumerated by list_generators but absent from "
                         "list_operator_catalog — its port descriptors are not introspectable"}
               for g in gens if g not in catalog_names]

    # ---- report ----
    print("=== ADR-0047 port-truth inventory ===\n")
    print(f"note/control ops visible in the catalog: {len(labeled) + len(flagged)}")
    for r in sorted(labeled, key=lambda r: r["name"]):
        shapes = ", ".join(str(p["semantic_shape"]) for p in r["out_ports"]) or "-"
        print(f"  OK    {r['name']:<12} {r['kind']:<12} -> {shapes}")
    for r in sorted(flagged, key=lambda r: r["name"]):
        print(f"  LIE   {r['name']:<12} {r['kind']:<12} -> {r['issue']}")

    print(f"\nnote generators not introspectable via the catalog: {len(gen_gap)}")
    for r in sorted(gen_gap, key=lambda r: r["name"]):
        print(f"  GAP   {r['name']}")

    print(f"\n=== summary: {len(flagged)} unlabeled shim(s), {len(gen_gap)} non-introspectable generator(s) ===")
    if not flagged:
        print("all catalog-visible note/control ports declare their real stream. ✓")

    outdir = os.path.join(HERE, "reports")
    os.makedirs(outdir, exist_ok=True)
    path = os.path.join(outdir, "port_truth.json")
    with open(path, "w") as f:
        json.dump({"labeled": labeled, "flagged": flagged, "generator_gap": gen_gap}, f, indent=2)
    print(f"\nreport -> {path}")


if __name__ == "__main__":
    main()
