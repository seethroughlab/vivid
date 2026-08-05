#!/usr/bin/env python3
"""ADR-0033 Phase 2b: duplicate_audio_nodes end-to-end over the control server.

Drives the REAL app with NATIVE audio ops (deterministic — plugin state loads async and legitimately
differs across runs, so plugin duplication is verified interactively, not here). Builds
instrument -> Bitcrush -> SVFilter -> Output on a track, then duplicates [Bitcrush, SVFilter] and
asserts the ADR-0033 contract:
  - the copies get FRESH ids with the same op types + identical params,
  - the edge strictly BETWEEN the copied nodes is recreated,
  - no edge crosses the copied/uncopied boundary (external edges dropped),
  - undo removes the copies.

Needs the built app + a GPU, like test_persist_roundtrip.py.  Run:  uv run mcp/tests/test_duplicate_audio_nodes.py
"""
import json
import os
import subprocess
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = int(os.environ.get("VIVID_DUPA_PORT", "9881"))
APP = os.environ.get("VIVID_APP", str(ROOT / "build" / "vivid.app" / "Contents" / "MacOS" / "vivid"))
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


def wait_up(timeout=30.0):
    end = time.time() + timeout
    while time.time() < end:
        try:
            call("get_health"); return True
        except Exception:
            time.sleep(0.5)
    return False


def track_graph(ti):
    """The selected track's authoritative audio_graph {nodes:[{id,op,params,...}], edges:[...]}, or {}."""
    sess = ok("get_session")["session"]
    tr = sess.get("tracks", [])[ti]
    return tr.get("audio_graph", {})


def main():
    if not Path(APP).exists():
        print(f"SKIP: app not built at {APP} (set VIVID_APP)"); return 0
    env = {**os.environ, "VIVID_PORT": str(PORT), "VIVID_NO_RECOVER": "1", "VIVID_DISCARD_RECOVERY": "1"}
    proc = subprocess.Popen([APP], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    fails = []
    try:
        if not wait_up():
            if proc.poll() is not None:
                print(f"FAIL: app exited before the control server came up (code {proc.returncode})"); return 1
            print("SKIP: control server never came up (no window server / GPU in this environment)"); return 0

        ok("new_project")
        ti = ok("add_track", {"kind": "instrument", "instrument": "TestTone"}).get("track", 0)
        # Two native effects in series → flips the track authoritative: inst -> Bitcrush -> SVFilter -> Output.
        bc = ok("audio_graph_add_op", {"track": ti, "op": "Bitcrush"})["node"]
        sv = ok("audio_graph_add_op", {"track": ti, "op": "SVFilter"})["node"]
        ok("audio_graph_set_node_param", {"track": ti, "node": bc, "param": 0, "value": 0.33})   # distinctive

        g0 = track_graph(ti)
        n0 = len(g0.get("nodes", []))
        orig_bc = next((n for n in g0["nodes"] if n["id"] == bc), None)
        if not orig_bc:
            fails.append("original Bitcrush not found in get_session audio_graph")

        # Duplicate the two effects.
        dup = ok("duplicate_audio_nodes", {"track": ti, "ids": [bc, sv]})["ids"]
        if len(dup) != 2:
            fails.append(f"expected 2 new ids, got {dup}")
        if set(dup) & {bc, sv}:
            fails.append(f"new ids overlap originals: {dup}")

        g1 = track_graph(ti)
        if len(g1.get("nodes", [])) != n0 + 2:
            fails.append(f"node count {len(g1.get('nodes', []))} != {n0}+2")
        new = set(dup)
        new_nodes = [n for n in g1["nodes"] if n["id"] in new]
        ops = sorted(n["op"] for n in new_nodes)
        if ops != ["Bitcrush", "SVFilter"]:
            fails.append(f"copied op types wrong: {ops}")

        edges = g1.get("edges", [])
        internal = [e for e in edges if e["from"] in new and e["to"] in new]
        crossing = [e for e in edges if (e["from"] in new) != (e["to"] in new)]
        if len(internal) != 1:
            fails.append(f"expected 1 internal edge between the copies, got {internal}")
        if crossing:
            fails.append(f"external edges leaked across the copy boundary: {crossing}")

        # params copied: the cloned Bitcrush's params match the original's exactly (incl. the 0.33 we set).
        new_bc = next((n for n in new_nodes if n["op"] == "Bitcrush"), None)
        if new_bc and orig_bc and new_bc.get("params") != orig_bc.get("params"):
            fails.append(f"params not copied: orig={orig_bc.get('params')} new={new_bc.get('params')}")

        # undo removes the copies.
        ok("undo")
        if len(track_graph(ti).get("nodes", [])) != n0:
            fails.append("undo did not remove the duplicated nodes")

        if fails:
            for f in fails:
                print("FAIL:", f)
            return 1
        print("ok   test_duplicate_audio_nodes — fresh ids, params copied, internal edge kept, external dropped, undo clean")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


if __name__ == "__main__":
    raise SystemExit(main())
