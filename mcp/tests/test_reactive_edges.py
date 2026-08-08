#!/usr/bin/env python3
"""ADR-0053 Phase B (control-edge) integration guards, via the control server (a real app instance).

Two things the headless suite can't reach (they need the live VisualGraph / GPU):

  A. LARGE-GRAPH NO-CRASH — authoring an audio->visual mapping now materializes a Reactive SOURCE op
     (VisualGraph::add_node) live. The known live-topology fragility is adding a node to a big running
     graph; this builds a 30+ node graph, then adds a ReactiveMaster via connect_mapping and asserts the
     app survives with the edge in place.

  B. TRACK-DROP EDGE REMOVAL — a track's reactivity is a ReactiveTrack node + control edges. Deleting the
     track must remove that node AND cascade-drop its edges, leaving nothing dangling.

Run standalone (`VIVID_APP=... uv run mcp/tests/test_reactive_edges.py`). SKIPs (not fails) when the GUI
app can't come up (no window server / GPU), mirroring test_persist_roundtrip.
"""
import json
import os
import subprocess
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = int(os.environ.get("VIVID_ROUNDTRIP_PORT", "9879"))
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


def graph() -> dict:
    return ok("get_graph")


def reactive_nodes(g: dict) -> list:
    return [n for n in g["nodes"] if n["op"].startswith("Reactive")]


def edges_of(g: dict, node_id: int) -> list:
    n = next((x for x in g["nodes"] if x["id"] == node_id), None)
    return (n or {}).get("control_edges", []) if n else []


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

        # ---- A. Large-graph no-crash: build 30 nodes, then add a ReactiveMaster via connect_mapping ----
        ok("new_project")
        ids = [ok("add_node", {"op": "Shape"})["id"] for _ in range(30)]
        target = ids[0]
        pname = next(p["name"] for p in next(n for n in graph()["nodes"] if n["id"] == target)["params"])
        ok("connect_mapping", {"src": "master.low", "dst": f"node:{target}.{pname}",
                               "amount": 0.5, "lo": 0.1, "hi": 0.9})
        if proc.poll() is not None:
            print(f"FAIL: app crashed adding a ReactiveMaster to a 30-node graph (code {proc.returncode})")
            return 1
        g = graph()
        masters = [n for n in reactive_nodes(g) if n["op"] == "ReactiveMaster"]
        if len(masters) != 1:
            print(f"FAIL: expected exactly 1 ReactiveMaster after connect_mapping, got {len(masters)}")
            return 1
        if not edges_of(g, target):
            print("FAIL: connect_mapping on a large graph did not create a control edge")
            return 1
        print(f"ok   A large-graph: 30 nodes + ReactiveMaster added live, edge on node {target}, no crash")

        # ---- B. Track-drop removes the ReactiveTrack node + cascades its edges ----
        ti = ok("add_track", {"kind": "instrument", "instrument": "TestTone"})
        track = ti.get("track", ti.get("index", 0))
        t2 = ids[1]
        p2 = next(p["name"] for p in next(n for n in graph()["nodes"] if n["id"] == t2)["params"])
        ok("map_audio_to_visual_param", {"source": "track", "track": track,
                                         "characteristic": "level", "node_id": t2, "param": p2})
        g = graph()
        rtracks = [n for n in reactive_nodes(g) if n["op"] == "ReactiveTrack"]
        if len(rtracks) != 1 or not edges_of(g, t2):
            print(f"FAIL: track mapping did not create a ReactiveTrack + edge "
                  f"(reactive tracks {len(rtracks)}, edges {len(edges_of(g, t2))})")
            return 1

        ok("remove_track", {"track": track})
        if proc.poll() is not None:
            print(f"FAIL: app crashed removing a track that fed a control edge (code {proc.returncode})")
            return 1
        g = graph()
        if any(n["op"] == "ReactiveTrack" for n in g["nodes"]):
            print("FAIL: ReactiveTrack node survived its track's deletion")
            return 1
        if edges_of(g, t2):
            print("FAIL: control edge from a deleted track's ReactiveTrack was not cascaded away")
            return 1
        print("ok   B track-drop: ReactiveTrack node + its control edge removed, no crash")

        print("ok   test_reactive_edges — large-graph add + track-drop edge removal both hold")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


if __name__ == "__main__":
    raise SystemExit(main())
