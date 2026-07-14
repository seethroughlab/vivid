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
// ADR-0016: pick up edits to the shader library's FILES (an mtime poll, main thread). A body
// edit hot-swaps inside the live node; a header edit rebuilds that op's nodes.
void apply_shader_reloads(App& app);
// The pop-out output window. Callers do NOT normally call these: the Output node's `launch` /
// `display` params are the truth, and the frame loop reconciles the window from them (ADR-0014).
void open_popout(App& app, Window& win, int display_target);
void close_popout(App& app, Window& win);

}  // namespace vivid
