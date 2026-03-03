"""Vivid MCP Server — bridges MCP stdio to the Vivid runtime HTTP control server."""

import os
import json
import httpx
from mcp.server.fastmcp import FastMCP

VIVID_URL = os.environ.get("VIVID_URL", "http://127.0.0.1:9876")

mcp = FastMCP("vivid", instructions="""Vivid is a real-time audio-visual graph engine. You build node graphs that generate and process visuals, audio, and control signals, all running live.

## Three Domains

- **GPU** — texture-based visual operators (noise, shape, blur, composite, etc.). Ports use type `gpu_texture` for 2D image data or `gpu_scene` for 3D scene fragments. `Shape3D` generates 3D geometry (cube, sphere, torus, plane, cylinder) as a scene source; `Transform3D` applies position/rotation/scale to a scene; `SceneMerge` combines up to 4 scene inputs into one; `Light3D` adds directional or point lights; `Render3D` converts scene fragments to textures with multi-draw and dynamic lighting. Every visual graph needs a `video_out` node to display output.
- **Audio** — sample-based audio operators (oscillator, gain, reverb, etc.). Ports use type `audio_float`. Every audio graph needs an `audio_out` node to hear output.
- **Control** — scalar/spread signals for modulation (lfo, clock, math, sequencer, etc.). Ports use type `control_float`. Control outputs can also drive any numeric parameter directly.

## Port Compatibility

Connections must match types: `gpu_texture` → `gpu_texture`, `gpu_scene` → `gpu_scene`, `audio_float` → `audio_float`, `control_float` → `control_float` or any numeric parameter. Address format for ports: `"node_id/port_name"`. Use `Render3D` to convert `gpu_scene` → `gpu_texture` (3D→2D bridge).

## Workflow

1. `list_types` — discover available operators, their params, and ports
2. `add_node` — add nodes by type and unique ID
3. `connect` — wire outputs to inputs using `"node_id/port_name"` addresses
4. `set_param` — adjust parameters (takes effect immediately)
5. `inspect_graph` — verify the graph state, check live output values

## Common Patterns

- Connect an `lfo` output → a GPU node's parameter for animation
- Connect `clock` → `sequencer` for rhythmic patterns
- Audio chains: oscillator → effects → `audio_out`
- Visual chains: generators → filters → `video_out`
- Control signals modulate both GPU and audio params

## Composite Operators

Control operators can embed other operators internally using ChildOp<T>. Use `scaffold_operator`
with `variant="composite"` to generate a template. Useful for internal modulation (e.g. LFO driving
a gain stage) without exposing child operators as graph nodes. Control domain only.
""")


async def _post(method: str, body: dict | None = None) -> str:
    """POST to the Vivid control server and return the JSON response as text."""
    async with httpx.AsyncClient() as client:
        resp = await client.post(
            f"{VIVID_URL}/{method}",
            json=body or {},
            timeout=10.0,
        )
        return resp.text


@mcp.tool()
async def inspect_graph() -> str:
    """Get the full graph state: nodes with params (live values), input/output ports (with current output values), and connections."""
    return await _post("inspect_graph")


@mcp.tool()
async def list_types() -> str:
    """List all available operator types with their params (name, type, default, min, max) and ports (name, type, direction)."""
    return await _post("list_types")


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
async def connect(from_addr: str, to_addr: str) -> str:
    """Connect two ports. Address format is "node_id/port_name".

    Args:
        from_addr: Source port (e.g. "lfo1/value")
        to_addr: Destination port (e.g. "shape1/rotation")
    """
    return await _post("connect", {"from_addr": from_addr, "to_addr": to_addr})


@mcp.tool()
async def disconnect(from_addr: str, to_addr: str) -> str:
    """Disconnect two ports. Address format is "node_id/port_name".

    Args:
        from_addr: Source port (e.g. "lfo1/value")
        to_addr: Destination port (e.g. "shape1/rotation")
    """
    return await _post("disconnect", {"from_addr": from_addr, "to_addr": to_addr})


@mcp.tool()
async def set_connection_remap(
    from_addr: str, to_addr: str,
    from_min: float = 0.0, from_max: float = 1.0,
    to_min: float = 0.0, to_max: float = 1.0,
    clamp: bool = False,
) -> str:
    """Set the remap on a connection. Values are mapped from [from_min, from_max] to [to_min, to_max].

    Args:
        from_addr: Source port (e.g. "lfo1/value")
        to_addr: Destination port (e.g. "blur1/radius")
        from_min: Input range minimum (default 0.0)
        from_max: Input range maximum (default 1.0)
        to_min: Output range minimum (default 0.0)
        to_max: Output range maximum (default 1.0)
        clamp: Whether to clamp output to [min(to_min,to_max), max(to_min,to_max)]
    """
    return await _post("set_connection_remap", {
        "from_addr": from_addr, "to_addr": to_addr,
        "from_min": from_min, "from_max": from_max,
        "to_min": to_min, "to_max": to_max, "clamp": clamp,
    })


@mcp.tool()
async def set_param(node_id: str, param: str, value: float) -> str:
    """Set a parameter value on a node. Takes effect immediately.

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
async def save_graph(path: str | None = None) -> str:
    """Save the graph to disk.

    Args:
        path: File path to save to. If omitted, saves to the original graph file.
    """
    body = {"path": path} if path else {}
    return await _post("save_graph", body)


@mcp.tool()
async def load_graph() -> str:
    """Reload the graph from disk, rebuilding the scheduler."""
    return await _post("load_graph")


@mcp.tool()
async def set_string_param(node_id: str, param: str, value: str) -> str:
    """Set a string parameter (e.g. file path) on a node.

    Args:
        node_id: Target node
        param: Parameter name
        value: String value (e.g. a file path)
    """
    return await _post("set_string_param", {"node_id": node_id, "param": param, "value": value})


@mcp.tool()
async def set_resolution(node_id: str, width: int, height: int) -> str:
    """Set GPU texture resolution for a node. Use 0,0 to reset to default.

    Args:
        node_id: Target node
        width: Texture width in pixels
        height: Texture height in pixels
    """
    return await _post("set_resolution", {"node_id": node_id, "width": width, "height": height})


@mcp.tool()
async def set_node_layout(node_id: str, x: float, y: float) -> str:
    """Set the visual position of a node in the graph editor.

    Args:
        node_id: Target node
        x: X position
        y: Y position
    """
    return await _post("set_node_layout", {"node_id": node_id, "x": x, "y": y})


@mcp.tool()
async def inspect_node(node_id: str) -> str:
    """Inspect a single node: params with live values, input/output port values.

    Args:
        node_id: The node to inspect
    """
    return await _post("inspect", {"node_id": node_id})


@mcp.tool()
async def list_nodes() -> str:
    """List all nodes in the graph (lightweight: just id and type)."""
    return await _post("list_nodes")


@mcp.tool()
async def add_midi_mapping(node_id: str, param: str, cc: int, channel: int,
                           range_min: float, range_max: float) -> str:
    """Map a MIDI CC to a node parameter.

    Args:
        node_id: Target node
        param: Parameter name to control
        cc: MIDI CC number (0-127)
        channel: MIDI channel (0-15)
        range_min: Parameter value when CC is 0
        range_max: Parameter value when CC is 127
    """
    return await _post("add_midi_mapping", {
        "node_id": node_id, "param": param,
        "cc": cc, "channel": channel,
        "range_min": range_min, "range_max": range_max,
    })


@mcp.tool()
async def remove_midi_mapping(node_id: str, param: str) -> str:
    """Remove a MIDI CC mapping from a node parameter.

    Args:
        node_id: Target node
        param: Parameter name to unmap
    """
    return await _post("remove_midi_mapping", {"node_id": node_id, "param": param})


@mcp.tool()
async def update_midi_mapping(node_id: str, param: str,
                              range_min: float, range_max: float) -> str:
    """Update the range of an existing MIDI mapping.

    Args:
        node_id: Target node
        param: Parameter name
        range_min: New minimum value
        range_max: New maximum value
    """
    return await _post("update_midi_mapping", {
        "node_id": node_id, "param": param,
        "range_min": range_min, "range_max": range_max,
    })


@mcp.tool()
async def get_graph_errors() -> str:
    """Get a list of nodes that are in an error state."""
    return await _post("get_graph_errors")


@mcp.tool()
async def scaffold_operator(name: str, domain: str, variant: str = "") -> str:
    """Create a new operator from a template. Writes source, patches CMakeLists, triggers build.

    Args:
        name: Operator name in lowercase_with_underscores (e.g. "tone_gen")
        domain: One of "control", "audio", "gpu"
        variant: Template variant. Use "composite" for a ChildOp-based control operator with internal LFO + Smooth.
    """
    body: dict = {"name": name, "domain": domain}
    if variant:
        body["variant"] = variant
    return await _post("scaffold_operator", body)


@mcp.tool()
async def save_variation(name: str) -> str:
    """Save a named snapshot of all current parameter values across every node.

    Args:
        name: Name for the variation (e.g. "Intro", "Drop")
    """
    return await _post("save_variation", {"name": name})


@mcp.tool()
async def recall_variation(name: str) -> str:
    """Instantly recall a saved variation, restoring all parameter values.

    Args:
        name: Name of the variation to recall
    """
    return await _post("recall_variation", {"name": name})


@mcp.tool()
async def remove_variation(name: str) -> str:
    """Delete a saved variation.

    Args:
        name: Name of the variation to remove
    """
    return await _post("remove_variation", {"name": name})


@mcp.tool()
async def rename_variation(old_name: str, new_name: str) -> str:
    """Rename a saved variation.

    Args:
        old_name: Current name
        new_name: New name
    """
    return await _post("rename_variation", {"old_name": old_name, "new_name": new_name})


@mcp.tool()
async def update_variation(name: str) -> str:
    """Overwrite a variation with the current parameter values (re-save in place).

    Args:
        name: Name of the variation to update
    """
    return await _post("update_variation", {"name": name})


@mcp.tool()
async def list_variations() -> str:
    """List all saved variations. Active variation is marked with *."""
    return await _post("list_variations")


@mcp.tool()
async def queue_variation(name: str, quantize: str = "instant") -> str:
    """Queue a variation switch, optionally quantized to a beat boundary.

    Args:
        name: Name of the variation to switch to
        quantize: Timing — "instant", "beat", "bar", or "4bar"
    """
    return await _post("queue_variation", {"name": name, "quantize": quantize})


@mcp.tool()
async def set_quantize_clock(node_id: str) -> str:
    """Designate a Clock node for beat-synced variation switching.

    Args:
        node_id: ID of a Clock node whose beat_phase output drives quantization
    """
    return await _post("set_quantize_clock", {"node_id": node_id})


@mcp.tool()
async def save_preset(node_id: str, name: str) -> str:
    """Save a named preset of the current parameter values for a single operator instance.

    Args:
        node_id: The node to snapshot
        name: Name for the preset (e.g. "intro_chords")
    """
    return await _post("save_preset", {"node_id": node_id, "name": name})


@mcp.tool()
async def recall_preset(node_id: str, name: str) -> str:
    """Recall a saved preset, restoring that operator's parameter values.

    Args:
        node_id: The node to restore
        name: Name of the preset to recall
    """
    return await _post("recall_preset", {"node_id": node_id, "name": name})


@mcp.tool()
async def update_preset(node_id: str, name: str) -> str:
    """Overwrite an existing preset with the operator's current parameter values.

    Args:
        node_id: The node whose current params to save
        name: Name of the preset to update
    """
    return await _post("update_preset", {"node_id": node_id, "name": name})


@mcp.tool()
async def remove_preset(node_id: str, name: str) -> str:
    """Delete a saved preset from an operator.

    Args:
        node_id: The node owning the preset
        name: Name of the preset to remove
    """
    return await _post("remove_preset", {"node_id": node_id, "name": name})


@mcp.tool()
async def rename_preset(node_id: str, old_name: str, new_name: str) -> str:
    """Rename a saved preset on an operator.

    Args:
        node_id: The node owning the preset
        old_name: Current preset name
        new_name: New preset name
    """
    return await _post("rename_preset", {"node_id": node_id, "old_name": old_name, "new_name": new_name})


@mcp.tool()
async def list_presets(node_id: str) -> str:
    """List all saved presets for an operator instance.

    Args:
        node_id: The node to list presets for
    """
    return await _post("list_presets", {"node_id": node_id})


@mcp.tool()
async def set_state_preset(sm_node: str, state_idx: int, target_node: str, preset_name: str) -> str:
    """Bind a preset to a state machine state. When the state machine enters this state, the preset is recalled on the target node.

    Args:
        sm_node: ID of the StateMachine node
        state_idx: State index (0-7)
        target_node: ID of the node whose preset to recall
        preset_name: Name of the preset to recall
    """
    return await _post("set_state_preset", {
        "sm_node": sm_node, "state_idx": state_idx,
        "target_node": target_node, "preset_name": preset_name
    })


@mcp.tool()
async def remove_state_preset(sm_node: str, state_idx: int, target_node: str) -> str:
    """Remove a preset binding from a state machine state.

    Args:
        sm_node: ID of the StateMachine node
        state_idx: State index (0-7)
        target_node: ID of the target node to unbind
    """
    return await _post("remove_state_preset", {
        "sm_node": sm_node, "state_idx": state_idx, "target_node": target_node
    })


@mcp.tool()
async def clear_state_presets(sm_node: str) -> str:
    """Remove all preset bindings from a state machine.

    Args:
        sm_node: ID of the StateMachine node
    """
    return await _post("clear_state_presets", {"sm_node": sm_node})


@mcp.tool()
async def inspect_state_presets(sm_node: str) -> str:
    """Show all preset bindings for a state machine, organized by state index.

    Args:
        sm_node: ID of the StateMachine node
    """
    return await _post("inspect_state_presets", {"sm_node": sm_node})


@mcp.tool()
async def browse_packages() -> str:
    """Browse the package catalog. Returns available packages with install status, category, and tags."""
    return await _post("package_catalog")


@mcp.tool()
async def install_package(url: str) -> str:
    """Install an operator package from a git URL or local path.

    Args:
        url: Git URL (e.g. "https://github.com/user/vivid-drums") or local directory path
    """
    return await _post("install_package", {"url": url})


@mcp.tool()
async def uninstall_package(name: str) -> str:
    """Uninstall an operator package by name.

    Args:
        name: Package name (e.g. "vivid-drums")
    """
    return await _post("uninstall_package", {"name": name})


@mcp.tool()
async def link_package(path: str) -> str:
    """Link a local package directory for development. Creates a symlink instead
    of copying, so edits to the source are picked up on rebuild without reinstalling.

    Args:
        path: Absolute or relative path to the package directory (must contain vivid-package.json)
    """
    return await _post("link_package", {"path": path})


@mcp.tool()
async def unlink_package(name: str) -> str:
    """Unlink a development package. Removes the symlink but does not touch the source directory.

    Args:
        name: Package name (e.g. "vivid-glitch")
    """
    return await _post("unlink_package", {"name": name})


@mcp.tool()
async def rebuild_package(name: str) -> str:
    """Recompile operators for an installed or linked package. Use after editing
    source files in a linked package.

    Args:
        name: Package name (e.g. "vivid-glitch")
    """
    return await _post("rebuild_package", {"name": name})


@mcp.tool()
async def list_packages() -> str:
    """List installed operator packages with their operators."""
    return await _post("list_packages")


@mcp.tool()
async def read_package_docs(name: str) -> str:
    """Read the README documentation for an installed package."""
    return await _post("read_package_docs", {"name": name})


@mcp.tool()
async def list_package_examples(name: str) -> str:
    """List example graphs included with an installed package."""
    return await _post("list_package_examples", {"name": name})


@mcp.tool()
async def read_package_example(name: str, filename: str) -> str:
    """Read the content of an example graph from an installed package."""
    return await _post("read_package_example", {"name": name, "filename": filename})


@mcp.tool()
async def package_operator_docs(name: str) -> str:
    """Get detailed operator documentation for an installed package: params with types/ranges/defaults/choices, input/output ports, and domain."""
    return await _post("package_operator_docs", {"name": name})


@mcp.tool()
async def test_package(name: str) -> str:
    """Run tests for an installed package (graph + C++ tests). Returns per-test pass/fail/skip."""
    async with httpx.AsyncClient() as client:
        resp = await client.post(f"{VIVID_URL}/test_package",
                                  json={"name": name}, timeout=90.0)
        return resp.text


if __name__ == "__main__":
    mcp.run()
