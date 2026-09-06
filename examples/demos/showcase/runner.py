#!/usr/bin/env python3
"""Showcase QA / screenshot harness (ADR-0037 gate) — CLI + per-showcase orchestration.

For each showcase in the curated registry: regenerate (run its builder) -> load the saved portable
artifact -> verify (validate_project + get_health + run_quality_check) -> warm-capture a hero PNG
(+ analyze) -> gate PASS/WARN/FAIL. Writes reports/<id>.json + reports/index.json and prints a
summary. Exits non-zero iff any showcase FAILs.

The app is NOT launched here — it must already be running and serving the control server. Attach to
a dev build for iteration or the signed /Applications/Vivid.app to produce checked-in hero media:

    uv run examples/demos/showcase/runner.py                    # all showcases
    uv run examples/demos/showcase/runner.py --select shader-edit --app-build dev
    uv run examples/demos/showcase/runner.py --no-regen --app-build signed   # smoke saved artifacts
"""
from __future__ import annotations

import argparse
import importlib
import os
import shutil
import subprocess
import sys
import time
from datetime import datetime, timezone
from pathlib import Path

HERE = os.path.dirname(os.path.abspath(__file__))
DEMOS = os.path.dirname(HERE)                       # examples/demos
if DEMOS not in sys.path:
    sys.path.insert(0, DEMOS)

from vivid_demo import (  # noqa: E402
    SURGE, SURGE_FX, Vivid, call_optional, capture_video, require_control_server, warm_capture,
)
from showcase import gates, registry, report  # noqa: E402

DEFAULT_HEROES = Path(HERE) / "heroes"
DEFAULT_REPORTS = Path(HERE) / "reports"
CASSETTE = Path.home() / "Library" / "Audio" / "Plug-Ins" / "VST3" / "Cassette Drums.vst3"


def probe_capabilities(v: Vivid) -> dict:
    """Read-only detection of the optional prerequisites showcases may declare. Missing ones
    downgrade the relevant checks to WARN rather than failing the whole showcase."""
    return {
        "surge": os.path.exists(SURGE) or os.path.exists(SURGE_FX),
        "cassette": CASSETTE.exists(),
        "clang": shutil.which("clang++") is not None,
    }


def regenerate_demo(v: Vivid, showcase) -> dict:
    """Import the builder module and author the project in-process (build(v, save=True))."""
    try:
        mod = importlib.import_module(showcase.target)
        importlib.reload(mod)          # pick up edits across runs
        v.reset()
        mod.build(v, save=True)
        return {"mode": "import", "ok": True}
    except Exception as exc:  # noqa: BLE001
        return {"mode": "import", "ok": False, "error": f"{type(exc).__name__}: {exc}"}


def regenerate_tutorial(showcase, port: int) -> dict:
    """Run a tutorial build.py as a child process pointed at the same app."""
    env = {**os.environ, "VIVID_PORT": str(port)}
    try:
        proc = subprocess.run(
            ["uv", "run", str(showcase.target_path())],
            env=env, capture_output=True, text=True, timeout=900,
        )
    except Exception as exc:  # noqa: BLE001
        return {"mode": "subprocess", "ok": False, "error": f"{type(exc).__name__}: {exc}"}
    return {
        "mode": "subprocess",
        "ok": proc.returncode == 0,
        "returncode": proc.returncode,
        "stdout_tail": proc.stdout[-2000:],
        "stderr_tail": proc.stderr[-2000:],
    }


def run_showcase(v: Vivid, showcase, args, caps: dict) -> gates.ShowcaseResult:
    steps: dict = {}
    missing = gates.prereqs_missing(showcase, caps)

    # --- 1. REGENERATE (skip when a declared prereq is missing — can't author without it) ------
    if args.no_regen:
        steps["regen"] = {"mode": "skipped", "ok": True, "skipped_reason": "--no-regen"}
    elif missing and not args.force:
        steps["regen"] = {"mode": "skipped", "ok": True,
                          "skipped_reason": f"missing prereqs: {', '.join(missing)}",
                          "missing": missing}
        print(f"[{showcase.id}] regen skipped (missing {', '.join(missing)}); "
              f"loading existing artifact if present")
    elif showcase.kind is registry.Kind.DEMO:
        print(f"[{showcase.id}] regenerating (import {showcase.target})")
        steps["regen"] = regenerate_demo(v, showcase)
    else:
        print(f"[{showcase.id}] regenerating (uv run {showcase.target})")
        steps["regen"] = regenerate_tutorial(showcase, args.port)

    # --- 2. LOAD the saved portable artifact (acceptance-test the dir, not the live session) ---
    if not showcase.project_dir.exists():
        print(f"[{showcase.id}] WARNING: project dir does not exist: {showcase.project_dir}")
    steps["load"] = call_optional(v, "load_project", path=str(showcase.project_dir))

    # --- 3. VERIFY (PLAYING) -------------------------------------------------------------------
    # The rebuilt showcases are REACTIVE: the geometry demos render black when the transport is idle
    # (no notes -> no geometry), so we must exercise them PLAYING, exactly as they run. Start the
    # transport before judging non-blank output / capturing, then pause at the end.
    playable = not (missing and not args.force)   # can't meaningfully play what never authored
    if playable:
        call_optional(v, "set_playing", playing=True)
        call_optional(v, "launch_scene", scene=showcase.video_scene)  # money scene (songs: not the sparse intro)
        time.sleep(1.2)                              # let notes start flowing before we judge/capture
    steps["validate"] = call_optional(v, "validate_project")
    steps["health"] = call_optional(v, "get_health")
    steps["quality"] = call_optional(v, "run_quality_check", name="all")
    steps["assets"] = call_optional(v, "list_project_assets")

    # --- 4. optional AUDIO leg (already playing) -----------------------------------------------
    if args.audio and showcase.wants_audio and not missing:
        steps["audio"] = call_optional(v, "run_quality_check", name="no_audio_clipping")

    # --- 5. HERO CAPTURE (warm_capture retries until non-blank — rides past sparse per-note frames)
    hero_path = args.heroes / showcase.hero_name()
    args.heroes.mkdir(parents=True, exist_ok=True)
    steps["capture"] = warm_capture(v, str(hero_path), tries=args.warm_tries, delay=args.warm_delay)
    steps["analyze"] = call_optional(v, "analyze_frame", path=str(hero_path))

    # --- 5b. VIDEO CLIP (opt-in): record a short AV-synced clip for the website -----------------
    if args.video:
        video_path = args.heroes / showcase.video_name()
        print(f"[{showcase.id}] recording {args.video_seconds:.0f}s clip -> {video_path.name}")
        steps["video"] = capture_video(v, str(video_path),
                                       seconds=args.video_seconds, fps=args.video_fps,
                                       scene=showcase.video_scene)
    elif playable:
        call_optional(v, "set_playing", playing=False)   # (the video path pauses internally)

    # --- 6. GATE + per-showcase report ---------------------------------------------------------
    result = gates.evaluate(showcase, steps, caps)
    print(f"[{showcase.id}] {result.gate.upper()}"
          + (f" — {'; '.join(result.reasons)}" if result.reasons else ""))
    return result


def parse_args(argv=None) -> argparse.Namespace:
    p = argparse.ArgumentParser(description="Showcase QA / screenshot harness (ADR-0037 gate)")
    p.add_argument("--select", nargs="+", metavar="ID", help="only these showcase ids")
    p.add_argument("--type", type=int, metavar="N", help="only showcases covering ADR-0037 type N")
    p.add_argument("--port", type=int, default=int(os.environ.get("VIVID_PORT", "9876")),
                   help="control-server port (default env VIVID_PORT or 9876)")
    p.add_argument("--out", type=Path, default=DEFAULT_REPORTS, help="report output dir")
    p.add_argument("--heroes", type=Path, default=DEFAULT_HEROES, help="hero PNG output dir")
    p.add_argument("--app-build", default="unknown", help="label for the running app (dev|signed)")
    p.add_argument("--no-regen", action="store_true", help="load+verify+capture existing artifacts only")
    p.add_argument("--force", action="store_true", help="regenerate even when a prereq is missing")
    p.add_argument("--audio", action="store_true", help="play transport + run no_audio_clipping")
    p.add_argument("--video", action="store_true",
                   help="also record a short AV clip per showcase (heroes/<id>.mp4) for the website")
    p.add_argument("--video-seconds", type=float, default=6.0, help="clip length seconds (default 6)")
    p.add_argument("--video-fps", type=float, default=30.0, help="clip frame rate (default 30)")
    p.add_argument("--warm-tries", type=int, default=12, help="warm_capture attempts (default 12)")
    p.add_argument("--warm-delay", type=float, default=0.5, help="warm_capture delay seconds")
    p.add_argument("--json", action="store_true", help="print machine-readable index to stdout")
    p.add_argument("--list", action="store_true", help="list the registry and exit")
    return p.parse_args(argv)


def main(argv=None) -> int:
    args = parse_args(argv)

    if args.list:
        for s in registry.SHOWCASES:
            print(f"{s.id:<16} type {s.adr0037_type}  {s.kind.value:<8} {s.title}")
        gaps = registry.missing_types()
        if gaps:
            print("\nUNCOVERED ADR-0037 types: " + ", ".join(str(t) for t in gaps))
        return 0

    shows = registry.select(args.select, args.type)
    if not shows:
        print("no showcases selected", file=sys.stderr)
        return 2

    v = Vivid(port=args.port)
    require_control_server(v, "the showcase QA harness")
    caps = probe_capabilities(v)
    print("capabilities: " + ", ".join(f"{k}={'yes' if val else 'no'}" for k, val in caps.items()))

    meta = {
        "generated_at": datetime.now(timezone.utc).isoformat(),
        "app_build": args.app_build,
        "port": args.port,
    }

    results: list[gates.ShowcaseResult] = []
    for s in shows:
        result = run_showcase(v, s, args, caps)
        report.write_showcase(args.out, result, meta)
        results.append(result)

    index = {**meta, **gates.build_index(results, caps, registry.missing_types())}
    report.write_index(args.out, index)
    report.print_summary(index, as_json=args.json)
    print(f"reports -> {args.out}")

    return 1 if any(r.gate == "fail" for r in results) else 0


if __name__ == "__main__":
    raise SystemExit(main())
