#!/usr/bin/env python3
"""Sanity-check the generated operator embedding layout.

Compares a few expected clusters (drums, time-based effects, generators) and
reports how tightly each group lands together vs. the overall spread. A healthy
layout has intra-cluster distance much smaller than the global mean.

Usage:
    python3 tools/check_operator_clusters.py [path-to-operator_embeddings.json]
"""

from __future__ import annotations

import json
import math
import pathlib
import sys

DEFAULT = pathlib.Path(__file__).resolve().parent.parent / "resources" / "operator_embeddings.json"

CLUSTERS = {
    "drums": ["DrumKick", "DrumSnare", "DrumHiHat", "DrumClap",
              "DrumTom", "DrumCymbal"],
    "time_effects": ["Reverb", "Delay", "PingPongDelay",
                     "Chorus", "Flanger", "Phaser"],
    "generators": ["Noise", "Oscillator", "Lfo"],
    "sequencing": ["Arpeggiator", "Euclidean", "DrumSequencer",
                   "Clock", "Alternate"],
    "particles": ["Particles2D", "Flocking", "Fluid"],
}


def pairwise_mean(points: list[tuple[float, float]]) -> float:
    if len(points) < 2:
        return 0.0
    total, n = 0.0, 0
    for i in range(len(points)):
        for j in range(i + 1, len(points)):
            dx = points[i][0] - points[j][0]
            dy = points[i][1] - points[j][1]
            total += math.sqrt(dx * dx + dy * dy)
            n += 1
    return total / n


def main() -> int:
    path = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    if not path.exists():
        print(f"no layout at {path}", file=sys.stderr)
        return 1
    data = json.loads(path.read_text())
    ops = {o["name"]: o for o in data["operators"]}
    print(f"loaded {len(ops)} operators from {path}")

    all_xy = [(o["xy"][0], o["xy"][1]) for o in ops.values()]
    global_mean = pairwise_mean(all_xy)
    print(f"global pairwise mean distance: {global_mean:.3f}")

    for label, names in CLUSTERS.items():
        present = [ops[n] for n in names if n in ops]
        missing = [n for n in names if n not in ops]
        if len(present) < 2:
            print(f"[{label}] skipped ({len(present)}/{len(names)} present; missing {missing})")
            continue
        xys = [(o["xy"][0], o["xy"][1]) for o in present]
        cluster_mean = pairwise_mean(xys)
        ratio = cluster_mean / global_mean if global_mean else 0.0
        verdict = "tight" if ratio < 0.4 else ("loose" if ratio < 0.7 else "scattered")
        print(f"[{label:14}] {len(present)}/{len(names):<2} ops  "
              f"mean_d={cluster_mean:.3f}  ratio={ratio:.2f}  {verdict}")
        if missing:
            print(f"  (missing: {missing})")
    return 0


if __name__ == "__main__":
    sys.exit(main())
