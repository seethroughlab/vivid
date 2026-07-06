#pragma once
struct GLFWwindow;

namespace vivid {
// Register the GLFW key/char/scroll/mouse-button handlers on `w`. The window's
// user pointer must already point at an AudioState. Call after the session/graph
// exist (the handlers read them via the user pointer).
void install_input_callbacks(GLFWwindow* w);
}  // namespace vivid
