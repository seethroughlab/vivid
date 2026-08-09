#!/usr/bin/env python3
"""Optimistic-concurrency contract for clip edits (API-cleanup Phase-7 follow-up b).

The bridge's read-modify-write note tools do get_clip -> transform -> set_clip as two separate control
dispatches. Without a guard, two agents editing the SAME clip interleave (get,get,set,set) and the last
writer silently clobbers the first. The fix: get_clip reports a per-clip `rev`; set_clip takes an optional
`expected_rev` and rejects a stale write with code 'conflict' instead of overwriting. This drives the real
app over the control server and asserts that contract end-to-end:

  * get_clip / set_clip report a monotonically advancing `rev`,
  * a write with the CURRENT rev succeeds,
  * a write with a STALE rev is rejected ('conflict') and does NOT mutate the clip,
  * the conflict reply hands back the current rev (so a caller can re-read + retry),
  * a write with NO expected_rev is unconditional (backward compatible).

Needs the built app + a GPU (drives the shipping binary), so it runs on the self-hosted macOS runner /
locally, not in portable headless CI. Run:  uv run mcp/tests/test_clip_conflict.py
"""
import json
import os
import subprocess
import sys
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = int(os.environ.get("VIVID_CONFLICT_PORT", "9881"))
APP = os.environ.get("VIVID_APP", str(ROOT / "build" / "vivid.app" / "Contents" / "MacOS" / "vivid"))
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


NOTE_A = [{"p": 60, "s": 0.0, "d": 1.0, "v": 0.8}]
NOTE_B = [{"p": 62, "s": 0.0, "d": 1.0, "v": 0.8}]
NOTE_C = [{"p": 64, "s": 0.0, "d": 1.0, "v": 0.8}]


def pitches(track: int, scene: int) -> list:
    return [n["p"] for n in ok("get_clip", {"track": track, "scene": scene})["notes"]]


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
        ok("new_project")
        t = ok("add_track", {"kind": "instrument", "instrument": "TestTone"})
        ti = t.get("track", t.get("index", 0))

        # Seed the clip. set_clip reports the post-write rev.
        r0 = ok("set_clip", {"track": ti, "scene": 0, "length": 2.0, "notes": NOTE_A})["rev"]
        # get_clip reports the same rev (a read doesn't advance it).
        gc = ok("get_clip", {"track": ti, "scene": 0})
        if gc.get("rev") != r0:
            print(f"FAIL: get_clip rev {gc.get('rev')} != set_clip rev {r0}")
            return 1

        # A conditional write with the CURRENT rev succeeds and advances the rev.
        r1 = ok("set_clip", {"track": ti, "scene": 0, "length": 2.0, "notes": NOTE_B, "expected_rev": r0})["rev"]
        if r1 <= r0:
            print(f"FAIL: rev did not advance on a successful write ({r0} -> {r1})")
            return 1
        if pitches(ti, 0) != [62]:
            print(f"FAIL: conditional write did not land: {pitches(ti, 0)}")
            return 1

        # A STALE conditional write (the now-outdated r0) must be REJECTED and must not mutate the clip.
        stale = call("set_clip", {"track": ti, "scene": 0, "length": 2.0, "notes": NOTE_C, "expected_rev": r0})
        if stale.get("ok") is not False or stale.get("code") != "conflict":
            print(f"FAIL: stale write was not rejected with 'conflict': {stale}")
            return 1
        if stale.get("rev") != r1:
            print(f"FAIL: conflict reply did not carry the current rev (got {stale.get('rev')}, want {r1})")
            return 1
        if pitches(ti, 0) != [62]:
            print(f"FAIL: rejected write still mutated the clip: {pitches(ti, 0)}")
            return 1

        # Re-reading the current rev and retrying lands the edit (the caller's reconcile path).
        r2 = ok("set_clip", {"track": ti, "scene": 0, "length": 2.0, "notes": NOTE_C, "expected_rev": r1})["rev"]
        if r2 <= r1 or pitches(ti, 0) != [64]:
            print(f"FAIL: retry after re-read did not land (rev {r1}->{r2}, pitches {pitches(ti, 0)})")
            return 1

        # An UNCONDITIONAL write (no expected_rev) is always accepted — backward compatible.
        ok("set_clip", {"track": ti, "scene": 0, "length": 2.0, "notes": NOTE_A})
        if pitches(ti, 0) != [60]:
            print(f"FAIL: unconditional write did not land: {pitches(ti, 0)}")
            return 1

        print("ok   test_clip_conflict — get/set_clip rev advances; stale expected_rev rejected "
              "(clip untouched); retry lands; unconditional write unaffected")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
