#include "runtime/core/editor_window_host_api.h"

#include <GLFW/glfw3.h>

namespace vivid {

const char* ed_get_clipboard_text(void* opaque) {
    auto* ctx = static_cast<HostCtx*>(opaque);
    if (!ctx) return nullptr;
    if (!ctx->glfw) {
        ctx->clipboard_cache.clear();
        return ctx->clipboard_cache.c_str();
    }
    const char* s = glfwGetClipboardString(ctx->glfw);
    if (!s) {
        ctx->clipboard_cache.clear();
        return ctx->clipboard_cache.c_str();
    }
    ctx->clipboard_cache = s;  // GLFW buffer is ephemeral; stabilise it
    return ctx->clipboard_cache.c_str();
}

void ed_set_clipboard_text(void* opaque, const char* text) {
    auto* ctx = static_cast<HostCtx*>(opaque);
    if (!ctx || !ctx->glfw) return;
    glfwSetClipboardString(ctx->glfw, text ? text : "");
}

void ed_set_cursor(void* opaque, VividCursorKind kind) {
    auto* ctx = static_cast<HostCtx*>(opaque);
    if (ctx) ctx->requested_cursor = kind;
}

void ed_capture_pointer(void* opaque) {
    auto* ctx = static_cast<HostCtx*>(opaque);
    if (ctx) ctx->pointer_captured = true;
}
void ed_release_pointer(void* opaque) {
    auto* ctx = static_cast<HostCtx*>(opaque);
    if (ctx) ctx->pointer_captured = false;
}
int ed_has_pointer_capture(void* opaque) {
    auto* ctx = static_cast<HostCtx*>(opaque);
    return (ctx && ctx->pointer_captured) ? 1 : 0;
}

void ed_request_focus(void* opaque) {
    auto* ctx = static_cast<HostCtx*>(opaque);
    if (ctx) ctx->request_focus = true;
}
int ed_has_focus(void* opaque) {
    auto* ctx = static_cast<HostCtx*>(opaque);
    if (!ctx || !ctx->glfw) return 0;
    return glfwGetWindowAttrib(ctx->glfw, GLFW_FOCUSED) ? 1 : 0;
}

void ed_set_status_text(void* opaque, const char* text) {
    auto* ctx = static_cast<HostCtx*>(opaque);
    if (!ctx) return;
    ctx->status_text = text ? text : "";
}
void ed_show_tooltip(void* opaque, const char* text) {
    auto* ctx = static_cast<HostCtx*>(opaque);
    if (!ctx) return;
    ctx->tooltip_text = text ? text : "";
}

VividEditorHostAPI make_host_api(HostCtx* ctx) {
    VividEditorHostAPI h{};
    h.opaque               = ctx;
    h.get_clipboard_text   = ed_get_clipboard_text;
    h.set_clipboard_text   = ed_set_clipboard_text;
    h.set_cursor           = ed_set_cursor;
    h.capture_pointer      = ed_capture_pointer;
    h.release_pointer      = ed_release_pointer;
    h.has_pointer_capture  = ed_has_pointer_capture;
    h.request_focus        = ed_request_focus;
    h.has_focus            = ed_has_focus;
    h.set_status_text      = ed_set_status_text;
    h.show_tooltip         = ed_show_tooltip;
    return h;
}

} // namespace vivid
