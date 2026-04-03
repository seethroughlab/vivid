#pragma once

#include "operator_api/input_state.h"
#include "runtime/debug/ui_test_runner.h"
#include <GLFW/glfw3.h>
#include <atomic>
#include <string>
#include <vector>

namespace vivid::ui { class NodeGraphUI; }
namespace vivid { class RuntimeAPI; class Graph; struct Settings; }

// WindowUserData lives at global scope (matches original definition)
struct WindowUserData {
    vivid::ui::NodeGraphUI* graph_ui = nullptr;
    vivid::RuntimeAPI* runtime_api = nullptr;
    vivid::Graph* graph = nullptr;
    vivid::Settings* settings = nullptr;

    // Input forwarding to operators (when UI hidden)
    std::vector<VividInputEvent> pending_events;
    double raw_mouse_x = 0.0, raw_mouse_y = 0.0;  // window coords
    int buttons_held = 0;   // bitmask: bit 0=left, 1=right, 2=middle
    int current_mods = 0;

    // Drag-and-drop graph loading
    std::string pending_drop_path;
};

namespace vivid {

// Monitor topology serial — incremented on monitor connect/disconnect
extern std::atomic<uint64_t> g_monitor_topology_serial;

void refresh_window_title(GLFWwindow* window, const std::string& graph_path,
                          bool analysis_enabled);

void monitor_callback(GLFWmonitor* monitor, int event);
bool monitor_connected(GLFWmonitor* monitor);
GLFWmonitor* monitor_for_window(GLFWwindow* window);
GLFWmonitor* monitor_for_target(int target, GLFWwindow* window);
void clamp_window_rect_to_monitor(GLFWmonitor* monitor, int* x, int* y, int* w, int* h);

void run_ui_test_script_frame(UITestScript& script,
                              vivid::ui::NodeGraphUI& graph_ui,
                              WindowUserData& window_user_data,
                              std::string& screenshot_path,
                              int& screenshot_delay,
                              uint64_t frame_count);

// GLFW callbacks
void char_callback(GLFWwindow* w, unsigned int codepoint);
void key_callback(GLFWwindow* w, int key, int scancode, int action, int mods);
void cursor_pos_callback(GLFWwindow* w, double xpos, double ypos);
void mouse_button_callback(GLFWwindow* w, int button, int action, int mods);
void scroll_callback(GLFWwindow* w, double xoffset, double yoffset);
void drop_callback(GLFWwindow* w, int count, const char** paths);

} // namespace vivid
