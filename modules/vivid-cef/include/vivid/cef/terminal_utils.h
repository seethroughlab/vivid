#pragma once

/**
 * @file terminal_utils.h
 * @brief Helper functions for terminal keyboard input handling
 *
 * These utilities convert GLFW key events to terminal escape sequences,
 * enabling direct PTY input that bypasses CEF's keyboard processing.
 */

#include <vivid/cef/types.h>
#include <string>

// GLFW key codes (from glfw3.h)
// We define these here to avoid requiring GLFW header in user code
#define VIVID_KEY_SPACE           32
#define VIVID_KEY_ESCAPE          256
#define VIVID_KEY_ENTER           257
#define VIVID_KEY_TAB             258
#define VIVID_KEY_BACKSPACE       259
#define VIVID_KEY_INSERT          260
#define VIVID_KEY_DELETE          261
#define VIVID_KEY_RIGHT           262
#define VIVID_KEY_LEFT            263
#define VIVID_KEY_DOWN            264
#define VIVID_KEY_UP              265
#define VIVID_KEY_PAGE_UP         266
#define VIVID_KEY_PAGE_DOWN       267
#define VIVID_KEY_HOME            268
#define VIVID_KEY_END             269
#define VIVID_KEY_A               65
#define VIVID_KEY_Z               90
#define VIVID_KEY_F1              290
#define VIVID_KEY_F12             301

namespace vivid::cef {

/**
 * @brief Convert GLFW key + modifiers to terminal escape sequence
 * @param glfwKey The GLFW key code
 * @param mods Bitmask of KeyModifiers
 * @return Terminal escape sequence, or empty string if not a special key
 *
 * Returns empty string for regular characters (use char callback instead).
 */
inline std::string keyToTerminalSequence(int glfwKey, uint32_t mods) {
    bool ctrl = (mods & ModControl) != 0;

    switch (glfwKey) {
        case VIVID_KEY_ENTER:     return "\r";
        case VIVID_KEY_BACKSPACE: return "\x7f";  // DEL character
        case VIVID_KEY_TAB:       return "\t";
        case VIVID_KEY_ESCAPE:    return "\x1b";
        case VIVID_KEY_UP:        return "\x1b[A";
        case VIVID_KEY_DOWN:      return "\x1b[B";
        case VIVID_KEY_RIGHT:     return "\x1b[C";
        case VIVID_KEY_LEFT:      return "\x1b[D";
        case VIVID_KEY_HOME:      return "\x1b[H";
        case VIVID_KEY_END:       return "\x1b[F";
        case VIVID_KEY_DELETE:    return "\x1b[3~";
        case VIVID_KEY_PAGE_UP:   return "\x1b[5~";
        case VIVID_KEY_PAGE_DOWN: return "\x1b[6~";
        case VIVID_KEY_INSERT:    return "\x1b[2~";
        default:
            // Ctrl+letter -> control character (Ctrl+C = 0x03, Ctrl+D = 0x04, etc.)
            if (ctrl && glfwKey >= VIVID_KEY_A && glfwKey <= VIVID_KEY_Z) {
                return std::string(1, char(glfwKey - VIVID_KEY_A + 1));
            }
            // Function keys
            if (glfwKey >= VIVID_KEY_F1 && glfwKey <= VIVID_KEY_F12) {
                int fnum = glfwKey - VIVID_KEY_F1 + 1;
                // F1-F4 use different sequences than F5-F12
                if (fnum <= 4) {
                    return "\x1bO" + std::string(1, char('P' + fnum - 1));
                } else {
                    // F5=15, F6=17, F7=18, F8=19, F9=20, F10=21, F11=23, F12=24
                    static const int codes[] = {15, 17, 18, 19, 20, 21, 23, 24};
                    return "\x1b[" + std::to_string(codes[fnum - 5]) + "~";
                }
            }
            return "";
    }
}

/**
 * @brief Encode Unicode codepoint to UTF-8
 * @param cp Unicode codepoint
 * @return UTF-8 encoded string
 */
inline std::string codepointToUtf8(uint32_t cp) {
    std::string result;
    if (cp < 0x80) {
        result += static_cast<char>(cp);
    } else if (cp < 0x800) {
        result += static_cast<char>(0xC0 | (cp >> 6));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        result += static_cast<char>(0xE0 | (cp >> 12));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        result += static_cast<char>(0xF0 | (cp >> 18));
        result += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        result += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        result += static_cast<char>(0x80 | (cp & 0x3F));
    }
    return result;
}

} // namespace vivid::cef
