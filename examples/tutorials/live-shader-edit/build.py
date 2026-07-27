#!/usr/bin/env python3
"""ADR-0034 follow-up: pressure-test live editing of a project-local CustomShader file."""

from __future__ import annotations

import importlib.util
import json
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
DEMOS = REPO / "examples" / "demos"
FIRST = REPO / "examples" / "tutorials" / "mcp-native-first-project" / "build.py"
sys.path.insert(0, str(DEMOS))

from vivid_demo import Vivid, find  # noqa: E402


def load_first_project_constants():
    spec = importlib.util.spec_from_file_location("mcp_native_first_project_build", FIRST)
    if spec is None or spec.loader is None:
        raise SystemExit(f"Could not import first tutorial builder at {FIRST}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


FIRST_BUILD = load_first_project_constants()
PROJECT = FIRST_BUILD.PROJECT
SHADER_PATH = FIRST_BUILD.SHADER_PATH
SHADER_REF = FIRST_BUILD.SHADER_REF
PROOF_JSON = PROJECT / "live-edit-proof.json"
BEFORE_PNG = PROJECT / "live-edit-before.png"
AFTER_EDIT_PNG = PROJECT / "live-edit-after-file-edit.png"
AFTER_RELOAD_PNG = PROJECT / "live-edit-after-reload-project-files.png"


EDIT_MARKER = "// live-shader-edit tutorial mutation"


def require_control_server(v: Vivid) -> None:
    try:
        v.call("status")
    except Exception as exc:
        raise SystemExit(
            "Vivid must be running before pressure-testing live shader edit. "
            f"Could not reach the control server at {v.base}: {exc}"
        ) from exc


def mutate_shader() -> None:
    if not SHADER_PATH.exists():
        raise SystemExit(
            f"Missing {SHADER_PATH}. Run examples/tutorials/mcp-native-first-project/build.py first."
        )
    src = SHADER_PATH.read_text()
    if EDIT_MARKER in src:
        return
    src = src.replace(
        "vec3 b = vec3(0.08, 0.12, 0.18);",
        "vec3 b = vec3(0.02, 0.18, 0.14);",
    )
    src = src.replace(
        "color *= vignette * (0.65 + u_glow * 1.2);",
        "color *= vignette * (0.85 + u_glow * 1.9);\n    " + EDIT_MARKER,
    )
    SHADER_PATH.write_text(src)


def custom_shader_node(v: Vivid) -> int:
    nodes = v.graph()["nodes"]
    node_id = find(nodes, "CustomShader")
    if node_id is None:
        raise SystemExit("Loaded project has no CustomShader node.")
    return node_id


def call_optional(v: Vivid, method: str, **payload) -> dict | None:
    try:
        return v.call(method, **payload)
    except RuntimeError as exc:
        print(f"[warn] {method} skipped: {exc}")
        return None


def build() -> None:
    v = Vivid()
    require_control_server(v)

    if not (PROJECT / "project.json").exists():
        raise SystemExit(
            f"Missing first tutorial project at {PROJECT}. "
            "Run examples/tutorials/mcp-native-first-project/build.py first."
        )

    v.load_project(str(PROJECT))
    v.launch_scene(0)
    v.play()
    time.sleep(1.0)

    custom_shader_node(v)
    proof: dict[str, object] = {
        "project": str(PROJECT),
        "shader": SHADER_REF,
        "before": call_optional(v, "capture_frame", path=str(BEFORE_PNG)),
    }

    mutate_shader()
    time.sleep(1.0)
    proof["after_file_edit"] = call_optional(v, "capture_frame", path=str(AFTER_EDIT_PNG))

    proof["reload_project_files"] = call_optional(v, "reload_project_files")
    time.sleep(1.0)
    proof["after_reload_project_files"] = call_optional(v, "capture_frame", path=str(AFTER_RELOAD_PNG))
    proof["quality_after_reload_project_files"] = call_optional(v, "run_quality_check", name="all")

    PROOF_JSON.write_text(json.dumps(proof, indent=2) + "\n")
    print(f"live shader edit proof -> {PROOF_JSON}")


if __name__ == "__main__":
    build()
