#!/usr/bin/env python3
"""ADR-0040 Golden Path A: build the first MCP-native creative-coding tutorial project.

Run with the Vivid app already open:

    uv run examples/tutorials/mcp-native-first-project/build.py

The script authors a folder project over the control server, using project-local shader code as the
creative material. It assumes Surge XT is installed as the beginner instrument; TestTone remains a
runtime utility, not the public onboarding voice.
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

from vivid_demo import Vivid, find, surge_preset  # noqa: E402

PROJECT = HERE / "project"
SHADER_OP = "PulseField"
SHADER_NAME = "pulse_field.wgsl"
SHADER_PATH = PROJECT / "shaders" / SHADER_NAME
FRICTION_LOG = PROJECT / "FRICTION-LOG.md"
PROOF_JSON = PROJECT / "proof.json"
CAPTURE_PNG = PROJECT / "capture.png"

SHADER_SOURCE = """/*{
  "version": 1,
  "name": "PulseField",
  "summary": "Project-local pulse field driven by audio mappings.",
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


def ensure_friction_log() -> None:
    PROJECT.mkdir(parents=True, exist_ok=True)
    FRICTION_LOG.write_text(
        "# Friction Log\n\n"
        "- UI-only steps: none exercised by the builder. Tutorial prose still needs a human path or\n"
        "  an explicit MCP-client-first framing.\n"
        "- MCP-required steps: reset, save folder project, create track, load Surge XT, add notes,\n"
        "  scaffold a project-local shader operator, create visual graph, discover sources/destinations, map audio\n"
        "  characteristics to visual params, save, reload, capture, validate, and run quality checks.\n"
        "- Confusing or missing diagnostics: stale autosave/recovery state can emit warnings before a\n"
        "  scripted tutorial reset unless Vivid is launched with `VIVID_DISCARD_RECOVERY=1`. The builder\n"
        "  itself preflights the control server before deleting the generated project.\n"
        "- Save/load issues:\n"
        "  - Watch for project-local shader registration/reload behavior. The project shader should\n"
        f"    remain under `shaders/{SHADER_NAME}` and register as `{SHADER_OP}` after reload.\n"
        "- Visual verification issues: post-reload capture should be nonblank; current visual is a proof\n"
        "  artifact, not yet a website-grade showcase image.\n"
        "- Audio verification issues: Surge XT is assumed installed; builder preflight now prints\n"
        "  install/relaunch instructions before touching the generated project.\n"
        "- Mapping/discovery issues: builder uses `map_audio_to_visual_param`; tutorial prose shows\n"
        "  `list_mapping_sources` and `list_mapping_destinations` immediately before the helper call.\n"
        "- Follow-up bugs: decide whether proof artifacts are checked in, regenerated in CI, or ignored\n"
        "  locally.\n"
    )


def remove_generated_project() -> None:
    if PROJECT.exists():
        shutil.rmtree(PROJECT)


def call_optional(v: Vivid, method: str, **payload) -> dict | None:
    try:
        return v.call(method, **payload)
    except RuntimeError as exc:
        print(f"[warn] {method} skipped: {exc}")
        return None


def check_control_server(v: Vivid) -> str | None:
    try:
        v.call("status")
    except Exception as exc:
        return (
            "Vivid must be running before building this tutorial project.\n"
            f"Could not reach the control server at {v.base}: {exc}\n"
            "Launch Vivid and confirm the control server is listening. If you changed ports, set VIVID_PORT."
        )
    return None


def format_prereq_report(report: dict) -> str:
    lines = [
        f"Tutorial preflight failed for {report.get('tutorial', 'unknown tutorial')}.",
        report.get("summary", "Some prerequisites need attention."),
    ]
    checks = report.get("checks", [])
    if checks:
        lines.append("\nChecks:")
        for check in checks:
            status = str(check.get("status", "?")).upper()
            summary = check.get("summary", check.get("name", "unnamed check"))
            lines.append(f"- [{status}] {summary}")
            if check.get("path"):
                lines.append(f"  path: {check['path']}")
            if check.get("suggestion"):
                lines.append(f"  suggestion: {check['suggestion']}")
            matches = check.get("matches")
            if matches:
                lines.append("  catalog matches:")
                for match in matches[:8]:
                    lines.append(
                        "  - "
                        f"{match.get('format', '?')} {match.get('class', '?')} "
                        f"{match.get('name', '?')} -> {match.get('path', '?')}"
                    )
    actions = report.get("next_actions", [])
    if actions:
        lines.append("\nNext actions:")
        for action in actions:
            lines.append(f"- {action.get('title', 'Action')}: {action.get('detail', '')}")
    if report.get("free_plugin_list"):
        lines.append(f"\nFree plugin context: {report['free_plugin_list']}")
    return "\n".join(lines)


def preflight(v: Vivid) -> None:
    issues: list[str] = []
    control_issue = check_control_server(v)
    if control_issue:
        issues.append(control_issue)
    else:
        report = v.call("check_tutorial_prereqs", tutorial="mcp_native_first_project")
        if not report.get("ready", False):
            issues.append(format_prereq_report(report))

    if issues:
        raise SystemExit("\n\n---\n\n".join(issues))


def build() -> None:
    v = Vivid()
    preflight(v)

    remove_generated_project()
    ensure_friction_log()

    v.reset()
    v.bpm(96)

    # Save early so the app has a folder-project context for project-relative assets.
    v.save_project(str(PROJECT))
    shader_info = v.call(
        "scaffold_project_shader_operator",
        name=SHADER_OP,
        filename=SHADER_NAME,
        source=SHADER_SOURCE,
        overwrite=True,
    )
    if not shader_info.get("registered", False):
        raise RuntimeError(f"project shader did not register: {shader_info}")

    track = v.add_graph_track("tone")
    surge_preset(v, track, "pluck", prefer="Sync", gain=0.55)
    v.set_clip(
        track,
        0,
        [
            {"p": 48, "s": 0.0, "d": 0.45, "v": 0.95},
            {"p": 55, "s": 0.5, "d": 0.30, "v": 0.70},
            {"p": 60, "s": 1.0, "d": 0.45, "v": 0.85},
            {"p": 67, "s": 1.5, "d": 0.30, "v": 0.70},
            {"p": 72, "s": 2.0, "d": 0.45, "v": 0.85},
            {"p": 67, "s": 2.5, "d": 0.30, "v": 0.65},
            {"p": 60, "s": 3.0, "d": 0.65, "v": 0.75},
        ],
        4.0,
    )

    nodes = v.graph()["nodes"]
    out = find(nodes, "Output")
    shader = v.add_node(SHADER_OP)
    v.set_node_param(shader, "warp", 0.35)
    v.set_node_param(shader, "hue", 0.58)
    v.set_node_param(shader, "density", 0.42)
    v.set_node_param(shader, "glow", 0.55)

    blur = v.add_node("Blur")
    v.set_node_param(blur, "radius", 0.08)
    v.connect(blur, shader)
    if out is not None:
        v.connect(out, blur)
        v.set_active_output(out)

    # Use the first-class mapping helper rather than raw source/destination strings. The tutorial
    # prose asks the user/agent to discover these choices with list_mapping_sources and
    # list_mapping_destinations before connecting them.
    v.track_viz(track, "gate", shader, "warp", amount=0.9, lo=0.18, hi=0.8)
    v.track_viz(track, "note", shader, "hue", amount=0.8, lo=0.15, hi=0.9)
    v.master_viz("level", shader, "glow", amount=0.8, lo=0.35, hi=0.95)
    v.master_viz("transient", shader, "density", amount=0.7, lo=0.25, hi=0.85)

    v.save_project(str(PROJECT))

    # Acceptance test the portable artifact, not the still-live authoring session.
    v.load_project(str(PROJECT))

    v.launch_scene(0)
    v.play()

    print(f"saved project -> {PROJECT}")
    print(f"project-local shader -> {SHADER_PATH}")

    # These are proof hooks, not hard requirements for generation. They can warn while audio/visual
    # buffers are still warming up.
    time.sleep(1.0)
    proof = {
        "post_reload_capture_frame": call_optional(v, "capture_frame", path=str(CAPTURE_PNG)),
        "validate_project": call_optional(v, "validate_project"),
        "list_project_assets": call_optional(v, "list_project_assets"),
        "all_quality_checks": call_optional(v, "run_quality_check", name="all"),
        "mappings_resolve": call_optional(v, "run_quality_check", name="mappings_resolve"),
        "no_quarantined_operators": call_optional(v, "run_quality_check", name="no_quarantined_operators"),
    }
    PROOF_JSON.write_text(json.dumps(proof, indent=2) + "\n")
    print(f"proof hooks -> {PROOF_JSON}")


if __name__ == "__main__":
    build()
