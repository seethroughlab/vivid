#include "app/editor_window.h"

#include <GLFW/glfw3.h>

#include "app/app.h"
#include "app/window_prefs.h"   // UI-5.4c: persist the float window's geometry
#include "gpu/gpu_context.h"
#include "ui/node_graph.h"
#include "ui/operator_draw_bridge.h"

#include <cstdio>
#include <string>
#include <vector>

#ifndef VIVID_FONT_PATH
#define VIVID_FONT_PATH ""
#endif

namespace vivid {

namespace {
inline VividColor col(float r, float g, float b, float a = 1.f) { return { r, g, b, a }; }

VividInspectorTheme editor_theme() {
    VividInspectorTheme t{};
    t.bg          = col(0.11f, 0.12f, 0.14f);
    t.accent      = col(0.36f, 0.78f, 0.92f);   // cyan (visual domain)
    t.dim_text    = col(0.55f, 0.57f, 0.60f);
    t.bright_text = col(0.85f, 0.87f, 0.90f);
    t.separator   = col(0.22f, 0.23f, 0.26f);
    t.dark_bg     = col(0.07f, 0.08f, 0.09f);
    t.slider_fill = col(0.36f, 0.78f, 0.92f);
    t.slider_track= col(0.18f, 0.19f, 0.22f);
    t.corner_radius = 3.f;
    return t;
}

// Command sink: an editor's set_param(name,value) → the node's base param by that name.
struct EditorCmd { ui::NodeGraph* g; int node; };
void editor_set_param(void* o, const char* name, float v) {
    auto* c = static_cast<EditorCmd*>(o);
    if (!c || !c->g || !name) return;
    const int pc = c->g->op_param_count_at(c->node);
    for (int l = 0; l < pc; ++l)
        if (std::string(c->g->op_param_label_at(c->node, l)) == name) {
            c->g->set_op_param_base_at(c->node, l, v);
            c->g->note_edit("Adjust Param", "editor-param");   // ADR-0017 (floated op editor)
            return;
        }
}

void clear_pass(WGPUCommandEncoder encoder, WGPUTextureView view, float r, float g, float b) {
    WGPURenderPassColorAttachment color{};
    color.view = view; color.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    color.loadOp = WGPULoadOp_Clear; color.storeOp = WGPUStoreOp_Store;
    color.clearValue = WGPUColor{ r, g, b, 1.0 };
    WGPURenderPassDescriptor rp{}; rp.colorAttachmentCount = 1; rp.colorAttachments = &color;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &rp);
    wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
}

// --- GLFW callback thunks (window user pointer is the EditorWindow) ---
EditorWindow* ew_of(GLFWwindow* w) { return static_cast<EditorWindow*>(glfwGetWindowUserPointer(w)); }
void cb_cursor(GLFWwindow* w, double x, double y)                 { if (auto* e = ew_of(w)) e->on_cursor(x, y); }
void cb_button(GLFWwindow* w, int b, int a, int m)               { if (auto* e = ew_of(w)) e->on_button(b, a, m); }
void cb_scroll(GLFWwindow* w, double dx, double dy)             { if (auto* e = ew_of(w)) e->on_scroll(dx, dy); }
void cb_key(GLFWwindow* w, int k, int sc, int a, int m)         { if (auto* e = ew_of(w)) e->on_key(k, sc, a, m); }
void cb_char(GLFWwindow* w, unsigned int cp)                    { if (auto* e = ew_of(w)) e->on_char(cp); }

// --- VividEditorHostAPI trampolines (opaque is the EditorWindow) ---
EditorWindow* host_ew(void* o) { return static_cast<EditorWindow*>(o); }
const char* h_get_clipboard(void* o) { auto* e = host_ew(o); return e && e->glfw() ? glfwGetClipboardString(e->glfw()) : ""; }
void h_set_clipboard(void* o, const char* t) { auto* e = host_ew(o); if (e && e->glfw()) glfwSetClipboardString(e->glfw(), t ? t : ""); }
void h_set_cursor(void* o, VividCursorKind k) { if (auto* e = host_ew(o)) e->set_cursor_kind(k); }
void h_capture(void* o)   { if (auto* e = host_ew(o)) e->capture_pointer(); }
void h_release(void* o)   { if (auto* e = host_ew(o)) e->release_pointer(); }
int  h_has_capture(void* o){ auto* e = host_ew(o); return e && e->has_pointer_capture() ? 1 : 0; }
void h_request_focus(void* o) { auto* e = host_ew(o); if (e && e->glfw()) glfwFocusWindow(e->glfw()); }
int  h_has_focus(void* o) { auto* e = host_ew(o); return (e && e->glfw() && glfwGetWindowAttrib(e->glfw(), GLFW_FOCUSED)) ? 1 : 0; }
void h_set_status(void* o, const char* t) { if (auto* e = host_ew(o)) e->set_status(t); }
void h_show_tooltip(void* o, const char* t) { if (auto* e = host_ew(o)) e->set_status(t); }   // MVP: shown in the footer

VividEditorHostAPI make_host_api(EditorWindow* e) {
    VividEditorHostAPI h{};
    h.opaque = e;
    h.get_clipboard_text = h_get_clipboard; h.set_clipboard_text = h_set_clipboard;
    h.set_cursor = h_set_cursor;
    h.capture_pointer = h_capture; h.release_pointer = h_release; h.has_pointer_capture = h_has_capture;
    h.request_focus = h_request_focus; h.has_focus = h_has_focus;
    h.set_status_text = h_set_status; h.show_tooltip = h_show_tooltip;
    return h;
}

int glfw_cursor_shape(VividCursorKind k) {
    switch (k) {
        case VIVID_CURSOR_IBEAM:     return GLFW_IBEAM_CURSOR;
        case VIVID_CURSOR_CROSSHAIR: return GLFW_CROSSHAIR_CURSOR;
        case VIVID_CURSOR_HAND:      return GLFW_HAND_CURSOR;
        case VIVID_CURSOR_RESIZE_H:  return GLFW_HRESIZE_CURSOR;
        case VIVID_CURSOR_RESIZE_V:  return GLFW_VRESIZE_CURSOR;
#ifdef GLFW_RESIZE_NESW_CURSOR
        case VIVID_CURSOR_RESIZE_NESW: return GLFW_RESIZE_NESW_CURSOR;
        case VIVID_CURSOR_RESIZE_NWSE: return GLFW_RESIZE_NWSE_CURSOR;
#endif
        default: return GLFW_ARROW_CURSOR;
    }
}
}  // namespace

EditorWindow::~EditorWindow() {
    for (auto* c : cursors_) if (c) glfwDestroyCursor(c);
    if (renderer_ok_) renderer_.shutdown();
    if (glfw_) glfwDestroyWindow(glfw_);
}

bool EditorWindow::open(App& app, int node, const std::string& title, int w, int h) {
    if (glfw_) return true;
    if (!app.gpu || !app.graph) return false;
    node_ = node;
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfw_ = glfwCreateWindow(w > 0 ? w : 420, h > 0 ? h : 220, title.c_str(), nullptr, nullptr);
    if (!glfw_) { std::fprintf(stderr, "[vivid] editor window create failed\n"); return false; }
    int fbw = 0, fbh = 0; glfwGetFramebufferSize(glfw_, &fbw, &fbh);
    int lw = 0, lh = 0;   glfwGetWindowSize(glfw_, &lw, &lh);
    if (fbw <= 0 || fbh <= 0 || !app.gpu->open_editor_surface(glfw_, static_cast<uint32_t>(fbw), static_cast<uint32_t>(fbh))) {
        glfwDestroyWindow(glfw_); glfw_ = nullptr; return false;
    }
    fb_w_ = fbw; fb_h_ = fbh;
    const float dpi = (lw > 0) ? static_cast<float>(fbw) / static_cast<float>(lw) : 1.0f;
    renderer_ok_ = renderer_.init(app.gpu->device(), app.gpu->surface_format(), VIVID_FONT_PATH, 15.0f, dpi);
    if (!renderer_ok_) std::fprintf(stderr, "[vivid] editor window Renderer2D init failed\n");
    // Full input: this window's own GLFW callbacks accumulate a VividEditorEvent stream.
    glfwSetWindowUserPointer(glfw_, this);
    glfwSetCursorPosCallback(glfw_, cb_cursor);
    glfwSetMouseButtonCallback(glfw_, cb_button);
    glfwSetScrollCallback(glfw_, cb_scroll);
    glfwSetKeyCallback(glfw_, cb_key);
    glfwSetCharCallback(glfw_, cb_char);
    glfwGetCursorPos(glfw_, &mx_, &my_); prev_mx_ = mx_; prev_my_ = my_;
    return true;
}

void EditorWindow::close(App& app) {
    // UI-5.4c: remember this window's size + position for the next float-out (across restarts).
    if (glfw_) {
        int w = 0, h = 0, x = 0, y = 0;
        glfwGetWindowSize(glfw_, &w, &h); glfwGetWindowPos(glfw_, &x, &y);
        if (w > 0 && h > 0) save_window_prefs({ w, h, x, y, true, true }, editor_window_prefs_path());
    }
    for (auto*& c : cursors_) { if (c) glfwDestroyCursor(c); c = nullptr; }
    if (renderer_ok_) { renderer_.shutdown(); renderer_ok_ = false; }
    if (app.gpu) app.gpu->close_editor_surface();
    if (glfw_) { glfwDestroyWindow(glfw_); glfw_ = nullptr; }
    node_ = -1; fb_w_ = fb_h_ = 0; events_.clear();
}

// --- input accumulation (editor-window-local logical px) ---
void EditorWindow::on_cursor(double x, double y) {
    mx_ = x; my_ = y;
    VividEditorEvent e{}; e.type = VIVID_EDITOR_EVENT_MOUSE_MOVE; e.x = (float)x; e.y = (float)y;
    events_.push_back(e);
}
void EditorWindow::on_button(int button, int action, int mods) {
    const int vb = (button == GLFW_MOUSE_BUTTON_RIGHT) ? 1 : (button == GLFW_MOUSE_BUTTON_MIDDLE ? 2 : 0);
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) { left_down_ = true; left_clicked_ = true; }
        else if (action == GLFW_RELEASE) { left_down_ = false; left_released_ = true; }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        right_clicked_ = true;
    }
    shift_down_ = (mods & GLFW_MOD_SHIFT) != 0;
    VividEditorEvent e{}; e.type = VIVID_EDITOR_EVENT_MOUSE_BUTTON; e.x = (float)mx_; e.y = (float)my_;
    e.button = vb; e.action = (action == GLFW_PRESS) ? 1 : (action == GLFW_REPEAT ? 2 : 0); e.modifiers = mods;
    events_.push_back(e);
}
void EditorWindow::on_scroll(double dx, double dy) {
    VividEditorEvent e{}; e.type = VIVID_EDITOR_EVENT_MOUSE_SCROLL; e.x = (float)mx_; e.y = (float)my_;
    e.scroll_dx = (float)dx; e.scroll_dy = (float)dy;
    events_.push_back(e);
}
void EditorWindow::on_key(int key, int scancode, int action, int mods) {
    shift_down_ = (mods & GLFW_MOD_SHIFT) != 0;
    VividEditorEvent e{}; e.type = VIVID_EDITOR_EVENT_KEY;
    e.key = key; e.scancode = scancode; e.action = (action == GLFW_PRESS) ? 1 : (action == GLFW_REPEAT ? 2 : 0); e.modifiers = mods;
    events_.push_back(e);
}
void EditorWindow::on_char(unsigned int codepoint) {
    VividEditorEvent e{}; e.type = VIVID_EDITOR_EVENT_CHAR; e.codepoint = codepoint;
    events_.push_back(e);
}

void EditorWindow::apply_cursor(VividCursorKind kind) {
    if (!glfw_) return;
    const int idx = (kind < 9) ? (int)kind : 0;
    if (kind == VIVID_CURSOR_DEFAULT) { glfwSetCursor(glfw_, nullptr); return; }
    if (!cursors_[idx]) cursors_[idx] = glfwCreateStandardCursor(glfw_cursor_shape(kind));
    glfwSetCursor(glfw_, cursors_[idx]);   // null cursor (unsupported shape) falls back to the system arrow
}

bool EditorWindow::render(App& app) {
    if (!glfw_ || !app.gpu) return false;
    if (glfwWindowShouldClose(glfw_)) return false;
    auto* g = app.graph;
    if (!g || node_ < 0 || !g->op_has_editor(node_)) return false;   // node gone / lost its editor

    int fbw = 0, fbh = 0; glfwGetFramebufferSize(glfw_, &fbw, &fbh);
    int lw = 0, lh = 0;   glfwGetWindowSize(glfw_, &lw, &lh);
    if (fbw <= 0 || fbh <= 0 || lw <= 0 || lh <= 0) { events_.clear(); return true; }   // minimized
    if (fbw != fb_w_ || fbh != fb_h_) { app.gpu->resize_editor_surface((uint32_t)fbw, (uint32_t)fbh); fb_w_ = fbw; fb_h_ = fbh; }

    const int pc = g->op_param_count_at(node_);
    std::vector<float> pv(pc > 0 ? pc : 1, 0.f);
    for (int i = 0; i < pc; ++i) pv[i] = g->op_param_base_at(node_, i);

    FrameState f;
    if (!app.gpu->begin_editor_surface(f)) { events_.clear(); return true; }   // transiently unavailable
    clear_pass(f.encoder, f.view, 0.07f, 0.08f, 0.09f);

    // Reserve a footer for the op's status text (host.set_status_text / show_tooltip).
    const bool has_status = !status_.empty();
    const float footer_h = has_status ? 18.f : 0.f;
    const float surf_h = (float)lh - footer_h;

    ui::DrawBridge db{ &renderer_, 0.f, 0.f, 0.f, 0.f, (float)lw, surf_h };
    EditorCmd cmd{ g, node_ };
    VividEditorContext ctx{};
    ctx.surface_width = (float)lw; ctx.surface_height = surf_h;
    ctx.dpi_scale = (lw > 0) ? (float)fbw / (float)lw : 1.f;
    ctx.draw = ui::make_op_draw_api(&db);
    ctx.theme = editor_theme();
    ctx.commands.opaque = &cmd; ctx.commands.set_param = editor_set_param;
    ctx.param_values = pv.data(); ctx.param_count = (uint32_t)pc;
    ctx.mouse.x = (float)mx_; ctx.mouse.y = (float)my_;
    ctx.mouse.prev_x = (float)prev_mx_; ctx.mouse.prev_y = (float)prev_my_;
    ctx.mouse.left_down = left_down_ ? 1 : 0; ctx.mouse.left_clicked = left_clicked_ ? 1 : 0;
    ctx.mouse.left_released = left_released_ ? 1 : 0; ctx.mouse.right_clicked = right_clicked_ ? 1 : 0;
    ctx.mouse.shift_down = shift_down_ ? 1 : 0;
    ctx.events = events_.empty() ? nullptr : events_.data(); ctx.event_count = (uint32_t)events_.size();
    ctx.host = make_host_api(this);
    ctx.time = glfwGetTime();

    requested_cursor_ = VIVID_CURSOR_DEFAULT;   // reset each frame; the op re-requests during draw
    status_.clear();                            // status is transient — the op re-sets it each frame
    if (renderer_ok_) {
        g->op_draw_editor(node_, &ctx);
        if (!status_.empty()) {   // footer strip (host status/tooltip)
            const VividInspectorTheme th = editor_theme();
            renderer_.draw_rect(0.f, surf_h, (float)lw, footer_h, th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 1.f);
            renderer_.draw_text(6.f, surf_h + 3.f, status_.c_str(), th.dim_text.r, th.dim_text.g, th.dim_text.b, 1.f, 0.72f);
        }
        renderer_.flush(f.encoder, f.view, (uint32_t)lw, (uint32_t)lh, (uint32_t)fbw, (uint32_t)fbh);
    }
    app.gpu->end_editor_surface(f);
    apply_cursor(requested_cursor_);

    prev_mx_ = mx_; prev_my_ = my_;
    events_.clear();
    left_clicked_ = left_released_ = right_clicked_ = false;   // edges are per-frame
    return !ctx.request_close;
}

}  // namespace vivid
