#pragma once

/**
 * @file event.h
 * @brief Frame-rate event types for input handling (keyboard, mouse, window, timing)
 *
 * Events are collected each frame and exposed via event operators.
 * Unlike AudioEvent (for audio thread communication), these events are
 * processed on the main thread during normal chain processing.
 */

#include <cstdint>

namespace vivid {

/**
 * @brief Event types for frame-rate event operators
 *
 * Note: Timing events (beats, sequencer steps) are handled separately via
 * AudioOperator with atomic flags. This enum is only for UI input events.
 */
enum class EventType : uint8_t {
    // Keyboard events
    KeyPress,         ///< Key was pressed this frame
    KeyRelease,       ///< Key was released this frame

    // Mouse events
    MousePress,       ///< Mouse button was pressed
    MouseRelease,     ///< Mouse button was released
    MouseMove,        ///< Mouse position changed
    MouseScroll,      ///< Mouse wheel scrolled

    // Window events
    WindowResize,     ///< Window was resized
    WindowFocus,      ///< Window gained focus
    WindowUnfocus,    ///< Window lost focus
};

/**
 * @brief Single event instance
 *
 * A lightweight struct representing a discrete event that occurred.
 * Multiple events of the same type can occur in a single frame.
 */
struct Event {
    EventType type;             ///< What kind of event
    int32_t code = 0;           ///< Key code, mouse button (0=left, 1=right, 2=middle), step number
    float value1 = 0.0f;        ///< Mouse X position, scroll X delta, velocity, width (for resize)
    float value2 = 0.0f;        ///< Mouse Y position, scroll Y delta, height (for resize)
    uint32_t frame = 0;         ///< Frame number when event occurred

    // Convenience constructors
    static Event keyPress(int32_t keyCode) {
        return Event{EventType::KeyPress, keyCode, 0.0f, 0.0f, 0};
    }

    static Event keyRelease(int32_t keyCode) {
        return Event{EventType::KeyRelease, keyCode, 0.0f, 0.0f, 0};
    }

    static Event mousePress(int32_t button, float x, float y) {
        return Event{EventType::MousePress, button, x, y, 0};
    }

    static Event mouseRelease(int32_t button, float x, float y) {
        return Event{EventType::MouseRelease, button, x, y, 0};
    }

    static Event mouseMove(float x, float y) {
        return Event{EventType::MouseMove, 0, x, y, 0};
    }

    static Event mouseScroll(float dx, float dy) {
        return Event{EventType::MouseScroll, 0, dx, dy, 0};
    }

    static Event windowResize(int width, int height) {
        return Event{EventType::WindowResize, 0, static_cast<float>(width), static_cast<float>(height), 0};
    }

    static Event windowFocus() {
        return Event{EventType::WindowFocus, 0, 0.0f, 0.0f, 0};
    }

    static Event windowUnfocus() {
        return Event{EventType::WindowUnfocus, 0, 0.0f, 0.0f, 0};
    }
};

} // namespace vivid
