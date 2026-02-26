"""Vivid MCP Server — bridges MCP stdio to the Vivid runtime HTTP control server."""

import os
import json
import httpx
from mcp.server.fastmcp import FastMCP

VIVID_URL = os.environ.get("VIVID_URL", "http://127.0.0.1:9876")

mcp = FastMCP("vivid")


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


if __name__ == "__main__":
    mcp.run()
