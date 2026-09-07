#!/usr/bin/env python3
"""Stop-track end-to-end test (session clip-stop feature).

The session grid could LAUNCH a clip but had no way to STOP one — a playing column ran forever. This
drives the REAL app over the control server to prove the new stop path: launch a clip (active_clip
becomes its scene), `stop_track` (active_clip returns to -1 = idle), then relaunch (active back to the
scene) so we know stop didn't wedge the track. Also exercises `stop_all` across two tracks.

Both the engine boundary-apply (queued=-2 -> active=-1) and the control/MCP handlers are covered; the
UI (empty-slot click -> stop_track) shares the same session_stop_track entry point verified here.

Launch quantization is set to the shortest grid so the queued->active transition applies within a
beat or two — the test polls rather than assuming an instant apply.

Needs the built app + a GPU (drives the shipping binary): runs on the self-hosted macOS runner /
locally, not in portable headless CI. Run:  uv run mcp/tests/test_stop_track.py
"""
import json
import os
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = int(os.environ.get("VIVID_STOPTRACK_PORT", "9878"))
APP = os.environ.get("VIVID_APP", str(ROOT / "build" / "Vivid.app" / "Contents" / "MacOS" / "vivid"))
BASE = f"http://127.0.0.1:{PORT}"


def call(method: str, body: dict | None = None) -> dict:
    data = json.dumps(body or {}).encode()
    req = urllib.request.Request(f"{BASE}/{method}", data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.loads(r.read().decode())


def ok(method: str, body: dict | None = None) -> dict:
    r = call(method, body)
    if not r.get("ok", False):
        raise SystemExit(f"FAIL: {method} -> {r}")
    return r


def wait_up(timeout=30.0) -> bool:
    end = time.time() + timeout
    while time.time() < end:
        try:
            call("get_health")
            return True
        except Exception:
            time.sleep(0.5)
    return False


def active(track: int) -> int:
    for t in ok("list_tracks")["tracks"]:
        if t.get("index", t.get("track")) == track:
            return t["active_clip"]
    raise SystemExit(f"FAIL: track {track} not in list_tracks")


def wait_active(track: int, want: int, timeout=8.0) -> bool:
    """Poll active_clip until it reaches `want` (launch/stop applies at the next launch boundary)."""
    end = time.time() + timeout
    while time.time() < end:
        if active(track) == want:
            return True
        time.sleep(0.05)
    return False


def build_project() -> tuple[int, int]:
    """Two instrument tracks, each with a note clip in scene 0, transport rolling on the tightest grid."""
    ok("new_project")
    t0 = ok("add_track", {"kind": "instrument", "instrument": "TestTone"})
    t1 = ok("add_track", {"kind": "instrument", "instrument": "TestTone"})
    a = t0.get("track", t0.get("index", 0))
    b = t1.get("track", t1.get("index", 1))
    for tr in (a, b):
        ok("set_clip", {"track": tr, "scene": 0, "length": 4.0,
                        "notes": [{"p": 60, "s": 0.0, "d": 4.0, "v": 0.8}]})
    ok("set_launch_quantize", {"bars": 1})   # 1-bar grid (the minimum) so launch/stop apply at each bar
    ok("set_playing", {"playing": True})
    return a, b


def main() -> int:
    if not Path(APP).exists():
        print(f"SKIP: app not built at {APP} (set VIVID_APP)")
        return 0
    env = {**os.environ, "VIVID_PORT": str(PORT), "VIVID_NO_RECOVER": "1", "VIVID_DISCARD_RECOVERY": "1"}
    proc = subprocess.Popen([APP], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        if not wait_up():
            if proc.poll() is not None:
                print(f"FAIL: app exited before the control server came up (code {proc.returncode})")
                return 1
            print("SKIP: control server never came up (no window server / GPU in this environment)")
            return 0
        a, b = build_project()

        # 1) Launch a clip -> the track goes active on that scene.
        ok("launch_clip", {"track": a, "scene": 0})
        if not wait_active(a, 0):
            print(f"FAIL: launch_clip did not make track {a} active (active={active(a)})")
            return 1

        # 2) stop_track -> the track returns to idle (-1).
        ok("stop_track", {"track": a})
        if not wait_active(a, -1):
            print(f"FAIL: stop_track did not idle track {a} (active={active(a)})")
            return 1

        # 3) Relaunch -> the track is not wedged; it plays again.
        ok("launch_clip", {"track": a, "scene": 0})
        if not wait_active(a, 0):
            print(f"FAIL: track {a} would not relaunch after stop (active={active(a)})")
            return 1

        # 4) stop_all across both tracks: launch both, then stop everything -> both idle.
        ok("launch_clip", {"track": a, "scene": 0})
        ok("launch_clip", {"track": b, "scene": 0})
        if not (wait_active(a, 0) and wait_active(b, 0)):
            print(f"FAIL: could not get both tracks active before stop_all (a={active(a)} b={active(b)})")
            return 1
        ok("stop_all")
        if not (wait_active(a, -1) and wait_active(b, -1)):
            print(f"FAIL: stop_all did not idle both tracks (a={active(a)} b={active(b)})")
            return 1

        print("ok   test_stop_track — launch/stop_track/relaunch and stop_all all behaved (2 tracks)")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
