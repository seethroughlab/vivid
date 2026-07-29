#!/usr/bin/env python3
"""Capture a 1080p AV showcase clip from a running Vivid app (ADR-0041 Phase 1d re-shoot rig).

Drives the loopback control server (127.0.0.1:$VIVID_PORT): builds a showcase demo, forces the
Output node to 1080p (16:9), plays the transport, then records `--seconds` of live render + master
audio via the `export_video` control tool.

macOS suspends Vivid's render + audio thread whenever the app is NOT the foreground window, which
freezes the export into byte-identical frames. So during the record we hold Vivid in front with a
repeated `osascript ... activate`, and afterwards we sanity-check the clip for real motion (a frozen
capture is rejected, never silently shipped).

Build path per showcase kind (see examples/demos/showcase/registry.py):
  - DEMO      : import the builder module (pulse/mirror/neon), call build(v, save=False).
  - TUTORIAL  : run the standalone build.py as a subprocess (it connects + loads its project itself).

Usage:
  uv run site/scripts/capture-showcase-video.py <id> [--seconds 24] [--fps 60] [--out DIR]
    <id> ∈ {first-project, pulse-song, mirror-bridge, shader-edit, neon-song}  (registry ids)
"""
from __future__ import annotations

import argparse
import importlib
import json
import os
import subprocess
import sys
import threading
import time
import urllib.request
from pathlib import Path
from types import SimpleNamespace

REPO = Path(__file__).resolve().parents[2]
DEMOS = REPO / "examples" / "demos"
sys.path.insert(0, str(DEMOS))
sys.path.insert(0, str(REPO / "examples" / "demos" / "showcase"))

import registry  # noqa: E402  (from examples/demos/showcase)

PORT = int(os.environ.get("VIVID_PORT", "9876"))
BASE = f"http://127.0.0.1:{PORT}"

# Output node enum indices (app/src/gpu/output_format.h): aspect 0 = 16:9, height 3 = 1080.
ASPECT_16_9 = 0
HEIGHT_1080 = 3


def call(method: str, timeout: float = 120.0, **payload) -> dict:
    req = urllib.request.Request(f"{BASE}/{method}", data=json.dumps(payload).encode(),
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=timeout) as r:
        res = json.loads(r.read())
    if not res.get("ok", False):
        raise RuntimeError(f"{method} failed: {res.get('code')} {res.get('error')}")
    return res


def find_node(op_type: str) -> int | None:
    for n in call("get_graph").get("nodes", []):
        if n.get("op") == op_type:
            return n.get("id")
    return None


def foreground(stop: threading.Event):
    """Keep Vivid the front app for the whole record — else the frame loop suspends and freezes."""
    while not stop.is_set():
        subprocess.run(["osascript", "-e", 'tell application "vivid" to activate'],
                       capture_output=True)
        stop.wait(0.75)


def build_demo(show: registry.Showcase):
    """Author the project INTO the running app so it's live and playing before we record."""
    from vivid_demo import Vivid  # noqa: E402
    v = Vivid()
    if show.kind is registry.Kind.DEMO:
        mod = importlib.import_module(show.target)  # e.g. "pulse" -> examples/demos/pulse.py
        importlib.reload(mod)
        print(f"[capture] building demo module '{show.target}' …", flush=True)
        mod.build(v, save=False)
    else:  # TUTORIAL: standalone build.py connects + loads its own project
        bp = show.target_path()
        print(f"[capture] running tutorial builder {bp} …", flush=True)
        env = {**os.environ, "VIVID_PORT": str(PORT)}
        r = subprocess.run([sys.executable, str(bp)], cwd=str(bp.parent), env=env,
                           capture_output=True, text=True, timeout=900)
        if r.returncode != 0:
            raise RuntimeError(f"tutorial build failed:\n{r.stdout[-2000:]}\n{r.stderr[-2000:]}")
        call("set_playing", playing=True)
    return v


def force_1080p():
    out = find_node("Output")
    if out is None:
        raise RuntimeError("no Output node in the visual graph")
    call("set_node_param", node_id=out, name="aspect", value=float(ASPECT_16_9))
    call("set_node_param", node_id=out, name="height", value=float(HEIGHT_1080))
    print(f"[capture] Output node {out} -> 16:9 / 1080", flush=True)


def verify_motion(path: Path) -> dict:
    """Reject a frozen capture: sample scene-change scores; a live clip has non-trivial motion."""
    dur = subprocess.run(
        ["ffprobe", "-v", "error", "-select_streams", "v:0", "-show_entries",
         "format=duration:stream=width,height,nb_read_frames", "-count_frames",
         "-of", "json", str(path)], capture_output=True, text=True)
    meta = json.loads(dur.stdout or "{}")
    scene = subprocess.run(
        ["ffmpeg", "-v", "error", "-i", str(path), "-vf",
         "select='gt(scene,0)',metadata=print:file=-", "-an", "-f", "null", "-"],
        capture_output=True, text=True)
    scores = [float(l.split("=")[1]) for l in scene.stdout.splitlines()
              if "scene_score=" in l]
    peak = max(scores) if scores else 0.0
    st = (meta.get("streams") or [{}])[0]
    return {"width": st.get("width"), "height": st.get("height"),
            "frames": st.get("nb_read_frames"),
            "duration": (meta.get("format") or {}).get("duration"),
            "motion_peak": round(peak, 4), "frozen": peak < 0.002}


def main():
    ap = argparse.ArgumentParser(description="Capture a 1080p AV showcase clip (Phase 1d re-shoot)")
    ap.add_argument("id", help="registry showcase id")
    ap.add_argument("--seconds", type=float, default=24.0)
    ap.add_argument("--fps", type=float, default=60.0)
    ap.add_argument("--warm", type=float, default=4.0, help="seconds to let the scene run before recording")
    ap.add_argument("--out", type=Path,
                    default=DEMOS / "showcase" / "heroes", help="output dir for <id>.mp4")
    args = ap.parse_args()

    show = registry.by_id(args.id)
    if show is None:
        # Not a registered showcase — treat as a bare demo module examples/demos/<id>.py
        # (the ADR-0041 demos spectrum/blob/crystal/storm live outside the QA registry).
        if not (DEMOS / f"{args.id}.py").exists():
            sys.exit(f"unknown id '{args.id}': no registry entry and no examples/demos/{args.id}.py; "
                     f"registered: {', '.join(s.id for s in registry.SHOWCASES)}")
        show = SimpleNamespace(id=args.id, kind=registry.Kind.DEMO, target=args.id)

    call("get_health", timeout=10)  # fail fast if the app isn't up
    build_demo(show)

    args.out.mkdir(parents=True, exist_ok=True)
    path = args.out / f"{show.id}.mp4"
    if path.exists():
        path.unlink()

    # Foreground the app BEFORE resizing + warming: macOS App-Naps the frame loop while Vivid is
    # unfocused, so apply_output_settings never runs and the render target stays at its default 720 —
    # then the export locks that in. Holding focus first lets the RT actually resize to 1080.
    stop = threading.Event()
    holder = threading.Thread(target=foreground, args=(stop,), daemon=True)
    holder.start()
    try:
        force_1080p()
        print(f"[capture] warming {args.warm}s …", flush=True)
        time.sleep(args.warm)          # frames advance now (foregrounded) -> RT resizes to 1080
        force_1080p()                  # re-assert right before recording (belt-and-suspenders)

        # Perform the song through its sections DURING the record: evenly launch each scene across the
        # export window so the clip develops (intro -> verse -> chorus). Single-scene demos just play 0.
        n_scenes = int(call("status").get("scenes", 1))
        try:
            call("set_launch_quantize", bars=1)   # switch on the next bar so transitions land cleanly
        except Exception:
            pass
        pending = sorted((i * args.seconds / n_scenes, i) for i in range(1, n_scenes))  # (elapsed_s, scene)

        t0 = time.time()
        r = call("export_video", path=str(path), seconds=args.seconds, fps=args.fps)
        extra = f", performing {n_scenes} scenes" if n_scenes > 1 else ""
        print(f"[capture] recording {args.seconds}s @ {r.get('width')}x{r.get('height')}{extra} …", flush=True)
        deadline = t0 + args.seconds + 30
        while time.time() < deadline:
            time.sleep(0.5)
            elapsed = time.time() - t0
            while pending and elapsed >= pending[0][0]:
                _, sc = pending.pop(0)
                call("launch_scene", scene=sc)
                print(f"[capture]   -> scene {sc} at {elapsed:.1f}s", flush=True)
            st = call("video_export_status")
            if not st.get("recording"):
                print(f"[capture] done: {st.get('frames')} frames, "
                      f"{st.get('elapsed_sec'):.1f}s", flush=True)
                break
        else:
            raise RuntimeError("export did not finish before deadline")
    finally:
        stop.set()
        holder.join(timeout=3)

    info = verify_motion(path)
    print(f"[capture] {path.name}: {json.dumps(info)}", flush=True)
    if info["frozen"]:
        sys.exit(f"[capture] FROZEN capture (motion_peak={info['motion_peak']}) — "
                 f"app was likely not foregrounded. Not shipping {path}.")
    if str(info.get("height")) != "1080":
        sys.exit(f"[capture] wrong resolution {info.get('width')}x{info.get('height')} — expected *x1080.")
    print(f"[capture] OK -> {path}", flush=True)


if __name__ == "__main__":
    main()
