#!/usr/bin/env python3
"""Golden save/load round-trip test (audit Code Ph4 P1-03).

The core promise of persistence — user work survives save/reopen — had no automated end-to-end
evidence. This drives the REAL app over the control server: build a project, snapshot the canonical
document (`get_session` == `session_to_json`), `save_project` then `load_project` (the
`session_from_json` restore path), snapshot again, and assert the two documents are identical. A
regression in the restore path (id remap, edge replay, FX-by-name, clip/generator/mapping rebuild)
flips this red instead of shipping silently.

Native ops only (no plugins) so the round-trip is deterministic — a missing/async plugin would
legitimately differ across a reload and is out of scope here.

Needs the built app + a GPU (drives the shipping binary), so it runs on the self-hosted macOS runner /
locally, not in the portable headless CI. Run:  uv run mcp/tests/test_persist_roundtrip.py
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
PORT = int(os.environ.get("VIVID_ROUNDTRIP_PORT", "9878"))
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


def build_project() -> None:
    """A modest but structurally varied NATIVE project: instrument + audio tracks, note clips across
    scenes, a visual op chain with edges, and an audio->visual mapping — enough to exercise id remap,
    edge replay, clip and mapping restore."""
    ok("new_project")
    # Two tracks: a native instrument (TestTone) and an audio track.
    t0 = ok("add_track", {"kind": "instrument", "instrument": "TestTone"})
    t1 = ok("add_track", {"kind": "audio"})
    ti = t0.get("track", t0.get("index", 0))
    ta = t1.get("track", t1.get("index", 1))
    # Note clips on the instrument track across two scenes (varied pitch/timing).
    ok("set_clip", {"track": ti, "scene": 0, "length": 4.0,
                    "notes": [{"p": 60, "s": 0.0, "d": 0.5, "v": 0.9},
                              {"p": 64, "s": 1.0, "d": 0.5, "v": 0.7},
                              {"p": 67, "s": 2.0, "d": 1.0, "v": 0.8}]})
    ok("set_clip", {"track": ti, "scene": 1, "length": 2.0,
                    "notes": [{"p": 48, "s": 0.0, "d": 2.0, "v": 0.6}]})
    # A visual op chain feeding Output: NoiseField -> Blur -> Output(id 0).
    nf = ok("add_node", {"op": "NoiseField"})["id"]
    bl = ok("add_node", {"op": "Blur"})["id"]
    ok("connect_nodes", {"node_id": bl, "input_id": nf, "port": 0})   # NoiseField -> Blur
    ok("connect_nodes", {"node_id": 0, "input_id": bl, "port": 0})    # Blur -> Output
    # An audio->visual mapping with a non-param dst (node:<id>:<index>) — stays a registry object, so it
    # exercises the reverse-path mapping restore. (ADR-0053 B4: a REAL node:<id>.<param> audio mapping now
    # migrates to a control edge instead — covered separately below.)
    ok("connect_mapping", {"src": "master.level", "dst": f"node:{bl}:0", "amount": 0.8, "curve": 0.2})
    # ADR-0053 Phase B4: a real audio->visual mapping to a NAMED param becomes a typed control EDGE from a
    # Reactive SOURCE op (connect_mapping is now sugar over it). Exercises the edge + reactive-node restore
    # path. Query the blur's first param name so the dst is valid regardless of the op's schema.
    blur = next(n for n in ok("get_graph")["nodes"] if n["id"] == bl)
    pname = blur["params"][0]["name"]
    ok("connect_mapping", {"src": "master.low", "dst": f"node:{bl}.{pname}",
                           "amount": 0.6, "curve": 0.1, "lo": 0.1, "hi": 0.9,
                           "attack": 0.02, "release": 0.2})
    # ADR-0033 P5: a per-node label + a sticky note (both persist in the "graph" block; schema v4).
    ok("set_node_name", {"node_id": bl, "name": "Soft Blur"})
    ok("add_annotation", {"x": 720.0, "y": 300.0, "text": "master.level drives the blur"})
    # ADR-0033 P3: a native effect node on the AUDIO track, bypassed — its "bypassed" flag must survive
    # the round-trip (this is what proves bypass persistence end-to-end). On the audio track (no note
    # subgraph) so the assertion is the flag itself, not entangled with note-node id ordering.
    fx = ok("audio_graph_add_op", {"track": ta, "op": "Bitcrush"})["node"]
    ok("set_node_bypass", {"track": ta, "ids": [fx], "bypass": True})


def snapshot() -> dict:
    return ok("get_session")["session"]


def diff(a, b, path="") -> list[str]:
    out = []
    if type(a) is not type(b):
        return [f"{path}: type {type(a).__name__} != {type(b).__name__}"]
    if isinstance(a, dict):
        for k in sorted(set(a) | set(b)):
            if k not in a: out.append(f"{path}.{k}: missing in A")
            elif k not in b: out.append(f"{path}.{k}: missing in B")
            else: out += diff(a[k], b[k], f"{path}.{k}")
    elif isinstance(a, list):
        if len(a) != len(b):
            out.append(f"{path}: len {len(a)} != {len(b)}")
        else:
            for i, (x, y) in enumerate(zip(a, b)):
                out += diff(x, y, f"{path}[{i}]")
    elif a != b:
        out.append(f"{path}: {a!r} != {b!r}")
    return out


def main() -> int:
    if not Path(APP).exists():
        print(f"SKIP: app not built at {APP} (set VIVID_APP)")
        return 0
    env = {**os.environ, "VIVID_PORT": str(PORT), "VIVID_NO_RECOVER": "1", "VIVID_DISCARD_RECOVERY": "1"}
    proc = subprocess.Popen([APP], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        if not wait_up():
            # Distinguish a real crash (fail) from "this environment can't run the GUI app" (skip). The
            # app needs a window server + GPU, which a headless CI job may lack — that's not a persist bug.
            if proc.poll() is not None:
                print(f"FAIL: app exited before the control server came up (code {proc.returncode})")
                return 1
            print("SKIP: control server never came up (no window server / GPU in this environment)")
            return 0
        build_project()
        time.sleep(0.5)
        before = snapshot()
        # Guard: the builder produced varied state, so A==B isn't trivially comparing thin documents.
        g = before.get("graph", {})
        checks = {
            ">=2 tracks": len(before.get("tracks", [])) >= 2,
            "note clips": any(c.get("notes") for t in before.get("tracks", []) for c in t.get("clips", [])),
            "visual op chain (>=2)": len(g.get("chain", [])) >= 2,
            "wired edges": any(n.get("in", -1) >= 0 for n in g.get("chain", [])),
            "a mapping": len(g.get("mappings", [])) >= 1,
            "a control edge": any(n.get("control_edges") for n in g.get("chain", [])),  # ADR-0053 B4
            "a reactive source op": any(n.get("op_type", "").startswith("Reactive")     # ADR-0053 B4
                                        for n in g.get("chain", [])),
            "a node label": any(n.get("name") for n in g.get("chain", [])),   # ADR-0033 P5
            "an annotation": len(g.get("annotations", [])) >= 1,             # ADR-0033 P5
            "a bypassed node": '"bypassed"' in json.dumps(before),           # ADR-0033 P3
        }
        missing = [k for k, v in checks.items() if not v]
        if missing:
            print(f"FAIL: builder did not produce the expected state: {missing}")
            print("  document:", json.dumps(before)[:600])
            return 1
        with tempfile.TemporaryDirectory() as d:
            proj = str(Path(d) / "roundtrip.vivid")
            ok("save_project", {"path": proj})
            ok("new_project")                 # fully clear, so load rebuilds from disk (not a no-op)
            ok("load_project", {"path": proj})
            time.sleep(0.5)
            after = snapshot()
        deltas = diff(before, after)
        if deltas:
            print(f"FAIL: save/load round-trip changed the document ({len(deltas)} diffs):")
            for d in deltas[:40]:
                print("  ", d)
            return 1
        print(f"ok   test_persist_roundtrip — document identical across save/load "
              f"({len(before.get('tracks', []))} tracks, chain {len(g.get('chain', []))}, "
              f"{len(g.get('mappings', []))} mapping(s), {len(json.dumps(before))} bytes)")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


if __name__ == "__main__":
    sys.exit(main())
