#pragma once
// UI-5: an operator's custom editor floated out into its own native OS window. Reuses the same
// VividEditorContext + operator_draw_bridge as the in-dock host (UI-4b); the difference is purely
// where it lives — a dedicated GLFW window with its own wgpu surface (GpuContext aux_editor_) and
// its own Renderer2D. Input is polled each frame (cursor + left button), which is all a drag-based
// editor needs; the full VividEditorEvent stream + host services (clipboard/cursor/focus) are a
// later extension.
#include "ui/renderer_2d.h"

#include <string>

struct GLFWwindow;

namespace vivid {

struct App;

class EditorWindow {
public:
    ~EditorWindow();
    bool is_open() const { return glfw_ != nullptr; }
    int  node() const { return node_; }

    // Open a native window hosting visual node `node`'s custom editor (must have one). `w`/`h` are
    // the initial logical size (from the op's editor metadata). Returns false on any GPU/window
    // failure (and leaves this closed).
    bool open(App& app, int node, const std::string& title, int w, int h);
    void close(App& app);

    // Per-frame: poll input + size, build the editor context, draw + present. Returns false when
    // the window should close (user closed it, the operator asked to close, or the node lost its
    // editor) — the caller then close()s it.
    bool render(App& app);

    GLFWwindow* glfw() const { return glfw_; }

private:
    GLFWwindow*     glfw_ = nullptr;
    ui::Renderer2D  renderer_;
    bool            renderer_ok_ = false;
    int             node_  = -1;
    int             fb_w_ = 0, fb_h_ = 0;
    double          prev_mx_ = 0.0, prev_my_ = 0.0;
};

}  // namespace vivid
