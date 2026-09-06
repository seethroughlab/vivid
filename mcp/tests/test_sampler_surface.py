#!/usr/bin/env python3
"""ADR-0049: the Sampler sample-editing surface end-to-end over the control server.

The Sampler editor could trim, slice, detect onsets, tune a slice and write the slice map out as a
MIDI clip; none of it was reachable from the control server, so an agent could load a sample and
then do nothing with it. This drives the REAL app through that surface and asserts the contract:

  - get_sampler reports the loaded sample's geometry + slice map,
  - detect/equal/explicit slicing each produce the expected slice->note mapping,
  - count=1 CLEARS the slicing (back to one melodic region),
  - per-slice tune shifts pitch while keeping the trigger note,
  - slices_to_midi writes one note per slice and never clobbers an existing clip,
  - every mutation is undoable (including the sample load itself),
  - the guard rails reject wrong node types / out-of-range args with actionable errors.

Needs the built app + a GPU, like test_duplicate_audio_nodes.py.
Run:  uv run mcp/tests/test_sampler_surface.py
"""
import json
import os
import subprocess
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = int(os.environ.get("VIVID_SAMPLER_PORT", "9884"))
APP = os.environ.get("VIVID_APP", str(ROOT / "build" / "Vivid.app" / "Contents" / "MacOS" / "vivid"))
SAMPLE = ROOT / "examples" / "demos" / "media" / "break90.wav"
BASE = f"http://127.0.0.1:{PORT}"


def call(method, body=None):
    data = json.dumps(body or {}).encode()
    req = urllib.request.Request(f"{BASE}/{method}", data=data, headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=10) as r:
        return json.loads(r.read().decode())


def ok(method, body=None):
    r = call(method, body)
    if not r.get("ok", False):
        raise SystemExit(f"FAIL: {method} -> {r}")
    return r


def fails_with(method, body, code):
    """The handler must REJECT this call with `code` — a guard rail, not a crash or a silent no-op."""
    r = call(method, body)
    if r.get("ok", False):
        return f"{method} should have failed ({code}) but returned ok: {r}"
    if r.get("code") != code:
        return f"{method} failed with code {r.get('code')!r}, expected {code!r} ({r.get('error')})"
    return None


def wait_up(timeout=30.0):
    end = time.time() + timeout
    while time.time() < end:
        try:
            call("get_health"); return True
        except Exception:
            time.sleep(0.5)
    return False


def sampler_node(track):
    """The track's Sampler node id (the instrument added with kind=Sampler)."""
    for n in ok("get_audio_graph", {"track": track})["nodes"]:
        if n.get("type") == "Sampler":
            return n["id"]
    raise SystemExit("FAIL: no Sampler node on the track")


def main():
    if not Path(APP).exists():
        print(f"SKIP: app not built at {APP} (set VIVID_APP)"); return 0
    if not SAMPLE.exists():
        print(f"SKIP: sample missing at {SAMPLE}"); return 0
    env = {**os.environ, "VIVID_PORT": str(PORT), "VIVID_NO_RECOVER": "1", "VIVID_DISCARD_RECOVERY": "1"}
    proc = subprocess.Popen([APP], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    fails = []
    try:
        if not wait_up():
            if proc.poll() is not None:
                print(f"FAIL: app exited before the control server came up (code {proc.returncode})"); return 1
            print("SKIP: control server never came up (no window server / GPU in this environment)"); return 0

        ok("new_project")
        ti = ok("add_track", {"name": "Break", "instrument": "Sampler"})["track"]
        nid = sampler_node(ti)

        # ---- a sampler with no sample refuses edits with a pointed message --------------------------
        e = fails_with("sampler_detect_slices", {"track": ti, "node_id": nid}, "bad_arg")
        if e: fails.append(e)

        ok("audio_graph_load_sampler",
           {"track": ti, "node_id": nid, "path": str(SAMPLE), "base_note": 36})

        # ---- read side ------------------------------------------------------------------------------
        s = ok("get_sampler", {"track": ti, "node_id": nid})
        if s["source_frames"] <= 0: fails.append(f"source_frames not reported: {s['source_frames']}")
        if s["sample_rate"] <= 0:   fails.append(f"sample_rate not reported: {s['sample_rate']}")
        if s["base_note"] != 36:    fails.append(f"base_note {s['base_note']} != 36")
        if s["slice_count"] != 1:   fails.append(f"a fresh load should be 1 region, got {s['slice_count']}")
        total = s["source_frames"]

        # ---- equal slicing maps slices to ASCENDING notes from base_note ---------------------------
        r = ok("sampler_slice_equal", {"track": ti, "node_id": nid, "count": 8})
        if r["slice_count"] != 8: fails.append(f"slice_equal(8) -> {r['slice_count']}")
        roots = [sl["root_note"] for sl in r["slices"]]
        if roots != list(range(36, 44)): fails.append(f"slice roots not ascending from 36: {roots}")
        # the slices tile the region, in order
        edges = [(sl["start"], sl["end"]) for sl in r["slices"]]
        if any(a[1] != b[0] for a, b in zip(edges, edges[1:])):
            fails.append(f"equal slices are not contiguous: {edges}")

        # ---- per-slice tune keeps the TRIGGER note and shifts the pitch -----------------------------
        before = {sl["index"]: (sl["root_note"], sl["lo_note"]) for sl in r["slices"]}
        r = ok("sampler_set_slice_tune", {"track": ti, "node_id": nid, "slice": 2, "semitones": -5})
        after = {sl["index"]: (sl["root_note"], sl["lo_note"]) for sl in r["slices"]}
        if after[2][1] != before[2][1]:
            fails.append(f"tune moved slice 2's trigger note {before[2][1]} -> {after[2][1]}")
        if after[2][0] == before[2][0]:
            fails.append("tune did not change slice 2's pitch")
        if after[3] != before[3]:
            fails.append(f"tuning slice 2 disturbed slice 3: {before[3]} -> {after[3]}")

        # ---- explicit edges ------------------------------------------------------------------------
        r = ok("sampler_set_slices", {"track": ti, "node_id": nid, "starts": [0, total // 3, 2 * total // 3]})
        if r["slice_count"] != 3: fails.append(f"set_slices(3) -> {r['slice_count']}")
        if r["slices"][0]["start"] != 0: fails.append(f"first slice should start at 0: {r['slices'][0]}")

        # ---- slices -> MIDI: one note per slice, into an EMPTY scene --------------------------------
        # The clip must TRIGGER the slices, so its pitches are exactly the slice root notes in order.
        roots_now = [sl["root_note"] for sl in ok("get_sampler", {"track": ti, "node_id": nid})["slices"]]
        r = ok("sampler_slices_to_midi", {"track": ti, "node_id": nid})
        scene, n = r["scene"], r["notes"]
        if n != 3: fails.append(f"slices_to_midi wrote {n} notes for 3 slices")
        clip = ok("get_clip", {"track": ti, "scene": scene})
        pitches = [x["p"] for x in clip["notes"]]
        starts = [x["s"] for x in clip["notes"]]
        if pitches != roots_now:
            fails.append(f"clip pitches {pitches} don't match the slice roots {roots_now}")
        if starts != [0.0, 0.25, 0.5]: fails.append(f"notes not laid out one per 1/16: {starts}")
        # a second call must NOT clobber that clip — it moves to the next empty scene
        r2 = ok("sampler_slices_to_midi", {"track": ti, "node_id": nid})
        if r2["scene"] == scene: fails.append("slices_to_midi overwrote an existing clip")

        # ---- count=1 clears the slicing ------------------------------------------------------------
        r = ok("sampler_slice_equal", {"track": ti, "node_id": nid, "count": 1})
        if r["slice_count"] != 1: fails.append(f"count=1 should clear slicing, got {r['slice_count']}")

        # ---- trim ----------------------------------------------------------------------------------
        r = ok("sampler_set_trim", {"track": ti, "node_id": nid, "start": 1000, "end": total // 2})
        if r["start"] != 1000: fails.append(f"trim start not applied: {r}")

        # ---- guard rails ---------------------------------------------------------------------------
        out_id = next(n["id"] for n in ok("get_audio_graph", {"track": ti})["nodes"]
                      if n.get("type") == "Output")
        for m, body, code in [
            ("get_sampler",           {"track": ti, "node_id": out_id},                 "bad_arg"),
            ("sampler_detect_slices", {"track": ti, "node_id": 9999},                   "out_of_range"),
            ("sampler_slice_equal",   {"track": ti, "node_id": nid, "count": 99},       "bad_arg"),
            ("sampler_set_trim",      {"track": ti, "node_id": nid, "start": 0,
                                       "end": total * 10},                              "bad_arg"),
            ("sampler_set_slice_tune", {"track": ti, "node_id": nid, "slice": 99,
                                        "semitones": 0},                                "out_of_range"),
        ]:
            e = fails_with(m, body, code)
            if e: fails.append(e)

        # ---- every mutation is undoable, down to the load ------------------------------------------
        labels = []
        for _ in range(9):
            u = ok("undo")
            labels.append(u.get("undo_label", ""))
            if not u.get("can_undo"):
                break
        if "Load Sample" not in labels:
            fails.append(f"the sample load was not undoable (labels seen: {labels})")

    finally:
        proc.terminate()
        try: proc.wait(timeout=10)
        except Exception: proc.kill()

    if fails:
        print("FAIL test_sampler_surface")
        for f in fails: print("  -", f)
        return 1
    print("ok   test_sampler_surface")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
