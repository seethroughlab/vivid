#!/usr/bin/env python3
"""Generate minimal per-operator EXAMPLE projects from the audit scaffold (ADR-0021 + ADR-0054).

Reuses the ADR-0042 audit scaffold (`scaffolds.build_scaffold` — it already builds a minimal
renderable graph for any operator from its ports) and, instead of capturing a preview PNG, SAVES it
as a browsable folder project under `examples/operators/<Op>/`. This turns the ephemeral audit
fixtures into a real "here's operator X in isolation" set (the gap: `examples/demos/projects/` holds
only polished demo compositions, not per-operator demos).

Portability by design: examples are built from the scaffold's **canonical sources only** (Shape3D,
etc.) — NO external asset paths (no test-card/glTF), so a saved project opens on any machine. An op
that only renders once seeded with a texture / mesh / live signal renders blank here and is SKIPPED
(logged as `needs-input`) — those get a seeded generator pass later.

For an operator NOT in the lean core (ADR-0054), the op's package is COPIED into the example folder,
so `load_project` compiles + registers it on open (project-local operators, `project_io.cpp:83`).
The example both DEMONSTRATES the op and CARRIES it — no dependency on the default install.

Prereq: launch the app by DIRECT binary path with a control port (a fresh, lean instance):
    VIVID_PORT=9877 app/build/vivid.app/Contents/MacOS/vivid &
Then:
    uv run tools/operator_audit/gen_examples.py            # every renderable visual op
    uv run tools/operator_audit/gen_examples.py Render3D   # explicit op(s)
"""
import os
import sys
import time
import shutil
import subprocess

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

SKIP = {"Output"}     # the sink node, not a content op to demonstrate
SETTLE = 0.4          # seconds to let the graph render before the blank check

# macOS pauses a fully-occluded window's render loop (App Nap) -> blank frames. Keep Vivid frontmost
# (same workaround as capture_previews.py / audit.py).
_FG = ['osascript', '-e',
       'tell application "System Events" to set frontmost of (first process whose name is "vivid") to true']


def foreground():
    try:
        subprocess.run(_FG, capture_output=True, timeout=5)
    except Exception:
        pass


def rendered(a: dict) -> bool:
    """Non-blank in the audit sense (mirrors audit.py / capture_previews.py::rendered)."""
    return (not a.get("is_blank", True)
            or a.get("brightness", 0.0) > 0.02
            or a.get("contrast", 0.0) > 0.01
            or a.get("color_spread", 0.0) > 0.02)


def find_output(v) -> int:
    for n in v.graph()["nodes"]:
        if (n.get("type") or n.get("op")) == "Output":
            return n["id"]
    raise RuntimeError("no Output node in a fresh session")


def carry_package(pkg_dir: str, op_name: str, dst: str):
    """Carry ONLY `op_name` from a (possibly multi-op) source package into the example folder as a
    single-op project-local package: a minimal manifest + just that op's source + any vendor/ headers.
    (A shared package like content-visual holds several ops; an example should carry only its own.)"""
    import json
    src_manifest = json.load(open(os.path.join(pkg_dir, "vivid-package.json")))
    entry = next(o for o in src_manifest["operators"] if o["name"] == op_name)
    # minimal single-op manifest (keep vendor deps — the vendored headers travel with it)
    out = {"name": op_name, "version": src_manifest.get("version", "0.1.0"), "operators": [entry]}
    if "dependencies" in src_manifest:
        out["dependencies"] = src_manifest["dependencies"]
    os.makedirs(dst, exist_ok=True)
    json.dump(out, open(os.path.join(dst, "vivid-package.json"), "w"), indent=2)
    # just this op's source
    shutil.copy2(os.path.join(pkg_dir, entry["source"]), os.path.join(dst, entry["source"]))
    # vendored headers (shared across the package's ops) if declared
    for dep in src_manifest.get("dependencies", {}).get("vendor", []):
        inc = dep.get("include")
        if inc and os.path.isdir(os.path.join(pkg_dir, inc)):
            shutil.copytree(os.path.join(pkg_dir, inc), os.path.join(dst, inc), dirs_exist_ok=True)


def gen_one(v, op) -> str:
    """Returns a status string prefixed with the outcome: saved / skip-* / error."""
    name = op["name"]
    dst = os.path.join(EXAMPLES, name)
    pkg = PACKAGE_FOR.get(name)

    v.call("new_project")
    out = find_output(v)
    try:
        _op_node, terminal = scaffolds.build_scaffold(v, op, scaffolds.Sources(v))
    except Exception as e:
        return f"error   {name}: scaffold: {str(e)[:90]}"
    if terminal is None:
        return f"skip    {name}: no GPU terminal (audio/unknown)"
    try:
        v.connect(out, terminal, 0, 0)
    except Exception as e:
        return f"error   {name}: wire: {str(e)[:90]}"

    # Only keep examples that actually render self-contained (no seeded texture/mesh/signal).
    time.sleep(SETTLE)
    a = v.call("analyze_frame").get("analysis", {})
    if not rendered(a):
        foreground(); time.sleep(SETTLE + 0.2)
        a = v.call("analyze_frame").get("analysis", {})
    if not rendered(a):
        return f"skip    {name}: needs-input (blank self-contained)"

    os.makedirs(dst, exist_ok=True)
    v.save_project(dst)
    if pkg:
        carry_package(pkg, name, dst)
    return f"saved   {name}{' (+pkg)' if pkg else ''}"


def visual_ops(v):
    return [o for o in v.call("list_operators")["operators"] if o["name"] not in SKIP]


def main(argv):
    v = Vivid(int(os.environ.get("VIVID_PORT", "9877")))

    # Ensure moved-out ops are registered in THIS instance so we can build their graphs.
    for _name, pkg in PACKAGE_FOR.items():
        try:
            v.call("install_operator_package", path=pkg)
        except Exception as e:
            print(f"warn    install {os.path.basename(pkg)}: {str(e)[:80]}")

    catalog = {o["name"]: o for o in v.call("list_operators")["operators"]}
    if argv:
        ops = [catalog[n] for n in argv if n in catalog]
        missing = [n for n in argv if n not in catalog]
        for n in missing:
            print(f"error   {n}: not in catalog")
    else:
        ops = [o for o in catalog.values() if o["name"] not in SKIP]

    saved = skipped = errored = 0
    for op in sorted(ops, key=lambda o: o["name"]):
        line = gen_one(v, op)
        print(line)
        tag = line.split()[0]
        saved += tag == "saved"
        skipped += tag == "skip"
        errored += tag == "error"
    print(f"\n== {saved} saved, {skipped} skipped, {errored} errored ==")
    return 1 if errored else 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))
