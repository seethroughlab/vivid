#pragma once
#include <string>
#include <vector>
#include <unordered_set>

// Forward declare GLFW types
struct GLFWwindow;
struct GLFWmonitor;

namespace vivid {

/**
 * @brief Information about a display monitor.
 */
struct MonitorInfo {
    int index;              ///< Monitor index (0 = primary)
    std::string name;       ///< Monitor name
    int width;              ///< Resolution width
    int height;             ///< Resolution height
    int refreshRate;        ///< Refresh rate in Hz
    int posX;               ///< Position X in virtual screen space
    int posY;               ///< Position Y in virtual screen space
    bool isPrimary;         ///< Is this the primary monitor?
};

class Window {
public:
    Window(int width, int height, const std::string& title, bool fullscreen = false);
    ~Window();

    // Non-copyable
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Lifecycle
    bool shouldClose() const;
    void pollEvents();
    void swapBuffers();

    // Accessors
    GLFWwindow* handle() const { return window_; }
    int width() const { return width_; }
    int height() const { return height_; }
    bool wasResized() const { return resized_; }
    void clearResizedFlag() { resized_ = false; }

    // Window properties
    void setTitle(const std::string& title);

    // Set resize callback
    void setResizeCallback(void (*callback)(int width, int height, void* userdata), void* userdata);

    // === Window Management ===

    /// Set fullscreen mode on the current or specified monitor
    void setFullscreen(bool fullscreen, int monitorIndex = -1);

    /// Toggle fullscreen mode
    void toggleFullscreen();

    /// Check if currently fullscreen
    bool isFullscreen() const { return isFullscreen_; }

    /// Set borderless window mode (no decorations)
    void setBorderless(bool borderless);

    /// Check if borderless
    bool isBorderless() const { return isBorderless_; }

    /// Set cursor visibility
    void setCursorVisible(bool visible);

    /// Check if cursor is visible
    bool isCursorVisible() const { return cursorVisible_; }

    /// Set always-on-top (floating) mode
    void setAlwaysOnTop(bool alwaysOnTop);

    /// Check if always-on-top
    bool isAlwaysOnTop() const { return alwaysOnTop_; }

    /// Set window position
    void setPosition(int x, int y);

    /// Get window position
    void getPosition(int& x, int& y) const;

    /// Set window size (content area)
    void setSize(int width, int height);

    /// Enumerate available monitors
    static std::vector<MonitorInfo> enumerateMonitors();

    /// Print monitor info to stdout
    static void printMonitors();

    /// Move window to specified monitor (centers on monitor)
    void moveToMonitor(int monitorIndex);

    // Keyboard input
    bool isKeyDown(int key) const;           // Key is currently held
    bool wasKeyPressed(int key) const;       // Key was just pressed this frame
    bool wasKeyReleased(int key) const;      // Key was just released this frame

    // Mouse input
    float mouseX() const { return mouseX_; }           // Mouse X position (pixels)
    float mouseY() const { return mouseY_; }           // Mouse Y position (pixels)
    float mouseNormX() const { return mouseX_ / width_; }   // Normalized [0, 1]
    float mouseNormY() const { return mouseY_ / height_; }  // Normalized [0, 1]
    bool isMouseDown(int button) const;                // Button is currently held (0=left, 1=right, 2=middle)
    bool wasMousePressed(int button) const;            // Button was just pressed this frame
    bool wasMouseReleased(int button) const;           // Button was just released this frame
    float scrollDeltaX() const { return scrollDeltaX_; }  // Horizontal scroll this frame
    float scrollDeltaY() const { return scrollDeltaY_; }  // Vertical scroll this frame

    void clearInputState();                  // Call at end of frame

private:
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);
    static void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
    static void mouseButtonCallback(GLFWwindow* window, int button, int action, int mods);
    static void cursorPosCallback(GLFWwindow* window, double xpos, double ypos);
    static void scrollCallback(GLFWwindow* window, double xoffset, double yoffset);

    GLFWwindow* window_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool resized_ = false;

    void (*resizeCallback_)(int, int, void*) = nullptr;
    void* resizeUserdata_ = nullptr;

    // Window state
    bool isFullscreen_ = false;
    bool isBorderless_ = false;
    bool cursorVisible_ = true;
    bool alwaysOnTop_ = false;

    // Saved windowed position/size for restoring from fullscreen
    int windowedX_ = 100;
    int windowedY_ = 100;
    int windowedWidth_ = 1280;
    int windowedHeight_ = 720;

    // Keyboard state
    std::unordered_set<int> keysDown_;       // Currently held keys
    std::unordered_set<int> keysPressed_;    // Pressed this frame
    std::unordered_set<int> keysReleased_;   // Released this frame

    // Mouse state
    float mouseX_ = 0.0f;
    float mouseY_ = 0.0f;
    std::unordered_set<int> mouseButtonsDown_;
    std::unordered_set<int> mouseButtonsPressed_;
    std::unordered_set<int> mouseButtonsReleased_;
    float scrollDeltaX_ = 0.0f;
    float scrollDeltaY_ = 0.0f;
};

} // namespace vivid
