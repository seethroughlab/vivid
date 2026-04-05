"""Vivid Operator Development MCP Server — API docs, examples, and operator lifecycle tools."""

import os
import json
import httpx
from pathlib import Path
from mcp.server.fastmcp import FastMCP

VIVID_URL = os.environ.get("VIVID_URL", "http://127.0.0.1:9876")
PROJECT_ROOT = Path(__file__).resolve().parent.parent
OPERATOR_API_DIR = PROJECT_ROOT / "src" / "operator_api"
OPERATORS_DIR = PROJECT_ROOT / "operators"
OPDEV_DOCS_DIR = Path(__file__).resolve().parent / "opdev_docs"

# Allowlisted API headers (no path traversal)
ALLOWED_HEADERS = {
    "operator.h", "types.h", "gpu_operator.h",
    "gpu_common.h", "gpu_types.h", "wgsl_filter.h", "wgsl_preprocessor.h",
    "audio_dsp.h", "adsr.h", "child_op.h", "midi_types.h",
    "input_state.h", "type_id.h",
    "create_request.h", "data_driven_filter.h",
    "thumbnail.h", "draw_ui_helpers.h", "draw_plot_helpers.h",
    "embedded_op.h", "adsr_inspector.h", "texture_readback.h",
}

# Doc topic mapping
DOC_TOPICS = {
    "core": "core_api.md",
    "control": "control_domain.md",
    "audio": "audio_domain.md",
    "gpu": "gpu_domain.md",
    "dsp": "dsp_utilities.md",
    "advanced": "advanced.md",
    "conventions": "conventions.md",
    "data_driven": "data_driven.md",
}

# Valid operator envs
OPERATOR_ENVS = {"control", "audio", "gpu", "shared"}

mcp = FastMCP("vivid-opdev", instructions="""Vivid Operator Development Server — tools for building custom operators against the Vivid operator API.

## Getting Started

Start with the discovery tools before scaffolding:
- `search_example_operators(query)` — find operators by keyword
- `get_capability_guidance(capability)` — learn how to use a specific feature
- `recommend_starting_point(goal)` — get a recommended approach for your goal

## Workflow

1. **Research** — use `get_operator_api_docs(topic)` to learn the relevant API surface
2. **Study examples** — use `list_example_operators()` and `get_example_operator()` to see real implementations
3. **Scaffold** — use `scaffold_operator(name, env)` to generate a starter template. Then edit the source to add custom ports, params, and behavior.
4. **Implement** — edit the generated source, using API docs and examples as reference
5. **Build & Test** — use `rebuild_package()` and `test_package()` to iterate
6. **Wire up** — use graph tools (`add_node`, `connect`, `set_param`) to test in context

## API Documentation Topics

- `"core"` — Param<T>, OperatorBase, VIVID_REGISTER, collect_params/ports, semantic metadata
- `"control"` — VividFrameContext, float/lane-array/string/handle ports, frame-rate processing
- `"audio"` — VividAudioContext, planar buffers, sample rate, channel counts, thread safety
- `"gpu"` — VividGpuContext, WgslFilterBase, gpu_common helpers, WGSL patterns, hot-reload
- `"dsp"` — Oscillators, waveforms, noise generators, SVF filter, decay envelope, ADSR
- `"advanced"` — ChildOp<T> composites, handle ports, MIDI, input events, media streams
- `"conventions"` — Naming, file layout, semantic tags, CMakeLists, package manifest
- `"data_driven"` — Pure-WGSL operators with JSON metadata headers (no C++ needed)

## Key Patterns

- **Param<T>**: Declare params as member variables. Runtime auto-syncs values before each process call.
- **collect_params/collect_ports**: Override to declare your operator's interface.
- **VIVID_REGISTER(ClassName)**: Macro at end of .cpp generates all extern "C" entry points.
- **WgslFilterBase**: For GPU filters, write only the fragment shader in a .wgsl file — the base class handles everything else (vertex shader, uniforms, pipeline, hot-reload).
- **ChildOp<T>**: Embed operators as members for internal modulation (frame-cadence only).
- **prepare_instance_assets()**: Use for expensive one-time CPU prep after graph params/file params have been synced into the instance. Do not hide heavyweight first-use work in `draw_thumbnail()` or use this hook for GPU/window-thread setup.

## Three Envs

- **Control** (OperatorBase + FrameProcessable) — main thread, ~60 Hz, scalar/lane-array/string/handle ports
- **Audio** (OperatorBase + AudioProcessable) — audio thread, per-buffer, planar float buffers
- **GPU** (OperatorBase + GpuProcessable) — main thread, ~60 Hz, WebGPU textures
""")


# ---------------------------------------------------------------------------
# Capability guidance lookup table
# ---------------------------------------------------------------------------

CAPABILITY_GUIDANCE = {
    "custom_port": {
        "explanation": "Typed opaque data ports for passing complex payloads between operators. Two transports: CUSTOM_VALUE (small structs copied by value) and CUSTOM_REF (shared handle registry for any size).",
        "doc_topic": "advanced",
        "example_operators": [
            {"env": "control", "name": "midi_input"},
            {"env": "control", "name": "drum_kit"},
            {"env": "audio", "name": "sampler"},
        ],
        "code_snippet": (
            '#include "operator_api/type_id.h"\n'
            '#include "operator_api/port_type_registry.h"\n\n'
            'void collect_ports(std::vector<VividPortDescriptor>& out) override {\n'
            '    out.push_back(VIVID_CUSTOM_REF_PORT("my_data", VIVID_PORT_OUTPUT, MyType));\n'
            '}'
        ),
    },
    "custom_value_port": {
        "explanation": "Small struct ports (≤256 bytes) copied by value each frame. Use VIVID_CUSTOM_VALUE_PORT macro and VIVID_DECLARE_CUSTOM_VALUE_TYPE at file scope.",
        "doc_topic": "advanced",
        "example_operators": [
            {"env": "control", "name": "sequencer"},
            {"env": "control", "name": "phase_to_midi"},
        ],
        "code_snippet": (
            'VIVID_DECLARE_CUSTOM_VALUE_TYPE(MyStruct, "com.example.my_struct", "MyStruct", false);\n\n'
            'void collect_ports(std::vector<VividPortDescriptor>& out) override {\n'
            '    out.push_back(VIVID_CUSTOM_VALUE_PORT("data", VIVID_PORT_OUTPUT, MyStruct));\n'
            '}'
        ),
    },
    "custom_ref_port": {
        "explanation": "Shared-handle ports for large or opaque data (MIDI buffers, media handles). Use VIVID_CUSTOM_REF_PORT macro and VIVID_DESCRIBE_REF_TYPE at file scope.",
        "doc_topic": "advanced",
        "example_operators": [
            {"env": "control", "name": "sequencer"},
            {"env": "control", "name": "drum_kit"},
        ],
        "code_snippet": (
            'VIVID_DECLARE_CUSTOM_REF_TYPE(MyHandle, "com.example.my_handle", "MyHandle", false);\n\n'
            'void collect_ports(std::vector<VividPortDescriptor>& out) override {\n'
            '    out.push_back(VIVID_CUSTOM_REF_PORT("handle", VIVID_PORT_OUTPUT, MyHandle));\n'
            '}'
        ),
    },
    "file_drop": {
        "explanation": "File drop parameters let users drag files onto an operator. Declare a Param<vivid::FilePath> and the runtime handles the file picker / drag-drop UI. Use prepare_instance_assets() for expensive initial file-backed prep when the graph already provides a path; keep main_thread_update() for work that truly needs the live main thread or later file changes.",
        "doc_topic": "core",
        "example_operators": [
            {"env": "gpu", "name": "texture_loader"},
            {"env": "gpu", "name": "lut_apply"},
            {"env": "gpu", "name": "svg_render"},
        ],
        "code_snippet": (
            'vivid::Param<vivid::FilePath> file{"file"};\n\n'
            'void main_thread_update(double time) override {\n'
            '    if (file.changed()) {\n'
            '        // Load the file at file.value.path\n'
            '    }\n'
            '}'
        ),
    },
    "thumbnail": {
        "explanation": "Custom thumbnails let operators render a visual preview in the node graph. Override draw_thumbnail() for the fast draw path, and move heavyweight one-time CPU prep into prepare_instance_assets().",
        "doc_topic": "advanced",
        "example_operators": [
            {"env": "control", "name": "envelope"},
            {"env": "control", "name": "clock"},
            {"env": "control", "name": "smooth"},
        ],
        "code_snippet": (
            'void draw_thumbnail(const VividThumbnailContext* ctx) override {\n'
            '    // Draw into ctx using the thumbnail API\n'
            '}'
        ),
    },
    "child_op": {
        "explanation": "ChildOp<T> lets you embed another operator as a member variable for internal modulation. Frame-cadence only. The child inherits time/frame from the parent.",
        "doc_topic": "advanced",
        "example_operators": [
            {"env": "control", "name": "modulated_gain"},
        ],
        "code_snippet": (
            '#include "operator_api/child_op.h"\n'
            '#include "control/lfo/lfo.h"\n\n'
            'vivid::ChildOp<LFO> lfo;\n\n'
            'void process_frame(const VividFrameContext* ctx) override {\n'
            '    lfo.set_param("frequency", 2.0f);\n'
            '    lfo.process(ctx);\n'
            '    float mod = lfo.output("value");\n'
            '}'
        ),
    },
    "embedded_op": {
        "explanation": "EmbeddedOp is like ChildOp<T> but the operator type is selected at runtime. Use this when a slot can hold different modulator types (e.g. LFO, Envelope, MSEG). Supports has_param/has_input/has_output for duck-typing safety.",
        "doc_topic": "advanced",
        "example_operators": [
            {"env": "control", "name": "modulated_gain"},
        ],
        "code_snippet": (
            '#include "operator_api/embedded_op.h"\n\n'
            'vivid::EmbeddedOp slot(std::make_unique<Envelope>());\n'
            'slot.set_param("attack", 0.1f);\n'
            'slot.process(parent_ctx);\n'
            'float val = slot.output("value");'
        ),
    },
    "midi": {
        "explanation": "MIDI input via VividMidiBuffer. Operators receive a buffer of timestamped MIDI messages each audio frame.",
        "doc_topic": "advanced",
        "example_operators": [
            {"env": "control", "name": "midi_input"},
            {"env": "audio", "name": "midi_file_player"},
            {"env": "control", "name": "phase_to_midi"},
        ],
        "code_snippet": (
            '#include "operator_api/midi_types.h"\n\n'
            '// Use VIVID_CUSTOM_VALUE_PORT or VIVID_CUSTOM_REF_PORT for midi buffer port.\n'
            '// See midi_input operator for full example.'
        ),
    },
    "input_events": {
        "explanation": "Mouse and keyboard input via VividInputState. Access current mouse position and process input events (clicks, key presses) each frame.",
        "doc_topic": "advanced",
        "example_operators": [
            {"env": "control", "name": "mouse"},
            {"env": "control", "name": "keyboard"},
        ],
        "code_snippet": (
            '#include "operator_api/input_state.h"\n\n'
            'void process_frame(const VividFrameContext* ctx) override {\n'
            '    const VividInputState* input = vivid_input(ctx);\n'
            '    if (!input) return;\n'
            '    float mx = input->mouse_x;\n'
            '    float my = input->mouse_y;\n'
            '}'
        ),
    },
    "media_stream": {
        "explanation": "Cross-cadence AV sync pattern. MovieFileAudio (audio thread) outputs scalar time/duration ports. MovieFileIn (GPU thread) receives audio_time via the cadence bridge to sync video frames. Shared decode/audio libraries live in operators/shared/.",
        "doc_topic": "advanced",
        "example_operators": [
            {"env": "gpu", "name": "movie_file_in"},
            {"env": "audio", "name": "movie_file_audio"},
        ],
        "code_snippet": (
            '// Cross-cadence sync uses standard scalar ports + cadence bridge.\n'
            '// MovieFileAudio outputs "time" and "duration" scalar ports.\n'
            '// MovieFileIn receives "audio_time" scalar input via cadence bridge.\n'
            '// See gpu/movie_file_in and audio/movie_file_audio for the full pattern.'
        ),
    },
    "gpu_compute": {
        "explanation": "GPU compute buffers allow read/write structured data on the GPU. Use VividComputeBuffer with appropriate usage flags.",
        "doc_topic": "gpu",
        "example_operators": [
            {"env": "gpu", "name": "texture_analysis"},
        ],
        "code_snippet": (
            '// GPU compute is an advanced pattern. Study gpu/texture_analysis for\n'
            '// a working example of compute buffer usage.'
        ),
    },
}

# ---------------------------------------------------------------------------
# HTTP helper (same pattern as graph server)
# ---------------------------------------------------------------------------

async def _post(method: str, body: dict | None = None) -> str:
    """POST to the Vivid control server and return the JSON response as text."""
    async with httpx.AsyncClient() as client:
        resp = await client.post(
            f"{VIVID_URL}/{method}",
            json=body or {},
            timeout=10.0,
        )
        return resp.text


# ---------------------------------------------------------------------------
# Resource tools — read on demand, keep context lean
# ---------------------------------------------------------------------------

@mcp.tool()
async def get_operator_api_docs(topic: str) -> str:
    """Get curated operator API documentation by topic.

    Args:
        topic: One of "core", "control", "audio", "gpu", "dsp", "advanced", "conventions"
    """
    topic = topic.lower().strip()
    if topic not in DOC_TOPICS:
        return json.dumps({
            "ok": False,
            "error": f"Unknown topic '{topic}'. Available: {', '.join(sorted(DOC_TOPICS.keys()))}"
        })

    doc_path = OPDEV_DOCS_DIR / DOC_TOPICS[topic]
    if not doc_path.is_file():
        return json.dumps({"ok": False, "error": f"Doc file not found: {DOC_TOPICS[topic]}"})

    content = doc_path.read_text(encoding="utf-8")
    return json.dumps({"ok": True, "topic": topic, "content": content})


@mcp.tool()
async def get_api_header(header: str) -> str:
    """Get raw source of an operator API header file.

    Args:
        header: Header filename (e.g. "operator.h", "types.h", "wgsl_filter.h")
    """
    header = header.strip()
    # Security: only allow known header filenames, no path components
    basename = os.path.basename(header)
    if basename not in ALLOWED_HEADERS:
        return json.dumps({
            "ok": False,
            "error": f"Header '{basename}' not in allowlist. Available: {', '.join(sorted(ALLOWED_HEADERS))}"
        })

    header_path = OPERATOR_API_DIR / basename
    if not header_path.is_file():
        return json.dumps({"ok": False, "error": f"Header file not found: {basename}"})

    content = header_path.read_text(encoding="utf-8")
    return json.dumps({"ok": True, "header": basename, "content": content})


@mcp.tool()
async def list_example_operators(env: str | None = None) -> str:
    """List all example operators in the codebase.

    Args:
        env: Optional filter — "control", "audio", "gpu", or "shared". Omit to list all.
    """
    operators = []
    envs_to_scan = OPERATOR_ENVS
    if env:
        env_name = env.lower().strip()
        if env_name not in OPERATOR_ENVS:
            return json.dumps({
                "ok": False,
                "error": f"Unknown env '{env_name}'. Available: {', '.join(sorted(OPERATOR_ENVS))}"
            })
        envs_to_scan = {env_name}

    for d in sorted(envs_to_scan):
        domain_dir = OPERATORS_DIR / d
        if not domain_dir.is_dir():
            continue
        for op_dir in sorted(domain_dir.iterdir()):
            if op_dir.is_dir() and not op_dir.name.startswith("."):
                operators.append({"env": d, "name": op_dir.name})

    return json.dumps({"ok": True, "count": len(operators), "operators": operators})


@mcp.tool()
async def get_example_operator(env: str, name: str) -> str:
    """Get the full source code of an example operator.

    Args:
        env: Operator env ("control", "audio", "gpu", "shared")
        name: Operator directory name (e.g. "lfo", "gain", "noise")
    """
    env_name = env.lower().strip()
    name = name.strip()

    if env_name not in OPERATOR_ENVS:
        return json.dumps({
            "ok": False,
            "error": f"Unknown env '{env_name}'. Available: {', '.join(sorted(OPERATOR_ENVS))}"
        })

    # Security: prevent path traversal
    if "/" in name or "\\" in name or ".." in name:
        return json.dumps({"ok": False, "error": "Invalid operator name"})

    op_dir = OPERATORS_DIR / env_name / name
    if not op_dir.is_dir():
        return json.dumps({"ok": False, "error": f"Operator not found: {env_name}/{name}"})

    files = {}
    for f in sorted(op_dir.iterdir()):
        if f.is_file() and f.suffix in {".cpp", ".h", ".wgsl", ".json"}:
            try:
                files[f.name] = f.read_text(encoding="utf-8")
            except Exception as e:
                files[f.name] = f"<error reading file: {e}>"

    if not files:
        return json.dumps({"ok": False, "error": f"No source files found in {env_name}/{name}"})

    return json.dumps({
        "ok": True,
        "env": env_name,
        "name": name,
        "files": files,
    })


# ---------------------------------------------------------------------------
# Discovery tools — search, guidance, recommendations
# ---------------------------------------------------------------------------

MAX_SEARCH_RESULTS = 20
SEARCH_EXTENSIONS = {".cpp", ".h", ".wgsl"}


@mcp.tool()
async def search_example_operators(query: str, env: str | None = None) -> str:
    """Keyword search across operator names and source files.

    Args:
        query: Search keyword (case-insensitive)
        env: Optional env filter — "control", "audio", "gpu", or "shared"
    """
    query = query.strip()
    if not query:
        return json.dumps({"ok": False, "error": "Query must not be empty"})

    query_lower = query.lower()

    envs_to_scan = OPERATOR_ENVS
    if env:
        env_name = env.lower().strip()
        if env_name not in OPERATOR_ENVS:
            return json.dumps({
                "ok": False,
                "error": f"Unknown env '{env_name}'. Available: {', '.join(sorted(OPERATOR_ENVS))}"
            })
        envs_to_scan = {env_name}

    matches = []
    for d in sorted(envs_to_scan):
        domain_dir = OPERATORS_DIR / d
        if not domain_dir.is_dir():
            continue
        for op_dir in sorted(domain_dir.iterdir()):
            if not op_dir.is_dir() or op_dir.name.startswith("."):
                continue
            if len(matches) >= MAX_SEARCH_RESULTS:
                break

            # Check operator name
            if query_lower in op_dir.name.lower():
                matches.append({
                    "env": d,
                    "name": op_dir.name,
                    "matched_in": "name",
                    "snippet": op_dir.name,
                })
                continue

            # Check source file contents
            found_in_source = False
            for f in sorted(op_dir.iterdir()):
                if found_in_source or len(matches) >= MAX_SEARCH_RESULTS:
                    break
                if not f.is_file() or f.suffix not in SEARCH_EXTENSIONS:
                    continue
                try:
                    for line in f.read_text(encoding="utf-8", errors="replace").splitlines():
                        if query_lower in line.lower():
                            snippet = line.strip()
                            if len(snippet) > 120:
                                snippet = snippet[:120] + "..."
                            matches.append({
                                "env": d,
                                "name": op_dir.name,
                                "matched_in": "source",
                                "snippet": snippet,
                            })
                            found_in_source = True
                            break
                except Exception:
                    continue

    return json.dumps({"ok": True, "count": len(matches), "matches": matches})


@mcp.tool()
async def get_capability_guidance(capability: str) -> str:
    """Get structured guidance for implementing a specific operator capability.

    Args:
        capability: Capability name (e.g. "custom_port", "file_drop", "thumbnail", "child_op", "midi", "input_events", "media_stream", "custom_ref_port", "gpu_compute")
    """
    capability = capability.strip().lower()
    if capability not in CAPABILITY_GUIDANCE:
        return json.dumps({
            "ok": False,
            "error": f"Unknown capability '{capability}'. Available: {', '.join(sorted(CAPABILITY_GUIDANCE.keys()))}"
        })

    entry = CAPABILITY_GUIDANCE[capability]
    return json.dumps({"ok": True, "capability": capability, **entry})


@mcp.tool()
async def recommend_starting_point(goal: str) -> str:
    """Recommend an approach for building an operator based on a free-text goal.

    Args:
        goal: Description of what you want the operator to do
    """
    goal = goal.strip()
    if not goal:
        return json.dumps({"ok": False, "error": "Goal must not be empty"})

    goal_lower = goal.lower()

    # Check if the goal mentions a known operator name
    all_operators = []
    for d in sorted(OPERATOR_ENVS):
        domain_dir = OPERATORS_DIR / d
        if not domain_dir.is_dir():
            continue
        for op_dir in sorted(domain_dir.iterdir()):
            if op_dir.is_dir() and not op_dir.name.startswith("."):
                all_operators.append({"env": d, "name": op_dir.name})

    for op in all_operators:
        if op["name"] in goal_lower:
            return json.dumps({
                "ok": True,
                "approach": "clone_example",
                "reasoning": f"Your goal mentions the existing operator '{op['name']}' in {op['env']}. Cloning it gives you a working starting point with the right structure.",
                "next_steps": [
                    f"get_example_operator(env=\"{op['env']}\", name=\"{op['name']}\")",
                    f"Study the source to understand its structure",
                    f"scaffold_operator(name=\"my_{op['name']}\", env=\"{op['env']}\")",
                    "Copy relevant patterns from the example into your scaffold",
                ],
            })

    # Check if the goal matches a known capability
    capability_keywords = {
        "custom_port": ["custom port", "typed port", "opaque port", "custom_port"],
        "custom_value_port": ["value port", "custom value", "custom_value_port"],
        "custom_ref_port": ["ref port", "reference port", "handle port", "custom_ref_port"],
        "file_drop": ["file drop", "file picker", "drag drop", "drag and drop", "load file", "file_drop", "file param"],
        "thumbnail": ["thumbnail", "preview", "custom thumbnail"],
        "child_op": ["child op", "child_op", "composite", "embed operator", "internal modulation"],
        "embedded_op": ["embedded op", "embedded_op", "runtime polymorphic", "dynamic slot", "modulator slot"],
        "midi": ["midi", "note on", "note off", "midi input"],
        "input_events": ["mouse", "keyboard", "click", "input event", "key press"],
        "media_stream": ["media stream", "video playback", "movie", "media_stream", "av sync", "cross-cadence"],
        "gpu_compute": ["compute", "gpu compute", "compute buffer", "gpu_compute"],
    }

    matched_capability = None
    for cap, keywords in capability_keywords.items():
        for kw in keywords:
            if kw in goal_lower:
                matched_capability = cap
                break
        if matched_capability:
            break

    if matched_capability:
        guidance = CAPABILITY_GUIDANCE[matched_capability]
        env_hint = "control"
        if matched_capability in ("gpu_compute",):
            env_hint = "gpu"
        elif matched_capability in ("media_stream",):
            env_hint = "control"
        # Infer env from goal keywords
        if "audio" in goal_lower:
            env_hint = "audio"
        elif "gpu" in goal_lower or "shader" in goal_lower or "texture" in goal_lower:
            env_hint = "gpu"

        return json.dumps({
            "ok": True,
            "approach": "scaffold",
            "reasoning": f"Your goal involves the '{matched_capability}' capability. Start with a scaffold and add the capability using the guidance and examples.",
            "next_steps": [
                f"get_capability_guidance(capability=\"{matched_capability}\")",
                f"get_example_operator(env=\"{guidance['example_operators'][0]['env']}\", name=\"{guidance['example_operators'][0]['name']}\")",
                f"scaffold_operator(name=\"my_operator\", env=\"{env_hint}\")",
                "Apply the capability pattern to your scaffolded operator",
            ],
        })

    # Generic recommendation — infer env from goal
    env_hint = "control"
    if "audio" in goal_lower or "sound" in goal_lower or "synth" in goal_lower or "filter" in goal_lower:
        env_hint = "audio"
    elif "gpu" in goal_lower or "shader" in goal_lower or "texture" in goal_lower or "visual" in goal_lower:
        env_hint = "gpu"

    return json.dumps({
        "ok": True,
        "approach": "scaffold",
        "reasoning": f"Starting with a {env_hint} scaffold gives you the right base structure. Study related examples for patterns.",
        "next_steps": [
            f"list_example_operators(env=\"{env_hint}\")",
            f"scaffold_operator(name=\"my_operator\", env=\"{env_hint}\")",
            "Implement your logic in the generated process function",
        ],
    })


# ---------------------------------------------------------------------------
# Action tools — forwarded to control server
# ---------------------------------------------------------------------------

@mcp.tool()
async def scaffold_operator(name: str, env: str, variant: str = "") -> str:
    """Scaffold a starter operator template. Creates a minimal working operator with
    env-appropriate defaults.

    This is typically step 3 in the workflow — after researching docs and studying examples.
    After scaffolding, edit the generated source to add custom ports, params, and behavior.

    Writes source, patches CMakeLists, triggers build.

    Args:
        name: Operator name in lowercase_with_underscores (e.g. "tone_gen")
        env: One of "control", "audio", "gpu"
        variant: Template variant. Use "composite" for a ChildOp-based control operator.
    """
    body: dict = {"name": name, "env": env}
    if variant:
        body["variant"] = variant
    return await _post("scaffold_operator", body)


@mcp.tool()
async def rebuild_package(name: str) -> str:
    """Recompile operators for an installed or linked package.

    Args:
        name: Package name (e.g. "vivid-glitch")
    """
    return await _post("rebuild_package", {"name": name})


@mcp.tool()
async def link_package(path: str) -> str:
    """Link a local package directory for development (symlink, edits picked up on rebuild).

    Args:
        path: Path to package directory (must contain vivid-package.json)
    """
    return await _post("link_package", {"path": path})


@mcp.tool()
async def unlink_package(name: str) -> str:
    """Unlink a development package (removes symlink, not source).

    Args:
        name: Package name
    """
    return await _post("unlink_package", {"name": name})


@mcp.tool()
async def test_package(name: str) -> str:
    """Run tests for a package (graph + C++ tests). Returns per-test pass/fail/skip.

    Args:
        name: Package name
    """
    async with httpx.AsyncClient() as client:
        resp = await client.post(f"{VIVID_URL}/test_package",
                                  json={"name": name}, timeout=90.0)
        return resp.text


# ---------------------------------------------------------------------------
# Essential graph tools — for testing operators in context
# ---------------------------------------------------------------------------

@mcp.tool()
async def add_node(type: str, id: str) -> str:
    """Add a new node to the graph.

    Args:
        type: Operator type name (e.g. "lfo", "shape", "oscillator")
        id: Unique node identifier
    """
    return await _post("add_node", {"type": type, "id": id})


@mcp.tool()
async def remove_node(node_id: str) -> str:
    """Remove a node and all its connections from the graph.

    Args:
        node_id: The node to remove
    """
    return await _post("remove_node", {"node_id": node_id})


@mcp.tool()
async def connect(from_addr: str, to_addr: str, semantic_defaults: bool = True) -> str:
    """Connect two ports. Address format is "node_id/port_name".

    Args:
        from_addr: Source port (e.g. "lfo1/value")
        to_addr: Destination port (e.g. "shape1/rotation")
        semantic_defaults: Apply semantic-tag-based default remap (default true)
    """
    return await _post("connect", {
        "from_addr": from_addr,
        "to_addr": to_addr,
        "semantic_defaults": semantic_defaults,
    })


@mcp.tool()
async def disconnect(from_addr: str, to_addr: str) -> str:
    """Disconnect two ports.

    Args:
        from_addr: Source port
        to_addr: Destination port
    """
    return await _post("disconnect", {"from_addr": from_addr, "to_addr": to_addr})


@mcp.tool()
async def set_param(node_id: str, param: str, value: float) -> str:
    """Set a parameter value on a node.

    Args:
        node_id: Target node
        param: Parameter name
        value: New value (float)
    """
    return await _post("set_param", {"node_id": node_id, "param": param, "value": value})


@mcp.tool()
async def get_param(node_id: str, param: str) -> str:
    """Get a parameter's current value.

    Args:
        node_id: Target node
        param: Parameter name
    """
    return await _post("get_param", {"node_id": node_id, "param": param})


@mcp.tool()
async def set_string_param(node_id: str, param: str, value: str) -> str:
    """Set a string parameter on a node.

    Args:
        node_id: Target node
        param: Parameter name
        value: String value
    """
    return await _post("set_string_param", {"node_id": node_id, "param": param, "value": value})


@mcp.tool()
async def inspect_graph() -> str:
    """Get the full graph state: nodes with params, ports, and connections."""
    return await _post("inspect_graph")


@mcp.tool()
async def inspect_node(node_id: str) -> str:
    """Inspect a single node: params with live values, port values.

    Args:
        node_id: The node to inspect
    """
    return await _post("inspect", {"node_id": node_id})


@mcp.tool()
async def list_types() -> str:
    """List all available operator types with params and ports."""
    return await _post("list_types")


@mcp.tool()
async def list_nodes() -> str:
    """List all nodes in the graph (id and type)."""
    return await _post("list_nodes")


@mcp.tool()
async def introspect_nodes(include_payload: bool = False) -> str:
    """Get per-node introspection with compact summary.

    Args:
        include_payload: Include full result data (default false)
    """
    return await _post("introspect_nodes", {"include_payload": include_payload})


@mcp.tool()
async def run_diagnostics(include_payload: bool = False) -> str:
    """Run graph-level diagnostics.

    Args:
        include_payload: Include full findings (default false)
    """
    return await _post("run_diagnostics", {"include_payload": include_payload})


def _start_heartbeat() -> None:
    """Start a daemon thread that pings /mcp_ping every 15 s."""
    import threading
    import time

    def _loop():
        while True:
            try:
                import urllib.request
                data = b'{"server":"opdev"}'
                req = urllib.request.Request(
                    f"{VIVID_URL}/mcp_ping",
                    data=data,
                    headers={"Content-Type": "application/json"},
                    method="POST",
                )
                urllib.request.urlopen(req, timeout=2)
            except Exception:
                pass
            time.sleep(15)

    t = threading.Thread(target=_loop, daemon=True)
    t.start()


if __name__ == "__main__":
    _start_heartbeat()
    mcp.run()
