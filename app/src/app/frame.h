#pragma once

namespace vivid {
struct App;
struct Window;

// Run the macOS frame loop for `win` against the shared `app`: drains queued MCP
// commands, publishes audio characteristics into the graph, applies drags +
// mappings, and renders the window each tick until it closes. Blocks until exit.
void run_frame_loop(App& app, Window& win);

}  // namespace vivid
