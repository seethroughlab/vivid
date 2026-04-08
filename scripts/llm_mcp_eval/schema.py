"""Convert MCP tool schemas to OpenAI and Anthropic tool-call formats."""

from __future__ import annotations

from typing import Any

from mcp.types import Tool


def mcp_to_openai(name: str, tool: Tool) -> dict[str, Any]:
    """Convert an MCP Tool to an OpenAI function-calling tool definition."""
    return {
        "type": "function",
        "function": {
            "name": name,
            "description": tool.description or "",
            "parameters": tool.inputSchema,
        },
    }


def mcp_to_anthropic(name: str, tool: Tool) -> dict[str, Any]:
    """Convert an MCP Tool to an Anthropic tool_use definition."""
    return {
        "name": name,
        "description": tool.description or "",
        "input_schema": tool.inputSchema,
    }
