#pragma once

#include <string>

// Locating the MCP bridge that ships INSIDE the app bundle (ADR-0040 follow-up).
//
// ADR-0040 makes "Vivid is an MCP-native creative coding app" a product gate, but until now the
// bridge only existed in a repo checkout: the release DMG carried the .app alone, and the only
// client config in the tree hardcoded the maintainer's path. A user who downloaded a release had
// no way to connect an agent at all. The bridge is now copied into Contents/Resources/mcp by the
// `vivid_mcp_bridge` build target, and this is how the app points a user at it.
namespace vivid::app {

// Absolute path to the bundled bridge directory, or "" if it isn't there. Resolution mirrors the
// other bundled resources (examples/, shaders/, operator_api/): VIVID_MCP_DIR override, then
// <app>/Contents/Resources/mcp, then <exe_dir>/mcp for a non-bundle build.
std::string mcp_bridge_dir();

// The one-line command that registers the bundled bridge with Claude Code, e.g.
//   claude mcp add vivid -- uv run --directory "/Applications/Vivid.app/Contents/Resources/mcp" vivid_mcp.py
// Returns "" when the bridge is missing, so callers can say so rather than print a broken command.
std::string mcp_setup_command();

}  // namespace vivid::app
