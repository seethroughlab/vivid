#pragma once

#include <functional>
#include <string>
#include <cstdint>

namespace vivid::cef {

/**
 * @brief Mouse button identifiers
 */
enum class MouseButton {
    Left = 0,
    Middle = 1,
    Right = 2
};

/**
 * @brief Cursor types reported by CEF
 */
enum class CursorType {
    Default,
    Pointer,      // Hand cursor (links)
    Text,         // I-beam (text input)
    Wait,         // Busy
    Progress,     // Busy with background activity
    CrossHair,
    Help,
    Move,
    ResizeN,
    ResizeS,
    ResizeE,
    ResizeW,
    ResizeNE,
    ResizeNW,
    ResizeSE,
    ResizeSW,
    ResizeEW,
    ResizeNS,
    ResizeNESW,
    ResizeNWSE,
    NotAllowed,
    Grab,
    Grabbing,
    Custom
};

/**
 * @brief Console message severity levels
 */
struct ConsoleMessage {
    enum class Level {
        Debug,
        Info,
        Warning,
        Error
    };
};

/**
 * @brief Input state for Browser when Context's input has been blocked
 *
 * Use this to pass raw input state to Browser when the Context's mouse
 * state has been zeroed (e.g., when visualizer is visible).
 */
struct RawInputState {
    float mouseX = 0;
    float mouseY = 0;
    bool mouseButtons[3] = {false, false, false};  // Left, Right, Middle
    float scrollX = 0;
    float scrollY = 0;
    bool shiftHeld = false;
    bool ctrlHeld = false;
    bool altHeld = false;
    bool superHeld = false;
    bool keyDown[512] = {};
    std::vector<uint32_t> characterInput;
};

// Callback types
using LoadEndCallback = std::function<void(const std::string& url, int httpStatus)>;
using ConsoleCallback = std::function<void(ConsoleMessage::Level level, const std::string& message,
                                           const std::string& source, int line)>;
using JSResultCallback = std::function<void(bool success, const std::string& value)>;
using JSCallback = std::function<void(const std::string& jsonArgs)>;
using CursorChangeCallback = std::function<void(CursorType cursor)>;

} // namespace vivid::cef
