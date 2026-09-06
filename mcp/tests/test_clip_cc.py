#!/usr/bin/env python3
"""Clip-level controller automation against the real app (P4 Phase A).

A mod wheel, sustain pedal or pitch bend from a single-channel keyboard is a CHANNEL message — it
applies to every sounding note — so it cannot live in the per-note expression curves the clip editor
already paints. It gets its own clip-relative lanes, written through their OWN control method. That
separation is the point of most of these assertions:

  * set_clip_cc / get_clip round-trip lanes (including 128 = channel pressure, 129 = pitch bend),
  * set_clip (a NOTE edit) does NOT clobber lanes — otherwise transpose/quantize would wipe a take,
  * set_clip_cc does not disturb notes,
  * `rev` is shared by both writes and the expected_rev conflict guard covers lanes too,
  * lanes survive save + reload, and survive a pool stash + place round-trip,
  * an automation edit is UNDOABLE (it restores through the ParamsOnly path, which is easy to miss),
  * structurally bad lanes are rejected rather than silently half-applied.

Needs the built app + a GPU, so it runs on the self-hosted macOS runner / locally.
Run:  uv run mcp/tests/test_clip_cc.py
"""
import json
import os
import subprocess
import sys
import tempfile
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = int(os.environ.get("VIVID_CC_PORT", "9884"))
APP = os.environ.get("VIVID_APP", str(ROOT / "build" / "vivid.app" / "Contents" / "MacOS" / "vivid"))
BASE = f"http://127.0.0.1:{PORT}"


def call(method: str, body: dict | None = None) -> dict:
    data = json.dumps(body or {}).encode()
    req = urllib.request.Request(f"{BASE}/{method}", data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=20) as r:
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


NOTES = [{"p": 60, "s": 0.0, "d": 1.0, "v": 0.8}, {"p": 64, "s": 1.0, "d": 1.0, "v": 0.8}]
LANES = [
    {"n": 1,   "ch": 0, "pts": [[0.0, 0.0], [2.0, 1.0], [4.0, 0.25]]},   # mod wheel sweep
    {"n": 129, "ch": 0, "pts": [[0.0, 0.5], [1.0, 0.75], [2.0, 0.5]]},   # pitch bend, centered at 0.5
]


def lanes_of(track: int, scene: int) -> list:
    return ok("get_clip", {"track": track, "scene": scene}).get("cc", [])


def pitches(track: int, scene: int) -> list:
    return sorted(n["p"] for n in ok("get_clip", {"track": track, "scene": scene})["notes"])


def main() -> int:
    if not Path(APP).exists():
        print(f"SKIP: app not built at {APP} (set VIVID_APP)")
        return 0
    env = {**os.environ, "VIVID_PORT": str(PORT), "VIVID_NO_RECOVER": "1", "VIVID_DISCARD_RECOVERY": "1"}
    proc = subprocess.Popen([APP], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    tmp = tempfile.TemporaryDirectory()
    td = Path(tmp.name)
    try:
        if not wait_up():
            if proc.poll() is not None:
                print(f"FAIL: app exited before the control server came up (code {proc.returncode})")
                return 1
            print("SKIP: control server never came up (no window server / GPU in this environment)")
            return 0
        ok("new_project")
        t = ok("add_track", {"kind": "instrument", "instrument": "TestTone"})
        ti = t.get("track", t.get("index", 0))

        # --- round-trip ---
        ok("set_clip", {"track": ti, "scene": 0, "length": 4.0, "notes": NOTES})
        r = ok("set_clip_cc", {"track": ti, "scene": 0, "cc": LANES})
        if r["lanes"] != 2:
            print(f"FAIL: set_clip_cc reported {r['lanes']} lanes")
            return 1
        got = lanes_of(ti, 0)
        if len(got) != 2:
            print(f"FAIL: get_clip returned {len(got)} lanes: {got}")
            return 1
        by_n = {l["n"]: l for l in got}
        if 1 not in by_n or 129 not in by_n:
            print(f"FAIL: lane numbers not preserved: {[l['n'] for l in got]}")
            return 1
        if len(by_n[1]["pts"]) != 3 or abs(by_n[1]["pts"][1][0] - 2.0) > 1e-6 or abs(by_n[1]["pts"][1][1] - 1.0) > 1e-6:
            print(f"FAIL: breakpoints not preserved: {by_n[1]['pts']}")
            return 1

        # --- the separation that matters: a NOTE edit must not wipe automation ---
        ok("set_clip", {"track": ti, "scene": 0, "length": 4.0,
                        "notes": [{"p": 67, "s": 0.0, "d": 1.0, "v": 0.8}]})
        if len(lanes_of(ti, 0)) != 2:
            print("FAIL: set_clip (a note edit) clobbered the automation lanes")
            return 1
        # ...and neither must a read-modify-write transform. The bridge's theory tools (transpose,
        # quantize, harmonize, ...) are all get_clip -> transform -> set_clip client-side, so this is
        # exactly the shape that would destroy a recorded take if lanes rode on set_clip.
        cur = ok("get_clip", {"track": ti, "scene": 0})
        moved = [{**n, "p": n["p"] + 5} for n in cur["notes"]]
        ok("set_clip", {"track": ti, "scene": 0, "length": cur["length"], "notes": moved})
        if len(lanes_of(ti, 0)) != 2:
            print("FAIL: a read-modify-write note transform clobbered the automation lanes")
            return 1
        if pitches(ti, 0) != [72]:
            print(f"FAIL: the transform did not apply: {pitches(ti, 0)}")
            return 1
        # ...and conversely a lane write must not disturb the notes.
        ok("set_clip_cc", {"track": ti, "scene": 0, "cc": LANES})
        if pitches(ti, 0) != [72]:
            print(f"FAIL: set_clip_cc disturbed the notes: {pitches(ti, 0)}")
            return 1

        # --- rev is shared; the conflict guard covers lanes ---
        rev = ok("get_clip", {"track": ti, "scene": 0})["rev"]
        r = call("set_clip_cc", {"track": ti, "scene": 0, "cc": [], "expected_rev": rev - 1})
        if r.get("ok") or r.get("code") != "conflict":
            print(f"FAIL: a stale expected_rev should conflict -> {r}")
            return 1
        if len(lanes_of(ti, 0)) != 2:
            print("FAIL: a rejected write still mutated the lanes")
            return 1
        ok("set_clip_cc", {"track": ti, "scene": 0, "cc": LANES, "expected_rev": rev})   # current rev works
        if ok("get_clip", {"track": ti, "scene": 0})["rev"] <= rev:
            print("FAIL: a lane write did not advance the shared rev")
            return 1

        # --- undo (restores through the ParamsOnly path — easy to miss) ---
        ok("set_clip_cc", {"track": ti, "scene": 0, "cc": [LANES[0]]})
        if len(lanes_of(ti, 0)) != 1:
            print("FAIL: the single-lane write did not land")
            return 1
        ok("undo")
        if len(lanes_of(ti, 0)) != 2:
            print(f"FAIL: undo of an automation edit did not restore the lanes -> {lanes_of(ti, 0)}")
            return 1

        # --- clearing ---
        ok("set_clip_cc", {"track": ti, "scene": 0, "cc": []})
        if lanes_of(ti, 0):
            print("FAIL: cc=[] did not clear the lanes")
            return 1
        ok("set_clip_cc", {"track": ti, "scene": 0, "cc": LANES})

        # --- validation: reject, don't half-apply ---
        for bad, why in (
            ({"cc": [{"n": 200, "pts": [[0.0, 0.5]]}]},          "controller number out of range"),
            ({"cc": [{"n": 1, "ch": 99, "pts": [[0.0, 0.5]]}]},  "channel out of range"),
            ({"cc": "nope"},                                      "cc not an array"),
            ({"cc": [{"n": i, "pts": [[0.0, 0.5]]} for i in range(20)]}, "more than 16 lanes"),
        ):
            r = call("set_clip_cc", {"track": ti, "scene": 0, **bad})
            if r.get("ok"):
                print(f"FAIL: {why} should have been rejected -> {r}")
                return 1
        if len(lanes_of(ti, 0)) != 2:
            print("FAIL: a rejected write left the lanes modified")
            return 1

        # --- save + reload ---
        proj = td / "cc_project.vivid"
        ok("save_project", {"path": str(proj)})
        ok("load_project", {"path": str(proj)})
        after = lanes_of(ti, 0)
        if len(after) != 2 or sorted(l["n"] for l in after) != [1, 129]:
            print(f"FAIL: lanes did not survive save+reload -> {after}")
            return 1
        if len(after[0]["pts"]) != 3:
            print(f"FAIL: breakpoints lost across save+reload -> {after[0]}")
            return 1

        # --- pool stash + place carries the lanes ---
        st = ok("pool_stash", {"track": ti, "scene": 0})
        if lanes_of(ti, 0):
            print("FAIL: stashing should empty the grid cell's lanes")
            return 1
        ok("pool_place", {"index": st["index"], "track": ti, "scene": 0})
        back = lanes_of(ti, 0)
        if len(back) != 2:
            print(f"FAIL: pool stash+place dropped the automation -> {back}")
            return 1

        print("ok   test_clip_cc — lanes round-trip, survive note edits/transforms/save/pool, "
              "undo restores them, bad input rejected")
        return 0
    finally:
        tmp.cleanup()
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
