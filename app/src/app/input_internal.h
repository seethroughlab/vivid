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

}  // namespace vivid::input
