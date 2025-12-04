// Vivid Context Implementation

#include "vivid/context.h"
#include "vivid/texture_utils.h"
#include "vivid/shader_utils.h"

// Platform-specific defines for GLFW native access
#if defined(PLATFORM_WIN32)
    #define GLFW_EXPOSE_NATIVE_WIN32 1
#elif defined(PLATFORM_LINUX)
    #define GLFW_EXPOSE_NATIVE_X11 1
#elif defined(PLATFORM_MACOS)
    #define GLFW_EXPOSE_NATIVE_COCOA 1
#endif

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

// Undef Windows macros
#ifdef GetObject
    #undef GetObject
#endif
#ifdef CreateWindow
    #undef CreateWindow
#endif

// Diligent Engine includes - use short paths provided by linked targets
#include "GraphicsTypes.h"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "RefCntAutoPtr.hpp"

#if VULKAN_SUPPORTED
    #include "EngineFactoryVk.h"
#endif

#if GL_SUPPORTED
    #include "EngineFactoryOpenGL.h"
#endif

#include <iostream>

// macOS-specific helper
#if defined(PLATFORM_MACOS)
extern void* GetNSWindowView(GLFWwindow* wnd);
#endif

namespace vivid {

using namespace Diligent;

Context::Context() = default;

Context::~Context() {
    shutdown();
}

bool Context::init(int width, int height, const std::string& title) {
    // Initialize GLFW
    if (glfwInit() != GLFW_TRUE) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }

    // We use Vulkan, so no OpenGL context needed
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    // Create window
    window_ = glfwCreateWindow(width, height, title.c_str(), nullptr, nullptr);
    if (!window_) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }

    // Get actual framebuffer size (accounts for Retina/HiDPI scaling)
    int fbWidth, fbHeight;
    glfwGetFramebufferSize(window_, &fbWidth, &fbHeight);
    width_ = fbWidth;
    height_ = fbHeight;

    // Set user pointer for callbacks
    glfwSetWindowUserPointer(window_, this);

    // Set callbacks
    glfwSetFramebufferSizeCallback(window_, onFramebufferResize);
    glfwSetCursorPosCallback(window_, onMouseMove);
    glfwSetMouseButtonCallback(window_, onMouseButton);
    glfwSetScrollCallback(window_, onScroll);
    glfwSetKeyCallback(window_, onKey);

    // Set minimum window size
    glfwSetWindowSizeLimits(window_, 320, 240, GLFW_DONT_CARE, GLFW_DONT_CARE);

    // Initialize Diligent Engine
#if VULKAN_SUPPORTED
    // Load Vulkan engine factory
    IEngineFactoryVk* factoryVk = LoadAndGetEngineFactoryVk();
    if (!factoryVk) {
        std::cerr << "Failed to load Vulkan engine factory" << std::endl;
        return false;
    }

    EngineVkCreateInfo engineCI;
    // Enable validation in debug builds
#ifdef _DEBUG
    engineCI.EnableValidation = true;
#endif

    factoryVk->CreateDeviceAndContextsVk(engineCI, &device_, &immediateContext_);

    if (!device_ || !immediateContext_) {
        std::cerr << "Failed to create Vulkan device and context" << std::endl;
        return false;
    }

    // Create swap chain with framebuffer dimensions
    SwapChainDesc scDesc;
    scDesc.Width = width_;
    scDesc.Height = height_;
    scDesc.ColorBufferFormat = TEX_FORMAT_RGBA8_UNORM_SRGB;
    scDesc.DepthBufferFormat = TEX_FORMAT_D32_FLOAT;

    // Get native window handle
#if defined(PLATFORM_WIN32)
    Win32NativeWindow nativeWindow{glfwGetWin32Window(window_)};
#elif defined(PLATFORM_LINUX)
    LinuxNativeWindow nativeWindow;
    nativeWindow.WindowId = glfwGetX11Window(window_);
    nativeWindow.pDisplay = glfwGetX11Display();
#elif defined(PLATFORM_MACOS)
    MacOSNativeWindow nativeWindow;
    nativeWindow.pNSView = GetNSWindowView(window_);
#endif

    factoryVk->CreateSwapChainVk(device_, immediateContext_, scDesc, nativeWindow, &swapChain_);

    if (!swapChain_) {
        std::cerr << "Failed to create Vulkan swap chain" << std::endl;
        return false;
    }

#elif GL_SUPPORTED
    // Fallback to OpenGL if Vulkan not available
    glfwMakeContextCurrent(window_);

    IEngineFactoryOpenGL* factoryGL = LoadAndGetEngineFactoryOpenGL();
    if (!factoryGL) {
        std::cerr << "Failed to load OpenGL engine factory" << std::endl;
        return false;
    }

    EngineGLCreateInfo engineCI;
#if defined(PLATFORM_WIN32)
    engineCI.Window.hWnd = glfwGetWin32Window(window_);
#elif defined(PLATFORM_LINUX)
    engineCI.Window.WindowId = glfwGetX11Window(window_);
    engineCI.Window.pDisplay = glfwGetX11Display();
#elif defined(PLATFORM_MACOS)
    engineCI.Window.pNSView = GetNSWindowView(window_);
#endif

    SwapChainDesc scDesc;
    scDesc.Width = width;
    scDesc.Height = height;

    factoryGL->CreateDeviceAndSwapChainGL(engineCI, &device_, &immediateContext_, scDesc, &swapChain_);

    if (!device_ || !immediateContext_ || !swapChain_) {
        std::cerr << "Failed to create OpenGL device" << std::endl;
        return false;
    }
#else
    std::cerr << "No graphics backend available" << std::endl;
    return false;
#endif

    // Initialize utilities
    textureUtils_ = std::make_unique<TextureUtils>(device_, immediateContext_);
    shaderUtils_ = std::make_unique<ShaderUtils>(device_, immediateContext_);
    fullscreenQuad_ = std::make_unique<FullscreenQuad>(device_, immediateContext_);

    // Initialize timing
    lastFrameTime_ = glfwGetTime();

    std::cout << "Graphics initialized: " << device_->GetAdapterInfo().Description << std::endl;

    return true;
}

void Context::shutdown() {
    fullscreenQuad_.reset();
    shaderUtils_.reset();
    textureUtils_.reset();

    if (immediateContext_) {
        immediateContext_->Flush();
    }

    // Release Diligent objects (they're ref-counted)
    if (swapChain_) {
        swapChain_->Release();
        swapChain_ = nullptr;
    }
    if (immediateContext_) {
        immediateContext_->Release();
        immediateContext_ = nullptr;
    }
    if (device_) {
        device_->Release();
        device_ = nullptr;
    }

    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }

    glfwTerminate();
}

void Context::beginFrame() {
    // Update timing
    double currentTime = glfwGetTime();
    dt_ = static_cast<float>(currentTime - lastFrameTime_);
    lastFrameTime_ = currentTime;
    time_ = static_cast<float>(currentTime);
    frame_++;

    // Get current render target
    auto* rtv = swapChain_->GetCurrentBackBufferRTV();
    auto* dsv = swapChain_->GetDepthBufferDSV();

    // Set render targets
    immediateContext_->SetRenderTargets(1, &rtv, dsv, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Clear with a color based on time (for testing)
    float r = 0.2f + 0.1f * std::sin(time_ * 0.5f);
    float g = 0.2f + 0.1f * std::sin(time_ * 0.7f + 2.0f);
    float b = 0.3f + 0.1f * std::sin(time_ * 1.1f + 4.0f);
    float clearColor[] = {r, g, b, 1.0f};
    immediateContext_->ClearRenderTarget(rtv, clearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    immediateContext_->ClearDepthStencil(dsv, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

void Context::endFrame() {
    swapChain_->Present();
}

bool Context::shouldClose() const {
    return window_ && glfwWindowShouldClose(window_);
}

void Context::pollEvents() {
    // Reset per-frame input state BEFORE polling (so callbacks can set them)
    for (int i = 0; i < 8; i++) {
        mouseButtonsPressed_[i] = false;
    }
    for (int i = 0; i < 512; i++) {
        keysPressed_[i] = false;
    }
    scrollDelta_ = glm::vec2(0.0f, 0.0f);

    glfwPollEvents();

    // Check for ESC key to close
    if (window_ && glfwGetKey(window_, GLFW_KEY_ESCAPE) == GLFW_PRESS) {
        glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}

bool Context::wasMousePressed(int button) const {
    if (button >= 0 && button < 8) {
        return mouseButtonsPressed_[button];
    }
    return false;
}

bool Context::isMouseDown(int button) const {
    if (button >= 0 && button < 8) {
        return mouseButtons_[button];
    }
    return false;
}

Diligent::ITextureView* Context::currentRTV() const {
    return swapChain_ ? swapChain_->GetCurrentBackBufferRTV() : nullptr;
}

Diligent::ITextureView* Context::currentDSV() const {
    return swapChain_ ? swapChain_->GetDepthBufferDSV() : nullptr;
}

// GLFW Callbacks
void Context::onFramebufferResize(GLFWwindow* window, int width, int height) {
    auto* ctx = static_cast<Context*>(glfwGetWindowUserPointer(window));
    if (ctx && ctx->swapChain_ && width > 0 && height > 0) {
        ctx->width_ = width;
        ctx->height_ = height;
        ctx->swapChain_->Resize(static_cast<Uint32>(width), static_cast<Uint32>(height));
    }
}

void Context::onMouseMove(GLFWwindow* window, double x, double y) {
    auto* ctx = static_cast<Context*>(glfwGetWindowUserPointer(window));
    if (ctx) {
        // Handle content scale for retina displays
        float xscale = 1.0f, yscale = 1.0f;
        glfwGetWindowContentScale(window, &xscale, &yscale);
        ctx->mousePos_ = glm::vec2(
            static_cast<float>(x * xscale),
            static_cast<float>(y * yscale)
        );
    }
}

void Context::onMouseButton(GLFWwindow* window, int button, int action, int mods) {
    auto* ctx = static_cast<Context*>(glfwGetWindowUserPointer(window));
    if (ctx && button >= 0 && button < 8) {
        if (action == GLFW_PRESS) {
            ctx->mouseButtons_[button] = true;
            ctx->mouseButtonsPressed_[button] = true;
        } else if (action == GLFW_RELEASE) {
            ctx->mouseButtons_[button] = false;
        }
    }
}

void Context::onScroll(GLFWwindow* window, double xoffset, double yoffset) {
    auto* ctx = static_cast<Context*>(glfwGetWindowUserPointer(window));
    if (ctx) {
        ctx->scrollDelta_.x += static_cast<float>(xoffset);
        ctx->scrollDelta_.y += static_cast<float>(yoffset);
    }
}

void Context::onKey(GLFWwindow* window, int key, int scancode, int action, int mods) {
    auto* ctx = static_cast<Context*>(glfwGetWindowUserPointer(window));
    if (ctx && key >= 0 && key < 512) {
        if (action == GLFW_PRESS) {
            ctx->keys_[key] = true;
            ctx->keysPressed_[key] = true;
        } else if (action == GLFW_RELEASE) {
            ctx->keys_[key] = false;
        }
    }
}

bool Context::wasKeyPressed(int key) const {
    if (key >= 0 && key < 512) {
        return keysPressed_[key];
    }
    return false;
}

bool Context::isKeyDown(int key) const {
    if (key >= 0 && key < 512) {
        return keys_[key];
    }
    return false;
}

} // namespace vivid
