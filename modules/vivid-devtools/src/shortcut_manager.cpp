// ShortcutManager implementation
// Handles keyboard shortcuts with platform-native modifiers

#include <vivid/devtools/shortcut_manager.h>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <sstream>

namespace vivid {

void ShortcutManager::registerShortcut(const Shortcut& shortcut) {
    // Remove existing shortcut with same ID if present
    unregisterShortcut(shortcut.id);
    m_shortcuts.push_back(shortcut);
}

void ShortcutManager::unregisterShortcut(const std::string& id) {
    m_shortcuts.erase(
        std::remove_if(m_shortcuts.begin(), m_shortcuts.end(),
            [&id](const Shortcut& s) { return s.id == id; }),
        m_shortcuts.end()
    );
}

bool ShortcutManager::handleKeyDown(int key, int mods) {
    for (const auto& shortcut : m_shortcuts) {
        if (shortcut.key != key) continue;

        // Check modifiers
        int requiredMods = shortcut.modifiers;

        // Resolve MOD_CMD_OR_CTRL to platform-specific modifier
        bool needsCmdOrCtrl = (requiredMods & MOD_CMD_OR_CTRL) != 0;
        requiredMods &= ~MOD_CMD_OR_CTRL;  // Clear custom flag

        if (needsCmdOrCtrl) {
#ifdef __APPLE__
            requiredMods |= MOD_SUPER;  // Cmd on macOS
#else
            requiredMods |= MOD_CTRL;   // Ctrl on Windows/Linux
#endif
        }

        // Check if required modifiers match (allow extra modifiers like CapsLock)
        if ((mods & (MOD_SHIFT | MOD_CTRL | MOD_ALT | MOD_SUPER)) == requiredMods) {
            if (shortcut.callback) {
                shortcut.callback();
            }
            return true;  // Shortcut consumed input
        }
    }
    return false;
}

bool ShortcutManager::isCmdOrCtrl(int mods) {
#ifdef __APPLE__
    return (mods & MOD_SUPER) != 0;  // Cmd on macOS
#else
    return (mods & MOD_CTRL) != 0;   // Ctrl on Windows/Linux
#endif
}

const char* ShortcutManager::cmdOrCtrlName() {
#ifdef __APPLE__
    return "Cmd";
#else
    return "Ctrl";
#endif
}

std::string ShortcutManager::formatShortcut(const Shortcut& shortcut) {
    std::ostringstream ss;
    int mods = shortcut.modifiers;

    // Handle CMD_OR_CTRL first
    if (mods & MOD_CMD_OR_CTRL) {
        ss << cmdOrCtrlName() << "+";
        mods &= ~MOD_CMD_OR_CTRL;
    }

    // Standard modifiers
    if (mods & MOD_CTRL) ss << "Ctrl+";
    if (mods & MOD_ALT) ss << "Alt+";
    if (mods & MOD_SHIFT) ss << "Shift+";
    if (mods & MOD_SUPER) {
#ifdef __APPLE__
        ss << "Cmd+";
#else
        ss << "Win+";
#endif
    }

    // Key name
    switch (shortcut.key) {
        case GLFW_KEY_F1: ss << "F1"; break;
        case GLFW_KEY_F2: ss << "F2"; break;
        case GLFW_KEY_F3: ss << "F3"; break;
        case GLFW_KEY_F4: ss << "F4"; break;
        case GLFW_KEY_F5: ss << "F5"; break;
        case GLFW_KEY_F6: ss << "F6"; break;
        case GLFW_KEY_F7: ss << "F7"; break;
        case GLFW_KEY_F8: ss << "F8"; break;
        case GLFW_KEY_F9: ss << "F9"; break;
        case GLFW_KEY_F10: ss << "F10"; break;
        case GLFW_KEY_F11: ss << "F11"; break;
        case GLFW_KEY_F12: ss << "F12"; break;
        case GLFW_KEY_ESCAPE: ss << "Esc"; break;
        case GLFW_KEY_ENTER: ss << "Enter"; break;
        case GLFW_KEY_TAB: ss << "Tab"; break;
        case GLFW_KEY_SPACE: ss << "Space"; break;
        case GLFW_KEY_COMMA: ss << ","; break;
        case GLFW_KEY_SLASH: ss << "/"; break;
        default:
            // For alphanumeric keys, use the character
            if (shortcut.key >= GLFW_KEY_0 && shortcut.key <= GLFW_KEY_9) {
                ss << static_cast<char>('0' + (shortcut.key - GLFW_KEY_0));
            } else if (shortcut.key >= GLFW_KEY_A && shortcut.key <= GLFW_KEY_Z) {
                ss << static_cast<char>('A' + (shortcut.key - GLFW_KEY_A));
            } else {
                ss << "Key" << shortcut.key;
            }
            break;
    }

    return ss.str();
}

} // namespace vivid
