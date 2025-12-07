// Vivid V3 - Core Runtime
// Minimal core: window, timing, input, hot-reload, display

#include <vivid/vivid.h>
#include <vivid/context.h>
#include <vivid/display.h>
#include <vivid/hot_reload.h>
#include <webgpu/webgpu.h>
#include <glfw3webgpu.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <iostream>
#include <string>
#include <filesystem>

using namespace vivid;

namespace fs = std::filesystem;

// -----------------------------------------------------------------------------
// WebGPU Initialization Helpers
// -----------------------------------------------------------------------------

// Helper to create WGPUStringView from C string
inline WGPUStringView toStringView(const char* str) {
    WGPUStringView sv;
    sv.data = str;
    sv.length = WGPU_STRLEN;  // Null-terminated
    return sv;
}

struct AdapterUserData {
    WGPUAdapter adapter = nullptr;
    bool done = false;
};

void onAdapterRequestEnded(WGPURequestAdapterStatus status, WGPUAdapter adapter,
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

void onDeviceRequestEnded(WGPURequestDeviceStatus status, WGPUDevice device,
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

void onDeviceLost(WGPUDevice const* device, WGPUDeviceLostReason reason,
                  WGPUStringView message, void* userdata1, void* userdata2) {
    std::cerr << "WebGPU Device Lost: "
              << (message.data ? std::string(message.data, message.length == WGPU_STRLEN ? strlen(message.data) : message.length) : "unknown")
              << std::endl;
}

void onDeviceError(WGPUDevice const* device, WGPUErrorType type,
                   WGPUStringView message, void* userdata1, void* userdata2) {
    std::cerr << "WebGPU Error: "
              << (message.data ? std::string(message.data, message.length == WGPU_STRLEN ? strlen(message.data) : message.length) : "unknown")
              << std::endl;
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

int main(int argc, char** argv) {
    std::cout << "Vivid V3 - Starting..." << std::endl;

    // Parse arguments
    fs::path projectPath;
    if (argc >= 2) {
        projectPath = argv[1];
    }

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return 1;
    }

    // No OpenGL context - we're using WebGPU
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    // Create window
    GLFWwindow* window = glfwCreateWindow(1280, 720, "Vivid", nullptr, nullptr);
    if (!window) {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return 1;
    }

    // Create WebGPU instance
    WGPUInstanceDescriptor instanceDesc = {};
    WGPUInstance instance = wgpuCreateInstance(&instanceDesc);
    if (!instance) {
        std::cerr << "Failed to create WebGPU instance" << std::endl;
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Create surface from GLFW window
    WGPUSurface surface = glfwCreateWindowWGPUSurface(instance, window);
    if (!surface) {
        std::cerr << "Failed to create surface" << std::endl;
        wgpuInstanceRelease(instance);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    // Request adapter
    std::cout << "Requesting adapter..." << std::endl;
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = surface;
    adapterOpts.powerPreference = WGPUPowerPreference_HighPerformance;

    AdapterUserData adapterData;
    WGPURequestAdapterCallbackInfo adapterCallback = {};
    adapterCallback.mode = WGPUCallbackMode_AllowSpontaneous;
    adapterCallback.callback = onAdapterRequestEnded;
    adapterCallback.userdata1 = &adapterData;

    wgpuInstanceRequestAdapter(instance, &adapterOpts, adapterCallback);

    // Wait for adapter (wgpu-native processes synchronously with AllowSpontaneous)
    while (!adapterData.done) {
        // Spin - in practice wgpu-native returns immediately
    }

    if (!adapterData.adapter) {
        std::cerr << "Failed to get adapter" << std::endl;
        wgpuSurfaceRelease(surface);
        wgpuInstanceRelease(instance);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    WGPUAdapter adapter = adapterData.adapter;

    // Print adapter info
    WGPUAdapterInfo info = {};
    wgpuAdapterGetInfo(adapter, &info);
    std::cout << "Adapter: ";
    if (info.device.data && info.device.length > 0) {
        size_t len = info.device.length == WGPU_STRLEN ? strlen(info.device.data) : info.device.length;
        std::cout << std::string(info.device.data, len);
    } else {
        std::cout << "unknown";
    }
    std::cout << std::endl;

    std::cout << "Backend: ";
    switch (info.backendType) {
        case WGPUBackendType_Metal: std::cout << "Metal"; break;
        case WGPUBackendType_Vulkan: std::cout << "Vulkan"; break;
        case WGPUBackendType_D3D12: std::cout << "D3D12"; break;
        case WGPUBackendType_D3D11: std::cout << "D3D11"; break;
        default: std::cout << "Other"; break;
    }
    std::cout << std::endl;

    // Request device
    std::cout << "Requesting device..." << std::endl;
    WGPUDeviceDescriptor deviceDesc = {};
    deviceDesc.label = toStringView("Vivid Device");

    // Use default limits (don't require specific limits)

    // Set error callbacks
    deviceDesc.deviceLostCallbackInfo.callback = onDeviceLost;
    deviceDesc.uncapturedErrorCallbackInfo.callback = onDeviceError;

    DeviceUserData deviceData;
    WGPURequestDeviceCallbackInfo deviceCallback = {};
    deviceCallback.mode = WGPUCallbackMode_AllowSpontaneous;
    deviceCallback.callback = onDeviceRequestEnded;
    deviceCallback.userdata1 = &deviceData;

    wgpuAdapterRequestDevice(adapter, &deviceDesc, deviceCallback);

    while (!deviceData.done) {
        // Spin
    }

    if (!deviceData.device) {
        std::cerr << "Failed to get device" << std::endl;
        wgpuAdapterRelease(adapter);
        wgpuSurfaceRelease(surface);
        wgpuInstanceRelease(instance);
        glfwDestroyWindow(window);
        glfwTerminate();
        return 1;
    }

    WGPUDevice device = deviceData.device;

    // Get queue
    WGPUQueue queue = wgpuDeviceGetQueue(device);

    // Configure surface
    int width, height;
    glfwGetFramebufferSize(window, &width, &height);

    // Query surface capabilities to get preferred format
    WGPUSurfaceCapabilities capabilities = {};
    wgpuSurfaceGetCapabilities(surface, adapter, &capabilities);

    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_BGRA8Unorm;  // Default
    if (capabilities.formatCount > 0) {
        surfaceFormat = capabilities.formats[0];
        std::cout << "Using surface format: " << surfaceFormat << std::endl;
    }

    WGPUPresentMode presentMode = WGPUPresentMode_Fifo;  // Default
    if (capabilities.presentModeCount > 0) {
        presentMode = capabilities.presentModes[0];
        std::cout << "Using present mode: " << presentMode << std::endl;
    }

    wgpuSurfaceCapabilitiesFreeMembers(capabilities);

    WGPUSurfaceConfiguration config = {};
    config.device = device;
    config.format = surfaceFormat;
    config.width = static_cast<uint32_t>(width);
    config.height = static_cast<uint32_t>(height);
    config.presentMode = presentMode;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.usage = WGPUTextureUsage_RenderAttachment;
    wgpuSurfaceConfigure(surface, &config);

    std::cout << "WebGPU initialized successfully!" << std::endl;
    std::cout << "Window size: " << width << "x" << height << std::endl;

    // Create context
    Context ctx(window, device, queue);

    // Create display
    Display display(device, queue, surfaceFormat);
    if (!display.isValid()) {
        std::cerr << "Warning: Display initialization failed (shaders may be missing)" << std::endl;
    }

    // Create hot-reload system
    HotReload hotReload;

    // Set up the chain if path was provided
    if (!projectPath.empty()) {
        // Look for chain.cpp in the project path
        fs::path chainPath;
        if (fs::is_directory(projectPath)) {
            chainPath = projectPath / "chain.cpp";
        } else if (fs::is_regular_file(projectPath)) {
            chainPath = projectPath;
        }

        if (fs::exists(chainPath)) {
            std::cout << "Loading chain: " << chainPath << std::endl;
            hotReload.setSourceFile(chainPath);
        } else {
            ctx.setError("Chain file not found: " + chainPath.string());
        }
    } else {
        ctx.setError("No chain specified. Usage: vivid <path/to/chain.cpp>");
    }

    // Track if setup has been called
    bool chainNeedsSetup = true;

    // Main loop
    double lastFpsTime = glfwGetTime();
    int frameCount = 0;

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Begin frame (updates time, input, etc.)
        ctx.beginFrame();

        // Handle window resize
        if (ctx.width() != width || ctx.height() != height) {
            width = ctx.width();
            height = ctx.height();
            if (width > 0 && height > 0) {
                config.width = static_cast<uint32_t>(width);
                config.height = static_cast<uint32_t>(height);
                wgpuSurfaceConfigure(surface, &config);
            }
        }

        // Skip frame if minimized
        if (width == 0 || height == 0) {
            ctx.endFrame();
            continue;
        }

        // Get current texture
        WGPUSurfaceTexture surfaceTexture;
        wgpuSurfaceGetCurrentTexture(surface, &surfaceTexture);
        if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
            surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
            ctx.endFrame();
            continue;
        }

        // Create view with explicit format matching the surface texture
        WGPUTextureViewDescriptor viewDesc = {};
        viewDesc.format = surfaceFormat;
        viewDesc.dimension = WGPUTextureViewDimension_2D;
        viewDesc.baseMipLevel = 0;
        viewDesc.mipLevelCount = 1;
        viewDesc.baseArrayLayer = 0;
        viewDesc.arrayLayerCount = 1;
        viewDesc.aspect = WGPUTextureAspect_All;
        WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, &viewDesc);

        // Check for hot-reload
        if (hotReload.update()) {
            chainNeedsSetup = true;
        }

        // Update error state from hot-reload
        if (hotReload.hasError()) {
            ctx.setError(hotReload.getError());
        } else if (hotReload.isLoaded()) {
            ctx.clearError();
        }

        // Call chain functions if loaded
        if (hotReload.isLoaded()) {
            // Call setup if needed (after reload)
            if (chainNeedsSetup) {
                hotReload.getSetupFn()(ctx);
                chainNeedsSetup = false;
            }

            // Call update every frame
            hotReload.getUpdateFn()(ctx);
        }

        // Create command encoder
        WGPUCommandEncoderDescriptor encoderDesc = {};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

        // Render pass - clear to black
        WGPURenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = view;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = {0.0, 0.0, 0.0, 1.0};

        WGPURenderPassDescriptor renderPassDesc = {};
        renderPassDesc.colorAttachmentCount = 1;
        renderPassDesc.colorAttachments = &colorAttachment;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

        // Update display with current screen size
        display.setScreenSize(width, height);

        // If chain set an output texture, blit it to the screen
        if (ctx.outputTexture() && display.isValid()) {
            display.blit(pass, ctx.outputTexture());
        }

        // Render error message if present
        if (ctx.hasError() && display.isValid()) {
            display.renderText(pass, ctx.errorMessage(), 20.0f, 20.0f, 2.0f);
        }

        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        // Submit
        WGPUCommandBufferDescriptor cmdBufferDesc = {};
        WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
        wgpuQueueSubmit(queue, 1, &cmdBuffer);

        // Release command resources
        wgpuCommandBufferRelease(cmdBuffer);
        wgpuCommandEncoderRelease(encoder);

        // Present BEFORE releasing the texture view
        wgpuSurfacePresent(surface);

        // Release texture view after present (surface owns the texture)
        wgpuTextureViewRelease(view);
        // Note: Don't release surfaceTexture.texture - it's owned by the surface

        // End frame
        ctx.endFrame();

        // FPS counter
        frameCount++;
        double currentTime = glfwGetTime();
        if (currentTime - lastFpsTime >= 1.0) {
            std::string title = "Vivid - " + std::to_string(frameCount) + " fps";
            glfwSetWindowTitle(window, title.c_str());
            frameCount = 0;
            lastFpsTime = currentTime;
        }
    }

    // Cleanup
    std::cout << "Shutting down..." << std::endl;
    wgpuSurfaceUnconfigure(surface);
    wgpuQueueRelease(queue);
    wgpuDeviceRelease(device);
    wgpuAdapterRelease(adapter);
    wgpuSurfaceRelease(surface);
    wgpuInstanceRelease(instance);
    glfwDestroyWindow(window);
    glfwTerminate();

    return 0;
}
