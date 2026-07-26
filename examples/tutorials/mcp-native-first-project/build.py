#!/usr/bin/env python3
"""ADR-0034 Golden Path A: build the first MCP-native creative-coding tutorial project.

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

from vivid_demo import SURGE, Vivid, find, surge_preset  # noqa: E402

PROJECT = HERE / "project"
ASSET_SHADER_DIR = PROJECT / "assets" / "shaders"
SHADER_NAME = "pulse_field.glsl"
SHADER_REF = f"assets/shaders/{SHADER_NAME}"
SHADER_PATH = ASSET_SHADER_DIR / SHADER_NAME
FRICTION_LOG = PROJECT / "FRICTION-LOG.md"
PROOF_JSON = PROJECT / "proof.json"
CAPTURE_PNG = PROJECT / "capture.png"

SHADER_SOURCE = """#version 450
// ADR-0034 Golden Path A: project-local CustomShader source.
// The CustomShader params map to u_warp/u_hue/u_density/u_glow.
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform U {
    vec2 u_res;
    float u_time;
    float u_warp;
    float u_hue;
    float u_density;
    float u_glow;
};

float ring(vec2 uv, vec2 center, float radius, float width) {
    float d = abs(distance(uv, center) - radius);
    return 1.0 - smoothstep(0.0, width, d);
}

void main() {
    vec2 uv = v_uv;
    vec2 p = uv * 2.0 - 1.0;
    p.x *= u_res.x / max(1.0, u_res.y);

    float t = u_time;
    float twist = sin((p.x + p.y) * (3.0 + u_density * 10.0) + t * 1.8);
    vec2 q = p + vec2(cos(p.y * 4.0 + t), sin(p.x * 4.0 - t)) * (0.04 + u_warp * 0.18);

    float beam = 0.5 + 0.5 * sin((q.x * 5.0 + q.y * 2.0) + twist * 1.4 + t * 2.0);
    float pulse = ring(uv, vec2(0.5), 0.16 + u_density * 0.24, 0.08);
    float vignette = smoothstep(1.3, 0.15, length(p));

    vec3 a = 0.5 + 0.5 * cos(vec3(0.0, 2.1, 4.2) + u_hue * 6.2831853);
    vec3 b = vec3(0.08, 0.12, 0.18);
    vec3 color = mix(b, a, beam * 0.65 + pulse * 0.55);
    color *= vignette * (0.65 + u_glow * 1.2);
    o_color = vec4(color, 1.0);
}
"""


def ensure_project_shader() -> None:
    ASSET_SHADER_DIR.mkdir(parents=True, exist_ok=True)
    SHADER_PATH.write_text(SHADER_SOURCE)


def ensure_friction_log() -> None:
    FRICTION_LOG.write_text(
        "# Friction Log\n\n"
        "- UI-only steps: none exercised by the builder. Tutorial prose still needs a human path or\n"
        "  an explicit MCP-client-first framing.\n"
        "- MCP-required steps: reset, save folder project, create track, load Surge XT, add notes,\n"
        "  create visual graph, set project-local shader, discover sources/destinations, map audio\n"
        "  characteristics to visual params, save, reload, capture, validate, and run quality checks.\n"
        "- Confusing or missing diagnostics: stale autosave/recovery state can emit warnings before a\n"
        "  scripted tutorial reset unless Vivid is launched with `VIVID_DISCARD_RECOVERY=1`. The builder\n"
        "  itself preflights the control server before deleting the generated project.\n"
        "- Save/load issues:\n"
        "  - Watch for project-relative `CustomShader.file` reload behavior. The desired saved value is\n"
        f"    `{SHADER_REF}`, but the live operator must receive a project-resolved absolute\n"
        "    path after reload.\n"
        "- Visual verification issues: post-reload capture should be nonblank; current visual is a proof\n"
        "  artifact, not yet a website-grade showcase image.\n"
        "- Audio verification issues: Surge XT is assumed installed; tutorial prose needs download and\n"
        "  missing-plugin recovery instructions.\n"
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


def require_control_server(v: Vivid) -> None:
    try:
        v.call("status")
    except Exception as exc:
        raise SystemExit(
            "Vivid must be running before building this tutorial project. "
            f"Could not reach the control server at {v.base}: {exc}"
        ) from exc


def build() -> None:
    if not Path(SURGE).exists():
        raise SystemExit(
            "Surge XT is required for this tutorial. Expected CLAP bundle at "
            f"{SURGE!r}."
        )

    v = Vivid()
    require_control_server(v)

    remove_generated_project()
    ensure_project_shader()
    ensure_friction_log()

    v.reset()
    v.bpm(96)

    # Save early so the app has a folder-project context for project-relative assets.
    v.save_project(str(PROJECT))

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
    shader = v.add_node("CustomShader")
    v.set_node_file(shader, "file", SHADER_REF)
    v.set_node_param(shader, "warp", 0.35)
    v.set_node_param(shader, "hue", 0.58)
    v.set_node_param(shader, "density", 0.42)
    v.set_node_param(shader, "glow", 0.55)

    feedback = v.add_node("Feedback")
    v.set_node_param(feedback, "decay", 0.28)
    blur = v.add_node("Blur")
    v.set_node_param(blur, "radius", 0.08)
    v.connect(feedback, shader)
    v.connect(blur, feedback)
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
