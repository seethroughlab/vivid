#!/usr/bin/env python3
"""Offline unit test for the bridge's read-modify-write retry loop (_rmw).

The optimistic-concurrency contract has two halves: the native compare-and-set (covered end-to-end by
test_clip_conflict.py, which needs the app) and the bridge's retry-on-conflict loop (this file, which
needs neither app nor network — it swaps _post for a fake). It asserts:

  * a clean write is a single get+set with the observed rev handed back as expected_rev,
  * a transient 'conflict' triggers re-read + re-apply + re-write, and the retry lands,
  * a PERSISTENT conflict gives up after the bounded number of tries and surfaces 'conflict'
    (it never loops forever),
  * a get_clip error short-circuits without attempting a write.

Run:  uv run mcp/tests/test_rmw_retry.py
"""
import importlib.util
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def load_bridge():
    spec = importlib.util.spec_from_file_location("vivid_mcp", ROOT / "mcp" / "vivid_mcp.py")
    mod = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(mod)
    return mod


class FakeServer:
    """A one-clip control server: a monotonic rev + a `rival` writer that can bump it between the
    bridge's get and set to force a conflict. `conflict_rounds` = how many times the rival jumps in
    before it stops (so the retry can eventually win)."""
    def __init__(self, notes, conflict_rounds=0):
        self.notes = list(notes)
        self.rev = 1
        self.conflict_rounds = conflict_rounds
        self.gets = 0
        self.sets = 0

    def post(self, method, body=None):
        body = body or {}
        if method == "get_clip":
            self.gets += 1
            # The rival lands its own edit right after we hand out the clip, advancing the rev.
            observed = self.rev
            if self.conflict_rounds > 0:
                self.conflict_rounds -= 1
                self.rev += 1  # someone else wrote; the rev the caller just read is now stale
            return {"ok": True, "notes": list(self.notes), "length": 4.0, "rev": observed}
        if method == "set_clip":
            self.sets += 1
            want = body.get("expected_rev")
            if want is not None and want != self.rev:
                return {"ok": False, "code": "conflict", "error": "stale", "rev": self.rev}
            self.notes = list(body.get("notes", []))
            self.rev += 1
            return {"ok": True, "notes": len(self.notes), "rev": self.rev}
        raise AssertionError(f"unexpected method {method}")


FAILS = []


def check(cond, msg):
    if not cond:
        FAILS.append(msg)


def main() -> int:
    vm = load_bridge()
    add_one = lambda notes, length: notes + [{"p": 60, "s": 0.0, "d": 1.0, "v": 0.8}]

    # 1) Clean write: one get, one set, and the write carries the rev the read observed.
    srv = FakeServer(notes=[], conflict_rounds=0)
    vm._post = srv.post
    res = vm._rmw(0, 0, add_one)
    check(res.get("ok") is True, f"clean write should succeed: {res}")
    check(srv.gets == 1 and srv.sets == 1, f"clean write should be one get+set, got {srv.gets}/{srv.sets}")
    check(len(srv.notes) == 1, f"clean write should land one note, got {srv.notes}")

    # 2) Transient conflict: the rival wins the first round, the retry re-reads and lands.
    srv = FakeServer(notes=[], conflict_rounds=1)
    vm._post = srv.post
    res = vm._rmw(0, 0, add_one)
    check(res.get("ok") is True, f"transient conflict should eventually succeed: {res}")
    check(srv.sets == 2, f"transient conflict should take a second set, got {srv.sets}")

    # 3) Persistent conflict: the rival wins every round -> give up with 'conflict', bounded tries.
    srv = FakeServer(notes=[], conflict_rounds=99)
    vm._post = srv.post
    res = vm._rmw(0, 0, add_one, _tries=3)
    check(res.get("ok") is False and res.get("code") == "conflict",
          f"persistent conflict should surface 'conflict': {res}")
    check(srv.sets == 3, f"persistent conflict should attempt exactly _tries sets, got {srv.sets}")

    # 4) get_clip error short-circuits: no write attempted.
    def failing_post(method, body=None):
        if method == "get_clip":
            return {"ok": False, "code": "out_of_range", "error": "bad scene"}
        raise AssertionError("set_clip should not be reached after a get error")
    vm._post = failing_post
    res = vm._rmw(0, 0, add_one)
    check(res.get("code") == "out_of_range", f"get error should short-circuit: {res}")

    if FAILS:
        for f in FAILS:
            print("FAIL:", f)
        return 1
    print("ok   test_rmw_retry — clean write, transient-conflict retry, bounded give-up, get-error short-circuit")
    return 0


if __name__ == "__main__":
    sys.exit(main())
