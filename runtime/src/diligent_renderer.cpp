#include "vivid/diligent_renderer.h"

// GLFW
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>

#if defined(__APPLE__)
    #define GLFW_EXPOSE_NATIVE_COCOA
#elif defined(_WIN32)
    #define GLFW_EXPOSE_NATIVE_WIN32
#else
    #define GLFW_EXPOSE_NATIVE_X11
#endif
#include <GLFW/glfw3native.h>

// Diligent Engine
#include "EngineFactoryVk.h"
#include "RenderDevice.h"
#include "DeviceContext.h"
#include "SwapChain.h"
#include "RefCntAutoPtr.hpp"

#include <iostream>
#include <stdexcept>

#if defined(__APPLE__)
// Helper function defined in macos_helpers.mm
extern "C" void* getContentViewFromWindow(void* nsWindow);
#endif

namespace vivid {

using namespace Diligent;

// Internal state holding Diligent objects
struct DiligentRenderer::DiligentState {
    RefCntAutoPtr<IRenderDevice> device;
    RefCntAutoPtr<IDeviceContext> context;
    RefCntAutoPtr<ISwapChain> swapChain;
    RefCntAutoPtr<IEngineFactoryVk> engineFactory;
};

DiligentRenderer::DiligentRenderer()
    : m_state(std::make_unique<DiligentState>()) {
}

DiligentRenderer::~DiligentRenderer() {
    shutdown();
}

bool DiligentRenderer::initialize(const RendererConfig& config) {
    if (!initGLFW(config)) {
        return false;
    }

    if (!initDiligent()) {
        glfwDestroyWindow(m_window);
        glfwTerminate();
        return false;
    }

    m_lastFrameTime = glfwGetTime();
    return true;
}

bool DiligentRenderer::initGLFW(const RendererConfig& config) {
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }

    // No OpenGL context needed - we're using Vulkan
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    // Create window
    GLFWmonitor* monitor = config.fullscreen ? glfwGetPrimaryMonitor() : nullptr;
    m_window = glfwCreateWindow(
        config.windowWidth,
        config.windowHeight,
        config.windowTitle.c_str(),
        monitor,
        nullptr
    );

    if (!m_window) {
        std::cerr << "Failed to create GLFW window" << std::endl;
        glfwTerminate();
        return false;
    }

    // Store this pointer for callbacks
    glfwSetWindowUserPointer(m_window, this);
    glfwSetFramebufferSizeCallback(m_window, framebufferSizeCallback);

    // Get actual framebuffer size (may differ on HiDPI displays)
    glfwGetFramebufferSize(m_window, &m_windowWidth, &m_windowHeight);

    return true;
}

bool DiligentRenderer::initDiligent() {
    // Get the Vulkan engine factory
    auto* factoryVk = GetEngineFactoryVk();
    m_state->engineFactory = factoryVk;

    // Engine creation info
    EngineVkCreateInfo engineCI;
    engineCI.EnableValidation = true;  // Enable validation in debug builds

#ifdef NDEBUG
    engineCI.EnableValidation = false;
#endif

    // Create render device and context
    factoryVk->CreateDeviceAndContextsVk(engineCI, &m_state->device, &m_state->context);

    if (!m_state->device || !m_state->context) {
        std::cerr << "Failed to create Vulkan device and context" << std::endl;
        return false;
    }

    // Create swap chain
    SwapChainDesc swapChainDesc;
    swapChainDesc.Width = m_windowWidth;
    swapChainDesc.Height = m_windowHeight;
    swapChainDesc.ColorBufferFormat = TEX_FORMAT_RGBA8_UNORM_SRGB;
    swapChainDesc.DepthBufferFormat = TEX_FORMAT_D32_FLOAT;

    // Get native window handle
#if defined(__APPLE__)
    // macOS: Get the Cocoa window's content view
    void* cocoaWindow = glfwGetCocoaWindow(m_window);
    void* contentView = getContentViewFromWindow(cocoaWindow);

    if (!contentView) {
        std::cerr << "Failed to get NSView from window" << std::endl;
        return false;
    }

    // Create swap chain with the NSView
    factoryVk->CreateSwapChainVk(
        m_state->device,
        m_state->context,
        swapChainDesc,
        NativeWindow{contentView},
        &m_state->swapChain
    );
#elif defined(_WIN32)
    HWND hwnd = glfwGetWin32Window(m_window);
    factoryVk->CreateSwapChainVk(
        m_state->device,
        m_state->context,
        swapChainDesc,
        Win32NativeWindow{hwnd},
        &m_state->swapChain
    );
#else
    // Linux X11
    Window x11Window = glfwGetX11Window(m_window);
    Display* x11Display = glfwGetX11Display();
    factoryVk->CreateSwapChainVk(
        m_state->device,
        m_state->context,
        swapChainDesc,
        LinuxNativeWindow{x11Window, x11Display},
        &m_state->swapChain
    );
#endif

    if (!m_state->swapChain) {
        std::cerr << "Failed to create swap chain" << std::endl;
        return false;
    }

    std::cout << "Vivid renderer initialized successfully" << std::endl;
    std::cout << "  Backend: Vulkan" << std::endl;
    std::cout << "  Window size: " << m_windowWidth << "x" << m_windowHeight << std::endl;

    return true;
}

void DiligentRenderer::shutdown() {
    if (m_state) {
        // Release Diligent objects in order
        m_state->swapChain.Release();
        m_state->context.Release();
        m_state->device.Release();
        m_state->engineFactory.Release();
    }

    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }

    glfwTerminate();
}

void DiligentRenderer::beginFrame() {
    // Update timing
    double currentTime = glfwGetTime();
    m_deltaTime = currentTime - m_lastFrameTime;
    m_lastFrameTime = currentTime;
    m_frameCount++;

    // Set render targets to swap chain back buffer
    auto* pRTV = m_state->swapChain->GetCurrentBackBufferRTV();
    auto* pDSV = m_state->swapChain->GetDepthBufferDSV();
    m_state->context->SetRenderTargets(1, &pRTV, pDSV, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);

    // Set viewport
    Viewport vp;
    vp.TopLeftX = 0;
    vp.TopLeftY = 0;
    vp.Width = static_cast<float>(m_windowWidth);
    vp.Height = static_cast<float>(m_windowHeight);
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    m_state->context->SetViewports(1, &vp, m_windowWidth, m_windowHeight);
}

void DiligentRenderer::endFrame() {
    // Flush all commands
    m_state->context->Flush();
}

void DiligentRenderer::present() {
    m_state->swapChain->Present();
}

void DiligentRenderer::clear(float r, float g, float b, float a) {
    auto* pRTV = m_state->swapChain->GetCurrentBackBufferRTV();
    auto* pDSV = m_state->swapChain->GetDepthBufferDSV();

    float clearColor[] = {r, g, b, a};
    m_state->context->ClearRenderTarget(pRTV, clearColor, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
    m_state->context->ClearDepthStencil(pDSV, CLEAR_DEPTH_FLAG, 1.0f, 0, RESOURCE_STATE_TRANSITION_MODE_TRANSITION);
}

bool DiligentRenderer::shouldClose() const {
    return glfwWindowShouldClose(m_window);
}

void DiligentRenderer::pollEvents() {
    glfwPollEvents();
}

float DiligentRenderer::getAspectRatio() const {
    if (m_windowHeight == 0) return 1.0f;
    return static_cast<float>(m_windowWidth) / static_cast<float>(m_windowHeight);
}

void DiligentRenderer::setResizeCallback(std::function<void(int, int)> callback) {
    m_resizeCallback = std::move(callback);
}

IRenderDevice* DiligentRenderer::getDevice() const {
    return m_state->device.RawPtr();
}

IDeviceContext* DiligentRenderer::getContext() const {
    return m_state->context.RawPtr();
}

ISwapChain* DiligentRenderer::getSwapChain() const {
    return m_state->swapChain.RawPtr();
}

ITextureView* DiligentRenderer::getCurrentRTV() const {
    return m_state->swapChain->GetCurrentBackBufferRTV();
}

ITextureView* DiligentRenderer::getDepthDSV() const {
    return m_state->swapChain->GetDepthBufferDSV();
}

double DiligentRenderer::getTime() const {
    return glfwGetTime();
}

void DiligentRenderer::handleResize(int width, int height) {
    if (width == 0 || height == 0) {
        return;  // Window minimized
    }

    m_windowWidth = width;
    m_windowHeight = height;

    // Resize the swap chain
    if (m_state->swapChain) {
        m_state->swapChain->Resize(width, height);
    }

    // Call user callback if set
    if (m_resizeCallback) {
        m_resizeCallback(width, height);
    }
}

void DiligentRenderer::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    auto* renderer = static_cast<DiligentRenderer*>(glfwGetWindowUserPointer(window));
    if (renderer) {
        renderer->handleResize(width, height);
    }
}

} // namespace vivid
