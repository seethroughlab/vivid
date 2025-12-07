#pragma once

// Vivid V3 - Context
// Runtime state passed to chain setup/update functions

#include <webgpu/webgpu.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <string>

namespace vivid {

// Key state
struct KeyState {
    bool pressed = false;   // True during the frame the key was pressed
    bool held = false;      // True while the key is held down
    bool released = false;  // True during the frame the key was released
};

// Mouse button state
struct MouseButtonState {
    bool pressed = false;   // True during the frame the button was pressed
    bool held = false;      // True while the button is held down
    bool released = false;  // True during the frame the button was released
};

class Context {
public:
    Context(GLFWwindow* window, WGPUDevice device, WGPUQueue queue);
    ~Context();

    // Called each frame before update
    void beginFrame();

    // Called each frame after update
    void endFrame();

    // -------------------------------------------------------------------------
    // Time
    // -------------------------------------------------------------------------

    // Time since program start (seconds)
    double time() const { return m_time; }

    // Time since last frame (seconds)
    double dt() const { return m_dt; }

    // Current frame number (0-indexed)
    uint64_t frame() const { return m_frame; }

    // -------------------------------------------------------------------------
    // Window
    // -------------------------------------------------------------------------

    // Window dimensions in pixels
    int width() const { return m_width; }
    int height() const { return m_height; }

    // Aspect ratio (width / height)
    float aspect() const { return m_height > 0 ? static_cast<float>(m_width) / m_height : 1.0f; }

    // -------------------------------------------------------------------------
    // Mouse
    // -------------------------------------------------------------------------

    // Mouse position in pixels (0,0 is top-left)
    glm::vec2 mouse() const { return m_mousePos; }

    // Normalized mouse position (-1 to 1, with Y up)
    glm::vec2 mouseNorm() const;

    // Mouse button state (0=left, 1=right, 2=middle)
    const MouseButtonState& mouseButton(int button) const;

    // Mouse scroll delta since last frame
    glm::vec2 scroll() const { return m_scroll; }

    // -------------------------------------------------------------------------
    // Keyboard
    // -------------------------------------------------------------------------

    // Key state by GLFW key code (e.g., GLFW_KEY_SPACE)
    const KeyState& key(int keyCode) const;

    // -------------------------------------------------------------------------
    // WebGPU Access
    // -------------------------------------------------------------------------

    WGPUDevice device() const { return m_device; }
    WGPUQueue queue() const { return m_queue; }

    // -------------------------------------------------------------------------
    // Output Texture (set by chain, read by display)
    // -------------------------------------------------------------------------

    void setOutputTexture(WGPUTextureView texture) { m_outputTexture = texture; }
    WGPUTextureView outputTexture() const { return m_outputTexture; }

    // -------------------------------------------------------------------------
    // Error State
    // -------------------------------------------------------------------------

    bool hasError() const { return !m_errorMessage.empty(); }
    const std::string& errorMessage() const { return m_errorMessage; }
    void setError(const std::string& message) { m_errorMessage = message; }
    void clearError() { m_errorMessage.clear(); }

private:
    GLFWwindow* m_window;
    WGPUDevice m_device;
    WGPUQueue m_queue;

    // Time
    double m_time = 0.0;
    double m_dt = 0.0;
    double m_lastTime = 0.0;
    uint64_t m_frame = 0;

    // Window
    int m_width = 0;
    int m_height = 0;

    // Mouse
    glm::vec2 m_mousePos = {0, 0};
    glm::vec2 m_lastMousePos = {0, 0};
    glm::vec2 m_scroll = {0, 0};
    MouseButtonState m_mouseButtons[3];
    bool m_mouseButtonPrev[3] = {false, false, false};

    // Keyboard
    static constexpr int MAX_KEYS = GLFW_KEY_LAST + 1;
    KeyState m_keys[MAX_KEYS];
    bool m_keyPrev[MAX_KEYS] = {};

    // Output
    WGPUTextureView m_outputTexture = nullptr;

    // Error
    std::string m_errorMessage;

    // Default state for out-of-range queries
    static const KeyState s_defaultKeyState;
    static const MouseButtonState s_defaultMouseState;
};

} // namespace vivid
