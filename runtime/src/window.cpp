#include "window.h"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <iostream>
#include <iomanip>

namespace vivid {

Window::Window(int width, int height, const std::string& title, bool fullscreen)
    : width_(width), height_(height) {

    // Initialize GLFW
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    // Set hints for WebGPU (no OpenGL context)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
#ifdef _WIN32
    // Disable resize on Windows due to wgpu-native D3D12 surface reconfigure issue
    // TODO: Fix proper surface recreation on resize
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
#else
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
#endif

    // Create window
    GLFWmonitor* monitor = fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    window_ = glfwCreateWindow(width, height, title.c_str(), monitor, nullptr);

    if (!window_) {
        glfwTerminate();
        throw std::runtime_error("Failed to create GLFW window");
    }

    // Store this pointer for callbacks
    glfwSetWindowUserPointer(window_, this);

    // Set up resize callback
    glfwSetFramebufferSizeCallback(window_, framebufferResizeCallback);

    // Set up key callback
    glfwSetKeyCallback(window_, keyCallback);

    // Set up mouse callbacks
    glfwSetMouseButtonCallback(window_, mouseButtonCallback);
    glfwSetCursorPosCallback(window_, cursorPosCallback);
    glfwSetScrollCallback(window_, scrollCallback);

    std::cout << "[Window] Created " << width << "x" << height << " window\n";
}

Window::~Window() {
    if (window_) {
        glfwDestroyWindow(window_);
    }
    glfwTerminate();
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(window_);
}

void Window::pollEvents() {
    glfwPollEvents();
}

void Window::swapBuffers() {
    // Not used with WebGPU (swap chain handles presentation)
    // But kept for potential future use
}

void Window::setTitle(const std::string& title) {
    if (window_) {
        glfwSetWindowTitle(window_, title.c_str());
    }
}

void Window::setResizeCallback(void (*callback)(int, int, void*), void* userdata) {
    resizeCallback_ = callback;
    resizeUserdata_ = userdata;
}

void Window::framebufferResizeCallback(GLFWwindow* glfwWindow, int width, int height) {
    auto* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
    if (window) {
        window->width_ = width;
        window->height_ = height;
        window->resized_ = true;

        if (window->resizeCallback_) {
            window->resizeCallback_(width, height, window->resizeUserdata_);
        }
    }
}

void Window::keyCallback(GLFWwindow* glfwWindow, int key, int scancode, int action, int mods) {
    (void)scancode;
    (void)mods;
    auto* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
    if (!window || key < 0) return;

    if (action == GLFW_PRESS) {
        window->keysDown_.insert(key);
        window->keysPressed_.insert(key);
    } else if (action == GLFW_RELEASE) {
        window->keysDown_.erase(key);
        window->keysReleased_.insert(key);
    }
}

void Window::mouseButtonCallback(GLFWwindow* glfwWindow, int button, int action, int mods) {
    (void)mods;
    auto* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
    if (!window || button < 0) return;

    if (action == GLFW_PRESS) {
        window->mouseButtonsDown_.insert(button);
        window->mouseButtonsPressed_.insert(button);
    } else if (action == GLFW_RELEASE) {
        window->mouseButtonsDown_.erase(button);
        window->mouseButtonsReleased_.insert(button);
    }
}

void Window::cursorPosCallback(GLFWwindow* glfwWindow, double xpos, double ypos) {
    auto* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
    if (!window) return;

    window->mouseX_ = static_cast<float>(xpos);
    window->mouseY_ = static_cast<float>(ypos);
}

void Window::scrollCallback(GLFWwindow* glfwWindow, double xoffset, double yoffset) {
    auto* window = static_cast<Window*>(glfwGetWindowUserPointer(glfwWindow));
    if (!window) return;

    window->scrollDeltaX_ += static_cast<float>(xoffset);
    window->scrollDeltaY_ += static_cast<float>(yoffset);
}

bool Window::isKeyDown(int key) const {
    return keysDown_.count(key) > 0;
}

bool Window::wasKeyPressed(int key) const {
    return keysPressed_.count(key) > 0;
}

bool Window::wasKeyReleased(int key) const {
    return keysReleased_.count(key) > 0;
}

bool Window::isMouseDown(int button) const {
    return mouseButtonsDown_.count(button) > 0;
}

bool Window::wasMousePressed(int button) const {
    return mouseButtonsPressed_.count(button) > 0;
}

bool Window::wasMouseReleased(int button) const {
    return mouseButtonsReleased_.count(button) > 0;
}

void Window::clearInputState() {
    keysPressed_.clear();
    keysReleased_.clear();
    mouseButtonsPressed_.clear();
    mouseButtonsReleased_.clear();
    scrollDeltaX_ = 0.0f;
    scrollDeltaY_ = 0.0f;
}

// === Window Management Implementation ===

void Window::setFullscreen(bool fullscreen, int monitorIndex) {
    if (fullscreen == isFullscreen_) return;

    if (fullscreen) {
        // Save current windowed position/size
        glfwGetWindowPos(window_, &windowedX_, &windowedY_);
        glfwGetWindowSize(window_, &windowedWidth_, &windowedHeight_);

        // Get target monitor
        int monitorCount;
        GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
        GLFWmonitor* monitor = nullptr;

        if (monitorIndex >= 0 && monitorIndex < monitorCount) {
            monitor = monitors[monitorIndex];
        } else {
            // Use monitor the window is currently on
            monitor = glfwGetPrimaryMonitor();

            // Find which monitor the window center is on
            int wx, wy;
            glfwGetWindowPos(window_, &wx, &wy);
            int centerX = wx + width_ / 2;
            int centerY = wy + height_ / 2;

            for (int i = 0; i < monitorCount; i++) {
                int mx, my;
                glfwGetMonitorPos(monitors[i], &mx, &my);
                const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
                if (centerX >= mx && centerX < mx + mode->width &&
                    centerY >= my && centerY < my + mode->height) {
                    monitor = monitors[i];
                    break;
                }
            }
        }

        const GLFWvidmode* mode = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
        isFullscreen_ = true;
        std::cout << "[Window] Entered fullscreen (" << mode->width << "x" << mode->height << ")\n";
    } else {
        // Restore windowed mode
        glfwSetWindowMonitor(window_, nullptr, windowedX_, windowedY_, windowedWidth_, windowedHeight_, 0);
        isFullscreen_ = false;
        std::cout << "[Window] Exited fullscreen (" << windowedWidth_ << "x" << windowedHeight_ << ")\n";
    }
}

void Window::toggleFullscreen() {
    setFullscreen(!isFullscreen_);
}

void Window::setBorderless(bool borderless) {
    if (borderless == isBorderless_) return;

    glfwSetWindowAttrib(window_, GLFW_DECORATED, borderless ? GLFW_FALSE : GLFW_TRUE);
    isBorderless_ = borderless;
    std::cout << "[Window] Borderless: " << (borderless ? "ON" : "OFF") << "\n";
}

void Window::setCursorVisible(bool visible) {
    if (visible == cursorVisible_) return;

    glfwSetInputMode(window_, GLFW_CURSOR, visible ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
    cursorVisible_ = visible;
}

void Window::setAlwaysOnTop(bool alwaysOnTop) {
    if (alwaysOnTop == alwaysOnTop_) return;

    glfwSetWindowAttrib(window_, GLFW_FLOATING, alwaysOnTop ? GLFW_TRUE : GLFW_FALSE);
    alwaysOnTop_ = alwaysOnTop;
    std::cout << "[Window] Always on top: " << (alwaysOnTop ? "ON" : "OFF") << "\n";
}

void Window::setPosition(int x, int y) {
    glfwSetWindowPos(window_, x, y);
}

void Window::getPosition(int& x, int& y) const {
    glfwGetWindowPos(window_, &x, &y);
}

void Window::setSize(int width, int height) {
    glfwSetWindowSize(window_, width, height);
}

std::vector<MonitorInfo> Window::enumerateMonitors() {
    std::vector<MonitorInfo> result;

    // Ensure GLFW is initialized
    if (!glfwInit()) return result;

    int count;
    GLFWmonitor** monitors = glfwGetMonitors(&count);
    GLFWmonitor* primary = glfwGetPrimaryMonitor();

    for (int i = 0; i < count; i++) {
        MonitorInfo info;
        info.index = i;
        info.name = glfwGetMonitorName(monitors[i]);

        const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
        info.width = mode->width;
        info.height = mode->height;
        info.refreshRate = mode->refreshRate;

        glfwGetMonitorPos(monitors[i], &info.posX, &info.posY);
        info.isPrimary = (monitors[i] == primary);

        result.push_back(info);
    }

    return result;
}

void Window::printMonitors() {
    auto monitors = enumerateMonitors();

    std::cout << "\n[Window] Available monitors:\n";
    std::cout << std::string(70, '-') << "\n";

    for (const auto& m : monitors) {
        std::cout << "  [" << m.index << "] " << m.name;
        if (m.isPrimary) std::cout << " (primary)";
        std::cout << "\n";
        std::cout << "      " << m.width << "x" << m.height << " @ " << m.refreshRate << "Hz";
        std::cout << "  pos: (" << m.posX << ", " << m.posY << ")\n";
    }

    std::cout << std::string(70, '-') << "\n\n";
}

void Window::moveToMonitor(int monitorIndex) {
    int count;
    GLFWmonitor** monitors = glfwGetMonitors(&count);

    if (monitorIndex < 0 || monitorIndex >= count) {
        std::cerr << "[Window] Invalid monitor index: " << monitorIndex << "\n";
        return;
    }

    GLFWmonitor* monitor = monitors[monitorIndex];
    const GLFWvidmode* mode = glfwGetVideoMode(monitor);

    int mx, my;
    glfwGetMonitorPos(monitor, &mx, &my);

    if (isFullscreen_) {
        // Switch to fullscreen on new monitor
        glfwSetWindowMonitor(window_, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
    } else {
        // Center window on new monitor
        int newX = mx + (mode->width - width_) / 2;
        int newY = my + (mode->height - height_) / 2;
        glfwSetWindowPos(window_, newX, newY);
    }

    std::cout << "[Window] Moved to monitor " << monitorIndex << " (" << glfwGetMonitorName(monitor) << ")\n";
}

} // namespace vivid
