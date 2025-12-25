#pragma once

namespace vivid::mcp {

/// Run the MCP server over stdio
/// Connects to running Vivid instance via WebSocket and provides tools for Claude
/// @return Exit code (0 = success)
int runServer();

} // namespace vivid::mcp
