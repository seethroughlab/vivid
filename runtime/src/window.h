#pragma once
#include <string>

// Forward declare GLFW types
struct GLFWwindow;

namespace vivid {

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

    // Set resize callback
    void setResizeCallback(void (*callback)(int width, int height, void* userdata), void* userdata);

private:
    static void framebufferResizeCallback(GLFWwindow* window, int width, int height);

    GLFWwindow* window_ = nullptr;
    int width_ = 0;
    int height_ = 0;
    bool resized_ = false;

    void (*resizeCallback_)(int, int, void*) = nullptr;
    void* resizeUserdata_ = nullptr;
};

} // namespace vivid
