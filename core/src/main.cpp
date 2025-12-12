// Vivid - Core Runtime
// Minimal core: window, timing, input, hot-reload, display

#include <vivid/vivid.h>
#include <vivid/context.h>
#include <vivid/display.h>
#include <vivid/hot_reload.h>
#include <vivid/editor_bridge.h>
#include <vivid/audio_buffer.h>
#include "imgui/imgui_integration.h"
#include "imgui/chain_visualizer.h"
#include <webgpu/webgpu.h>
#include <webgpu/wgpu.h>  // wgpu-native extensions (wgpuDevicePoll)
#include <glfw3webgpu.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

#include <iostream>
#include <iomanip>
#include <string>
#include <cstring>
#include <filesystem>

// Memory debugging (macOS)
#ifdef __APPLE__
#include <mach/mach.h>
#endif

using namespace vivid;

namespace fs = std::filesystem;

// -----------------------------------------------------------------------------
// Memory Debugging
// -----------------------------------------------------------------------------

#ifdef __APPLE__
size_t getMemoryUsageMB() {
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) {
        return info.resident_size / (1024 * 1024);  // Convert to MB
    }
    return 0;
}
#else
size_t getMemoryUsageMB() {
    return 0;  // Not implemented for other platforms
}
#endif

static double g_lastMemoryLogTime = 0.0;
static size_t g_initialMemory = 0;

static size_t g_lastMemory = 0;

void logMemoryUsage(double time) {
    size_t currentMB = getMemoryUsageMB();
    if (currentMB == 0) return;  // Not supported on this platform

    if (g_initialMemory == 0) {
        g_initialMemory = currentMB;
        g_lastMemory = currentMB;
        std::cout << "=== Memory Tracking Started ===" << std::endl;
    }

    int64_t deltaMB = static_cast<int64_t>(currentMB) - static_cast<int64_t>(g_initialMemory);
    int64_t deltaFromLast = static_cast<int64_t>(currentMB) - static_cast<int64_t>(g_lastMemory);

    std::cout << "[" << std::fixed << std::setprecision(1) << time << "s] "
              << "Memory: " << currentMB << " MB "
              << "(total: " << (deltaMB >= 0 ? "+" : "") << deltaMB << " MB, "
              << "last 10s: " << (deltaFromLast >= 0 ? "+" : "") << deltaFromLast << " MB)"
              << std::endl;

    g_lastMemory = currentMB;
}

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
    std::cout << "Vivid - Starting..." << std::endl;

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

    // Request BC texture compression for HAP video codec support
    WGPUFeatureName requiredFeatures[] = {
        WGPUFeatureName_TextureCompressionBC
    };
    deviceDesc.requiredFeatures = requiredFeatures;
    deviceDesc.requiredFeatureCount = 1;

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

    // Set up scroll callback
    glfwSetWindowUserPointer(window, &ctx);
    glfwSetScrollCallback(window, [](GLFWwindow* w, double xoffset, double yoffset) {
        Context* c = static_cast<Context*>(glfwGetWindowUserPointer(w));
        if (c) c->addScroll(static_cast<float>(xoffset), static_cast<float>(yoffset));
    });

    // Create display
    Display display(device, queue, surfaceFormat);
    if (!display.isValid()) {
        std::cerr << "Warning: Display initialization failed (shaders may be missing)" << std::endl;
    }

    // Initialize ImGui
    vivid::imgui::init(device, queue, surfaceFormat);

    // Create chain visualizer
    vivid::imgui::ChainVisualizer chainVisualizer;

    // Create hot-reload system
    HotReload hotReload;

    // Create editor bridge (WebSocket server for VS Code extension)
    EditorBridge editorBridge;
    editorBridge.start();
    editorBridge.onReloadCommand([&hotReload](const std::string&) {
        std::cout << "[EditorBridge] Force reload triggered by editor\n";
        hotReload.forceReload();
    });
    editorBridge.onParamChange([&ctx](const std::string& opName, const std::string& paramName, const float value[4]) {
        if (!ctx.hasChain()) return;
        Operator* op = ctx.chain().getByName(opName);
        if (op) {
            op->setParam(paramName, value);
        }
    });

    // Helper to gather operator info from chain
    auto gatherOperatorInfo = [&ctx]() -> std::vector<EditorOperatorInfo> {
        std::vector<EditorOperatorInfo> result;
        if (!ctx.hasChain()) return result;

        Chain& chain = ctx.chain();
        for (const auto& name : chain.operatorNames()) {
            Operator* op = chain.getByName(name);
            if (!op) continue;

            EditorOperatorInfo info;
            info.chainName = name;
            info.displayName = op->name();
            info.outputType = outputKindName(op->outputKind());
            info.sourceLine = op->sourceLine;

            // Gather input names
            for (size_t i = 0; i < op->inputCount(); ++i) {
                Operator* input = op->getInput(static_cast<int>(i));
                if (input) {
                    info.inputNames.push_back(chain.getName(input));
                }
            }
            result.push_back(info);
        }
        return result;
    };

    // Helper to gather param values from chain
    auto gatherParamValues = [&ctx]() -> std::vector<EditorParamInfo> {
        std::vector<EditorParamInfo> result;
        if (!ctx.hasChain()) return result;

        Chain& chain = ctx.chain();
        for (const auto& name : chain.operatorNames()) {
            Operator* op = chain.getByName(name);
            if (!op) continue;

            for (const auto& decl : op->params()) {
                EditorParamInfo info;
                info.operatorName = name;
                info.paramName = decl.name;
                info.minVal = decl.minVal;
                info.maxVal = decl.maxVal;

                // Map ParamType to string
                switch (decl.type) {
                    case ParamType::Float:  info.paramType = "Float"; break;
                    case ParamType::Int:    info.paramType = "Int"; break;
                    case ParamType::Bool:   info.paramType = "Bool"; break;
                    case ParamType::Vec2:   info.paramType = "Vec2"; break;
                    case ParamType::Vec3:   info.paramType = "Vec3"; break;
                    case ParamType::Vec4:   info.paramType = "Vec4"; break;
                    case ParamType::Color:  info.paramType = "Color"; break;
                    case ParamType::String: info.paramType = "String"; break;
                    default:                info.paramType = "Unknown"; break;
                }

                // Get current value
                op->getParam(decl.name, info.value);
                result.push_back(info);
            }
        }
        return result;
    };

    // Extract project name for title bar
    std::string projectName;
    fs::path projectDir;
    if (!projectPath.empty()) {
        // Look for chain.cpp in the project path
        fs::path chainPath;
        if (fs::is_directory(projectPath)) {
            chainPath = projectPath / "chain.cpp";
            projectName = projectPath.filename().string();
            projectDir = projectPath;
        } else if (fs::is_regular_file(projectPath)) {
            chainPath = projectPath;
            projectName = projectPath.parent_path().filename().string();
            projectDir = projectPath.parent_path();
        }

        // Set imgui.ini to save in the project directory
        if (!projectDir.empty()) {
            vivid::imgui::setIniDirectory(projectDir.string().c_str());
        }

        if (fs::exists(chainPath)) {
            std::cout << "Loading chain: " << chainPath << std::endl;
            hotReload.setSourceFile(chainPath);
            ctx.setChainPath(chainPath.string());
        } else {
            ctx.setError("Chain file not found: " + chainPath.string());
        }
    } else {
        ctx.setError("No chain specified. Usage: vivid <path/to/chain.cpp>");
    }

    // Track if setup has been called
    bool chainNeedsSetup = true;

    // Fullscreen state tracking
    bool isFullscreen = false;
    int windowedX = 0, windowedY = 0, windowedWidth = 1280, windowedHeight = 720;
    glfwGetWindowPos(window, &windowedX, &windowedY);
    glfwGetWindowSize(window, &windowedWidth, &windowedHeight);

    // Main loop
    double lastFpsTime = glfwGetTime();
    int frameCount = 0;

    // Performance tracking for EditorBridge
    EditorPerformanceStats perfStats;
    double lastFrameTime = glfwGetTime();
    const size_t kHistorySize = 60;  // Keep last 60 samples

    while (!glfwWindowShouldClose(window)) {
        glfwPollEvents();

        // Memory logging every 10 seconds
        {
            double now = glfwGetTime();
            if (now - g_lastMemoryLogTime >= 10.0) {
                logMemoryUsage(now);
                g_lastMemoryLogTime = now;
            }
        }

        // Toggle chain visualizer on Tab key (edge detection)
        {
            static bool tabKeyWasPressed = false;
            bool tabKeyPressed = glfwGetKey(window, GLFW_KEY_TAB) == GLFW_PRESS;
            if (tabKeyPressed && !tabKeyWasPressed) {
                vivid::imgui::toggleVisible();
            }
            tabKeyWasPressed = tabKeyPressed;
        }

        // Toggle fullscreen on 'F' key (edge detection)
        {
            static bool fKeyWasPressed = false;
            bool fKeyPressed = glfwGetKey(window, GLFW_KEY_F) == GLFW_PRESS;
            if (fKeyPressed && !fKeyWasPressed) {
                if (!isFullscreen) {
                    // Save windowed position and size
                    glfwGetWindowPos(window, &windowedX, &windowedY);
                    glfwGetWindowSize(window, &windowedWidth, &windowedHeight);

                    // Get primary monitor
                    GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                    const GLFWvidmode* mode = glfwGetVideoMode(monitor);

                    // Enter fullscreen
                    glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
                    isFullscreen = true;
                } else {
                    // Exit fullscreen - restore windowed mode
                    glfwSetWindowMonitor(window, nullptr, windowedX, windowedY, windowedWidth, windowedHeight, 0);
                    isFullscreen = false;
                }
            }
            fKeyWasPressed = fKeyPressed;
        }

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

        // Handle vsync change
        if (ctx.vsyncChanged()) {
            config.presentMode = ctx.vsync() ? WGPUPresentMode_Fifo : WGPUPresentMode_Immediate;
            wgpuSurfaceConfigure(surface, &config);
        }

        // Handle fullscreen change (from ctx.fullscreen() API)
        if (ctx.fullscreenChanged()) {
            if (ctx.fullscreen() && !isFullscreen) {
                // Save windowed position and size
                glfwGetWindowPos(window, &windowedX, &windowedY);
                glfwGetWindowSize(window, &windowedWidth, &windowedHeight);

                // Get primary monitor
                GLFWmonitor* monitor = glfwGetPrimaryMonitor();
                const GLFWvidmode* mode = glfwGetVideoMode(monitor);

                // Enter fullscreen
                glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
                isFullscreen = true;
            } else if (!ctx.fullscreen() && isFullscreen) {
                // Exit fullscreen - restore windowed mode
                glfwSetWindowMonitor(window, nullptr, windowedX, windowedY, windowedWidth, windowedHeight, 0);
                isFullscreen = false;
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

        // Check for hot-reload using safe API:
        // 1. Check if reload needed (without unloading)
        // 2. Destroy chain operators while old code is still loaded
        // 3. Then reload (which unloads old library)
        bool justReloaded = false;
        if (hotReload.checkNeedsReload()) {
            // Save operator states before destroying chain
            if (ctx.hasChain()) {
                ctx.preserveStates(ctx.chain());
            }
            // Destroy operators BEFORE unloading the library
            // (their destructors are in the dylib code)
            ctx.clearRegisteredOperators();
            ctx.resetChain();

            // Now safe to reload (unloads old library, loads new one)
            hotReload.reload();
            chainNeedsSetup = true;
            justReloaded = true;
        }

        // Update error state from hot-reload
        if (hotReload.hasError()) {
            ctx.setError(hotReload.getError());
        } else if (hotReload.isLoaded()) {
            ctx.clearError();
        }

        // Notify connected editors of compile status
        if (justReloaded && editorBridge.clientCount() > 0) {
            if (hotReload.hasError()) {
                editorBridge.sendCompileStatus(false, hotReload.getError());
            } else {
                editorBridge.sendCompileStatus(true, "");
            }
        }

        // Call chain functions if loaded
        if (hotReload.isLoaded()) {
            // Call setup if needed (after reload)
            if (chainNeedsSetup) {
                // Chain was already reset before reload (to safely destroy operators)
                // Just call user's setup function
                hotReload.getSetupFn()(ctx);

                // Auto-initialize the chain
                ctx.chain().init(ctx);

                // Load and apply sidecar parameter overrides
                chainVisualizer.loadAndApplySidecar(ctx);

                // Restore preserved states (takes precedence over sidecar)
                if (ctx.hasPreservedStates()) {
                    ctx.restoreStates(ctx.chain());
                }

                chainNeedsSetup = false;

                // Send operator list to connected editors (Phase 2)
                if (editorBridge.clientCount() > 0) {
                    editorBridge.sendOperatorList(gatherOperatorInfo());
                }
            }

            // Call user's update function (parameter tweaks)
            hotReload.getUpdateFn()(ctx);

            // Auto-process the chain
            ctx.chain().process(ctx);

            // Capture frame for video export if recording
            if (chainVisualizer.exporter().isRecording() && ctx.outputTexture()) {
                // Get the underlying texture from the texture view
                // The output texture should be the chain's final output
                WGPUTexture outputTex = ctx.chain().outputTexture();
                if (outputTex) {
                    chainVisualizer.exporter().captureFrame(device, queue, outputTex);

                    // Capture audio if recording with audio
                    if (chainVisualizer.exporter().hasAudio()) {
                        // Generate audio synchronously for this video frame
                        // Calculate audio frames per video frame: 48000Hz / fps
                        float fps = chainVisualizer.exporter().fps();
                        uint32_t audioFramesPerVideoFrame = static_cast<uint32_t>(AUDIO_SAMPLE_RATE / fps);

                        // Use a static buffer for efficiency
                        static std::vector<float> audioBuffer;
                        if (audioBuffer.size() < audioFramesPerVideoFrame * AUDIO_CHANNELS) {
                            audioBuffer.resize(audioFramesPerVideoFrame * AUDIO_CHANNELS);
                        }

                        // Generate audio deterministically (no race condition)
                        ctx.chain().generateAudioForExport(audioBuffer.data(), audioFramesPerVideoFrame);
                        chainVisualizer.exporter().pushAudioSamples(
                            audioBuffer.data(), audioFramesPerVideoFrame);
                    }
                }
            }

            // Save snapshot if requested
            if (chainVisualizer.snapshotRequested()) {
                WGPUTexture outputTex = ctx.chain().outputTexture();
                if (outputTex) {
                    chainVisualizer.saveSnapshot(device, queue, outputTex, ctx);
                }
            }
        }

        // Create command encoder
        WGPUCommandEncoderDescriptor encoderDesc = {};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

        // Render pass - clear to black
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
        display.setScreenSize(width, height);

        // Build frame input for ImGui (needed for chain visualizer even if UI hidden)
        float xscale, yscale;
        glfwGetWindowContentScale(window, &xscale, &yscale);

        vivid::imgui::FrameInput frameInput;
        frameInput.width = ctx.width();
        frameInput.height = ctx.height();
        frameInput.contentScale = xscale;
        frameInput.dt = static_cast<float>(ctx.dt());
        frameInput.mousePos = ctx.mouse();
        frameInput.mouseDown[0] = ctx.mouseButton(0).held;
        frameInput.mouseDown[1] = ctx.mouseButton(1).held;
        frameInput.mouseDown[2] = ctx.mouseButton(2).held;
        frameInput.scroll = ctx.scroll();
        frameInput.keyCtrl = glfwGetKey(window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                             glfwGetKey(window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
        frameInput.keyShift = glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                              glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
        frameInput.keyAlt = glfwGetKey(window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                            glfwGetKey(window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
        frameInput.keySuper = glfwGetKey(window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
                              glfwGetKey(window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;

        // Run chain visualizer BEFORE blit so solo mode can override output texture
        if (vivid::imgui::isVisible()) {
            vivid::imgui::beginFrame(frameInput);
            chainVisualizer.render(frameInput, ctx);
        }

        // Blit output texture (may have been modified by solo mode)
        if (ctx.outputTexture() && display.isValid()) {
            display.blit(pass, ctx.outputTexture());
        }

        // Render ImGui on top of the blit
        if (vivid::imgui::isVisible()) {
            vivid::imgui::render(pass);
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

        // Poll device to process pending GPU work and prevent command buffer accumulation
        // This is critical when multiple operators submit command buffers per frame
        wgpuDevicePoll(device, false, nullptr);

        // Release texture view after present (surface owns the texture)
        wgpuTextureViewRelease(view);
        // Note: Don't release surfaceTexture.texture - it's owned by the surface

        // End frame
        ctx.endFrame();

        // FPS counter and title update
        frameCount++;
        double currentTime = glfwGetTime();

        // Track frame time for performance stats
        double frameTimeMs = (currentTime - lastFrameTime) * 1000.0;
        lastFrameTime = currentTime;

        // Update frame time history (rolling buffer)
        perfStats.frameTimeHistory.push_back(static_cast<float>(frameTimeMs));
        if (perfStats.frameTimeHistory.size() > kHistorySize) {
            perfStats.frameTimeHistory.erase(perfStats.frameTimeHistory.begin());
        }
        perfStats.frameTimeMs = static_cast<float>(frameTimeMs);

        if (currentTime - lastFpsTime >= 1.0) {
            std::string title;
            if (!projectName.empty()) {
                title = projectName + " - Vivid (" + std::to_string(frameCount) + " fps)";
            } else {
                title = "Vivid (" + std::to_string(frameCount) + " fps)";
            }
            glfwSetWindowTitle(window, title.c_str());

            // Update FPS in performance stats
            perfStats.fps = static_cast<float>(frameCount);
            perfStats.fpsHistory.push_back(perfStats.fps);
            if (perfStats.fpsHistory.size() > kHistorySize) {
                perfStats.fpsHistory.erase(perfStats.fpsHistory.begin());
            }

            frameCount = 0;
            lastFpsTime = currentTime;

            // Send param values and performance stats to editors once per second
            if (editorBridge.clientCount() > 0 && hotReload.isLoaded()) {
                editorBridge.sendParamValues(gatherParamValues());

                // Gather operator count and estimate texture memory
                perfStats.operatorCount = ctx.hasChain() ? ctx.chain().operatorNames().size() : 0;

                // Estimate texture memory: count operators and assume average texture size
                // More accurate would require querying each texture operator
                size_t textureOpCount = 0;
                if (ctx.hasChain()) {
                    for (const auto& name : ctx.chain().operatorNames()) {
                        Operator* op = ctx.chain().getByName(name);
                        if (op && op->outputKind() == OutputKind::Texture) {
                            textureOpCount++;
                        }
                    }
                }
                // Estimate: each texture is width*height*4 bytes (RGBA8)
                perfStats.textureMemoryBytes = textureOpCount * ctx.width() * ctx.height() * 4;

                editorBridge.sendPerformanceStats(perfStats);
            }
        }
    }

    // Cleanup
    std::cout << "Shutting down..." << std::endl;

    // Stop editor bridge
    editorBridge.stop();

    // Release chain operators before WebGPU cleanup (they hold textures/resources)
    ctx.resetChain();

    // Shutdown ImGui
    chainVisualizer.shutdown();
    vivid::imgui::shutdown();

    // Release display resources before WebGPU cleanup
    display.shutdown();

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
