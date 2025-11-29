#include "window.h"
#include <GLFW/glfw3.h>
#include <stdexcept>
#include <iostream>

namespace vivid {

Window::Window(int width, int height, const std::string& title, bool fullscreen)
    : width_(width), height_(height) {

    // Initialize GLFW
    if (!glfwInit()) {
        throw std::runtime_error("Failed to initialize GLFW");
    }

    // Set hints for WebGPU (no OpenGL context)
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

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

bool Window::isKeyDown(int key) const {
    return keysDown_.count(key) > 0;
}

bool Window::wasKeyPressed(int key) const {
    return keysPressed_.count(key) > 0;
}

bool Window::wasKeyReleased(int key) const {
    return keysReleased_.count(key) > 0;
}

void Window::clearInputState() {
    keysPressed_.clear();
    keysReleased_.clear();
}

} // namespace vivid
