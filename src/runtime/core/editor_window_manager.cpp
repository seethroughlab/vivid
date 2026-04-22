#include "runtime/core/editor_window_manager.h"

#include "runtime/core/editor_window_bookkeeping.h"
#include "runtime/core/editor_window_host_api.h"
#include "runtime/core/runtime_core.h"
#include "runtime/core/settings.h"
#include "runtime/graph/compiled_graph.h"
#include "runtime/gpu/gpu_context.h"
#include "runtime/operators/operator_loader.h"
#include "ui/rendering/renderer_2d.h"
#include "ui/ui_command_sink.h"
#include "common/gpu_util.h"

#include <GLFW/glfw3.h>
#include <glfw3webgpu.h>
#include <stb_image_write.h>
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace vivid {

namespace {

// Command adapter — mirrors the thunks in node_graph_draw_inspector_sections.cpp
// so editor set_param/set_string_param routes through UICommandSink and picks
// up undo/coalescing from RuntimeCommandSink.
struct EdCmdCtx {
    ui::UICommandSink* sink;
    std::string node_id;
};

void ed_set_param(void* opaque, const char* param_name, float value) {
    auto* ctx = static_cast<EdCmdCtx*>(opaque);
    if (!ctx || !ctx->sink || !param_name) return;
    ctx->sink->set_param(ctx->node_id, param_name, value);
}

void ed_set_string_param(void* opaque, const char* param_name, const char* value) {
    auto* ctx = static_cast<EdCmdCtx*>(opaque);
    if (!ctx || !ctx->sink || !param_name) return;
    ctx->sink->set_string_param(ctx->node_id, param_name, value ? value : "");
}

// (HostCtx + make_host_api live in runtime/core/editor_window_host_api.{h,cpp}
// so tests can exercise them without linking the manager.)

const char* surface_status_label(WGPUSurfaceGetCurrentTextureStatus status) {
    switch (status) {
        case WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal: return "SuccessOptimal";
        case WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal: return "SuccessSuboptimal";
        case WGPUSurfaceGetCurrentTextureStatus_Timeout: return "Timeout";
        case WGPUSurfaceGetCurrentTextureStatus_Outdated: return "Outdated";
        case WGPUSurfaceGetCurrentTextureStatus_Lost: return "Lost";
        case WGPUSurfaceGetCurrentTextureStatus_Error: return "Error";
        case WGPUSurfaceGetCurrentTextureStatus_Force32: return "Unset";
        default: return "Unknown";
    }
}

bool should_rate_limit_log(double now, double& last_log_time,
                           double min_interval_seconds = 1.0) {
    if (last_log_time < 0.0 || now - last_log_time >= min_interval_seconds) {
        last_log_time = now;
        return true;
    }
    return false;
}

} // namespace

// ---------------------------------------------------------------------------
// EditorWindow — one per open editor. GLFW + WGPU + Renderer2D + input buffers.
// ---------------------------------------------------------------------------

struct EditorWindow {
    std::string node_id;
    std::string type_name;    // operator type slug — captured at open; stable even if node disappears
    GLFWwindow* glfw = nullptr;
    WGPUSurface surface = nullptr;
    WGPUTextureFormat format = WGPUTextureFormat_BGRA8Unorm;
    uint32_t logical_width = 0;
    uint32_t logical_height = 0;
    uint32_t framebuffer_width = 0;
    uint32_t framebuffer_height = 0;

    std::unique_ptr<ui::Renderer2D> renderer;

    std::vector<VividEditorEvent> pending_events;
    VividEditorMouse mouse{};
    VividEditorMetadata meta{};

    // Explicit-close gate (follow-up: second-window automated test coverage).
    // `glfwWindowShouldClose` flips to true from three paths:
    //   * Cmd+W handled in `key_cb` — sets `explicit_close_requested` first.
    //   * Red-button click routed through `close_cb` — also sets the flag.
    //   * Cocoa lifecycle noise in subprocess / non-foreground contexts —
    //     sets the flag via an internal path, without going through
    //     either of our callbacks.
    // tick() checks `glfwWindowShouldClose && explicit_close_requested`
    // before marking the window for destroy; if the close flag snuck in
    // without our sentinel, we clear it and keep the window alive.
    bool explicit_close_requested = false;

    // Host services (Phase D). Per-frame fields (cursor / status / tooltip)
    // reset at the top of tick() before draw_editor; persistent fields
    // (pointer_captured) stay until the operator clears them.
    HostCtx host_ctx{};

    // Test-support: when non-null, the next tick() will encode a
    // copy-texture-to-buffer alongside the render pass and write the
    // resulting PNG to *capture_dest. Cleared after the capture completes.
    std::vector<uint8_t>* capture_dest = nullptr;

    WGPUSurfaceGetCurrentTextureStatus last_acquire_status =
        static_cast<WGPUSurfaceGetCurrentTextureStatus>(-1);
    double last_acquire_log_time = -1.0;
    double last_view_failure_log_time = -1.0;
    double last_submit_failure_log_time = -1.0;
    double last_empty_draw_log_time = -1.0;
};

// ---------------------------------------------------------------------------
// Impl — manager state.
// ---------------------------------------------------------------------------

struct EditorWindowManager::Impl {
    GpuContext& gpu;
    RuntimeCore& runtime;
    ui::UICommandSink& commands;
    ThemeProvider theme_provider;
    std::string font_path;
    float font_pt = 16.0f;
    Settings* settings = nullptr;

    EditorWindowBookkeeping bookkeeping;
    std::vector<std::unique_ptr<EditorWindow>> windows;

    // Cursor cache — GLFWcursor* is shared across all editor windows since
    // it's a process-wide image resource. Lazy-created on first request.
    std::array<GLFWcursor*, 9> cursor_cache{};  // one per VividCursorKind

    // Headless mode — when true, future open() calls create windows with
    // GLFW_VISIBLE=FALSE so they render offscreen on CI runners without a
    // display. Toggled by the --headless app flag; existing windows
    // unaffected.
    bool hidden_when_opening = false;

    // Last time we ticked. Remembered so capture_surface_png() can drive
    // an additional tick cycle when the caller wants synchronous capture
    // without orchestrating the frame clock itself.
    double last_tick_time = 0.0;

    Impl(GpuContext& g, RuntimeCore& r, ui::UICommandSink& c,
         ThemeProvider t, std::string fp, float pt, Settings* s)
        : gpu(g), runtime(r), commands(c), theme_provider(std::move(t)),
          font_path(std::move(fp)), font_pt(pt), settings(s) {}

    ~Impl() {
        for (auto* c : cursor_cache) {
            if (c) glfwDestroyCursor(c);
        }
    }

    GLFWcursor* cursor_for(VividCursorKind kind) {
        if (kind >= cursor_cache.size()) return nullptr;
        if (cursor_cache[kind]) return cursor_cache[kind];
        int shape = 0;
        switch (kind) {
            case VIVID_CURSOR_DEFAULT:
            case VIVID_CURSOR_ARROW:       shape = GLFW_ARROW_CURSOR;     break;
            case VIVID_CURSOR_IBEAM:       shape = GLFW_IBEAM_CURSOR;     break;
            case VIVID_CURSOR_CROSSHAIR:   shape = GLFW_CROSSHAIR_CURSOR; break;
            case VIVID_CURSOR_HAND:        shape = GLFW_HAND_CURSOR;      break;
            case VIVID_CURSOR_RESIZE_H:    shape = GLFW_HRESIZE_CURSOR;   break;
            case VIVID_CURSOR_RESIZE_V:    shape = GLFW_VRESIZE_CURSOR;   break;
            // GLFW diagonal cursors didn't land until 3.4; fall back to
            // the axis-aligned resize cursors on older toolchains.
#ifdef GLFW_RESIZE_NESW_CURSOR
            case VIVID_CURSOR_RESIZE_NESW: shape = GLFW_RESIZE_NESW_CURSOR; break;
#else
            case VIVID_CURSOR_RESIZE_NESW: shape = GLFW_HRESIZE_CURSOR; break;
#endif
#ifdef GLFW_RESIZE_NWSE_CURSOR
            case VIVID_CURSOR_RESIZE_NWSE: shape = GLFW_RESIZE_NWSE_CURSOR; break;
#else
            case VIVID_CURSOR_RESIZE_NWSE: shape = GLFW_VRESIZE_CURSOR; break;
#endif
            default: shape = GLFW_ARROW_CURSOR; break;
        }
        cursor_cache[kind] = glfwCreateStandardCursor(shape);
        return cursor_cache[kind];
    }

    EditorWindow* find(const std::string& id) {
        for (auto& w : windows) {
            if (w->node_id == id) return w.get();
        }
        return nullptr;
    }

    // Snapshot live window geometry into Settings (keyed by operator type slug)
    // and flush to disk. No-op if settings pointer is null (e.g. tests).
    void persist_geometry(const EditorWindow& w) {
        if (!settings || !w.glfw || w.type_name.empty()) return;
        int x = 0, y = 0, win_w = 0, win_h = 0;
        glfwGetWindowPos(w.glfw, &x, &y);
        glfwGetWindowSize(w.glfw, &win_w, &win_h);
        if (win_w <= 0 || win_h <= 0) return;  // ignore minimized/hidden state
        EditorWindowGeometry g;
        g.x = x;
        g.y = y;
        g.width = win_w;
        g.height = win_h;
        settings->editor_window_geometry_by_type[w.type_name] = g;
        vivid::save_settings(*settings);
    }

    bool destroy_window_by_id(const std::string& id) {
        for (auto it = windows.begin(); it != windows.end(); ++it) {
            if ((*it)->node_id == id) {
                persist_geometry(**it);
                destroy(**it);
                windows.erase(it);
                return true;
            }
        }
        return false;
    }

    void configure_surface(EditorWindow& w) {
        WGPUSurfaceConfiguration cfg{};
        cfg.device = gpu.device();
        cfg.format = w.format;
        // Include CopySrc so capture_surface_png() can copy the surface
        // texture into a staging buffer for readback. All current backends
        // (wgpu-native on macOS + Windows + Linux) support BGRA8 CopySrc;
        // the Renderer2D render pass is unaffected.
        cfg.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_CopySrc;
        cfg.alphaMode = WGPUCompositeAlphaMode_Auto;
        cfg.width = w.framebuffer_width;
        cfg.height = w.framebuffer_height;
        cfg.presentMode = WGPUPresentMode_Fifo;
        wgpuSurfaceConfigure(w.surface, &cfg);
    }

    void destroy(EditorWindow& w) {
        if (w.renderer) {
            w.renderer->shutdown();
            w.renderer.reset();
        }
        if (w.surface) {
            wgpuSurfaceUnconfigure(w.surface);
            wgpuSurfaceRelease(w.surface);
            w.surface = nullptr;
        }
        if (w.glfw) {
            glfwDestroyWindow(w.glfw);
            w.glfw = nullptr;
        }
    }
};

// ---------------------------------------------------------------------------
// GLFW callbacks — translate native events to VividEditorEvent in editor-local
// pixel coords. EditorWindow* comes via glfwSetWindowUserPointer.
// ---------------------------------------------------------------------------

namespace {

inline EditorWindow* ew_from(GLFWwindow* w) {
    return static_cast<EditorWindow*>(glfwGetWindowUserPointer(w));
}

void cursor_pos_cb(GLFWwindow* w, double x, double y) {
    auto* ew = ew_from(w);
    if (!ew) return;
    ew->mouse.prev_x = ew->mouse.x;
    ew->mouse.prev_y = ew->mouse.y;
    ew->mouse.x = static_cast<float>(x);
    ew->mouse.y = static_cast<float>(y);

    VividEditorEvent e{};
    e.type = VIVID_EDITOR_EVENT_MOUSE_MOVE;
    e.x = ew->mouse.x;
    e.y = ew->mouse.y;
    ew->pending_events.push_back(e);
}

void mouse_button_cb(GLFWwindow* w, int button, int action, int mods) {
    auto* ew = ew_from(w);
    if (!ew) return;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            ew->mouse.left_down = 1;
            ew->mouse.left_clicked = 1;
        } else if (action == GLFW_RELEASE) {
            ew->mouse.left_down = 0;
            ew->mouse.left_released = 1;
        }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            ew->mouse.right_clicked = 1;
        }
    }
    ew->mouse.shift_down = (mods & GLFW_MOD_SHIFT) ? 1 : 0;

    VividEditorEvent e{};
    e.type = VIVID_EDITOR_EVENT_MOUSE_BUTTON;
    e.x = ew->mouse.x;
    e.y = ew->mouse.y;
    e.button = button;
    e.action = action;
    e.modifiers = mods;
    ew->pending_events.push_back(e);
}

void scroll_cb(GLFWwindow* w, double dx, double dy) {
    auto* ew = ew_from(w);
    if (!ew) return;
    VividEditorEvent e{};
    e.type = VIVID_EDITOR_EVENT_MOUSE_SCROLL;
    e.x = ew->mouse.x;
    e.y = ew->mouse.y;
    e.scroll_dx = static_cast<float>(dx);
    e.scroll_dy = static_cast<float>(dy);
    ew->pending_events.push_back(e);
}

void key_cb(GLFWwindow* w, int key, int scancode, int action, int mods) {
    auto* ew = ew_from(w);
    if (!ew) return;
    ew->mouse.shift_down = (mods & GLFW_MOD_SHIFT) ? 1 : 0;

    // Cmd+W (macOS) / Ctrl+W (elsewhere): close the editor window. Handled
    // at the manager level so every operator gets the shortcut without
    // reimplementing it, and so the event never reaches the operator's
    // key handler (avoids spurious Cmd-modifier interpretation). Sets the
    // explicit-close sentinel first so tick() recognises this close is
    // user-requested (not a spurious Cocoa signal from the app lifecycle).
    if (action == GLFW_PRESS && key == GLFW_KEY_W &&
        (mods & (GLFW_MOD_SUPER | GLFW_MOD_CONTROL)) != 0) {
        ew->explicit_close_requested = true;
        glfwSetWindowShouldClose(w, GLFW_TRUE);
        return;
    }

    VividEditorEvent e{};
    e.type = VIVID_EDITOR_EVENT_KEY;
    e.key = key;
    e.scancode = scancode;
    e.action = action;
    e.modifiers = mods;
    ew->pending_events.push_back(e);
}

void char_cb(GLFWwindow* w, unsigned int codepoint) {
    auto* ew = ew_from(w);
    if (!ew) return;
    VividEditorEvent e{};
    e.type = VIVID_EDITOR_EVENT_CHAR;
    e.codepoint = codepoint;
    ew->pending_events.push_back(e);
}

// Native close path (red button / system close menu). GLFW sets the
// should-close flag before firing this callback; we stamp it as explicit
// so tick() recognises it as user-requested. Cocoa lifecycle noise in
// subprocess mode goes through a different internal path that does NOT
// fire this callback — tick() clears the flag defensively in that case.
void close_cb(GLFWwindow* w) {
    auto* ew = ew_from(w);
    if (!ew) return;
    ew->explicit_close_requested = true;
}

} // namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

EditorWindowManager::EditorWindowManager(GpuContext& gpu,
                                         RuntimeCore& runtime,
                                         ui::UICommandSink& commands,
                                         ThemeProvider theme_provider,
                                         std::string font_path,
                                         float font_pt,
                                         Settings* settings)
    : impl_(std::make_unique<Impl>(gpu, runtime, commands,
                                   std::move(theme_provider),
                                   std::move(font_path), font_pt,
                                   settings)) {}

EditorWindowManager::~EditorWindowManager() {
    close_all();
}

bool EditorWindowManager::is_open(const std::string& node_id) const {
    return impl_->bookkeeping.is_open(node_id);
}

void EditorWindowManager::focus(const std::string& node_id) {
    if (auto* ew = impl_->find(node_id)) {
        if (ew->glfw) glfwFocusWindow(ew->glfw);
    }
}

void EditorWindowManager::close(const std::string& node_id) {
    (void)impl_->bookkeeping.close_one(
        node_id, [&](const std::string& id) { impl_->destroy_window_by_id(id); });
}

void EditorWindowManager::close_all() {
    (void)impl_->bookkeeping.close_all(
        [&](const std::string& id) { impl_->destroy_window_by_id(id); });
    // Fallback for any windows that didn't come through bookkeeping (shouldn't
    // happen, but keeps teardown safe). These still persist geometry so a
    // relaunched app can restore size/pos.
    if (!impl_->windows.empty()) {
        for (auto& w : impl_->windows) {
            impl_->persist_geometry(*w);
            impl_->destroy(*w);
        }
        impl_->windows.clear();
    }
}

bool EditorWindowManager::open(const std::string& node_id) {
    // Refocus if already open.
    if (impl_->bookkeeping.is_open(node_id)) {
        focus(node_id);
        return true;
    }

    // Resolve live node + loader.
    auto* cg = impl_->runtime.compiled_graph();
    if (!cg) return false;
    auto* node = cg->find_node(node_id);
    if (!node || !node->loader || !node->instance) return false;
    if (!node->loader->has_editor()) return false;

    VividEditorMetadata meta = node->loader->editor_metadata();
    uint32_t w_px = meta.default_width  > 0 ? meta.default_width  : 800;
    uint32_t h_px = meta.default_height > 0 ? meta.default_height : 500;

    // Geometry lookup: per-operator-type persisted size/pos overrides metadata
    // defaults. width/height == 0 ⇒ use metadata. x/y == -1 ⇒ let OS place.
    const std::string type_name = node->type_name;
    int restore_x = -1, restore_y = -1;
    if (impl_->settings && !type_name.empty()) {
        auto it = impl_->settings->editor_window_geometry_by_type.find(type_name);
        if (it != impl_->settings->editor_window_geometry_by_type.end()) {
            const auto& g = it->second;
            if (g.width  > 0) w_px = static_cast<uint32_t>(g.width);
            if (g.height > 0) h_px = static_cast<uint32_t>(g.height);
            restore_x = g.x;
            restore_y = g.y;
        }
    }

    // Title: "<node_id> — <title_suffix>"
    std::string title = node_id;
    if (meta.title_suffix && meta.title_suffix[0] != '\0') {
        title += " — ";
        title += meta.title_suffix;
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_DECORATED, GLFW_TRUE);
    glfwWindowHint(GLFW_FOCUS_ON_SHOW, GLFW_TRUE);
    // Headless CI: skip showing the window so tests that open editors
    // don't require a live display server.
    glfwWindowHint(GLFW_VISIBLE,
                   impl_->hidden_when_opening ? GLFW_FALSE : GLFW_TRUE);

    GLFWwindow* gw = glfwCreateWindow(static_cast<int>(w_px),
                                      static_cast<int>(h_px),
                                      title.c_str(), nullptr, nullptr);
    if (!gw) {
        std::fprintf(stderr, "[vivid] EditorWindow: failed to create GLFW window for %s\n",
                     node_id.c_str());
        return false;
    }

    if (restore_x != -1 && restore_y != -1) {
        glfwSetWindowPos(gw, restore_x, restore_y);
    }

    if (meta.min_width > 0 || meta.min_height > 0) {
        glfwSetWindowSizeLimits(gw,
                                meta.min_width  > 0 ? static_cast<int>(meta.min_width)  : GLFW_DONT_CARE,
                                meta.min_height > 0 ? static_cast<int>(meta.min_height) : GLFW_DONT_CARE,
                                GLFW_DONT_CARE, GLFW_DONT_CARE);
    }

    WGPUSurface surface = glfwCreateWindowWGPUSurface(impl_->gpu.instance(), gw);
    if (!surface) {
        std::fprintf(stderr, "[vivid] EditorWindow: failed to create surface for %s\n",
                     node_id.c_str());
        glfwDestroyWindow(gw);
        return false;
    }

    WGPUTextureFormat fmt = WGPUTextureFormat_BGRA8Unorm;
    WGPUSurfaceCapabilities caps{};
    wgpuSurfaceGetCapabilities(surface, impl_->gpu.adapter(), &caps);
    if (caps.formatCount > 0) fmt = caps.formats[0];
    wgpuSurfaceCapabilitiesFreeMembers(caps);

    int win_w = 0, win_h = 0;
    int fb_w = 0, fb_h = 0;
    glfwGetWindowSize(gw, &win_w, &win_h);
    glfwGetFramebufferSize(gw, &fb_w, &fb_h);
    float xscale = 1.0f, yscale = 1.0f;
    glfwGetWindowContentScale(gw, &xscale, &yscale);
    const EditorWindowSurfaceMetrics metrics =
        make_editor_window_surface_metrics(win_w, win_h, fb_w, fb_h, xscale);

    auto ew = std::make_unique<EditorWindow>();
    ew->node_id = node_id;
    ew->type_name = type_name;
    ew->glfw = gw;
    ew->surface = surface;
    ew->format = fmt;
    ew->logical_width = metrics.logical_width > 0 ? metrics.logical_width : w_px;
    ew->logical_height = metrics.logical_height > 0 ? metrics.logical_height : h_px;
    ew->framebuffer_width = metrics.framebuffer_width > 0 ? metrics.framebuffer_width : w_px;
    ew->framebuffer_height = metrics.framebuffer_height > 0 ? metrics.framebuffer_height : h_px;
    ew->meta = meta;

    impl_->configure_surface(*ew);

    ew->renderer = std::make_unique<ui::Renderer2D>();
    if (!ew->renderer->init(impl_->gpu.device(), fmt,
                            impl_->font_path.c_str(), impl_->font_pt,
                            metrics.dpi_scale)) {
        std::fprintf(stderr, "[vivid] EditorWindow: failed to init Renderer2D for %s\n",
                     node_id.c_str());
        wgpuSurfaceUnconfigure(surface);
        wgpuSurfaceRelease(surface);
        glfwDestroyWindow(gw);
        return false;
    }

    glfwSetWindowUserPointer(gw, ew.get());
    glfwSetCursorPosCallback(gw, cursor_pos_cb);
    glfwSetMouseButtonCallback(gw, mouse_button_cb);
    glfwSetScrollCallback(gw, scroll_cb);
    glfwSetKeyCallback(gw, key_cb);
    glfwSetCharCallback(gw, char_cb);
    glfwSetWindowCloseCallback(gw, close_cb);

    if (!impl_->bookkeeping.record_open(node_id)) {
        impl_->destroy(*ew);
        focus(node_id);
        return true;
    }

    std::fprintf(stderr, "[vivid] EditorWindow opened for %s (%ux%u)\n",
                 node_id.c_str(), ew->logical_width, ew->logical_height);

    impl_->windows.push_back(std::move(ew));
    return true;
}

void EditorWindowManager::tick(double time) {
    impl_->last_tick_time = time;
    auto& windows = impl_->windows;
    if (windows.empty()) return;

    auto* cg = impl_->runtime.compiled_graph();

    for (auto& ew_ptr : windows) {
        EditorWindow& ew = *ew_ptr;
        if (!ew.glfw || !ew.surface || !ew.renderer) {
            impl_->bookkeeping.mark_for_destroy(ew.node_id);
            continue;
        }

        if (glfwWindowShouldClose(ew.glfw)) {
            if (ew.explicit_close_requested) {
                impl_->bookkeeping.mark_for_destroy(ew.node_id);
                continue;
            }
            // Close flag arrived without passing through key_cb (Cmd+W) or
            // close_cb (red button). Treat as a spurious Cocoa-lifecycle
            // signal and clear it so the window survives. Fixes the
            // ctest-subprocess regression where editor windows were torn
            // down seconds after open.
            glfwSetWindowShouldClose(ew.glfw, GLFW_FALSE);
        }

        // Resolve live state (never cached across reload boundaries).
        const CompiledNode* node = cg ? cg->find_node(ew.node_id) : nullptr;
        if (!node || !node->loader || !node->instance || !node->loader->has_editor()) {
            impl_->bookkeeping.mark_for_destroy(ew.node_id);
            continue;
        }

        // Refresh logical + framebuffer dimensions. Surface config stays in
        // framebuffer pixels; editor layout and input stay in logical coords.
        int win_w = 0, win_h = 0;
        int fb_w = 0, fb_h = 0;
        glfwGetWindowSize(ew.glfw, &win_w, &win_h);
        glfwGetFramebufferSize(ew.glfw, &fb_w, &fb_h);
        if (win_w <= 0 || win_h <= 0 || fb_w <= 0 || fb_h <= 0) {
            ew.pending_events.clear();
            continue;
        }
        float xscale = 1.0f, yscale = 1.0f;
        glfwGetWindowContentScale(ew.glfw, &xscale, &yscale);
        const EditorWindowSurfaceMetrics metrics =
            make_editor_window_surface_metrics(win_w, win_h, fb_w, fb_h, xscale);
        ew.logical_width = metrics.logical_width;
        ew.logical_height = metrics.logical_height;
        if (metrics.framebuffer_width != ew.framebuffer_width ||
            metrics.framebuffer_height != ew.framebuffer_height) {
            wgpuSurfaceUnconfigure(ew.surface);
            ew.framebuffer_width = metrics.framebuffer_width;
            ew.framebuffer_height = metrics.framebuffer_height;
            impl_->configure_surface(ew);
        }

        // Acquire surface texture.
        WGPUSurfaceTexture st{};
        wgpuSurfaceGetCurrentTexture(ew.surface, &st);
        if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
            st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
            if (st.status != ew.last_acquire_status ||
                should_rate_limit_log(time, ew.last_acquire_log_time)) {
                std::fprintf(stderr,
                             "[vivid] EditorWindow acquire failed for %s: status=%s logical=%ux%u fb=%ux%u last_error=%s\n",
                             ew.node_id.c_str(), surface_status_label(st.status),
                             ew.logical_width, ew.logical_height,
                             ew.framebuffer_width, ew.framebuffer_height,
                             impl_->gpu.last_error().empty() ? "<none>"
                                                             : impl_->gpu.last_error().c_str());
            }
            ew.last_acquire_status = st.status;
            if (st.texture) wgpuTextureRelease(st.texture);
            ew.pending_events.clear();
            continue;
        }
        ew.last_acquire_status = st.status;

        WGPUTextureViewDescriptor view_desc{};
        view_desc.label = to_sv("Editor Surface View");
        view_desc.format = ew.format;
        view_desc.dimension = WGPUTextureViewDimension_2D;
        view_desc.baseMipLevel = 0;
        view_desc.mipLevelCount = 1;
        view_desc.baseArrayLayer = 0;
        view_desc.arrayLayerCount = 1;
        WGPUTextureView view = wgpuTextureCreateView(st.texture, &view_desc);
        if (!view) {
            if (should_rate_limit_log(time, ew.last_view_failure_log_time)) {
                std::fprintf(stderr,
                             "[vivid] EditorWindow view creation failed for %s: logical=%ux%u fb=%ux%u last_error=%s\n",
                             ew.node_id.c_str(),
                             ew.logical_width, ew.logical_height,
                             ew.framebuffer_width, ew.framebuffer_height,
                             impl_->gpu.last_error().empty() ? "<none>"
                                                             : impl_->gpu.last_error().c_str());
            }
            wgpuTextureRelease(st.texture);
            ew.pending_events.clear();
            continue;
        }

        // Build the editor context.
        EdCmdCtx cmd_ctx{&impl_->commands, ew.node_id};

        VividEditorContext ctx{};
        ctx.surface_width = static_cast<float>(ew.logical_width);
        ctx.surface_height = static_cast<float>(ew.logical_height);
        ctx.dpi_scale = metrics.dpi_scale;

        ui::populate_draw_api(ctx.draw, *ew.renderer);
        ctx.commands.opaque = &cmd_ctx;
        ctx.commands.set_param = ed_set_param;
        ctx.commands.set_string_param = ed_set_string_param;
        ctx.theme = impl_->theme_provider ? impl_->theme_provider() : VividInspectorTheme{};

        ctx.param_values  = node->param_values.data();
        ctx.param_count   = static_cast<uint32_t>(node->param_values.size());
        ctx.output_values = node->output_values.data();
        ctx.output_count  = static_cast<uint32_t>(node->output_values.size());
        const EditorStringParamView string_params =
            make_editor_string_param_view(node->file_param_ptrs);
        ctx.string_param_values = string_params.values;
        ctx.string_param_count = string_params.count;

        ctx.mouse = ew.mouse;
        ctx.events = ew.pending_events.data();
        ctx.event_count = static_cast<uint32_t>(ew.pending_events.size());
        ctx.time = time;
        ctx.wants_keyboard = 0;
        ctx.request_close = 0;

        // Host services (Phase D). Reset per-frame fields; pointer
        // capture persists until the operator releases it. The HostCtx
        // lives on the EditorWindow so thunks can dispatch via opaque.
        ew.host_ctx.glfw            = ew.glfw;
        ew.host_ctx.requested_cursor = VIVID_CURSOR_DEFAULT;
        ew.host_ctx.status_text.clear();
        ew.host_ctx.tooltip_text.clear();
        ew.host_ctx.request_focus   = false;
        ctx.host = make_host_api(&ew.host_ctx);

        // Drive the operator-owned draw.
        node->loader->draw_editor(node->instance, &ctx);

        // Apply the operator's requested cursor shape for this frame.
        GLFWcursor* want = impl_->cursor_for(ew.host_ctx.requested_cursor);
        glfwSetCursor(ew.glfw, want);

        // Focus request.
        if (ew.host_ctx.request_focus) glfwFocusWindow(ew.glfw);

        // Transient chrome (status strip at bottom, tooltip near cursor).
        // Rendered after the operator draws so they overlay any cell/grid
        // glyphs beneath.
        if (!ew.host_ctx.status_text.empty() && ctx.draw.draw_text) {
            const float lh = ctx.draw.line_height ? ctx.draw.line_height(ctx.draw.opaque) : 14.0f;
            const float pad_x = 8.0f;
            const float strip_h = lh + 4.0f;
            const float strip_y = ctx.surface_height - strip_h;
            if (ctx.draw.draw_rect) {
                ctx.draw.draw_rect(ctx.draw.opaque, 0.0f, strip_y,
                    ctx.surface_width, strip_h,
                    VividColor{ctx.theme.dark_bg.r, ctx.theme.dark_bg.g,
                               ctx.theme.dark_bg.b, 0.9f});
            }
            ctx.draw.draw_text(ctx.draw.opaque, pad_x, strip_y + 2.0f,
                ew.host_ctx.status_text.c_str(),
                VividColor{ctx.theme.dim_text.r, ctx.theme.dim_text.g,
                           ctx.theme.dim_text.b, 0.9f}, 1.0f);
        }
        if (!ew.host_ctx.tooltip_text.empty() && ctx.draw.draw_text) {
            const float tw = ctx.draw.text_width
                ? ctx.draw.text_width(ctx.draw.opaque,
                                      ew.host_ctx.tooltip_text.c_str(), 1.0f)
                : static_cast<float>(ew.host_ctx.tooltip_text.size()) * 7.0f;
            const float lh = ctx.draw.line_height ? ctx.draw.line_height(ctx.draw.opaque) : 14.0f;
            const float pad_x = 6.0f;
            const float pad_y = 2.0f;
            const float bx = ew.mouse.x + 12.0f;
            const float by = ew.mouse.y + 12.0f;
            const float bw = tw + 2.0f * pad_x;
            const float bh = lh + 2.0f * pad_y;
            if (ctx.draw.draw_rect) {
                ctx.draw.draw_rect(ctx.draw.opaque, bx, by, bw, bh,
                    VividColor{ctx.theme.dark_bg.r, ctx.theme.dark_bg.g,
                               ctx.theme.dark_bg.b, 0.95f});
            }
            ctx.draw.draw_text(ctx.draw.opaque, bx + pad_x, by + pad_y,
                ew.host_ctx.tooltip_text.c_str(),
                VividColor{ctx.theme.bright_text.r, ctx.theme.bright_text.g,
                           ctx.theme.bright_text.b, 1.0f}, 1.0f);
        }
        const size_t pending_vertices = ew.renderer->pending_vertex_count();

        if (ctx.request_close) impl_->bookkeeping.mark_for_destroy(ew.node_id);
        if (pending_vertices == 0 &&
            should_rate_limit_log(time, ew.last_empty_draw_log_time)) {
            std::fprintf(stderr,
                         "[vivid] EditorWindow draw produced no geometry for %s: logical=%ux%u fb=%ux%u events=%u\n",
                         ew.node_id.c_str(),
                         ew.logical_width, ew.logical_height,
                         ew.framebuffer_width, ew.framebuffer_height,
                         ctx.event_count);
        }

        // Encode + flush + present.
        WGPUCommandEncoderDescriptor enc_desc{};
        enc_desc.label = to_sv("Editor Window Encoder");
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(impl_->gpu.device(), &enc_desc);

        // Clear pass (loadOp=Clear) so the Renderer2D flush below (loadOp=Load)
        // has a well-defined background. Theme bg provides the clear color.
        WGPURenderPassColorAttachment attach{};
        attach.view = view;
        attach.loadOp = WGPULoadOp_Clear;
        attach.storeOp = WGPUStoreOp_Store;
        attach.clearValue = {
            ctx.theme.bg.r,
            ctx.theme.bg.g,
            ctx.theme.bg.b,
            ctx.theme.bg.a > 0.0f ? ctx.theme.bg.a : 1.0f,
        };
        attach.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        WGPURenderPassDescriptor pass_desc{};
        pass_desc.label = to_sv("Editor Clear Pass");
        pass_desc.colorAttachmentCount = 1;
        pass_desc.colorAttachments = &attach;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &pass_desc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        ew.renderer->flush(encoder, view,
                           ew.logical_width, ew.logical_height,
                           ew.framebuffer_width, ew.framebuffer_height);

        // Optional screenshot capture — encode a copyTextureToBuffer into
        // the same command buffer, then synchronously map + write PNG
        // after submit. Only active when capture_dest is set (test path).
        WGPUBuffer capture_staging = nullptr;
        uint32_t cap_aligned_row = 0;
        uint32_t cap_width = 0;
        uint32_t cap_height = 0;
        if (ew.capture_dest) {
            cap_width = ew.framebuffer_width;
            cap_height = ew.framebuffer_height;
            constexpr uint32_t kRowAlignment = 256;
            const uint32_t bpp = 4;
            const uint32_t unpadded_row = cap_width * bpp;
            cap_aligned_row = (unpadded_row + kRowAlignment - 1) & ~(kRowAlignment - 1);
            const uint64_t buf_size =
                static_cast<uint64_t>(cap_aligned_row) * cap_height;
            WGPUBufferDescriptor desc{};
            desc.label = to_sv("Editor Screenshot Staging");
            desc.size = buf_size;
            desc.usage = WGPUBufferUsage_CopyDst | WGPUBufferUsage_MapRead;
            desc.mappedAtCreation = false;
            capture_staging = wgpuDeviceCreateBuffer(impl_->gpu.device(), &desc);
            if (capture_staging) {
                WGPUTexelCopyTextureInfo src{};
                src.texture = st.texture;
                src.mipLevel = 0;
                src.origin = {0, 0, 0};
                src.aspect = WGPUTextureAspect_All;
                WGPUTexelCopyBufferInfo dst{};
                dst.buffer = capture_staging;
                dst.layout.offset = 0;
                dst.layout.bytesPerRow = cap_aligned_row;
                dst.layout.rowsPerImage = cap_height;
                WGPUExtent3D copy_size = {cap_width, cap_height, 1};
                wgpuCommandEncoderCopyTextureToBuffer(encoder, &src, &dst, &copy_size);
            }
        }

        const bool submit_ok = gpu_submit(impl_->gpu.device(), impl_->gpu.queue(),
                                          encoder, "Editor Window Commands");

        // Fulfil the capture request now that the submit queued the copy.
        if (ew.capture_dest && capture_staging && submit_ok) {
            // Wait for the GPU to finish our copy before mapping.
            bool work_done = false;
            WGPUQueueWorkDoneCallbackInfo work_cb{};
            work_cb.mode = WGPUCallbackMode_AllowSpontaneous;
            work_cb.callback = [](WGPUQueueWorkDoneStatus, WGPUStringView,
                                  void* u1, void*) {
                *static_cast<bool*>(u1) = true;
            };
            work_cb.userdata1 = &work_done;
            wgpuQueueOnSubmittedWorkDone(impl_->gpu.queue(), work_cb);
            while (!work_done)
                wgpuDevicePoll(impl_->gpu.device(), true, nullptr);

            const uint64_t buf_size =
                static_cast<uint64_t>(cap_aligned_row) * cap_height;
            bool map_done = false;
            WGPUBufferMapCallbackInfo map_cb{};
            map_cb.mode = WGPUCallbackMode_AllowSpontaneous;
            map_cb.callback = [](WGPUMapAsyncStatus, WGPUStringView,
                                 void* u1, void*) {
                *static_cast<bool*>(u1) = true;
            };
            map_cb.userdata1 = &map_done;
            wgpuBufferMapAsync(capture_staging, WGPUMapMode_Read, 0, buf_size, map_cb);
            while (!map_done)
                wgpuDevicePoll(impl_->gpu.device(), true, nullptr);

            const uint8_t* mapped = static_cast<const uint8_t*>(
                wgpuBufferGetConstMappedRange(capture_staging, 0, buf_size));
            if (mapped) {
                const uint32_t bpp = 4;
                const uint32_t unpadded_row = cap_width * bpp;
                std::vector<uint8_t> pixels(
                    static_cast<std::size_t>(cap_width) * cap_height * bpp);
                for (uint32_t y = 0; y < cap_height; ++y) {
                    const uint8_t* src_row = mapped + y * cap_aligned_row;
                    uint8_t* dst_row = pixels.data() + y * unpadded_row;
                    for (uint32_t x = 0; x < cap_width; ++x) {
                        dst_row[x * 4 + 0] = src_row[x * 4 + 2];  // R <- B
                        dst_row[x * 4 + 1] = src_row[x * 4 + 1];  // G
                        dst_row[x * 4 + 2] = src_row[x * 4 + 0];  // B <- R
                        dst_row[x * 4 + 3] = src_row[x * 4 + 3];  // A
                    }
                }
                ew.capture_dest->clear();
                stbi_write_png_to_func(
                    [](void* ctx, void* data, int size) {
                        auto* v = static_cast<std::vector<uint8_t>*>(ctx);
                        const auto* bytes = static_cast<const uint8_t*>(data);
                        v->insert(v->end(), bytes, bytes + size);
                    },
                    ew.capture_dest, cap_width, cap_height, 4,
                    pixels.data(), static_cast<int>(unpadded_row));
            }
            wgpuBufferUnmap(capture_staging);
        }
        if (capture_staging) {
            wgpuBufferRelease(capture_staging);
            ew.capture_dest = nullptr;  // single-shot
        }

        if (submit_ok) {
            wgpuSurfacePresent(ew.surface);
        } else if (should_rate_limit_log(time, ew.last_submit_failure_log_time)) {
            std::fprintf(stderr,
                         "[vivid] EditorWindow submit failed for %s: acquire_status=%s logical=%ux%u fb=%ux%u pending_vertices=%zu last_error=%s\n",
                         ew.node_id.c_str(), surface_status_label(ew.last_acquire_status),
                         ew.logical_width, ew.logical_height,
                         ew.framebuffer_width, ew.framebuffer_height,
                         pending_vertices,
                         impl_->gpu.last_error().empty() ? "<none>"
                                                         : impl_->gpu.last_error().c_str());
        }
        wgpuTextureViewRelease(view);
        wgpuTextureRelease(st.texture);

        // End-of-frame bookkeeping.
        ew.pending_events.clear();
        ew.mouse.left_clicked = 0;
        ew.mouse.left_released = 0;
        ew.mouse.right_clicked = 0;
    }

    // Sweep.
    (void)impl_->bookkeeping.sweep([&](const std::string& node_id) {
        std::fprintf(stderr, "[vivid] EditorWindow closed for %s\n",
                     node_id.c_str());
        impl_->destroy_window_by_id(node_id);
    });
}

// ---------------------------------------------------------------------------
// Test-support surface (follow-up: second-window automated coverage).
// ---------------------------------------------------------------------------

bool EditorWindowManager::inject_event(const std::string& node_id,
                                       const VividEditorEvent& event) {
    EditorWindow* ew = impl_->find(node_id);
    if (!ew) return false;
    ew->pending_events.push_back(event);
    return true;
}

bool EditorWindowManager::inject_mouse_move(const std::string& node_id,
                                            float x, float y) {
    EditorWindow* ew = impl_->find(node_id);
    if (!ew) return false;
    ew->mouse.prev_x = ew->mouse.x;
    ew->mouse.prev_y = ew->mouse.y;
    ew->mouse.x = x;
    ew->mouse.y = y;
    VividEditorEvent e{};
    e.type = VIVID_EDITOR_EVENT_MOUSE_MOVE;
    e.x = x;
    e.y = y;
    ew->pending_events.push_back(e);
    return true;
}

bool EditorWindowManager::inject_mouse_button(const std::string& node_id,
                                              int button, int action, int mods) {
    EditorWindow* ew = impl_->find(node_id);
    if (!ew) return false;
    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            ew->mouse.left_down = 1;
            ew->mouse.left_clicked = 1;
        } else if (action == GLFW_RELEASE) {
            ew->mouse.left_down = 0;
            ew->mouse.left_released = 1;
        }
    } else if (button == GLFW_MOUSE_BUTTON_RIGHT && action == GLFW_PRESS) {
        ew->mouse.right_clicked = 1;
    }
    ew->mouse.shift_down = (mods & GLFW_MOD_SHIFT) ? 1 : 0;
    VividEditorEvent e{};
    e.type = VIVID_EDITOR_EVENT_MOUSE_BUTTON;
    e.x = ew->mouse.x;
    e.y = ew->mouse.y;
    e.button = button;
    e.action = action;
    e.modifiers = mods;
    ew->pending_events.push_back(e);
    return true;
}

bool EditorWindowManager::inject_key(const std::string& node_id,
                                     int key, int scancode, int action, int mods) {
    EditorWindow* ew = impl_->find(node_id);
    if (!ew) return false;
    ew->mouse.shift_down = (mods & GLFW_MOD_SHIFT) ? 1 : 0;
    VividEditorEvent e{};
    e.type = VIVID_EDITOR_EVENT_KEY;
    e.key = key;
    e.scancode = scancode;
    e.action = action;
    e.modifiers = mods;
    ew->pending_events.push_back(e);
    return true;
}

bool EditorWindowManager::inject_char(const std::string& node_id,
                                      uint32_t codepoint) {
    EditorWindow* ew = impl_->find(node_id);
    if (!ew) return false;
    VividEditorEvent e{};
    e.type = VIVID_EDITOR_EVENT_CHAR;
    e.codepoint = codepoint;
    ew->pending_events.push_back(e);
    return true;
}

std::optional<std::vector<uint8_t>>
EditorWindowManager::capture_surface_png(const std::string& node_id) {
    EditorWindow* ew = impl_->find(node_id);
    if (!ew || !ew->glfw || !ew->surface || !ew->renderer) return std::nullopt;
    std::vector<uint8_t> bytes;
    ew->capture_dest = &bytes;
    // Synchronously run one tick using the last known time. The tick
    // will encode a copyTextureToBuffer alongside the normal render and
    // fill `bytes` after the submit completes.
    tick(impl_->last_tick_time);
    // `capture_dest` is cleared inside tick() regardless of success.
    if (bytes.empty()) return std::nullopt;
    return bytes;
}

void EditorWindowManager::set_hidden_when_opening(bool hidden) {
    impl_->hidden_when_opening = hidden;
}

std::size_t EditorWindowManager::pending_event_count(const std::string& node_id) const {
    for (const auto& w : impl_->windows) {
        if (w->node_id == node_id) return w->pending_events.size();
    }
    return 0;
}

} // namespace vivid
