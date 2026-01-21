// Vivid - Production Runtime Implementation
// Minimal runtime loop for production bundles
// NO HotReload, NO MCP, NO Visualizer - just the core rendering loop

// Prevent Windows.h from defining min/max macros
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#endif

#include <vivid/runtime.h>
#include <vivid/context.h>
#include <vivid/display.h>
#include <vivid/chain.h>
#include <vivid/asset_loader.h>

#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>  // wgpu-native extensions
#include <glfw3webgpu.h>
#include <GLFW/glfw3.h>

#include <iostream>
#include <cstring>
#include <algorithm>

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#ifdef _WIN32
#include <windows.h>
#endif

#ifdef __linux__
#include <unistd.h>
#include <linux/limits.h>
#endif

namespace fs = std::filesystem;

namespace vivid {

// -----------------------------------------------------------------------------
// WebGPU Initialization Helpers
// -----------------------------------------------------------------------------

inline WGPUStringView toStringView(const char* str) {
    WGPUStringView sv;
    sv.data = str;
    sv.length = WGPU_STRLEN;
    return sv;
}

struct AdapterUserData {
    WGPUAdapter adapter = nullptr;
    bool done = false;
};

static void onAdapterRequestEnded(WGPURequestAdapterStatus status, WGPUAdapter adapter,
                                  WGPUStringView message, void* userdata1, void* userdata2) {
    auto* data = static_cast<AdapterUserData*>(userdata1);
    if (status == WGPURequestAdapterStatus_Success) {
        data->adapter = adapter;
    } else {
        std::cerr << "Failed to request adapter: "
                  << (message.data ? std::string(message.data, message.length == WGPU_STRLEN ? strlen(message.data) : message.length) : "unknown error")
                  << std::endl;
    }
    data->done = true;
}

struct DeviceUserData {
    WGPUDevice device = nullptr;
    bool done = false;
};

static void onDeviceRequestEnded(WGPURequestDeviceStatus status, WGPUDevice device,
                                 WGPUStringView message, void* userdata1, void* userdata2) {
    auto* data = static_cast<DeviceUserData*>(userdata1);
    if (status == WGPURequestDeviceStatus_Success) {
        data->device = device;
    } else {
        std::cerr << "Failed to request device: "
                  << (message.data ? std::string(message.data, message.length == WGPU_STRLEN ? strlen(message.data) : message.length) : "unknown error")
                  << std::endl;
    }
    data->done = true;
}

static void onDeviceLost(WGPUDevice const* device, WGPUDeviceLostReason reason,
                         WGPUStringView message, void* userdata1, void* userdata2) {
    std::cerr << "WebGPU Device Lost: "
              << (message.data ? std::string(message.data, message.length == WGPU_STRLEN ? strlen(message.data) : message.length) : "unknown")
              << std::endl;
}

static void onDeviceError(WGPUDevice const* device, WGPUErrorType type,
                          WGPUStringView message, void* userdata1, void* userdata2) {
    std::cerr << "WebGPU Error: "
              << (message.data ? std::string(message.data, message.length == WGPU_STRLEN ? strlen(message.data) : message.length) : "unknown")
              << std::endl;
}

// -----------------------------------------------------------------------------
// Runtime Implementation
// -----------------------------------------------------------------------------

Runtime::Runtime() = default;

Runtime::~Runtime() {
    shutdown();
}

int Runtime::init(const RuntimeConfig& config) {
    if (m_initialized) {
        return 0;
    }

    m_config = config;

    // Initialize window first
    if (!initWindow(config)) {
        return 1;
    }

    // Then WebGPU
    if (!initWebGPU()) {
        glfwDestroyWindow(m_window);
        glfwTerminate();
        return 1;
    }

    // Create context
    m_ctx = new Context(m_window, m_device, m_queue);

    // Set render resolution
    if (config.renderWidth > 0 && config.renderHeight > 0) {
        m_ctx->setRenderResolution(config.renderWidth, config.renderHeight);
    } else {
        m_ctx->setRenderResolution(config.windowWidth, config.windowHeight);
    }

    // Start in fullscreen if requested
    if (config.fullscreen) {
        m_ctx->fullscreen(true);
    }

    // Set display mode
    m_ctx->displayMode(config.displayMode);

    // Set up scroll callback
    glfwSetWindowUserPointer(m_window, m_ctx);
    glfwSetScrollCallback(m_window, [](GLFWwindow* w, double xoffset, double yoffset) {
        Context* c = static_cast<Context*>(glfwGetWindowUserPointer(w));
        if (c) c->addScroll(static_cast<float>(xoffset), static_cast<float>(yoffset));
    });

    // Create display
    m_display = new Display(m_device, m_queue, m_surfaceFormat);
    if (!m_display->isValid()) {
        std::cerr << "Warning: Display initialization failed (shaders may be missing)" << std::endl;
    }
    m_display->setDisplayMode(config.displayMode);

    // Set project directory for asset loading
    // In production bundles, assets are in the project directory relative to executable
    if (!config.assetsPath.empty()) {
        AssetLoader::instance().setProjectDir(config.assetsPath);
    }

    m_initialized = true;
    std::cout << "Vivid Runtime initialized" << std::endl;

    return 0;
}

bool Runtime::initWindow(const RuntimeConfig& config) {
    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return false;
    }

    // No OpenGL context - we're using WebGPU
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, config.resizable ? GLFW_TRUE : GLFW_FALSE);

    // Create window
    std::string windowTitle = config.title.empty() ? "Vivid" : config.title;
    m_window = glfwCreateWindow(config.windowWidth, config.windowHeight,
                                 windowTitle.c_str(), nullptr, nullptr);
    if (!m_window) {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return false;
    }

    // Store window dimensions
    m_width = config.windowWidth;
    m_height = config.windowHeight;
    m_windowedWidth = config.windowWidth;
    m_windowedHeight = config.windowHeight;

    return true;
}

bool Runtime::initWebGPU() {
    // Create WebGPU instance
    WGPUInstanceDescriptor instanceDesc = {};
    m_instance = wgpuCreateInstance(&instanceDesc);
    if (!m_instance) {
        std::cerr << "Failed to create WebGPU instance" << std::endl;
        return false;
    }

    // Create surface from GLFW window
    m_surface = glfwCreateWindowWGPUSurface(m_instance, m_window);
    if (!m_surface) {
        std::cerr << "Failed to create surface" << std::endl;
        wgpuInstanceRelease(m_instance);
        return false;
    }

    // Request adapter
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = m_surface;
    adapterOpts.powerPreference = WGPUPowerPreference_HighPerformance;

    AdapterUserData adapterData;
    WGPURequestAdapterCallbackInfo adapterCallback = {};
    adapterCallback.mode = WGPUCallbackMode_AllowSpontaneous;
    adapterCallback.callback = onAdapterRequestEnded;
    adapterCallback.userdata1 = &adapterData;

    wgpuInstanceRequestAdapter(m_instance, &adapterOpts, adapterCallback);
    while (!adapterData.done) {}

    if (!adapterData.adapter) {
        std::cerr << "Failed to get adapter" << std::endl;
        wgpuSurfaceRelease(m_surface);
        wgpuInstanceRelease(m_instance);
        return false;
    }

    m_adapter = adapterData.adapter;

    // Request device
    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.label = toStringView("Vivid Production Device");

    // Request BC texture compression for HAP video codec support
    WGPUFeatureName requiredFeatures[] = {
        WGPUFeatureName_TextureCompressionBC
    };
    deviceDesc.requiredFeatures = requiredFeatures;
    deviceDesc.requiredFeatureCount = 1;

    deviceDesc.deviceLostCallbackInfo.callback = onDeviceLost;
    deviceDesc.uncapturedErrorCallbackInfo.callback = onDeviceError;

    DeviceUserData deviceData;
    WGPURequestDeviceCallbackInfo deviceCallback = {};
    deviceCallback.mode = WGPUCallbackMode_AllowSpontaneous;
    deviceCallback.callback = onDeviceRequestEnded;
    deviceCallback.userdata1 = &deviceData;

    wgpuAdapterRequestDevice(m_adapter, &deviceDesc, deviceCallback);
    while (!deviceData.done) {}

    if (!deviceData.device) {
        std::cerr << "Failed to get device" << std::endl;
        wgpuAdapterRelease(m_adapter);
        wgpuSurfaceRelease(m_surface);
        wgpuInstanceRelease(m_instance);
        return false;
    }

    m_device = deviceData.device;
    m_queue = wgpuDeviceGetQueue(m_device);

    // Configure surface
    configureSurface();

    return true;
}

void Runtime::configureSurface() {
    glfwGetFramebufferSize(m_window, &m_width, &m_height);

    // Query surface capabilities
    WGPUSurfaceCapabilities capabilities = {};
    wgpuSurfaceGetCapabilities(m_surface, m_adapter, &capabilities);

    m_surfaceFormat = WGPUTextureFormat_BGRA8Unorm;
    if (capabilities.formatCount > 0) {
        m_surfaceFormat = capabilities.formats[0];
    }

    WGPUPresentMode presentMode = WGPUPresentMode_Fifo;  // vsync
    wgpuSurfaceCapabilitiesFreeMembers(capabilities);

    // Configure surface
    WGPUSurfaceConfigurationExtras configExtras = {};
    configExtras.chain.sType = static_cast<WGPUSType>(WGPUSType_SurfaceConfigurationExtras);
    configExtras.desiredMaximumFrameLatency = 2;

    WGPUSurfaceConfiguration config = {};
    config.nextInChain = &configExtras.chain;
    config.device = m_device;
    config.format = m_surfaceFormat;
    config.width = static_cast<uint32_t>(m_width);
    config.height = static_cast<uint32_t>(m_height);
    config.presentMode = presentMode;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.usage = WGPUTextureUsage_RenderAttachment;
    wgpuSurfaceConfigure(m_surface, &config);
}

int Runtime::run(SetupFn setup, UpdateFn update) {
    if (!m_initialized) {
        std::cerr << "Runtime not initialized" << std::endl;
        return 1;
    }

    // Run main loop
    while (!glfwWindowShouldClose(m_window)) {
        if (!mainLoopIteration(setup, update)) {
            break;
        }
    }

    return 0;
}

bool Runtime::mainLoopIteration(SetupFn setup, UpdateFn update) {
    glfwPollEvents();

    // Begin frame
    m_ctx->beginFrame();

    // Handle window resize
    int newWidth, newHeight;
    glfwGetFramebufferSize(m_window, &newWidth, &newHeight);
    if (newWidth != m_width || newHeight != m_height) {
        m_width = newWidth;
        m_height = newHeight;
        if (m_width > 0 && m_height > 0) {
            configureSurface();
        }
    }

    // Handle fullscreen change
    if (m_ctx->consumeFullscreenChange()) {
        if (m_ctx->fullscreen() && !m_isFullscreen) {
            glfwGetWindowPos(m_window, &m_windowedX, &m_windowedY);
            glfwGetWindowSize(m_window, &m_windowedWidth, &m_windowedHeight);

            int monitorCount = 0;
            GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
            int targetIdx = std::min(m_ctx->targetMonitor(), monitorCount - 1);
            targetIdx = std::max(0, targetIdx);
            GLFWmonitor* monitor = monitors[targetIdx];
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);

            glfwSetWindowMonitor(m_window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
            m_isFullscreen = true;
        } else if (!m_ctx->fullscreen() && m_isFullscreen) {
            glfwSetWindowMonitor(m_window, nullptr, m_windowedX, m_windowedY, m_windowedWidth, m_windowedHeight, 0);
            m_isFullscreen = false;
        }
    }

    // Handle display mode change
    if (m_ctx->consumeDisplayModeChange()) {
        m_display->setDisplayMode(m_ctx->displayMode());
    }

    // Skip frame if minimized
    if (m_width == 0 || m_height == 0) {
        m_ctx->endFrame();
        return true;
    }

    // Get current surface texture
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(m_surface, &surfaceTexture);
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        m_ctx->endFrame();
        return true;
    }

    // Create texture view
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = m_surfaceFormat;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;
    WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, &viewDesc);

    // Call setup once
    if (!m_chainInitialized) {
        setup(*m_ctx);
        m_ctx->chain().init(*m_ctx);
        m_chainInitialized = true;
    }

    // Call update
    update(*m_ctx);

    // Process chain
    m_ctx->chain().process(*m_ctx);

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(m_device, &encoderDesc);

    // Render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = view;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};

    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    // Update display with current screen size
    if (m_display->isValid()) {
        m_display->setScreenSize(m_width, m_height);
    }

    // Blit output texture
    if (m_ctx->outputTexture() && m_display->isValid()) {
        m_display->setTextureSize(m_ctx->renderWidth(), m_ctx->renderHeight());
        m_display->blit(pass, m_ctx->outputTexture());
    }

    // Render error message if present
    if (m_ctx->hasError() && m_display->isValid()) {
        m_display->renderText(pass, m_ctx->errorMessage(), 20.0f, 20.0f, 2.0f);
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    // Submit
    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    wgpuQueueSubmit(m_queue, 1, &cmdBuffer);

    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    // Present
    wgpuSurfacePresent(m_surface);

    // Poll device
    wgpuDevicePoll(m_device, false, nullptr);

    // Release texture resources
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(surfaceTexture.texture);

    // End frame
    m_ctx->endFrame();

    return true;
}

void Runtime::shutdown() {
    if (!m_initialized) {
        return;
    }

    if (m_display) {
        m_display->shutdown();
        delete m_display;
        m_display = nullptr;
    }

    if (m_ctx) {
        delete m_ctx;
        m_ctx = nullptr;
    }

    if (m_queue) {
        wgpuQueueRelease(m_queue);
        m_queue = nullptr;
    }

    if (m_device) {
        wgpuDeviceRelease(m_device);
        m_device = nullptr;
    }

    if (m_surface) {
        wgpuSurfaceRelease(m_surface);
        m_surface = nullptr;
    }

    if (m_adapter) {
        wgpuAdapterRelease(m_adapter);
        m_adapter = nullptr;
    }

    if (m_instance) {
        wgpuInstanceRelease(m_instance);
        m_instance = nullptr;
    }

    if (m_window) {
        glfwDestroyWindow(m_window);
        m_window = nullptr;
    }

    glfwTerminate();

    m_initialized = false;
}

} // namespace vivid
