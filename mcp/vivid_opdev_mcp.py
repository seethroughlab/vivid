"""Vivid Operator Development MCP Server — API docs, examples, and operator lifecycle tools."""

import asyncio
import fnmatch
import os
import json
import re
import subprocess
import httpx
from functools import lru_cache
from pathlib import Path
from mcp.server.fastmcp import FastMCP

VIVID_URL = os.environ.get("VIVID_URL", "http://127.0.0.1:9876")
_SCRIPT_DIR = Path(__file__).resolve().parent
_APP_RESOURCES_DIR = _SCRIPT_DIR.parent
_CHECKOUT_ROOT = _APP_RESOURCES_DIR if (_APP_RESOURCES_DIR / "CMakeLists.txt").is_file() and (_APP_RESOURCES_DIR / "src" / "runtime").is_dir() else None
_BUNDLED_SOURCE_ROOT = _APP_RESOURCES_DIR / "source" if (_APP_RESOURCES_DIR / "source" / "src").is_dir() else None
PROJECT_ROOT = _CHECKOUT_ROOT or _BUNDLED_SOURCE_ROOT or _APP_RESOURCES_DIR
OPERATOR_API_DIR = PROJECT_ROOT / "src" / "operator_api"
OPERATORS_DIR = PROJECT_ROOT / "operators"
OPDEV_DOCS_DIR = _SCRIPT_DIR / "opdev_docs"
SOURCE_ROOT_NAMES = ("src", "operators", "mcp", "tests", "docs")
SOURCE_TEXT_EXTENSIONS = {
    ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".mm",
    ".py", ".md", ".txt", ".json", ".toml", ".cmake", ".wgsl",
    ".yaml", ".yml", ".sh", ".inc", ".in", ".plist",
}
MAX_SOURCE_RESULTS = 100
MAX_BUILD_TASKS = 50
_TOOLCHAIN_PATH_DIRS = (
    "/opt/homebrew/bin",
    "/opt/homebrew/sbin",
    "/usr/local/bin",
    "/usr/bin",
    "/bin",
    "/usr/sbin",
    "/sbin",
)

# Allowlisted API headers (no path traversal)
ALLOWED_HEADERS = {
    "operator.h", "types.h", "gpu_operator.h",
    "gpu_common.h", "gpu_types.h", "wgsl_filter.h", "wgsl_preprocessor.h",
    "audio_dsp.h", "adsr.h", "child_op.h", "note_types.h",
    "input_state.h", "type_id.h",
    "create_request.h", "data_driven_filter.h",
    "thumbnail.h", "draw_ui_helpers.h", "draw_plot_helpers.h",
    "adsr_inspector.h", "texture_readback.h",
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
- `list_source_roots()` — inspect the read-only code roots available to opdev
- `search_source(query)` / `read_source_file(path)` — inspect runtime, operators, MCP, tests, and docs
- `search_example_operators(query)` — find operators by keyword
- `get_capability_guidance(capability)` — learn how to use a specific feature
- `recommend_starting_point(goal)` — get a recommended approach for your goal

## Workflow

1. **Research** — use `get_operator_api_docs(topic)` to learn the relevant API surface
2. **Read the codebase** — use `search_source()`, `read_source_file()`, and `read_source_span()` for repo-wide context
3. **Study examples** — use `list_example_operators()` and `get_example_operator()` to see real implementations
4. **Scaffold** — use `scaffold_operator(name, env)` to generate a starter template, not a full advanced-authoring implementation. Then edit the source to add custom ports, params, and behavior.
5. **Implement** — edit the generated source, using API docs and examples as reference
6. **Build & Test** — use `rebuild_package()`, `test_package()`, `get_build_activity()`, and `explain_build_failure()` to iterate
7. **Wire up** — use graph tools (`add_node`, `connect`, `set_param`) to test in context

## API Documentation Topics

- `"core"` — Param<T>, OperatorBase, VIVID_REGISTER, collect_params/ports, semantic metadata
- `"control"` — VividFrameContext, float/lane-array/string/custom ports, frame-rate processing
- `"audio"` — VividAudioContext, planar buffers, sample rate, channel counts, thread safety
- `"gpu"` — VividGpuContext, WgslFilterBase, gpu_common helpers, WGSL patterns, hot-reload
- `"dsp"` — Oscillators, waveforms, noise generators, SVF filter, decay envelope, ADSR
- `"advanced"` — ChildOp<T> composites, custom ports, MIDI, input events, media streams
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

- **Control** (OperatorBase + FrameProcessable) — main thread, ~60 Hz, scalar/lane-array/string/custom ports
- **Audio** (OperatorBase + AudioProcessable) — audio thread, per-buffer, planar float buffers
- **GPU** (OperatorBase + GpuProcessable) — main thread, ~60 Hz, WebGPU textures

## Example Discovery vs Scaffolding

- Example discovery can include `"shared"` because the repo contains reusable helpers under `operators/shared/`.
- `scaffold_operator(name, env)` targets only `"control"`, `"audio"`, or `"gpu"` starter templates.

## Source Access

- The opdev server has read-only access to the Vivid codebase roots: `src`, `operators`, `mcp`, `tests`, and `docs`.
- In a source checkout, opdev reads directly from the checkout.
- In a release app, opdev prefers bundled read-only source under `Resources/source` when available.
- Build activity tools require a running Vivid runtime because they read the live build console over the control server.
""")


# ---------------------------------------------------------------------------
# Capability guidance lookup table
# ---------------------------------------------------------------------------

CAPABILITY_GUIDANCE = {
    "custom_port": {
        "explanation": "Typed opaque data ports for passing complex payloads between operators. Two transports: CUSTOM_VALUE (small structs copied by value) and CUSTOM_REF (shared-handle-backed refs for any size).",
        "doc_topic": "advanced",
        "example_operators": [
            {"env": "control", "name": "midi_input"},
            {"env": "control", "name": "drum_kit"},
            {"env": "audio", "name": "sampler"},
        ],
        "code_snippet": (
            '#include "operator_api/type_id.h"\n'
            '#include "operator_api/port_type_registry.h"\n\n'
            'VIVID_DECLARE_CUSTOM_REF_TYPE(MyType, "com.example.my_type", "MyType", false);\n\n'
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
        "explanation": "Shared-handle-backed custom ports for large or opaque data (MIDI buffers, media handles). Declare the type with VIVID_DECLARE_CUSTOM_REF_TYPE, use VIVID_CUSTOM_REF_PORT for the port, and export metadata with VIVID_DESCRIBE_REF_TYPE when you want the convenience path.",
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
    "midi": {
        "explanation": "Native note transport. Note streams between operators inside the graph use VividNoteBuffer (a buffer of timestamped per-note events keyed by uint64 note_id, carrying NOTE_ON/OFF plus PITCH_BEND/PRESSURE/TIMBRE expression). External MIDI 1.0 / MPE wire data is parsed at the I/O boundary (MidiInput, MidiFilePlayer) and translated into this internal protocol.",
        "doc_topic": "advanced",
        "example_operators": [
            {"env": "control", "name": "midi_input"},
            {"env": "audio", "name": "midi_file_player"},
            {"env": "control", "name": "phase_to_midi"},
            {"env": "control", "name": "tracker"},
        ],
        "code_snippet": (
            '#include "operator_api/note_types.h"\n\n'
            '// VIVID_CUSTOM_REF_PORT("notes_out", VIVID_PORT_OUTPUT, VividNoteBuffer)\n'
            '// vivid_sequencers::note_on/off/pitch_bend/pressure/timbre helpers in note_helpers.h.\n'
            '// See midi_input or tracker for emission, fm_synth for consumption.'
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


def _ensure_path_dirs(env: dict, dirs: list[str] | tuple[str, ...]) -> None:
    current = env.get("PATH", "")
    parts = [part for part in current.split(":") if part]
    for directory in dirs:
        if directory not in parts and os.path.isdir(directory):
            parts.append(directory)
    env["PATH"] = ":".join(parts)


def _runtime_subprocess_env() -> dict:
    env = os.environ.copy()
    _ensure_path_dirs(env, _TOOLCHAIN_PATH_DIRS)
    return env


def _resolve_vivid_bin() -> Path:
    env_bin = os.environ.get("VIVID_BIN")
    if env_bin:
        candidate = Path(env_bin).expanduser()
        if candidate.exists():
            return candidate.resolve()
    default_bin = PROJECT_ROOT / "build" / "vivid"
    if default_bin.exists():
        return default_bin.resolve()
    raise FileNotFoundError(
        "no launchable Vivid runtime binary found; set VIVID_BIN or build ./build/vivid"
    )


async def _runtime_is_reachable() -> bool:
    try:
        async with httpx.AsyncClient() as client:
            resp = await client.post(f"{VIVID_URL}/list_nodes", json={}, timeout=1.0)
        payload = json.loads(resp.text)
        return bool(payload.get("ok", False))
    except Exception:
        return False


async def _run_vivid_cli_json(args: list[str]) -> str:
    vivid_bin = _resolve_vivid_bin()
    proc = await asyncio.create_subprocess_exec(
        str(vivid_bin),
        *args,
        cwd=str(PROJECT_ROOT),
        stdout=asyncio.subprocess.PIPE,
        stderr=asyncio.subprocess.PIPE,
        env=_runtime_subprocess_env(),
    )
    stdout, stderr = await proc.communicate()
    out = stdout.decode("utf-8", errors="replace").strip()
    err = stderr.decode("utf-8", errors="replace").strip()
    if out:
        try:
            json.loads(out)
            return out
        except Exception:
            pass
    message = err or out or f"CLI command failed with exit code {proc.returncode}"
    return json.dumps({"ok": False, "error": message})


def _runtime_required_error(tool_name: str) -> str:
    return json.dumps({"ok": False, "error": f"{tool_name} requires a running Vivid runtime"})


def _selected_root_path(root_name: str) -> tuple[Path | None, str | None]:
    if root_name not in SOURCE_ROOT_NAMES:
        return None, None
    if _CHECKOUT_ROOT:
        checkout_path = _CHECKOUT_ROOT / root_name
        if checkout_path.is_dir():
            return checkout_path, "checkout"
    if _BUNDLED_SOURCE_ROOT:
        bundled_path = _BUNDLED_SOURCE_ROOT / root_name
        if bundled_path.is_dir():
            return bundled_path, "bundle"
    return None, None


def _root_listing() -> list[dict]:
    roots = []
    for root_name in SOURCE_ROOT_NAMES:
        checkout_available = bool(_CHECKOUT_ROOT and (_CHECKOUT_ROOT / root_name).is_dir())
        bundle_available = bool(_BUNDLED_SOURCE_ROOT and (_BUNDLED_SOURCE_ROOT / root_name).is_dir())
        selected_path, origin = _selected_root_path(root_name)
        roots.append({
            "name": root_name,
            "checkout_available": checkout_available,
            "bundle_available": bundle_available,
            "selected": selected_path is not None,
            "origin": origin or "",
            "path": selected_path.as_posix() if selected_path else "",
        })
    return roots


def _validate_root_names(roots: list[str] | None) -> tuple[list[str], str | None]:
    if not roots:
        return list(SOURCE_ROOT_NAMES), None
    normalized = []
    for root in roots:
        root_name = root.strip()
        if root_name not in SOURCE_ROOT_NAMES:
            return [], f"Unknown source root '{root_name}'. Available: {', '.join(SOURCE_ROOT_NAMES)}"
        normalized.append(root_name)
    return normalized, None


@lru_cache(maxsize=None)
def _source_files_for_root(root_name: str, root_path_str: str) -> tuple[tuple[str, str], ...]:
    root_path = Path(root_path_str)
    files: list[tuple[str, str]] = []
    for dirpath, dirnames, filenames in os.walk(root_path):
        dirnames[:] = [d for d in dirnames if d not in {".git", "build", "site", "__pycache__", ".pytest_cache", ".venv", ".venv-mcp"}]
        base = Path(dirpath)
        for filename in sorted(filenames):
            path = base / filename
            if path.suffix.lower() not in SOURCE_TEXT_EXTENSIONS:
                continue
            rel_path = (Path(root_name) / path.relative_to(root_path)).as_posix()
            files.append((rel_path, path.as_posix()))
    return tuple(files)


def _iter_source_files(roots: list[str] | None = None,
                       file_types: list[str] | None = None,
                       path_globs: list[str] | None = None):
    selected_roots, err = _validate_root_names(roots)
    if err:
        raise ValueError(err)

    normalized_exts = set()
    for ext in file_types or []:
        ext = ext.strip().lower()
        if not ext:
            continue
        if not ext.startswith("."):
            ext = "." + ext
        normalized_exts.add(ext)

    for root_name in selected_roots:
        root_path, origin = _selected_root_path(root_name)
        if not root_path or not origin:
            continue
        for rel_path, abs_path in _source_files_for_root(root_name, root_path.as_posix()):
            if normalized_exts and Path(rel_path).suffix.lower() not in normalized_exts:
                continue
            if path_globs and not any(fnmatch.fnmatch(rel_path, glob) for glob in path_globs):
                continue
            yield {
                "path": rel_path,
                "abs_path": abs_path,
                "root": root_name,
                "origin": origin,
            }


def _resolve_source_path(path: str) -> tuple[dict | None, str | None]:
    rel = Path(path.strip())
    if not path.strip() or rel.is_absolute():
        return None, "Path must be a repo-relative allowlisted path"
    parts = rel.parts
    if not parts:
        return None, "Path must not be empty"
    if any(part == ".." for part in parts):
        return None, "Path must not escape the allowlisted source roots"
    root_name = parts[0]
    if root_name not in SOURCE_ROOT_NAMES:
        return None, f"Path root '{root_name}' is not allowlisted"
    root_path, origin = _selected_root_path(root_name)
    if not root_path or not origin:
        return None, f"Source root '{root_name}' is not available"
    resolved = (root_path / Path(*parts[1:])).resolve()
    try:
        resolved.relative_to(root_path.resolve())
    except ValueError:
        return None, "Path must remain inside the allowlisted root"
    if not resolved.is_file():
        return None, f"Source file not found: {path}"
    return {
        "path": Path(*parts).as_posix(),
        "abs_path": resolved.as_posix(),
        "root": root_name,
        "origin": origin,
    }, None


def _read_source_text(abs_path: str) -> str:
    return Path(abs_path).read_text(encoding="utf-8", errors="replace")


def _classify_symbol_line(line: str, name: str) -> dict | None:
    escaped = re.escape(name)
    if re.search(rf"^\s*#\s*define\s+{escaped}\b", line):
        return {"kind": "macro", "is_definition": True}
    type_match = re.search(rf"\b(class|struct|enum|namespace)\s+{escaped}\b", line)
    if type_match:
        return {"kind": type_match.group(1), "is_definition": True}
    if re.search(rf"\b(using\s+{escaped}\b|typedef\b.*\b{escaped}\b)", line):
        return {"kind": "alias", "is_definition": True}
    if re.search(rf"(^|[^.>\w]){escaped}\s*\([^;]*\)\s*(const)?\s*(\{{|;|$)", line):
        return {"kind": "function", "is_definition": True}
    if re.search(rf"\b{escaped}\b", line):
        return {"kind": "identifier", "is_definition": False}
    return None


# ---------------------------------------------------------------------------
# Resource tools — read on demand, keep context lean
# ---------------------------------------------------------------------------

@mcp.tool()
async def list_source_roots() -> str:
    """List the read-only Vivid source roots available to opdev."""
    roots = _root_listing()
    selected = sum(1 for root in roots if root["selected"])
    return json.dumps({"ok": True, "count": selected, "roots": roots})


@mcp.tool()
async def search_source(query: str,
                        roots: list[str] | None = None,
                        limit: int = 20,
                        file_types: list[str] | None = None,
                        path_globs: list[str] | None = None) -> str:
    """Search the read-only Vivid codebase by text.

    Args:
        query: Case-insensitive text query
        roots: Optional source-root filter (`src`, `operators`, `mcp`, `tests`, `docs`)
        limit: Maximum matches to return
        file_types: Optional extension filter (e.g. [".cpp", ".h", ".py"])
        path_globs: Optional path glob filter (e.g. ["src/runtime/*", "docs/runtime/*.md"])
    """
    query = query.strip()
    if not query:
        return json.dumps({"ok": False, "error": "Query must not be empty"})

    try:
        matches = []
        lower_query = query.lower()
        for file_info in _iter_source_files(roots=roots, file_types=file_types, path_globs=path_globs):
            for line_no, line in enumerate(_read_source_text(file_info["abs_path"]).splitlines(), start=1):
                lower_line = line.lower()
                col = lower_line.find(lower_query)
                if col < 0:
                    continue
                snippet = line.strip()
                if len(snippet) > 200:
                    snippet = snippet[:200] + "..."
                matches.append({
                    "path": file_info["path"],
                    "root": file_info["root"],
                    "origin": file_info["origin"],
                    "line": line_no,
                    "column": col + 1,
                    "match_kind": "text",
                    "snippet": snippet,
                })
                if len(matches) >= max(1, min(limit, MAX_SOURCE_RESULTS)):
                    return json.dumps({"ok": True, "query": query, "count": len(matches), "matches": matches})
        return json.dumps({"ok": True, "query": query, "count": len(matches), "matches": matches})
    except ValueError as exc:
        return json.dumps({"ok": False, "error": str(exc)})


@mcp.tool()
async def read_source_file(path: str, max_bytes: int = 200000) -> str:
    """Read a source file from the read-only allowlisted code roots."""
    file_info, err = _resolve_source_path(path)
    if err:
        return json.dumps({"ok": False, "error": err})

    content = _read_source_text(file_info["abs_path"])
    max_bytes = max(1, min(max_bytes, 2 * 1024 * 1024))
    truncated = len(content.encode("utf-8")) > max_bytes
    if truncated:
        encoded = content.encode("utf-8")[:max_bytes]
        content = encoded.decode("utf-8", errors="ignore")

    return json.dumps({
        "ok": True,
        "path": file_info["path"],
        "root": file_info["root"],
        "origin": file_info["origin"],
        "content": content,
        "truncated": truncated,
        "bytes_returned": len(content.encode("utf-8")),
    })


@mcp.tool()
async def read_source_span(path: str, start_line: int, end_line: int) -> str:
    """Read a specific inclusive line span from a source file."""
    if start_line <= 0 or end_line <= 0 or end_line < start_line:
        return json.dumps({"ok": False, "error": "Invalid line range"})

    file_info, err = _resolve_source_path(path)
    if err:
        return json.dumps({"ok": False, "error": err})

    lines = _read_source_text(file_info["abs_path"]).splitlines()
    if start_line > len(lines):
        return json.dumps({"ok": False, "error": "start_line out of range"})

    actual_end = min(end_line, len(lines))
    selected = [{"line": idx, "text": lines[idx - 1]} for idx in range(start_line, actual_end + 1)]
    content = "\n".join(item["text"] for item in selected)
    return json.dumps({
        "ok": True,
        "path": file_info["path"],
        "root": file_info["root"],
        "origin": file_info["origin"],
        "start_line": start_line,
        "end_line": actual_end,
        "content": content,
        "lines": selected,
    })


@mcp.tool()
async def find_symbol(name: str, roots: list[str] | None = None, limit: int = 20) -> str:
    """Find likely symbol definitions, with fallback to token hits if no definitions match."""
    name = name.strip()
    if not name:
        return json.dumps({"ok": False, "error": "Name must not be empty"})

    try:
        matches = []
        fallback = []
        for file_info in _iter_source_files(roots=roots):
            for line_no, line in enumerate(_read_source_text(file_info["abs_path"]).splitlines(), start=1):
                classification = _classify_symbol_line(line, name)
                if not classification:
                    continue
                entry = {
                    "path": file_info["path"],
                    "root": file_info["root"],
                    "origin": file_info["origin"],
                    "line": line_no,
                    "kind": classification["kind"],
                    "is_definition": classification["is_definition"],
                    "snippet": line.strip(),
                }
                if classification["is_definition"]:
                    matches.append(entry)
                else:
                    fallback.append(entry)
                if len(matches) >= max(1, min(limit, MAX_SOURCE_RESULTS)):
                    return json.dumps({"ok": True, "name": name, "count": len(matches), "matches": matches})
        final_matches = matches if matches else fallback[:max(1, min(limit, MAX_SOURCE_RESULTS))]
        return json.dumps({"ok": True, "name": name, "count": len(final_matches), "matches": final_matches})
    except ValueError as exc:
        return json.dumps({"ok": False, "error": str(exc)})


@mcp.tool()
async def find_references(name: str, roots: list[str] | None = None, limit: int = 50) -> str:
    """Find token references to a symbol across the read-only code roots."""
    name = name.strip()
    if not name:
        return json.dumps({"ok": False, "error": "Name must not be empty"})

    try:
        matches = []
        token_re = re.compile(rf"\b{re.escape(name)}\b")
        for file_info in _iter_source_files(roots=roots):
            for line_no, line in enumerate(_read_source_text(file_info["abs_path"]).splitlines(), start=1):
                if not token_re.search(line):
                    continue
                classification = _classify_symbol_line(line, name) or {"kind": "identifier", "is_definition": False}
                matches.append({
                    "path": file_info["path"],
                    "root": file_info["root"],
                    "origin": file_info["origin"],
                    "line": line_no,
                    "kind": classification["kind"],
                    "is_definition": classification["is_definition"],
                    "snippet": line.strip(),
                })
                if len(matches) >= max(1, min(limit, MAX_SOURCE_RESULTS * 2)):
                    return json.dumps({"ok": True, "name": name, "count": len(matches), "matches": matches})
        return json.dumps({"ok": True, "name": name, "count": len(matches), "matches": matches})
    except ValueError as exc:
        return json.dumps({"ok": False, "error": str(exc)})


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
    raw = json.loads(await read_source_file(f"src/operator_api/{basename}"))
    if not raw["ok"]:
        return json.dumps({"ok": False, "error": raw["error"]})
    return json.dumps({"ok": True, "header": basename, "content": raw["content"]})


@mcp.tool()
async def list_example_operators(env: str | None = None) -> str:
    """List all example operators in the codebase.

    Args:
        env: Optional filter — "control", "audio", "gpu", or "shared". "shared" is for example discovery only. Omit to list all.
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
        env: Operator env ("control", "audio", "gpu", "shared"). "shared" is discovery-only, not a scaffold target.
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
        env: Optional env filter — "control", "audio", "gpu", or "shared". "shared" is discovery-only.
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

    # Infer the target env from goal keywords. If the goal clearly belongs to
    # one domain, the example-name match below is restricted to that domain so
    # ambiguous names like "noise" in audio don't get suggested for GPU goals.
    gpu_kw = ("gpu", "shader", "wgsl", "texture", "pixel", "fragment",
              "render", "filter", "screen", "frame", "image", "visual")
    audio_kw = ("audio", "sound", "synth", "oscillator", "drum", "sample",
                "reverb", "delay", "compressor", "fft", "dsp")
    control_kw = ("lfo", "envelope", "sequencer", "clock", "trigger", "midi",
                  "control signal")
    inferred_env = None
    if any(kw in goal_lower for kw in gpu_kw):       inferred_env = "gpu"
    elif any(kw in goal_lower for kw in audio_kw):   inferred_env = "audio"
    elif any(kw in goal_lower for kw in control_kw): inferred_env = "control"

    # Check if the goal mentions a known operator name
    all_operators = []
    for d in sorted(OPERATOR_ENVS):
        if inferred_env and d != inferred_env:
            continue
        domain_dir = OPERATORS_DIR / d
        if not domain_dir.is_dir():
            continue
        for op_dir in sorted(domain_dir.iterdir()):
            if op_dir.is_dir() and not op_dir.name.startswith("."):
                all_operators.append({"env": d, "name": op_dir.name})

    for op in all_operators:
        # Avoid trivial-substring matches: require the operator name to appear
        # as a word boundary OR to be at least 4 chars to reduce false hits.
        if len(op["name"]) < 4:
            continue
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
        "child_op": ["child op", "child_op", "owned child", "embed operator", "internal modulation"],
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
async def scaffold_operator(name: str, env: str, variant: str = "",
                            destination: str = "project") -> str:
    """Scaffold a starter operator template into the project's local-operators package.

    Default behavior creates a per-project operator beside the saved graph
    (auto-creating `<graph_dir>/operators/` as a linked workspace package the
    first time). Pass `destination="core"` only when adding a broadly-useful
    primitive that should ship with Vivid.

    This is typically step 3 in the workflow — after researching docs and
    studying examples. After scaffolding, edit the generated source to add
    custom ports, params, and behavior. This is a starter-template tool, not
    the full advanced-authoring API.

    Writes source, patches the destination CMakeLists, triggers build.

    Args:
        name: Operator name in lowercase_with_underscores (e.g. "tone_gen")
        env: One of "control", "audio", "gpu"
        variant: Template variant. Use "child_op" for a ChildOp-based control operator.
        destination: Where to place the operator. "project" (default) writes
            to the workspace's local-operators package beside the graph,
            auto-scaffolding the package if needed. "core" writes into the
            Vivid seed catalog. "package:<name>" targets an installed
            package. An absolute path targets that exact root.
    """
    if await _runtime_is_reachable():
        body: dict = {"name": name, "kind": env, "destination": destination}
        if variant:
            body["variant"] = variant
        return await _post("scaffold_operator", body)
    args = ["scaffold-operator", name, "--env", env]
    if variant:
        args.extend(["--variant", variant])
    if destination and destination != "project":
        args.extend(["--destination", destination])
    args.append("--json")
    return await _run_vivid_cli_json(args)


@mcp.tool()
async def rebuild_package(name: str) -> str:
    """Recompile operators for an installed or linked package.

    Args:
        name: Package name (e.g. "vivid-glitch")
    """
    if await _runtime_is_reachable():
        return await _post("rebuild_package", {"name": name})
    return await _run_vivid_cli_json(["rebuild", name, "--json"])


@mcp.tool()
async def link_package(path: str) -> str:
    """Link a local package directory for development (symlink, edits picked up on rebuild).

    Args:
        path: Path to package directory (must contain vivid-package.json)
    """
    if await _runtime_is_reachable():
        return await _post("link_package", {"path": path})
    return await _run_vivid_cli_json(["link", path, "--json"])


@mcp.tool()
async def unlink_package(name: str) -> str:
    """Unlink a development package (removes symlink, not source).

    Args:
        name: Package name
    """
    if await _runtime_is_reachable():
        return await _post("unlink_package", {"name": name})
    return await _run_vivid_cli_json(["unlink", name, "--json"])


@mcp.tool()
async def test_package(name: str) -> str:
    """Run tests for a package (graph + C++ tests). Returns per-test pass/fail/skip.

    Args:
        name: Package name
    """
    if await _runtime_is_reachable():
        async with httpx.AsyncClient() as client:
            resp = await client.post(f"{VIVID_URL}/test_package",
                                      json={"name": name}, timeout=90.0)
            return resp.text
    return await _run_vivid_cli_json(["test-package", name, "--json"])


@mcp.tool()
async def get_build_activity(scope: str = "recent", limit: int = 10) -> str:
    """Get structured recent or active build/test activity from the running Vivid build console."""
    scope = scope.strip().lower() or "recent"
    if scope not in {"recent", "active"}:
        return json.dumps({"ok": False, "error": "scope must be 'recent' or 'active'"})
    if not await _runtime_is_reachable():
        return _runtime_required_error("get_build_activity")
    try:
        return await _post("get_build_activity", {"scope": scope, "limit": max(1, min(limit, MAX_BUILD_TASKS))})
    except Exception as exc:
        return json.dumps({"ok": False, "error": f"build activity unavailable: {exc}"})


@mcp.tool()
async def explain_build_failure(task_id: str = "latest", max_lines: int = 40) -> str:
    """Explain the latest or a specific failed build/test task from the running Vivid build console."""
    body: dict = {"max_lines": max(5, min(max_lines, 200))}
    if task_id:
        if task_id != "latest":
            try:
                body["task_id"] = int(task_id)
            except ValueError:
                return json.dumps({"ok": False, "error": "task_id must be 'latest' or an integer string"})
        else:
            body["task_id"] = "latest"
    if not await _runtime_is_reachable():
        return _runtime_required_error("explain_build_failure")
    try:
        return await _post("explain_build_failure", body)
    except Exception as exc:
        return json.dumps({"ok": False, "error": f"build failure explanation unavailable: {exc}"})


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
    if not await _runtime_is_reachable():
        return _runtime_required_error("add_node")
    return await _post("add_node", {"type": type, "id": id})


@mcp.tool()
async def remove_node(node_id: str) -> str:
    """Remove a node and all its connections from the graph.

    Args:
        node_id: The node to remove
    """
    if not await _runtime_is_reachable():
        return _runtime_required_error("remove_node")
    return await _post("remove_node", {"node_id": node_id})


@mcp.tool()
async def connect(from_addr: str, to_addr: str, semantic_defaults: bool = True) -> str:
    """Connect two ports. Address format is "node_id/port_name".

    Args:
        from_addr: Source port (e.g. "lfo1/value")
        to_addr: Destination port (e.g. "shape1/rotation")
        semantic_defaults: Apply semantic-tag-based default remap (default true)
    """
    if not await _runtime_is_reachable():
        return _runtime_required_error("connect")
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
    if not await _runtime_is_reachable():
        return _runtime_required_error("disconnect")
    return await _post("disconnect", {"from_addr": from_addr, "to_addr": to_addr})


@mcp.tool()
async def set_param(node_id: str, param: str, value: float) -> str:
    """Set a parameter value on a node.

    Args:
        node_id: Target node
        param: Parameter name
        value: New value (float)
    """
    if not await _runtime_is_reachable():
        return _runtime_required_error("set_param")
    return await _post("set_param", {"node_id": node_id, "param": param, "value": value})


@mcp.tool()
async def get_param(node_id: str, param: str) -> str:
    """Get a parameter's current value.

    Args:
        node_id: Target node
        param: Parameter name
    """
    if not await _runtime_is_reachable():
        return _runtime_required_error("get_param")
    return await _post("get_param", {"node_id": node_id, "param": param})


@mcp.tool()
async def set_string_param(node_id: str, param: str, value: str) -> str:
    """Set a string parameter on a node.

    Args:
        node_id: Target node
        param: Parameter name
        value: String value
    """
    if not await _runtime_is_reachable():
        return _runtime_required_error("set_string_param")
    return await _post("set_string_param", {"node_id": node_id, "param": param, "value": value})


@mcp.tool()
async def inspect_graph() -> str:
    """Get the full graph state: nodes with params, ports, and connections."""
    if not await _runtime_is_reachable():
        return _runtime_required_error("inspect_graph")
    return await _post("inspect_graph")


@mcp.tool()
async def inspect_node(node_id: str) -> str:
    """Inspect a single node: params with live values, port values.

    Args:
        node_id: The node to inspect
    """
    if not await _runtime_is_reachable():
        return _runtime_required_error("inspect_node")
    return await _post("inspect", {"node_id": node_id})


@mcp.tool()
async def list_types() -> str:
    """List all available operator types with params and ports."""
    return await _run_vivid_cli_json(["list-types", "--json"])


@mcp.tool()
async def list_nodes() -> str:
    """List all nodes in the graph (id and type)."""
    if not await _runtime_is_reachable():
        return _runtime_required_error("list_nodes")
    return await _post("list_nodes")


@mcp.tool()
async def introspect_nodes(include_payload: bool = False) -> str:
    """Get per-node introspection with compact summary.

    Args:
        include_payload: Include full result data (default false)
    """
    if not await _runtime_is_reachable():
        return _runtime_required_error("introspect_nodes")
    return await _post("introspect_nodes", {"include_payload": include_payload})


@mcp.tool()
async def run_diagnostics(include_payload: bool = False) -> str:
    """Run graph-level diagnostics.

    Args:
        include_payload: Include full findings (default false)
    """
    if not await _runtime_is_reachable():
        return _runtime_required_error("run_diagnostics")
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
