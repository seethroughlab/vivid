#pragma once

/**
 * @file shortcut_manager.h
 * @brief Keyboard shortcut system with platform-native modifiers
 *
 * Handles global keyboard shortcuts for DevTools panels.
 * Uses Cmd on macOS and Ctrl on Windows/Linux for platform-native behavior.
 */

#include <functional>
#include <string>
#include <vector>

namespace vivid {

/**
 * @brief Represents a keyboard shortcut
 */
struct Shortcut {
    int key;                          ///< GLFW key code
    int modifiers;                    ///< Required modifiers (platform-native Cmd/Ctrl)
    std::string id;                   ///< Unique identifier (e.g., "toggle_terminal")
    std::string label;                ///< Human-readable label (e.g., "Toggle Terminal")
    std::function<void()> callback;   ///< Action to execute
};

/**
 * @brief Manages keyboard shortcuts with platform-native modifier handling
 *
 * Features:
 * - Platform-aware Cmd/Ctrl detection (Cmd on macOS, Ctrl elsewhere)
 * - Shortcut registration and unregistration
 * - Help text generation for displaying shortcuts
 *
 * Usage:
 * @code
 * ShortcutManager shortcuts;
 * shortcuts.registerShortcut({
 *     GLFW_KEY_1,
 *     ShortcutManager::MOD_CMD_OR_CTRL,
 *     "toggle_terminal",
 *     "Toggle Terminal",
 *     [this]() { togglePanel("terminal"); }
 * });
 *
 * // In key handler:
 * if (shortcuts.handleKeyDown(key, mods)) {
 *     // Shortcut was triggered, input consumed
 * }
 * @endcode
 */
class ShortcutManager {
public:
    /// Modifier flag for Cmd on macOS, Ctrl on Windows/Linux
    static constexpr int MOD_CMD_OR_CTRL = 0x100;  // Custom flag, resolved at runtime

    /// Standard GLFW modifier flags
    static constexpr int MOD_SHIFT = 0x1;
    static constexpr int MOD_CTRL = 0x2;
    static constexpr int MOD_ALT = 0x4;
    static constexpr int MOD_SUPER = 0x8;  // Cmd on macOS, Windows key on Windows

    ShortcutManager() = default;
    ~ShortcutManager() = default;

    /**
     * @brief Register a keyboard shortcut
     * @param shortcut Shortcut definition
     */
    void registerShortcut(const Shortcut& shortcut);

    /**
     * @brief Unregister a shortcut by ID
     * @param id Shortcut identifier
     */
    void unregisterShortcut(const std::string& id);

    /**
     * @brief Handle a key down event
     * @param key GLFW key code
     * @param mods GLFW modifier flags
     * @return true if a shortcut was triggered (input consumed)
     */
    bool handleKeyDown(int key, int mods);

    /**
     * @brief Get all registered shortcuts
     * @return Vector of shortcuts (for help panel)
     */
    const std::vector<Shortcut>& shortcuts() const { return m_shortcuts; }

    /**
     * @brief Check if Cmd (macOS) or Ctrl (Windows/Linux) is pressed
     * @param mods GLFW modifier flags
     * @return true if the platform-native command modifier is active
     */
    static bool isCmdOrCtrl(int mods);

    /**
     * @brief Get the display name for Cmd/Ctrl based on platform
     * @return "Cmd" on macOS, "Ctrl" on other platforms
     */
    static const char* cmdOrCtrlName();

    /**
     * @brief Get display string for a shortcut
     * @param shortcut Shortcut to format
     * @return Formatted string like "Cmd+1" or "Ctrl+1"
     */
    static std::string formatShortcut(const Shortcut& shortcut);

private:
    std::vector<Shortcut> m_shortcuts;
};

} // namespace vivid
