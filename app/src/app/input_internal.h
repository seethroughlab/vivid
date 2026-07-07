#pragma once
// Phase D (#8): private interface between input.cpp (which owns install_input_callbacks + the four
// GLFW callbacks as thin, ORDER-PRESERVING dispatchers) and the per-concern input controllers.
//
// The mouse-button dispatch is a single ordered chain — GLFW calls one mouse_button function, and
// the precedence of its guards IS the behavior. So each controller exposes free functions that
// return `bool consumed` (mirroring the old early `return;`); the callback calls them in the exact
// same order and stops at the first that consumes the event. Key/scroll controllers likewise.
//
// Not a public header (not part of app/input.h): only the .cpp files in app/src/app/ include it.
struct GLFWwindow;
namespace vivid { struct Window; struct App; }

namespace vivid::input {

// ---- musical typing (input_typing.cpp) ----
// Handles the ` toggle + note/octave/velocity keys. Returns true when it swallowed the key
// (note-on/off need RELEASE, so this runs before the PRESS-only gate in key_callback).
bool typing_key(Window& win, App& app, int key, int action);

// ---- transport (input_transport.cpp) ----
bool transport_mouse(Window& win, App& app, int button, int action, double mx, double my);
bool transport_key(Window& win, App& app, int key);   // Space / R

// ---- plugin browser drag/drop (input_plugins.cpp) ----
bool plugins_scroll(Window& win, App& app, double yoff, double mx, double my);
bool plugins_release(Window& win, App& app, double mx, double my);      // drop a dragged plugin
void plugins_sidebar_press(Window& win, App& app, double mx, double my);  // PLUGINS-panel press (inside sidebar guard)

// ---- node graphs: visuals + audio-graph deep view (input_graph.cpp) ----
void graph_scroll(Window& win, App& app, double yoff, double mx, double my);   // zoom (never consumes)
bool graph_audio_dock(Window& win, App& app, int button, int action, double mx, double my);  // deep-view press
bool graph_drill_in(Window& win, App& app, int button, int action, double mx, double my);     // Device "Graph" button

// ---- clip editor routing (input_editor.cpp) ----
bool editor_key(Window& win, int key, int mods);                            // Esc close + on_key
bool editor_scroll(Window& win, double xoff, double yoff, int mods, double mx, double my);
bool editor_mouse(Window& win, int button, int action, double mx, double my, int mods);
void editor_open_clip(Window& win, App& app, int t, int sc, int tracks);   // double-click a grid cell

}  // namespace vivid::input
