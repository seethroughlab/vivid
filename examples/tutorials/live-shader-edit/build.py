#!/usr/bin/env python3
"""ADR-0040 tutorial tier 2 — Live Shader Edit (a first-class beginner walkthrough).

Run with the Vivid app already open:

    uv run examples/tutorials/live-shader-edit/build.py

This is the shader-focused sibling of `mcp-native-first-project`. It is self-contained: it scaffolds
its OWN tiny shader-only project (no synth, so no Surge XT prerequisite), then teaches the
creative-coding loop that is Vivid's public promise:

  1. discover the shader operator and its backing file over MCP (no guessing);
  2. edit the .wgsl and reload it live, keeping the node's identity;
  3. break the shader on purpose and recover using Vivid's own diagnostics;
  4. verify the output and save a reusable project.

It writes `project/live-edit-proof.json` and `project/FRICTION-LOG.md`.
"""

from __future__ import annotations

import json
import shutil
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
DEMOS = REPO / "examples" / "demos"
sys.path.insert(0, str(DEMOS))

from vivid_demo import Vivid, call_optional, find, require_control_server  # noqa: E402

PROJECT = HERE / "project"
SHADER_OP = "PulseField"
SHADER_NAME = "pulse_field.wgsl"
SHADER_PATH = PROJECT / "shaders" / SHADER_NAME
PROOF_JSON = PROJECT / "live-edit-proof.json"
FRICTION_LOG = PROJECT / "FRICTION-LOG.md"
BEFORE_PNG = PROJECT / "live-edit-before.png"
AFTER_EDIT_PNG = PROJECT / "live-edit-after-edit.png"
BROKEN_PNG = PROJECT / "live-edit-broken.png"
RECOVERED_PNG = PROJECT / "live-edit-recovered.png"

# A self-animating (u.time) project-local shader. No audio needed — the field moves on its own, so
# the tutorial can focus purely on the code->reload->pixels loop.
BASE_SHADER = """/*{
  "version": 1,
  "name": "PulseField",
  "summary": "Project-local pulse field for the live-shader-edit tutorial.",
  "keywords": ["project", "tutorial", "generator", "pulse"],
  "inputs": [],
  "params": [
    {"name": "warp", "type": "float", "default": 0.35, "min": 0, "max": 1,
     "semantic_intent": "domain warp amount"},
    {"name": "hue", "type": "float", "default": 0.58, "min": 0, "max": 1,
     "semantic_tag": "phase_01", "semantic_intent": "color hue"},
    {"name": "density", "type": "float", "default": 0.42, "min": 0, "max": 1,
     "semantic_intent": "pattern density"},
    {"name": "glow", "type": "float", "default": 0.55, "min": 0, "max": 1,
     "semantic_intent": "brightness"}
  ]
}*/
fn ring(uv: vec2f, center: vec2f, radius: f32, width: f32) -> f32 {
    let d = abs(distance(uv, center) - radius);
    return 1.0 - smoothstep(0.0, width, d);
}

@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let uv = inp.uv;
    var p = uv * 2.0 - vec2f(1.0);
    p.x = p.x * (u.res.x / max(1.0, u.res.y));

    let twist = sin((p.x + p.y) * (3.0 + u.density * 10.0) + u.time * 1.8);
    let q = p + vec2f(cos(p.y * 4.0 + u.time), sin(p.x * 4.0 - u.time)) * (0.04 + u.warp * 0.18);

    let beam = 0.5 + 0.5 * sin((q.x * 5.0 + q.y * 2.0) + twist * 1.4 + u.time * 2.0);
    let pulse = ring(uv, vec2f(0.5), 0.16 + u.density * 0.24, 0.08);
    let vignette = smoothstep(1.3, 0.15, length(p));

    let a = 0.5 + 0.5 * cos(vec3f(0.0, 2.1, 4.2) + u.hue * 6.2831853);
    let b = vec3f(0.08, 0.12, 0.18);
    var color = mix(b, a, beam * 0.65 + pulse * 0.55);
    color = color * vignette * (0.65 + u.glow * 1.2);
    return vec4f(color, 1.0);
}
"""

# STEP 2 edit — a visible creative change: shift the base tint and push brightness up.
EDIT_MARKER = "// live-shader-edit tutorial mutation"
BRIGHT_EDIT = BASE_SHADER.replace(
    "let b = vec3f(0.08, 0.12, 0.18);",
    "let b = vec3f(0.02, 0.18, 0.14);  " + EDIT_MARKER,
).replace(
    "color = color * vignette * (0.65 + u.glow * 1.2);",
    "color = color * vignette * (1.05 + u.glow * 1.9);",
)

# STEP 3 recovery — a deliberate WGSL COMPILE error (undeclared identifier). The header still parses,
# so the operator stays registered; the body fails to compile, which is exactly the failure Vivid now
# names per-node (ADR-0040 Phase 3). We fix it right after.
BROKEN_EDIT = BASE_SHADER.replace(
    "return vec4f(color, 1.0);",
    "return vec4f(color * this_symbol_does_not_exist, 1.0);",
)


def visual_op_issues(report: dict | None, op: str) -> list[dict]:
    """The validate_project issues that name this visual operator (broken/unregistered)."""
    if not report:
        return []
    return [
        i for i in report.get("issues", [])
        if i.get("op") == op and "visual operator" in str(i.get("issue", ""))
    ]


def catalog_source(v: Vivid, op: str) -> dict | None:
    """Ask the operator catalog what a beginner sees: does PulseField reveal its backing file?"""
    cat = call_optional(v, "find_operators", query=op, domain="visual")
    if not cat:
        return None
    for entry in cat.get("matches", []):
        if entry.get("name") == op:
            return {"format": entry.get("format"), "source": entry.get("source")}
    return None


def shader_entry(v: Vivid, op: str) -> dict | None:
    """The authoritative name -> path -> error row from the shader library."""
    shaders = call_optional(v, "list_shaders")
    if not shaders:
        return None
    for e in shaders.get("shaders", shaders.get("entries", [])):
        if e.get("name") == op:
            return e
    return None


def build() -> None:
    v = Vivid()
    require_control_server(v, "the live-shader-edit walkthrough")

    # --- Fresh, self-contained project (no synth prerequisite) --------------------------------
    if PROJECT.exists():
        shutil.rmtree(PROJECT)
    PROJECT.mkdir(parents=True, exist_ok=True)

    v.reset()
    v.bpm(96)
    v.save_project(str(PROJECT))   # folder context first, so project-relative assets resolve

    scaffold = v.call(
        "scaffold_project_shader_operator",
        name=SHADER_OP, filename=SHADER_NAME, source=BASE_SHADER, overwrite=True,
    )
    if not scaffold.get("registered", False):
        raise RuntimeError(f"project shader did not register: {scaffold}")

    out = find(v.graph()["nodes"], "Output")
    shader = v.add_node(SHADER_OP)
    blur = v.add_node("Blur")
    v.set_node_param(blur, "radius", 0.08)
    v.connect(blur, shader)
    if out is not None:
        v.connect(out, blur)
        v.set_active_output(out)
    v.save_project(str(PROJECT))

    # Acceptance-test the portable artifact, not the live authoring session.
    v.load_project(str(PROJECT))
    v.launch_scene(0)
    v.play()
    time.sleep(1.0)

    proof: dict[str, object] = {"project": str(PROJECT), "shader": str(SHADER_PATH), "op": SHADER_OP}

    # --- STEP 1: DISCOVER (node -> operator -> file) over MCP, no guessing --------------------
    node_id = find(v.graph()["nodes"], SHADER_OP)
    proof["discover"] = {
        "node_id": node_id,
        "catalog": catalog_source(v, SHADER_OP),   # find_operators now carries source {path,tier}
        "shader_library_entry": shader_entry(v, SHADER_OP),
    }
    print(f"[discover] {SHADER_OP} node={node_id} catalog_source={proof['discover']['catalog']}")

    # --- STEP 2: EDIT the shader file and reload live, keeping node identity -------------------
    proof["before"] = call_optional(v, "capture_frame", path=str(BEFORE_PNG))
    SHADER_PATH.write_text(BRIGHT_EDIT)
    proof["reload_after_edit"] = call_optional(v, "reload_project_files")
    time.sleep(0.8)
    # capture_frame both saves the PNG and returns is_blank/brightness (it renders on demand).
    proof["after_edit"] = call_optional(v, "capture_frame", path=str(AFTER_EDIT_PNG))
    proof["edit_delta"] = call_optional(
        v, "compare_frames", a={"path": str(BEFORE_PNG)}, b={"path": str(AFTER_EDIT_PNG)})
    node_after = find(v.graph()["nodes"], SHADER_OP)
    proof["node_identity_preserved"] = (node_after == node_id and node_after is not None)
    print(f"[edit] node stable={proof['node_identity_preserved']} "
          f"nonblank={not (proof['after_edit'] or {}).get('is_blank', True)} "
          f"nodes_rebuilt={(proof['reload_after_edit'] or {}).get('shader_nodes_rebuilt')}")

    # --- STEP 3: BREAK on purpose, then RECOVER using Vivid's diagnostics ----------------------
    SHADER_PATH.write_text(BROKEN_EDIT)
    proof["reload_after_break"] = call_optional(v, "reload_project_files")
    time.sleep(0.8)
    # Force a render first: the WGSL body compiles on first draw, so capture before we ask for health.
    proof["broken_capture"] = call_optional(v, "capture_frame", path=str(BROKEN_PNG))
    broken_validate = call_optional(v, "validate_project")
    proof["broken_validate_issues"] = visual_op_issues(broken_validate, SHADER_OP)
    proof["broken_shader_entry"] = shader_entry(v, SHADER_OP)
    proof["broken_signal_flow"] = (call_optional(v, "inspect_signal_flow") or {}).get(
        "visuals", {}).get("broken_ops")
    print(f"[break] validate reported {len(proof['broken_validate_issues'])} visual issue(s) for {SHADER_OP}")

    SHADER_PATH.write_text(BRIGHT_EDIT)   # fix it
    proof["reload_after_fix"] = call_optional(v, "reload_project_files")
    time.sleep(0.8)
    proof["recovered"] = call_optional(v, "capture_frame", path=str(RECOVERED_PNG))   # render before validate
    fixed_validate = call_optional(v, "validate_project")
    proof["fixed_validate_issues"] = visual_op_issues(fixed_validate, SHADER_OP)
    proof["quality_after_recovery"] = call_optional(v, "run_quality_check", name="all")
    print(f"[recover] validate visual issues now={len(proof['fixed_validate_issues'])} "
          f"nonblank={not (proof['recovered'] or {}).get('is_blank', True)}")

    v.save_project(str(PROJECT))
    PROOF_JSON.write_text(json.dumps(proof, indent=2) + "\n")
    write_friction_log(proof)
    print(f"live shader edit proof -> {PROOF_JSON}")

    # Hard, deterministic acceptance checks (evidence-based, not timing-sensitive).
    assert proof["node_identity_preserved"], "reload changed the shader node's identity"
    assert proof["broken_validate_issues"], "a broken shader was not reported by validate_project"
    assert not proof["fixed_validate_issues"], "validate_project still reports a broken op after fix"
    assert not (proof["recovered"] or {}).get("is_blank", True), "output blank after recovery"
    print("ACCEPTED: discover + edit + break/recover loop verified")


def write_friction_log(proof: dict) -> None:
    disc = proof.get("discover", {}) or {}
    cat = disc.get("catalog") or {}
    FRICTION_LOG.write_text(
        "# Friction Log — Live Shader Edit\n\n"
        "Self-contained (no Surge XT / synth prerequisite): the tutorial scaffolds its own\n"
        "shader-only project, so a beginner's first live shader edit needs nothing installed.\n\n"
        "- Discover node -> operator -> file: `find_operators` / `list_operator_catalog` now carry\n"
        f"  `source` for shader-backed ops (here: `{(cat.get('source') or {}).get('path')}`),\n"
        "  and `list_shaders` remains the authoritative name->path->error row. Before ADR-0040 tier 2\n"
        "  the catalog exposed no backing file, so a beginner had to know to call `list_shaders`.\n"
        "- Live edit: editing `shaders/pulse_field.wgsl` + `reload_project_files` re-registers the\n"
        f"  project shader tier while `{SHADER_OP}` keeps its node identity"
        f" (preserved={proof.get('node_identity_preserved')}).\n"
        "- Failure/recovery mode covered (Fulfillment Gate #8): a deliberate WGSL compile error is\n"
        "  named per-node by `validate_project` (issue 'visual operator failed to compile', with the\n"
        "  error text + a reload_project_files next-action) and by `inspect_signal_flow.broken_ops`;\n"
        "  fixing the file + reload clears it.\n"
        "- Visual verification: `analyze_frame` (is_blank/brightness) + `compare_frames` (hash_hamming)\n"
        "  record the change. Deltas are evidence, not hard asserts — the field self-animates, so\n"
        "  frame-to-frame differences are expected regardless of the edit.\n"
        "- Launch note: use `VIVID_DISCARD_RECOVERY=1` for a disposable tutorial run.\n"
    )


if __name__ == "__main__":
    build()
