#!/usr/bin/env python3
"""Generate minimal per-operator EXAMPLE projects from the audit scaffold (ADR-0021 + ADR-0054).

PROTOTYPE. Reuses the ADR-0042 audit scaffold (`scaffolds.build_scaffold`) — which already builds a
minimal renderable graph for any operator from its ports — and, instead of capturing a preview PNG,
SAVES it as a browsable folder project under `examples/operators/<Op>/`. This turns the ephemeral
audit fixtures into a real "here's operator X in isolation" set (the gap: today `examples/` holds only
polished demo compositions, not per-operator demos).

For an operator that is NOT in the lean core (shipped as an example package under ADR-0054), the op's
package is COPIED into the example folder, so `load_project` compiles + registers it on open
(project-local operators — `app/src/app/project_io.cpp:83`). The example therefore both DEMONSTRATES
the op and CARRIES it — no dependency on the op being in the default install.

Prereq: launch the app by DIRECT binary path with a control port (a fresh, lean instance):
    VIVID_PORT=9877 app/build/vivid.app/Contents/MacOS/vivid &
Then:
    uv run tools/operator_audit/gen_examples.py                 # the prototype pair
    uv run tools/operator_audit/gen_examples.py Render3D Bloom  # explicit ops
"""
import os
import sys
import shutil

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, os.path.join(HERE, "..", "..", "examples", "demos"))
sys.path.insert(0, HERE)

from vivid_demo import Vivid          # noqa: E402  the control-server client
import scaffolds                      # noqa: E402  the ADR-0042 per-op minimal-graph builder

REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
EXAMPLES = os.path.join(REPO, "examples", "operators")
PKG_ROOT = os.path.join(REPO, "app", "operators", "packages")

# Operators NOT in the lean core (ADR-0054) -> the package dir to carry into the example folder so the
# saved project compiles the op on load. Core ops need no entry.
PACKAGE_FOR = {
    "InstanceNoise": os.path.join(PKG_ROOT, "content-3d"),
    "TimeMachine":   os.path.join(PKG_ROOT, "content-visual"),
    "CosinePalette": os.path.join(PKG_ROOT, "content-visual"),
}

DEFAULT_OPS = ["Render3D", "InstanceNoise"]   # the prototype pair: one core, one packaged content op


def find_output(v) -> int:
    for n in v.graph()["nodes"]:
        if (n.get("type") or n.get("op")) == "Output":
            return n["id"]
    raise RuntimeError("no Output node in a fresh session")


def op_meta(v, name):
    for o in v.call("list_operators")["operators"]:
        if o["name"] == name:
            return o
    return None


def carry_package(pkg_dir: str, dst: str):
    """Copy the op's package (manifest + sources + vendor/) into the example folder, minus build junk."""
    for item in os.listdir(pkg_dir):
        if item in ("build", "__pycache__", ".DS_Store"):
            continue
        s = os.path.join(pkg_dir, item)
        d = os.path.join(dst, item)
        if os.path.isdir(s):
            shutil.copytree(s, d, dirs_exist_ok=True)
        else:
            shutil.copy2(s, d)


def gen_one(v, name: str) -> str:
    dst = os.path.join(EXAMPLES, name)
    pkg = PACKAGE_FOR.get(name)

    # 1. Non-core op: install its package into THIS instance so we can add its node while building.
    if pkg:
        r = v.call("install_operator_package", path=pkg)
        if not r.get("ok", True):
            return f"{name}: install failed: {r}"

    op = op_meta(v, name)
    if op is None:
        return f"{name}: NOT in catalog (core op missing, or package install failed)"

    # 2. Build the minimal scaffold and wire its terminal into Output (mirrors capture_previews).
    v.call("new_project")
    out = find_output(v)
    try:
        _op_node, terminal = scaffolds.build_scaffold(v, op, scaffolds.Sources(v))
    except Exception as e:
        return f"{name}: scaffold-error: {str(e)[:120]}"
    if terminal is None:
        return f"{name}: skip (no GPU-renderable terminal — audio/unknown)"
    try:
        v.connect(out, terminal, 0, 0)
    except Exception as e:
        return f"{name}: wire-error: {str(e)[:120]}"

    # 3. Save as a folder project (writes <dst>/project.json).
    os.makedirs(dst, exist_ok=True)
    v.save_project(dst)

    # 4. Non-core op: carry its package INTO the example folder so load_project compiles it.
    if pkg:
        carry_package(pkg, dst)

    tag = " (+carried package)" if pkg else ""
    return f"{name}: saved -> {os.path.relpath(dst, REPO)}{tag}"


def main(argv):
    names = argv or DEFAULT_OPS
    v = Vivid(int(os.environ.get("VIVID_PORT", "9877")))
    rc = 0
    for n in names:
        line = gen_one(v, n)
        print(line)
        if "error" in line or "NOT in catalog" in line:
            rc = 1
    return rc


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
