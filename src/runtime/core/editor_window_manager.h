#pragma once

#include "operator_api/types.h"
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace vivid {

class GpuContext;
class RuntimeCore;
struct Settings;
namespace ui { class UICommandSink; }

// Owns and ticks dedicated editor windows for operators that export
// VIVID_EDITOR. Each window runs on its own GLFWwindow + WGPUSurface sharing
// the main device/queue (see src/runtime/debug/output_window.cpp for the
// secondary-window pattern this class mirrors).
//
// Lifecycle: the manager does not cache OperatorLoader* or instance pointers.
// The owning node is resolved from runtime.compiled_graph()->find_node(id)
// every frame; a missing node, missing editor capability, or a reload_serial
// bump tears the window down on the next tick.
class EditorWindowManager {
public:
    // Called per-frame so theme reflects live UI style.
    using ThemeProvider = std::function<VividInspectorTheme()>;

    EditorWindowManager(GpuContext& gpu,
                        RuntimeCore& runtime,
                        ui::UICommandSink& commands,
                        ThemeProvider theme_provider,
                        std::string font_path,
                        float font_pt,
                        Settings* settings = nullptr);
    ~EditorWindowManager();

    EditorWindowManager(const EditorWindowManager&) = delete;
    EditorWindowManager& operator=(const EditorWindowManager&) = delete;

    // Open the editor for node_id, or refocus if one is already open.
    // Returns true if a window exists after the call.
    bool open(const std::string& node_id);

    bool is_open(const std::string& node_id) const;
    void focus(const std::string& node_id);
    void close(const std::string& node_id);
    void close_all();

    // Render every live editor once. Call after the primary window's end_frame.
    // time is the same clock passed to operator draw callbacks.
    void tick(double time);

    // ----- Test-support surface (follow-up: second-window automated coverage)
    //
    // The script-runner injection API lets automated tests drive editor
    // windows without racing the GLFW event pump. Events land in the
    // target editor's pending_events queue exactly like GLFW callbacks
    // would; mouse-state helpers update the cached `VividEditorMouse`
    // so operator widgets see consistent left_down / shift_down between
    // frames.
    //
    // capture_surface_png() reads back the editor's most recently
    // rendered frame via the same WGPU texture → staging buffer → PNG
    // path main_helpers.h exposes for the main window. Returns
    // std::nullopt if no editor is open for node_id or the readback
    // fails.

    bool inject_event(const std::string& node_id, const VividEditorEvent& event);
    bool inject_mouse_move(const std::string& node_id, float x, float y);
    bool inject_mouse_button(const std::string& node_id,
                             int button, int action, int mods);
    bool inject_key(const std::string& node_id,
                    int key, int scancode, int action, int mods);
    bool inject_char(const std::string& node_id, uint32_t codepoint);

    std::optional<std::vector<uint8_t>>
    capture_surface_png(const std::string& node_id);

    // When set to true, future open() calls create editor windows with
    // GLFW_VISIBLE=GLFW_FALSE so they render offscreen on headless CI.
    // Existing open windows are unaffected.
    void set_hidden_when_opening(bool hidden);

    // Test-only accessor: number of pending events queued for the named
    // editor window (0 if not open).
    std::size_t pending_event_count(const std::string& node_id) const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vivid
