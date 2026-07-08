#include "app/editor_window.h"

#include <GLFW/glfw3.h>

#include "app/app.h"
#include "gpu/gpu_context.h"
#include "ui/node_graph.h"
#include "ui/operator_draw_bridge.h"

#include <cstdio>
#include <vector>

#ifndef VIVID_FONT_PATH
#define VIVID_FONT_PATH ""
#endif

namespace vivid {

namespace {
// A dark editor palette (matches the app's detail-region look without coupling to ui_style).
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
        if (std::string(c->g->op_param_label_at(c->node, l)) == name) { c->g->set_op_param_base_at(c->node, l, v); return; }
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
}  // namespace

EditorWindow::~EditorWindow() {
    // The owner must call close(app) first (needs the GpuContext to release the surface). This is a
    // backstop for the renderer + window if that didn't happen.
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
    return true;
}

void EditorWindow::close(App& app) {
    if (renderer_ok_) { renderer_.shutdown(); renderer_ok_ = false; }
    if (app.gpu) app.gpu->close_editor_surface();
    if (glfw_) { glfwDestroyWindow(glfw_); glfw_ = nullptr; }
    node_ = -1; fb_w_ = fb_h_ = 0;
}

bool EditorWindow::render(App& app) {
    if (!glfw_ || !app.gpu) return false;
    if (glfwWindowShouldClose(glfw_)) return false;
    auto* g = app.graph;
    if (!g || node_ < 0 || !g->op_has_editor(node_)) return false;   // node gone / lost its editor

    int fbw = 0, fbh = 0; glfwGetFramebufferSize(glfw_, &fbw, &fbh);
    int lw = 0, lh = 0;   glfwGetWindowSize(glfw_, &lw, &lh);
    if (fbw <= 0 || fbh <= 0 || lw <= 0 || lh <= 0) return true;   // minimized — keep open, skip draw
    if (fbw != fb_w_ || fbh != fb_h_) { app.gpu->resize_editor_surface(static_cast<uint32_t>(fbw), static_cast<uint32_t>(fbh)); fb_w_ = fbw; fb_h_ = fbh; }

    // Poll input (logical coords; the whole window is the editor surface).
    double mx = 0, my = 0; glfwGetCursorPos(glfw_, &mx, &my);
    const bool left = glfwGetMouseButton(glfw_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;

    const int pc = g->op_param_count_at(node_);
    std::vector<float> pv(pc > 0 ? pc : 1, 0.f);
    for (int i = 0; i < pc; ++i) pv[i] = g->op_param_base_at(node_, i);

    FrameState f;
    if (!app.gpu->begin_editor_surface(f)) return true;   // transiently unavailable; keep open
    clear_pass(f.encoder, f.view, 0.07f, 0.08f, 0.09f);

    ui::DrawBridge db{ &renderer_, 0.f, 0.f, 0.f, 0.f, static_cast<float>(lw), static_cast<float>(lh) };
    EditorCmd cmd{ g, node_ };
    VividEditorContext ctx{};
    ctx.surface_width = static_cast<float>(lw); ctx.surface_height = static_cast<float>(lh);
    ctx.dpi_scale = (lw > 0) ? static_cast<float>(fbw) / static_cast<float>(lw) : 1.f;
    ctx.draw = ui::make_op_draw_api(&db);
    ctx.theme = editor_theme();
    ctx.commands.opaque = &cmd; ctx.commands.set_param = editor_set_param;
    ctx.param_values = pv.data(); ctx.param_count = static_cast<uint32_t>(pc);
    ctx.mouse.x = static_cast<float>(mx); ctx.mouse.y = static_cast<float>(my);
    ctx.mouse.prev_x = static_cast<float>(prev_mx_); ctx.mouse.prev_y = static_cast<float>(prev_my_);
    ctx.mouse.left_down = left ? 1 : 0;
    ctx.time = glfwGetTime();
    if (renderer_ok_) {
        g->op_draw_editor(node_, &ctx);
        renderer_.flush(f.encoder, f.view, static_cast<uint32_t>(lw), static_cast<uint32_t>(lh),
                        static_cast<uint32_t>(fbw), static_cast<uint32_t>(fbh));
    }
    app.gpu->end_editor_surface(f);
    prev_mx_ = mx; prev_my_ = my;
    return !ctx.request_close;
}

}  // namespace vivid
