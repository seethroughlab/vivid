#pragma once

namespace vivid {
struct App;
struct Window;

// Run the macOS frame loop for `win` against the shared `app`: drains queued MCP
// commands, publishes audio characteristics into the graph, applies drags +
// mappings, and renders the window each tick until it closes. Blocks until exit.
void run_frame_loop(App& app, Window& win);
void reap_plugin_windows(App& app, Window& win);
void publish_bridge_sources(App& app, Window& win);
void apply_audio_param_mappings(App& app);
void update_drag_continuations(App& app, Window& win, double mx, double my);
void update_visual_source_frame(App& app);

}  // namespace vivid
