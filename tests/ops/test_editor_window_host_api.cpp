// Unit tests for the editor-window host-service thunks
// (src/runtime/core/editor_window_host_api.{h,cpp}).
//
// Exercises the pure-state thunks (cursor / status / tooltip / pointer-
// capture / focus request) directly against a HostCtx that carries no
// GLFWwindow. Clipboard + has_focus are GLFW-backed — those paths are
// validated against a nullptr-GLFW HostCtx, which must degrade gracefully.

#include "runtime/core/editor_window_host_api.h"
#include "operator_api/types.h"

#include <cstdio>
#include <cstring>
#include <string>

#include "test_helpers.h"

int main() {
    std::fprintf(stderr, "=== Test: EditorWindowManager host API ===\n\n");

    using vivid::HostCtx;
    using vivid::make_host_api;

    // --- make_host_api populates every fn pointer ---
    {
        HostCtx ctx;
        VividEditorHostAPI api = make_host_api(&ctx);
        check(api.opaque == &ctx, "api.opaque points at the host ctx");
        check(api.get_clipboard_text != nullptr, "get_clipboard_text wired");
        check(api.set_clipboard_text != nullptr, "set_clipboard_text wired");
        check(api.set_cursor != nullptr, "set_cursor wired");
        check(api.capture_pointer != nullptr, "capture_pointer wired");
        check(api.release_pointer != nullptr, "release_pointer wired");
        check(api.has_pointer_capture != nullptr, "has_pointer_capture wired");
        check(api.request_focus != nullptr, "request_focus wired");
        check(api.has_focus != nullptr, "has_focus wired");
        check(api.set_status_text != nullptr, "set_status_text wired");
        check(api.show_tooltip != nullptr, "show_tooltip wired");
    }

    // --- Cursor kind round-trips through set_cursor ---
    {
        HostCtx ctx;
        VividEditorHostAPI api = make_host_api(&ctx);
        check(ctx.requested_cursor == VIVID_CURSOR_DEFAULT,
              "cursor starts at DEFAULT");
        api.set_cursor(api.opaque, VIVID_CURSOR_HAND);
        check(ctx.requested_cursor == VIVID_CURSOR_HAND,
              "set_cursor(HAND) → ctx stores HAND");
        api.set_cursor(api.opaque, VIVID_CURSOR_RESIZE_V);
        check(ctx.requested_cursor == VIVID_CURSOR_RESIZE_V,
              "set_cursor(RESIZE_V) → ctx stores RESIZE_V");
    }

    // --- Pointer capture state machine ---
    {
        HostCtx ctx;
        VividEditorHostAPI api = make_host_api(&ctx);
        check(api.has_pointer_capture(api.opaque) == 0,
              "pointer capture starts false");
        api.capture_pointer(api.opaque);
        check(api.has_pointer_capture(api.opaque) == 1,
              "capture_pointer → has_pointer_capture = 1");
        api.release_pointer(api.opaque);
        check(api.has_pointer_capture(api.opaque) == 0,
              "release_pointer → has_pointer_capture = 0");
    }

    // --- Focus request sets the flag (has_focus is GLFW-backed) ---
    {
        HostCtx ctx;
        VividEditorHostAPI api = make_host_api(&ctx);
        check(ctx.request_focus == false, "focus request flag starts false");
        api.request_focus(api.opaque);
        check(ctx.request_focus == true, "request_focus → flag set");

        // has_focus on a nullptr-GLFW HostCtx must return 0 safely.
        check(api.has_focus(api.opaque) == 0,
              "has_focus with no GLFW window returns 0");
    }

    // --- Status text and tooltip persist per-frame ---
    {
        HostCtx ctx;
        VividEditorHostAPI api = make_host_api(&ctx);
        check(ctx.status_text.empty(), "status_text starts empty");
        api.set_status_text(api.opaque, "kick · step 1");
        check(ctx.status_text == "kick · step 1",
              "set_status_text round-trip");
        api.set_status_text(api.opaque, nullptr);
        check(ctx.status_text.empty(), "set_status_text(nullptr) clears");

        api.show_tooltip(api.opaque, "hovered point 3");
        check(ctx.tooltip_text == "hovered point 3", "show_tooltip round-trip");
        api.show_tooltip(api.opaque, nullptr);
        check(ctx.tooltip_text.empty(), "show_tooltip(nullptr) clears");
    }

    // --- Clipboard on a nullptr-GLFW HostCtx degrades to empty string ---
    {
        HostCtx ctx;  // glfw stays nullptr
        VividEditorHostAPI api = make_host_api(&ctx);
        const char* s = api.get_clipboard_text(api.opaque);
        check(s != nullptr, "get_clipboard_text returns non-null even without GLFW");
        check(std::strlen(s) == 0, "get_clipboard_text without GLFW returns empty");
        // set_clipboard_text with no window is a no-op (must not crash).
        api.set_clipboard_text(api.opaque, "hello");
        check(true, "set_clipboard_text without GLFW is a safe no-op");
    }

    // --- Null opaque is always safe (operator-authored guards may leak one through) ---
    {
        VividEditorHostAPI api = make_host_api(nullptr);
        api.set_cursor(api.opaque, VIVID_CURSOR_HAND);
        api.capture_pointer(api.opaque);
        api.release_pointer(api.opaque);
        check(api.has_pointer_capture(api.opaque) == 0,
              "has_pointer_capture(nullptr) returns 0");
        check(api.has_focus(api.opaque) == 0,
              "has_focus(nullptr) returns 0");
        api.set_status_text(api.opaque, "x");
        api.show_tooltip(api.opaque, "x");
        check(true, "null opaque does not crash any thunk");
    }

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
