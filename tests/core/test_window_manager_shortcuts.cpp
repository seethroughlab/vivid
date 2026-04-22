// Focused contract test for the platform-specific editor-open shortcut helper.

#include "runtime/core/window_manager.h"

#include <cstdio>

#include "test_helpers.h"

int main() {
    std::fprintf(stderr, "=== Test: editor-open shortcut contract ===\n\n");

    using vivid::EditorShortcutPlatform;
    using vivid::is_open_editor_shortcut_for_platform;

    check(is_open_editor_shortcut_for_platform(
              EditorShortcutPlatform::MacOS,
              GLFW_KEY_E, GLFW_PRESS, GLFW_MOD_SUPER),
          "macOS: Cmd+E opens the editor");
    check(!is_open_editor_shortcut_for_platform(
               EditorShortcutPlatform::MacOS,
               GLFW_KEY_E, GLFW_PRESS, GLFW_MOD_CONTROL),
          "macOS: Ctrl+E does not open the editor");
    check(!is_open_editor_shortcut_for_platform(
               EditorShortcutPlatform::MacOS,
               GLFW_KEY_E, GLFW_PRESS, GLFW_MOD_SUPER | GLFW_MOD_SHIFT),
          "macOS: Shift+Cmd+E does not open the editor");

    check(is_open_editor_shortcut_for_platform(
              EditorShortcutPlatform::Other,
              GLFW_KEY_E, GLFW_PRESS, GLFW_MOD_CONTROL),
          "non-macOS: Ctrl+E opens the editor");
    check(!is_open_editor_shortcut_for_platform(
               EditorShortcutPlatform::Other,
               GLFW_KEY_E, GLFW_PRESS, GLFW_MOD_SUPER),
          "non-macOS: Super+E does not open the editor");
    check(!is_open_editor_shortcut_for_platform(
               EditorShortcutPlatform::Other,
               GLFW_KEY_E, GLFW_PRESS, GLFW_MOD_CONTROL | GLFW_MOD_SHIFT),
          "non-macOS: Shift+Ctrl+E does not open the editor");

    check(!is_open_editor_shortcut_for_platform(
               EditorShortcutPlatform::MacOS,
               GLFW_KEY_E, GLFW_RELEASE, GLFW_MOD_SUPER),
          "shortcut only triggers on key press");
    check(!is_open_editor_shortcut_for_platform(
               EditorShortcutPlatform::Other,
               GLFW_KEY_R, GLFW_PRESS, GLFW_MOD_CONTROL),
          "other keys do not trigger the shortcut");

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
