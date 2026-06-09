#!/usr/bin/env python3
"""Value-model graph perf regression gate (lane-value Phase 8d).

Runs build/bench_value_graphs, parses its JSON, compares to
tests/benchmarks/value_graphs_baseline.json, prints a per-metric table, and
exits non-zero if the SCALAR metric regresses beyond tolerance.

Opt-in / manual (perf is machine-sensitive; not part of the default ctest run):
    uv run tools/bench_regression.py [build_dir]

The baseline is MACHINE-SPECIFIC (captured on the dev machine). Refresh on a new
machine with:
    ./build/bench_value_graphs "$PWD/build" > tests/benchmarks/value_graphs_baseline.json

Only `scalar_us` is gated (the scalar tick path is the stablest, lowest-variance
signal). many/audio/bridge are reported informationally — they're noisier and
hardware/scheduler-sensitive. The gate also requires an absolute floor so
sub-microsecond run-to-run jitter cannot false-fail.
"""
import json
import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BASELINE = REPO / "tests/benchmarks/value_graphs_baseline.json"

SCALAR_TOLERANCE_PCT = 15.0   # gated metric: allow 15% drift (dev-machine variance)
SCALAR_ABS_FLOOR_US = 0.30    # ...and only fail if the absolute increase exceeds this
GATED = "scalar_us"


def main() -> int:
    build_dir = sys.argv[1] if len(sys.argv) > 1 else str(REPO / "build")
    bench = Path(build_dir) / "bench_value_graphs"
    if not bench.exists():
        print(f"error: {bench} not found — build it first "
              f"(cmake --build build --target bench_value_graphs)", file=sys.stderr)
        return 2
    if not BASELINE.exists():
        print(f"error: baseline {BASELINE} not found", file=sys.stderr)
        return 2

    proc = subprocess.run([str(bench), build_dir], capture_output=True, text=True)
    if proc.returncode != 0:
        print(f"error: bench exited {proc.returncode}\n{proc.stderr}", file=sys.stderr)
        return 2
    try:
        current = json.loads(proc.stdout.strip().splitlines()[-1])
    except (json.JSONDecodeError, IndexError) as e:
        print(f"error: could not parse bench output: {e}\n{proc.stdout}", file=sys.stderr)
        return 2

    baseline = json.loads(BASELINE.read_text())

    print(f"{'metric':<18}{'baseline µs':>14}{'current µs':>14}{'Δ%':>10}   gate")
    print("-" * 66)
    regressed = False
    for key in baseline:
        base = float(baseline[key])
        cur = float(current.get(key, 0.0))
        delta = (cur - base) / base * 100.0 if base else 0.0
        gated = (key == GATED)
        flag = ""
        if gated and delta > SCALAR_TOLERANCE_PCT and (cur - base) > SCALAR_ABS_FLOOR_US:
            flag = "  REGRESSION"
            regressed = True
        elif gated:
            flag = "  (gated, ok)"
        print(f"{key:<18}{base:>14.4f}{cur:>14.4f}{delta:>+9.1f}%{flag}")

    if regressed:
        print(f"\nFAIL: {GATED} regressed > {SCALAR_TOLERANCE_PCT:.0f}% "
              f"(and > {SCALAR_ABS_FLOOR_US} µs). If intentional, refresh the baseline.")
        return 1
    print("\nOK: no gated regression.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
