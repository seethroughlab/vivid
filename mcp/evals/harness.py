"""Eval harness core (P4.6): the transports an agent drives + the conversation loop.

A *transport* is just `post(method, payload) -> dict`, the same contract as the MCP
bridge's _post. RealTransport speaks HTTP to the running control server; MockTransport
simulates a tiny slice of it so the harness (and the `fake` provider) run with no app and
no API keys — that's what makes the eval verifiable in CI.
"""
from __future__ import annotations

import json
import urllib.request
from dataclasses import dataclass, field


@dataclass
class Step:
    tool: str
    args: dict
    result: dict


@dataclass
class Transcript:
    steps: list[Step] = field(default_factory=list)
    final_answer: str = ""

    def tools_called(self) -> list[str]:
        return [s.tool for s in self.steps]


class RealTransport:
    """POST /<method> to the live control server (default port 9876 / $VIVID_PORT)."""
    def __init__(self, base_url: str):
        self.base_url = base_url.rstrip("/")

    def post(self, method: str, payload: dict | None = None) -> dict:
        data = json.dumps(payload or {}).encode()
        req = urllib.request.Request(f"{self.base_url}/{method}", data=data,
                                     headers={"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=15) as r:
            return json.loads(r.read().decode())


class MockTransport:
    """A dict-backed simulation of just enough control-server surface for the cases:
    add_node / connect_nodes / set_node_param mutate an in-memory graph that get_session
    reflects. Everything else returns a canned ok. Lets graders assert end state offline."""
    def __init__(self):
        self._next_id = 1
        self.nodes: list[dict] = []   # {id, op, input, params}

    def post(self, method: str, payload: dict | None = None) -> dict:
        b = payload or {}
        if method == "status":
            return {"ok": True, "ops": len(self.nodes), "op_types": ["Plasma", "Tint", "Output"]}
        if method == "get_version":
            return {"ok": True, "app_version": "0.0.0-mock", "operator_abi": 10, "session_schema": 1}
        if method == "get_health":
            return {"ok": True, "health": {"severity": "ok"}}
        if method == "list_operators":
            return {"ok": True, "operators": [
                {"name": "Plasma", "gpu": True}, {"name": "Tint", "gpu": True},
                {"name": "Output", "gpu": True}]}
        if method == "add_node":
            nid = self._next_id; self._next_id += 1
            self.nodes.append({"id": nid, "op": b.get("op_type", "?"), "input": -1, "params": {}})
            return {"ok": True, "id": nid}
        if method == "connect_nodes":
            for n in self.nodes:
                if n["id"] == b.get("node_id"):
                    n["input"] = b.get("input_id", -1)
            return {"ok": True}
        if method == "set_node_param":
            for n in self.nodes:
                if n["id"] == b.get("node_id"):
                    n["params"][str(b.get("index", 0))] = b.get("value", 0.0)
            return {"ok": True}
        if method == "get_session":
            return {"ok": True, "graph": {"chain": list(self.nodes)}}
        return {"ok": True}


def run_loop(provider, transport, goal: str, max_steps: int = 12) -> Transcript:
    """Drive provider <-> transport until the provider answers or max_steps is hit.
    The provider returns either {"tool", "args"} to act or {"answer"} to finish."""
    t = Transcript()
    for _ in range(max_steps):
        action = provider.next_action(goal, t)
        if "answer" in action:
            t.final_answer = action["answer"]
            return t
        tool, args = action["tool"], action.get("args", {})
        result = transport.post(tool, args)
        t.steps.append(Step(tool, args, result))
    return t
