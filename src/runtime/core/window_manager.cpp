#include "runtime/core/window_manager.h"
#include "runtime/core/editor_window_manager.h"
#include "ui/graph/node_graph.h"
#include <cstdio>
#include <filesystem>

#ifndef VIVID_CORE_VERSION
#define VIVID_CORE_VERSION "0.1.0"
#endif

namespace vivid {

std::atomic<uint64_t> g_monitor_topology_serial{0};

void refresh_window_title(GLFWwindow* window, const std::string& graph_path,
                                  bool analysis_enabled) {
    if (!window) return;
    std::string title = "Vivid";
    if (analysis_enabled) title += " [Analysis]";
    if (!graph_path.empty()) {
        std::string file = std::filesystem::path(graph_path).filename().string();
        if (!file.empty()) {
            title += " - ";
            title += file;
        }
    } else {
        title += " - Unsaved Document";
    }
    glfwSetWindowTitle(window, title.c_str());
}

void monitor_callback(GLFWmonitor* /*monitor*/, int event) {
    const char* ev = (event == GLFW_CONNECTED) ? "connected" :
                     (event == GLFW_DISCONNECTED) ? "disconnected" : "unknown";
    const uint64_t serial = g_monitor_topology_serial.fetch_add(1, std::memory_order_relaxed) + 1;
    std::fprintf(stderr, "[vivid] Monitor topology changed: %s (serial=%llu)\n",
                 ev, static_cast<unsigned long long>(serial));
}

bool monitor_connected(GLFWmonitor* monitor) {
    if (!monitor) return false;
    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    for (int i = 0; i < count; ++i) {
        if (monitors[i] == monitor) return true;
    }
    return false;
}

GLFWmonitor* monitor_for_window(GLFWwindow* window) {
    if (!window) return glfwGetPrimaryMonitor();
    int wx = 0, wy = 0, ww = 0, wh = 0;
    glfwGetWindowPos(window, &wx, &wy);
    glfwGetWindowSize(window, &ww, &wh);

    int count = 0;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    GLFWmonitor* best = glfwGetPrimaryMonitor();
    long best_overlap = -1;
    for (int i = 0; i < count; ++i) {
        int mx = 0, my = 0, mw = 0, mh = 0;
        glfwGetMonitorWorkarea(monitors[i], &mx, &my, &mw, &mh);
        int ix = std::max(wx, mx);
        int iy = std::max(wy, my);
        int ax = std::min(wx + ww, mx + mw);
        int ay = std::min(wy + wh, my + mh);
        long overlap = 0;
        if (ax > ix && ay > iy)
            overlap = static_cast<long>(ax - ix) * static_cast<long>(ay - iy);
        if (overlap > best_overlap) {
            best_overlap = overlap;
            best = monitors[i];
        }
    }
    return best;
}

GLFWmonitor* monitor_for_target(int target, GLFWwindow* window) {
    GLFWmonitor* primary = glfwGetPrimaryMonitor();
    if (target == 1) return primary;
    if (target == 2) {
        int count = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&count);
        for (int i = 0; i < count; ++i) {
            if (monitors[i] != primary) return monitors[i];
        }
        return primary;
    }
    return monitor_for_window(window);
}

void clamp_window_rect_to_monitor(GLFWmonitor* monitor, int* x, int* y, int* w, int* h) {
    if (!x || !y || !w || !h) return;
    if (!monitor) monitor = glfwGetPrimaryMonitor();
    if (!monitor) return;
    int mx = 0, my = 0, mw = 0, mh = 0;
    glfwGetMonitorWorkarea(monitor, &mx, &my, &mw, &mh);
    *w = std::max(640, std::min(*w, mw));
    *h = std::max(480, std::min(*h, mh));
    *x = std::max(mx, std::min(*x, mx + mw - *w));
    *y = std::max(my, std::min(*y, my + mh - *h));
}

void run_ui_test_script_frame(vivid::UITestScript& script,
                                     vivid::ui::NodeGraphUI& graph_ui,
                                     WindowUserData& window_user_data,
                                     std::string& screenshot_path,
                                     int& screenshot_delay,
                                     uint64_t frame_count,
                                     vivid::EditorWindowManager* editor_windows) {
    if (script.actions.empty() || script.next_action >= script.actions.size())
        return;
    if (script.wait_frames_remaining > 0) {
        --script.wait_frames_remaining;
        return;
    }

    while (script.next_action < script.actions.size()) {
        const auto& action = script.actions[script.next_action++];
        // Secondary-window routing: when target_window is set and an
        // editor manager is available, route mouse/key/char into that
        // editor's pending_events queue instead of the main graph UI.
        const bool to_editor = editor_windows && !action.target_window.empty();
        switch (action.type) {
            case vivid::UITestActionType::Wait:
                script.wait_frames_remaining = std::max(0, action.frames);
                return;
            case vivid::UITestActionType::MouseMove:
                if (to_editor) {
                    editor_windows->inject_mouse_move(action.target_window,
                                                      action.x, action.y);
                } else {
                    window_user_data.raw_mouse_x = action.x;
                    window_user_data.raw_mouse_y = action.y;
                    graph_ui.on_mouse_move(action.x, action.y);
                }
                std::fprintf(stderr,
                             "[vivid] UI script mouse_move to (%.1f, %.1f) target=%s on frame %llu\n",
                             static_cast<double>(action.x),
                             static_cast<double>(action.y),
                             action.target_window.empty() ? "main"
                                                           : action.target_window.c_str(),
                             static_cast<unsigned long long>(frame_count));
                break;
            case vivid::UITestActionType::MouseButton:
                if (to_editor) {
                    editor_windows->inject_mouse_button(action.target_window,
                                                         action.button,
                                                         action.mouse_action,
                                                         action.mods);
                } else {
                    window_user_data.current_mods = action.mods;
                    if (action.button >= 0 && action.button <= 2) {
                        if (action.mouse_action == GLFW_PRESS)
                            window_user_data.buttons_held |= (1 << action.button);
                        else if (action.mouse_action == GLFW_RELEASE)
                            window_user_data.buttons_held &= ~(1 << action.button);
                    }
                    graph_ui.on_mouse_button(action.button, action.mouse_action, action.mods);
                }
                std::fprintf(stderr,
                             "[vivid] UI script mouse_button %d/%d target=%s on frame %llu\n",
                             action.button, action.mouse_action,
                             action.target_window.empty() ? "main"
                                                           : action.target_window.c_str(),
                             static_cast<unsigned long long>(frame_count));
                break;
            case vivid::UITestActionType::Key:
                if (to_editor) {
                    editor_windows->inject_key(action.target_window,
                                                action.key, 0,
                                                action.key_action, action.mods);
                } else {
                    window_user_data.current_mods = action.mods;
                    graph_ui.on_key(action.key, action.key_action, action.mods);
                }
                std::fprintf(stderr,
                             "[vivid] UI script key %d/%d mods=%d target=%s on frame %llu\n",
                             action.key, action.key_action, action.mods,
                             action.target_window.empty() ? "main"
                                                           : action.target_window.c_str(),
                             static_cast<unsigned long long>(frame_count));
                break;
            case vivid::UITestActionType::CharInput:
                if (to_editor) {
                    editor_windows->inject_char(action.target_window, action.codepoint);
                } else {
                    graph_ui.on_char(action.codepoint);
                }
                std::fprintf(stderr,
                             "[vivid] UI script char %u target=%s on frame %llu\n",
                             action.codepoint,
                             action.target_window.empty() ? "main"
                                                           : action.target_window.c_str(),
                             static_cast<unsigned long long>(frame_count));
                break;
            case vivid::UITestActionType::Screenshot: {
                // Main-window screenshot scheduled (editor screenshots go
                // through the --editor-screenshot CLI, fired at end of run).
                std::filesystem::path shot_path(action.screenshot_path);
                if (!shot_path.is_absolute())
                    shot_path = script.source_dir / shot_path;
                screenshot_path = shot_path.string();
                screenshot_delay = static_cast<int>(frame_count) + action.screenshot_delay;
                std::fprintf(stderr,
                             "[vivid] UI script scheduled screenshot: %s at frame %d\n",
                             screenshot_path.c_str(), screenshot_delay);
                return;
            }
            case vivid::UITestActionType::Checkpoint:
                script.pending_checkpoint_labels.push_back(action.checkpoint_label);
                std::fprintf(stderr,
                             "[vivid] UI script checkpoint queued: %s on frame %llu\n",
                             action.checkpoint_label.c_str(),
                             static_cast<unsigned long long>(frame_count));
                break;
            case vivid::UITestActionType::OpenEditor:
                if (editor_windows && !action.target_window.empty()) {
                    editor_windows->open(action.target_window);
                    std::fprintf(stderr,
                                 "[vivid] UI script open_editor target=%s on frame %llu\n",
                                 action.target_window.c_str(),
                                 static_cast<unsigned long long>(frame_count));
                } else {
                    std::fprintf(stderr,
                                 "[vivid] UI script open_editor missing target / editor manager\n");
                }
                break;
        }
    }
}

void char_callback(GLFWwindow* w, unsigned int codepoint) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (!ud) return;
    if (ud->graph_ui && ud->graph_ui->visible()) {
        if (ud->graph_ui->wants_keyboard())
            ud->graph_ui->on_char(codepoint);
    } else {
        VividInputEvent ev{};
        ev.type = VIVID_INPUT_CHAR;
        ev.codepoint = codepoint;
        ev.mouse_x = static_cast<float>(ud->raw_mouse_x);
        ev.mouse_y = static_cast<float>(ud->raw_mouse_y);
        ev.modifiers = ud->current_mods;
        ud->pending_events.push_back(ev);
    }
}

void key_callback(GLFWwindow* w, int key, int scancode, int action, int mods) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (!ud) return;

    ud->current_mods = mods;

    // Tilde toggles graph UI visibility (intercept before any dispatch)
    if (key == GLFW_KEY_GRAVE_ACCENT && action == GLFW_PRESS && mods == 0) {
        if (ud->graph_ui) ud->graph_ui->toggle_visible();
        return;
    }

    // Cmd+E on macOS / Ctrl+E elsewhere opens the selected node's editor if
    // its operator exports VIVID_EDITOR. Manager open() silently no-ops for
    // non-editor operators, which gives the desired "otherwise do nothing"
    // semantics without adding extra UI-side branching here.
    if (is_open_editor_shortcut(key, action, mods) &&
        ud->graph_ui && ud->editor_windows &&
        ud->graph_ui->has_single_selection()) {
        ud->editor_windows->open(ud->graph_ui->single_selected_id());
        return;
    }

    // Cmd+S and Cmd+, are handled by the native macOS menu bar (macos_menu.mm).
    // On non-Apple platforms, fall through to the graph UI key handler.

    if (ud->graph_ui && ud->graph_ui->visible()) {
        ud->graph_ui->on_key(key, action, mods);
    } else {
        VividInputEvent ev{};
        ev.type = VIVID_INPUT_KEY;
        ev.key = key;
        ev.scancode = scancode;
        ev.action = action;
        ev.modifiers = mods;
        ev.mouse_x = static_cast<float>(ud->raw_mouse_x);
        ev.mouse_y = static_cast<float>(ud->raw_mouse_y);
        ud->pending_events.push_back(ev);
    }
}

void cursor_pos_callback(GLFWwindow* w, double xpos, double ypos) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (!ud) return;
    ud->raw_mouse_x = xpos;
    ud->raw_mouse_y = ypos;
    if (ud->graph_ui && ud->graph_ui->visible()) {
        ud->graph_ui->on_mouse_move(static_cast<float>(xpos), static_cast<float>(ypos));
    } else {
        VividInputEvent ev{};
        ev.type = VIVID_INPUT_MOUSE_MOVE;
        ev.mouse_x = static_cast<float>(xpos);  // will be normalized later
        ev.mouse_y = static_cast<float>(ypos);
        ev.modifiers = ud->current_mods;
        ev.button = -1;
        ud->pending_events.push_back(ev);
    }
}

void mouse_button_callback(GLFWwindow* w, int button, int action, int mods) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (!ud) return;
    ud->current_mods = mods;
    // Track button state
    if (button >= 0 && button <= 2) {
        if (action == GLFW_PRESS)
            ud->buttons_held |= (1 << button);
        else if (action == GLFW_RELEASE)
            ud->buttons_held &= ~(1 << button);
    }
    if (ud->graph_ui && ud->graph_ui->visible()) {
        ud->graph_ui->on_mouse_button(button, action, mods);
    } else {
        VividInputEvent ev{};
        ev.type = VIVID_INPUT_MOUSE_BUTTON;
        ev.button = button;
        ev.action = action;
        ev.modifiers = mods;
        ev.mouse_x = static_cast<float>(ud->raw_mouse_x);
        ev.mouse_y = static_cast<float>(ud->raw_mouse_y);
        ud->pending_events.push_back(ev);
    }
}

void scroll_callback(GLFWwindow* w, double xoffset, double yoffset) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (!ud) return;
    if (ud->graph_ui && ud->graph_ui->visible()) {
        int mods = 0;
        if (glfwGetKey(w, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
            glfwGetKey(w, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS)
            mods |= GLFW_MOD_SUPER;
        ud->graph_ui->on_scroll(
            static_cast<float>(xoffset), static_cast<float>(yoffset), mods);
    } else {
        VividInputEvent ev{};
        ev.type = VIVID_INPUT_MOUSE_SCROLL;
        ev.scroll_dx = static_cast<float>(xoffset);
        ev.scroll_dy = static_cast<float>(yoffset);
        ev.mouse_x = static_cast<float>(ud->raw_mouse_x);
        ev.mouse_y = static_cast<float>(ud->raw_mouse_y);
        ev.modifiers = ud->current_mods;
        ud->pending_events.push_back(ev);
    }
}

void drop_callback(GLFWwindow* w, int count, const char** paths) {
    auto* ud = static_cast<WindowUserData*>(glfwGetWindowUserPointer(w));
    if (!ud || count < 1) return;
    ud->pending_drop_path = paths[0];
}

} // namespace vivid
