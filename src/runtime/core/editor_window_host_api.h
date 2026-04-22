#pragma once
//
// Editor-window host-service context + thunks used by EditorWindowManager
// to populate VividEditorContext::host (Phase D of the editor-UI platform
// plan). Extracted into its own translation unit so the runtime and
// tests can exercise the state-bearing pieces independently of GLFW/WGPU.
//
// Usage:
//   HostCtx host_ctx;                     // member of EditorWindow
//   host_ctx.glfw = ew.glfw;
//   host_ctx.requested_cursor = VIVID_CURSOR_DEFAULT;  // reset per frame
//   host_ctx.status_text.clear();                      // reset per frame
//   host_ctx.tooltip_text.clear();                     // reset per frame
//   ctx.host = make_host_api(&host_ctx);               // into editor ctx

#include "operator_api/types.h"

#include <string>

struct GLFWwindow;  // forward-declared; only clipboard + focus thunks touch it

namespace vivid {

struct HostCtx {
    GLFWwindow*     glfw = nullptr;
    VividCursorKind requested_cursor = VIVID_CURSOR_DEFAULT;
    bool            pointer_captured = false;
    bool            request_focus    = false;
    std::string     status_text;
    std::string     tooltip_text;
    std::string     clipboard_cache;  // stable backing for get_clipboard_text
};

// Construct a VividEditorHostAPI bound to the given HostCtx. The struct
// is stable for the lifetime of *ctx; fn pointers remain valid as long
// as the ctx object is alive.
VividEditorHostAPI make_host_api(HostCtx* ctx);

// Individual thunk signatures are exposed for tests.  Operators should
// call through `VividEditorHostAPI` fn pointers, not these directly.
const char* ed_get_clipboard_text(void* opaque);
void        ed_set_clipboard_text(void* opaque, const char* text);
void        ed_set_cursor(void* opaque, VividCursorKind kind);
void        ed_capture_pointer(void* opaque);
void        ed_release_pointer(void* opaque);
int         ed_has_pointer_capture(void* opaque);
void        ed_request_focus(void* opaque);
int         ed_has_focus(void* opaque);
void        ed_set_status_text(void* opaque, const char* text);
void        ed_show_tooltip(void* opaque, const char* text);

} // namespace vivid
