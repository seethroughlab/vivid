#!/usr/bin/env python3
"""Utilities for talking to the Vivid MCP bridge over one long-lived stdio session."""

from __future__ import annotations

import json
import os
import pathlib
import sys
from contextlib import AsyncExitStack
from typing import Any

from mcp.client.session import ClientSession
from mcp.client.stdio import StdioServerParameters, stdio_client
from mcp.types import TextContent


REPO_ROOT = pathlib.Path(__file__).resolve().parent.parent
BRIDGE_PATH = REPO_ROOT / "mcp" / "vivid_mcp.py"


class VividMCPSession:
    """Long-lived stdio MCP client for `mcp/vivid_mcp.py`."""

    def __init__(self, python_executable: str | None = None) -> None:
        self.python_executable = python_executable or sys.executable
        self._exit_stack: AsyncExitStack | None = None
        self._session: ClientSession | None = None

    async def __aenter__(self) -> "VividMCPSession":
        params = StdioServerParameters(
            command=self.python_executable,
            args=[str(BRIDGE_PATH)],
            env=os.environ.copy(),
            cwd=str(REPO_ROOT),
        )
        self._exit_stack = AsyncExitStack()
        read_stream, write_stream = await self._exit_stack.enter_async_context(stdio_client(params))
        self._session = await self._exit_stack.enter_async_context(ClientSession(read_stream, write_stream))
        await self._session.initialize()
        return self

    async def __aexit__(self, exc_type, exc, tb) -> None:
        if self._exit_stack is not None:
            await self._exit_stack.aclose()
            self._exit_stack = None
        self._session = None

    async def call_tool(self, name: str, arguments: dict[str, Any] | None = None) -> dict[str, Any]:
        if self._session is None:
            raise RuntimeError("VividMCPSession is not initialized")

        result = await self._session.call_tool(name, arguments or {})
        if result.isError:
            raise RuntimeError(f"MCP tool {name} returned an error result")

        if result.structuredContent is not None:
            return self._normalize_payload(result.structuredContent)

        text_parts: list[str] = []
        for item in result.content:
            if isinstance(item, TextContent):
                text_parts.append(item.text)

        raw = "".join(text_parts).strip()
        if not raw:
            raise RuntimeError(f"MCP tool {name} returned no text payload")

        try:
            return self._normalize_payload(json.loads(raw))
        except json.JSONDecodeError as exc:
            raise RuntimeError(f"MCP tool {name} returned invalid JSON: {exc}") from exc

    @staticmethod
    def _normalize_payload(payload: Any) -> dict[str, Any]:
        if isinstance(payload, dict) and isinstance(payload.get("result"), str):
            raw = payload["result"].strip()
            if raw.startswith("{") or raw.startswith("["):
                try:
                    nested = json.loads(raw)
                except json.JSONDecodeError:
                    pass
                else:
                    if isinstance(nested, dict):
                        return nested
        if isinstance(payload, dict):
            return payload
        raise RuntimeError(f"unexpected MCP payload shape: {type(payload).__name__}")
