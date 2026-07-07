#!/usr/bin/env python3
"""MCP <-> control-server parity guard (static; no running app).

The MCP bridge (mcp/vivid_mcp.py) is a hand-written proxy over the control server
(app/src/cli/control_server.cpp). The two surfaces can silently drift: a control handler
gets added with no MCP tool (agents can't reach the feature), or an MCP tool `_post`s a
method that no longer exists (a dead tool). This test parses both files and fails on either
kind of drift.

Run standalone (`uv run mcp/tests/test_mcp_parity.py`) or under pytest.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
CLI_DIR = ROOT / "app" / "src" / "cli"
# Handlers register in control_server.cpp and (after the #7 split) in the per-family
# control_handlers_*.cpp files. The register_* free functions name their Handlers& parameter
# `handlers_`, so the `handlers_["..."]` regex matches uniformly across all of them.
CONTROL_FILES = [CLI_DIR / "control_server.cpp"] + sorted(CLI_DIR.glob("control_handlers_*.cpp"))
BRIDGE = ROOT / "mcp" / "vivid_mcp.py"

# Control methods intentionally NOT exposed as their own MCP tool. Each needs a reason so a
# future omission fails loudly instead of hiding here.
INTENTIONALLY_UNEXPOSED = {
    "set_playing": "exposed as the higher-level play()/stop() tools",
}


def control_methods() -> set[str]:
    methods: set[str] = set()
    for f in CONTROL_FILES:
        methods |= set(re.findall(r'handlers_\[\s*"([a-z0-9_]+)"\s*\]', f.read_text()))
    return methods


def bridge_methods() -> set[str]:
    return set(re.findall(r'_post\(\s*"([a-z0-9_]+)"', BRIDGE.read_text()))


def check() -> tuple[set[str], set[str]]:
    control = control_methods()
    bridge = bridge_methods()
    missing = control - bridge - set(INTENTIONALLY_UNEXPOSED)   # control handler, no MCP tool
    dangling = bridge - control                                  # MCP tool -> nonexistent method
    return missing, dangling


def test_mcp_control_parity():
    assert control_methods(), "parsed zero control handlers — regex/paths out of date"
    assert bridge_methods(), "parsed zero bridge _post calls — regex/paths out of date"
    missing, dangling = check()
    assert not missing, (
        "control methods with no MCP tool (expose them, or add to INTENTIONALLY_UNEXPOSED "
        f"with a reason): {sorted(missing)}"
    )
    assert not dangling, (
        f"MCP tools calling a nonexistent control method (typo, or handler removed): {sorted(dangling)}"
    )


if __name__ == "__main__":
    ctl, br = control_methods(), bridge_methods()
    miss, dang = check()
    print(f"control handlers: {len(ctl)} | bridge tools: {len(br)} | "
          f"intentionally unexposed: {len(INTENTIONALLY_UNEXPOSED)}")
    if miss:
        print(f"MISSING MCP tools for: {sorted(miss)}")
    if dang:
        print(f"DANGLING MCP tools (no control method): {sorted(dang)}")
    if miss or dang:
        sys.exit(1)
    print("PASS — MCP and control surfaces are in parity")
