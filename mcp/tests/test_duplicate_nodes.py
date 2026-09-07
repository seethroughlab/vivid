#!/usr/bin/env python3
"""ADR-0033 Phase 2: duplicate_nodes end-to-end over the control server.

Drives the REAL app: builds NoiseField -> Blur -> Output with a tuned param + an audio->param mapping,
then exercises duplicate_nodes and asserts the ADR-0033 contract:
  - copies get FRESH ids (distinct from the originals) and the same op types + param values,
  - an edge strictly BETWEEN copied nodes is recreated,
  - an edge to a node OUTSIDE the copied set is dropped,
  - an incoming audio->param mapping is replicated onto the copy.

Needs the built app + a GPU (drives the shipping binary), like test_persist_roundtrip.py.
Run:  uv run mcp/tests/test_duplicate_nodes.py
"""
import json
import os
import subprocess
import time
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PORT = int(os.environ.get("VIVID_DUP_PORT", "9879"))
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


def node_by_id(graph, nid):
    return next((n for n in graph["nodes"] if n["id"] == nid), None)


def main() -> int:
    if not Path(APP).exists():
        print(f"SKIP: app not built at {APP} (set VIVID_APP)")
        return 0
    env = {**os.environ, "VIVID_PORT": str(PORT), "VIVID_NO_RECOVER": "1", "VIVID_DISCARD_RECOVERY": "1"}
    proc = subprocess.Popen([APP], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    fails: list[str] = []
    try:
        if not wait_up():
            if proc.poll() is not None:
                print(f"FAIL: app exited before the control server came up (code {proc.returncode})")
                return 1
            print("SKIP: control server never came up (no window server / GPU in this environment)")
            return 0

        ok("new_project")
        nf = ok("add_node", {"op": "NoiseField"})["id"]
        bl = ok("add_node", {"op": "Blur"})["id"]
        ok("connect_nodes", {"node_id": bl, "input_id": nf, "port": 0})   # NoiseField -> Blur (internal)
        ok("connect_nodes", {"node_id": 0, "input_id": bl, "port": 0})    # Blur -> Output (external for a copy)

        # Tune the Blur's first param so we can prove params copy across a duplicate.
        g0 = ok("get_graph")
        blur0 = node_by_id(g0, bl)
        pname = blur0["params"][0]["name"]
        ok("set_node_param", {"node_id": bl, "name": pname, "value": 0.42})
        # An incoming audio->param mapping on the Blur (replicated onto the copy).
        ok("connect_mapping", {"src": "master.level", "dst": f"node:{bl}:0", "amount": 0.7})

        # --- (A) duplicate BOTH nodes: internal edge kept, external edge dropped, params + map copied.
        dup = ok("duplicate_nodes", {"ids": [nf, bl]})["ids"]
        if len(dup) != 2:
            fails.append(f"expected 2 new ids, got {dup}")
        if set(dup) & {nf, bl, 0}:
            fails.append(f"new ids overlap originals: {dup}")

        g = ok("get_graph")
        new_nodes = [node_by_id(g, i) for i in dup]
        if any(n is None for n in new_nodes):
            fails.append("a new id is missing from get_graph")
        else:
            ops = sorted(n["op"] for n in new_nodes)
            if ops != ["Blur", "NoiseField"]:
                fails.append(f"copied op types wrong: {ops}")
            new_nf = next((n for n in new_nodes if n["op"] == "NoiseField"), None)
            new_bl = next((n for n in new_nodes if n["op"] == "Blur"), None)
            # internal edge recreated: new Blur's input is the new NoiseField (not the original).
            if new_bl and new_bl.get("input") != new_nf["id"]:
                fails.append(f"internal edge not recreated: new Blur.input={new_bl.get('input')} new NF={new_nf['id']}")
            # external edge dropped: Output(0) still fed by the ORIGINAL Blur, not a copy.
            out = node_by_id(g, 0)
            if out and out.get("input") != bl:
                fails.append(f"Output input changed (external edge leaked): {out.get('input')}")
            # param value copied.
            if new_bl:
                base = next((p["base"] for p in new_bl["params"] if p["name"] == pname), None)
                if base is None or abs(base - 0.42) > 1e-4:
                    fails.append(f"param not copied: new Blur {pname} base={base}")

        # mapping replicated onto the new Blur id.
        if new_bl:
            maps = ok("get_mappings").get("mappings", [])
            if not any(str(new_bl["id"]) in m.get("dst", "") for m in maps):
                fails.append(f"mapping not replicated onto new Blur id {new_bl['id']}: {maps}")

        # --- (B) duplicate the Blur ALONE: its input (the NoiseField, now external) must be dropped.
        dup_b = ok("duplicate_nodes", {"ids": [bl]})["ids"]
        g2 = ok("get_graph")
        solo = node_by_id(g2, dup_b[0]) if dup_b else None
        if solo and solo.get("input", -1) != -1:
            fails.append(f"external edge not dropped on solo duplicate: input={solo.get('input')}")

        if fails:
            for f in fails:
                print("FAIL:", f)
            return 1
        print("ok   test_duplicate_nodes — fresh ids, internal edge kept, external dropped, params + mapping copied")
        return 0
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except Exception:
            proc.kill()


if __name__ == "__main__":
    raise SystemExit(main())
