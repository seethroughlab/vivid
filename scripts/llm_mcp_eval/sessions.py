"""Dual MCP session management — launches both Vivid MCP servers and exposes a unified tool surface."""

from __future__ import annotations

import json
import os
import pathlib
import sys
from contextlib import AsyncExitStack
from dataclasses import dataclass, field
from typing import Any

from mcp.client.session import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client
from mcp.types import TextContent, Tool

REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent.parent
MAIN_BRIDGE = REPO_ROOT / "mcp" / "vivid_mcp.py"
OPDEV_BRIDGE = REPO_ROOT / "mcp" / "vivid_opdev_mcp.py"

PREFIX_MAIN = "main__"
PREFIX_OPDEV = "opdev__"


@dataclass
class NamespacedTool:
    """An MCP tool with a namespace prefix for routing."""
    prefixed_name: str
    tool: Tool


class DualMCPSessions:
    """Async context manager that runs both MCP servers and presents a unified, namespaced tool surface."""

    def __init__(self, python_executable: str | None = None) -> None:
        self.python_executable = python_executable or sys.executable
        self._exit_stack: AsyncExitStack | None = None
        self._main_session: ClientSession | None = None
        self._opdev_session: ClientSession | None = None
        self._tools: dict[str, NamespacedTool] = {}
        self._all_tools: list[NamespacedTool] = []

    async def __aenter__(self) -> DualMCPSessions:
        self._exit_stack = AsyncExitStack()
        env = os.environ.copy()

        # Start main MCP server
        main_params = StdioServerParameters(
            command=self.python_executable,
            args=[str(MAIN_BRIDGE)],
            env=env,
            cwd=str(REPO_ROOT),
        )
        main_read, main_write = await self._exit_stack.enter_async_context(
            stdio_client(main_params)
        )
        self._main_session = await self._exit_stack.enter_async_context(
            ClientSession(main_read, main_write)
        )
        await self._main_session.initialize()

        # Start opdev MCP server
        opdev_params = StdioServerParameters(
            command=self.python_executable,
            args=[str(OPDEV_BRIDGE)],
            env=env,
            cwd=str(REPO_ROOT),
        )
        opdev_read, opdev_write = await self._exit_stack.enter_async_context(
            stdio_client(opdev_params)
        )
        self._opdev_session = await self._exit_stack.enter_async_context(
            ClientSession(opdev_read, opdev_write)
        )
        await self._opdev_session.initialize()

        # Discover and namespace all tools
        await self._discover_tools()
        return self

    async def __aexit__(self, exc_type, exc, tb) -> None:
        if self._exit_stack is not None:
            await self._exit_stack.aclose()
            self._exit_stack = None
        self._main_session = None
        self._opdev_session = None
        self._tools.clear()
        self._all_tools.clear()

    async def _discover_tools(self) -> None:
        self._tools.clear()
        self._all_tools.clear()

        main_result = await self._main_session.list_tools()
        for tool in main_result.tools:
            prefixed = PREFIX_MAIN + tool.name
            ns = NamespacedTool(prefixed_name=prefixed, tool=tool)
            self._tools[prefixed] = ns
            self._all_tools.append(ns)

        opdev_result = await self._opdev_session.list_tools()
        for tool in opdev_result.tools:
            prefixed = PREFIX_OPDEV + tool.name
            ns = NamespacedTool(prefixed_name=prefixed, tool=tool)
            self._tools[prefixed] = ns
            self._all_tools.append(ns)

    def list_tools(self, allowlist: set[str] | None = None) -> list[NamespacedTool]:
        """Return tool list, optionally filtered to an allowlist of prefixed names."""
        if allowlist is None:
            return list(self._all_tools)
        return [t for t in self._all_tools if t.prefixed_name in allowlist]

    async def call_tool(self, prefixed_name: str, arguments: dict[str, Any] | None = None) -> str:
        """Route a prefixed tool call to the correct MCP session. Returns the raw text result."""
        if prefixed_name.startswith(PREFIX_MAIN):
            session = self._main_session
            real_name = prefixed_name[len(PREFIX_MAIN):]
        elif prefixed_name.startswith(PREFIX_OPDEV):
            session = self._opdev_session
            real_name = prefixed_name[len(PREFIX_OPDEV):]
        else:
            raise ValueError(f"Unknown tool prefix in {prefixed_name!r}")

        if session is None:
            raise RuntimeError("Sessions not initialized")

        result = await session.call_tool(real_name, arguments or {})

        # Extract text content
        text_parts: list[str] = []
        if result.structuredContent is not None:
            return json.dumps(result.structuredContent, separators=(",", ":"), sort_keys=True)
        for item in result.content:
            if isinstance(item, TextContent):
                text_parts.append(item.text)
        return "".join(text_parts).strip()
