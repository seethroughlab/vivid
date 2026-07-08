#pragma once
// UI-5: an operator's custom editor floated out into its own native OS window. Reuses the same
// VividEditorContext + operator_draw_bridge as the in-dock host (UI-4b); the difference is purely
// where it lives — a dedicated GLFW window with its own wgpu surface (GpuContext aux_editor_) and
// its own Renderer2D. UI-5.4: full input — a VividEditorEvent stream (mouse/scroll/key/char) +
// mouse edges, collected by GLFW callbacks on this window, plus the VividEditorHostAPI services
// (clipboard/cursor/focus), so keyboard/clipboard editors work, not just drag.
#include "operator_api/types.h"   // VividEditorEvent / VividCursorKind
#include "ui/renderer_2d.h"

#include <string>
#include <vector>

struct GLFWwindow;
struct GLFWcursor;

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

    // Per-frame: build the editor context from the events collected since the last frame, draw +
    // present. Returns false when the window should close (user closed it, the operator asked to
    // close, or the node lost its editor) — the caller then close()s it.
    bool render(App& app);

    GLFWwindow* glfw() const { return glfw_; }

    // GLFW input callbacks (installed on this window; the GLFW user pointer is this EditorWindow).
    // They accumulate VividEditorEvents + track the mouse edge/level state consumed by render().
    void on_cursor(double x, double y);
    void on_button(int button, int action, int mods);
    void on_scroll(double dx, double dy);
    void on_key(int key, int scancode, int action, int mods);
    void on_char(unsigned int codepoint);

    // Host-service surface (VividEditorHostAPI), backed by GLFW on this window. Called by the
    // C trampolines built in render() (opaque = this).
    void set_cursor_kind(VividCursorKind kind) { requested_cursor_ = kind; }   // applied at frame end
    void capture_pointer()          { pointer_captured_ = true; }
    void release_pointer()          { pointer_captured_ = false; }
    bool has_pointer_capture() const { return pointer_captured_; }
    void set_status(const char* s)  { status_ = s ? s : ""; }
    const std::string& status() const { return status_; }

private:
    GLFWwindow*     glfw_ = nullptr;
    ui::Renderer2D  renderer_;
    bool            renderer_ok_ = false;
    int             node_  = -1;
    int             fb_w_ = 0, fb_h_ = 0;

    // Input collected between frames (editor-window-local logical px).
    std::vector<VividEditorEvent> events_;
    double  mx_ = 0.0, my_ = 0.0, prev_mx_ = 0.0, prev_my_ = 0.0;
    bool    left_down_ = false, left_clicked_ = false, left_released_ = false, right_clicked_ = false;
    bool    shift_down_ = false;

    // Cursor: the op requests a shape each frame via the host API; we apply it after draw + reset.
    VividCursorKind requested_cursor_ = VIVID_CURSOR_DEFAULT;
    GLFWcursor*     cursors_[9] = {};   // lazily-created standard cursors, indexed by VividCursorKind
    void apply_cursor(VividCursorKind kind);
    bool            pointer_captured_ = false;
    std::string     status_;   // set by the op via host.set_status_text; drawn in a footer strip
};

}  // namespace vivid
