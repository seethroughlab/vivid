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
    "operator.h", "types.h", "audio_operator.h", "gpu_operator.h",
    "gpu_common.h", "gpu_types.h", "wgsl_filter.h", "wgsl_preprocessor.h",
    "audio_dsp.h", "drum_dsp.h", "adsr.h", "child_op.h", "midi_types.h",
    "input_state.h", "media_stream.h", "media_clock.h", "type_id.h",
    "create_request.h", "data_driven_filter.h",
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
}

# Valid operator domains
OPERATOR_DOMAINS = {"control", "audio", "gpu", "shared"}

mcp = FastMCP("vivid-opdev", instructions="""Vivid Operator Development Server — tools for building custom operators against the Vivid operator API.

## Workflow

1. **Research** — use `get_operator_api_docs(topic)` to learn the relevant API surface
2. **Study examples** — use `list_example_operators()` and `get_example_operator()` to see real implementations
3. **Scaffold** — use `scaffold_operator(name, domain)` to generate a template
4. **Implement** — edit the generated source, using API docs and examples as reference
5. **Build & Test** — use `rebuild_package()` and `test_package()` to iterate
6. **Wire up** — use graph tools (`add_node`, `connect`, `set_param`) to test in context

## API Documentation Topics

- `"core"` — Param<T>, OperatorBase, VIVID_REGISTER, collect_params/ports, semantic metadata
- `"control"` — VividProcessContext, float/spread/string/handle ports, frame-rate processing
- `"audio"` — VividAudioContext, planar buffers, sample rate, channel counts, thread safety
- `"gpu"` — VividGpuContext, WgslFilterBase, gpu_common helpers, WGSL patterns, hot-reload
- `"dsp"` — Oscillators, waveforms, noise generators, SVF filter, decay envelope, ADSR
- `"advanced"` — ChildOp<T> composites, handle ports, MIDI, input events, media streams
- `"conventions"` — Naming, file layout, semantic tags, CMakeLists, package manifest

## Key Patterns

- **Param<T>**: Declare params as member variables. Runtime auto-syncs values before each process call.
- **collect_params/collect_ports**: Override to declare your operator's interface.
- **VIVID_REGISTER(ClassName)**: Macro at end of .cpp generates all extern "C" entry points.
- **WgslFilterBase**: For GPU filters, write only the fragment shader in a .wgsl file — the base class handles everything else (vertex shader, uniforms, pipeline, hot-reload).
- **ChildOp<T>**: Embed operators as members for internal modulation (control domain only).

## Three Domains

- **Control** (ControlOperatorBase) — main thread, ~60 Hz, scalar/spread/string/handle ports
- **Audio** (AudioOperatorBase) — audio thread, per-buffer, planar float buffers
- **GPU** (GpuOperatorBase) — main thread, ~60 Hz, WebGPU textures
""")


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
async def list_example_operators(domain: str | None = None) -> str:
    """List all example operators in the codebase.

    Args:
        domain: Optional filter — "control", "audio", "gpu", or "shared". Omit to list all.
    """
    operators = []
    domains_to_scan = OPERATOR_DOMAINS
    if domain:
        domain = domain.lower().strip()
        if domain not in OPERATOR_DOMAINS:
            return json.dumps({
                "ok": False,
                "error": f"Unknown domain '{domain}'. Available: {', '.join(sorted(OPERATOR_DOMAINS))}"
            })
        domains_to_scan = {domain}

    for d in sorted(domains_to_scan):
        domain_dir = OPERATORS_DIR / d
        if not domain_dir.is_dir():
            continue
        for op_dir in sorted(domain_dir.iterdir()):
            if op_dir.is_dir() and not op_dir.name.startswith("."):
                operators.append({"domain": d, "name": op_dir.name})

    return json.dumps({"ok": True, "count": len(operators), "operators": operators})


@mcp.tool()
async def get_example_operator(domain: str, name: str) -> str:
    """Get the full source code of an example operator.

    Args:
        domain: Operator domain ("control", "audio", "gpu", "shared")
        name: Operator directory name (e.g. "lfo", "gain", "noise")
    """
    domain = domain.lower().strip()
    name = name.strip()

    if domain not in OPERATOR_DOMAINS:
        return json.dumps({
            "ok": False,
            "error": f"Unknown domain '{domain}'. Available: {', '.join(sorted(OPERATOR_DOMAINS))}"
        })

    # Security: prevent path traversal
    if "/" in name or "\\" in name or ".." in name:
        return json.dumps({"ok": False, "error": "Invalid operator name"})

    op_dir = OPERATORS_DIR / domain / name
    if not op_dir.is_dir():
        return json.dumps({"ok": False, "error": f"Operator not found: {domain}/{name}"})

    files = {}
    for f in sorted(op_dir.iterdir()):
        if f.is_file() and f.suffix in {".cpp", ".h", ".wgsl", ".json"}:
            try:
                files[f.name] = f.read_text(encoding="utf-8")
            except Exception as e:
                files[f.name] = f"<error reading file: {e}>"

    if not files:
        return json.dumps({"ok": False, "error": f"No source files found in {domain}/{name}"})

    return json.dumps({
        "ok": True,
        "domain": domain,
        "name": name,
        "files": files,
    })


# ---------------------------------------------------------------------------
# Action tools — forwarded to control server
# ---------------------------------------------------------------------------

@mcp.tool()
async def scaffold_operator(name: str, domain: str, variant: str = "") -> str:
    """Create a new operator from a template. Writes source, patches CMakeLists, triggers build.

    Args:
        name: Operator name in lowercase_with_underscores (e.g. "tone_gen")
        domain: One of "control", "audio", "gpu"
        variant: Template variant. Use "composite" for a ChildOp-based control operator.
    """
    body: dict = {"name": name, "domain": domain}
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
    raw = await _post("introspect_nodes")
    # Return raw for simplicity in opdev context
    return raw


@mcp.tool()
async def run_diagnostics(include_payload: bool = False) -> str:
    """Run graph-level diagnostics.

    Args:
        include_payload: Include full findings (default false)
    """
    raw = await _post("run_diagnostics")
    return raw


if __name__ == "__main__":
    mcp.run()
