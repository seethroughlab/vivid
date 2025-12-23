// Vivid Application Implementation
// Contains WebGPU initialization, main loop, and cleanup

#include "app.h"

#include <vivid/vivid.h>
#include <vivid/context.h>
#include <vivid/display.h>
#include <vivid/hot_reload.h>
#include <vivid/editor_bridge.h>
#include <vivid/audio_buffer.h>
#include <vivid/video_exporter.h>
#include <vivid/cli.h>
#include <vivid/addon_manager.h>
#include <vivid/window_manager.h>
#include <vivid/asset_loader.h>
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
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <regex>
#include <functional>

// Memory debugging (macOS)
#ifdef __APPLE__
#include <mach/mach.h>
#endif

// Platform-specific helpers (autoreleasepool for macOS)
#include <vivid/platform_macos.h>

namespace fs = std::filesystem;

namespace vivid {

// -----------------------------------------------------------------------------
// Memory Debugging
// -----------------------------------------------------------------------------

#ifdef __APPLE__
static size_t getMemoryUsageMB() {
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO, (task_info_t)&info, &count) == KERN_SUCCESS) {
        return info.resident_size / (1024 * 1024);  // Convert to MB
    }
    return 0;
}
#else
static size_t getMemoryUsageMB() {
    return 0;  // Not implemented for other platforms
}
#endif

static double g_lastMemoryLogTime = 0.0;
static size_t g_initialMemory = 0;
static size_t g_lastMemory = 0;

static void logMemoryUsage(double time) {
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
// Main Loop Context
// -----------------------------------------------------------------------------
// Encapsulates all state needed for the main loop iteration.

struct MainLoopContext {
    // WebGPU infrastructure
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUSurface surface = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_BGRA8Unorm;
    WGPUSurfaceConfigurationExtras configExtras = {};
    WGPUSurfaceConfiguration config = {};

    // Window state
    GLFWwindow* window = nullptr;
    int width = 0;
    int height = 0;
    bool isFullscreen = false;
    int windowedX = 0;
    int windowedY = 0;
    int windowedWidth = 1280;
    int windowedHeight = 720;
    WindowManager* windowManager = nullptr;

    // Timing & performance
    double lastFpsTime = 0.0;
    int frameCount = 0;
    double lastFrameTime = 0.0;
    EditorPerformanceStats perfStats;
    static constexpr size_t kHistorySize = 60;

    // Loop control
    int snapshotFrameCounter = 0;
    bool snapshotSaved = false;
    VideoExporter cliRecorder;
    bool cliRecordingStarted = false;
    bool chainNeedsSetup = true;
    bool tabKeyWasPressed = false;

    // Core runtime objects (non-owning pointers)
    Context* ctx = nullptr;
    Display* display = nullptr;
    HotReload* hotReload = nullptr;
    vivid::imgui::ChainVisualizer* chainVisualizer = nullptr;
    EditorBridge* editorBridge = nullptr;

    // CLI args needed in loop
    std::string snapshotPath;
    int snapshotFrame = 5;
    bool headless = false;
    int renderWidth = 0;
    int renderHeight = 0;
    std::string recordPath;
    float recordFps = 60.0f;
    float recordDuration = 0.0f;
    bool recordAudio = false;
    ExportCodec recordCodec = ExportCodec::H264;
    int maxFrames = 0;
    int windowWidth = 1280;
    int windowHeight = 720;
    bool showUI = false;

    // Project info
    std::string projectName;

    // Callbacks (lambdas converted to std::function)
    std::function<void(const std::string&)> updateSourceLines;
    std::function<std::vector<EditorOperatorInfo>()> gatherOperatorInfo;
    std::function<std::vector<EditorParamInfo>()> gatherParamValues;
    std::function<EditorWindowState()> gatherWindowState;
};

// -----------------------------------------------------------------------------
// Main Loop Iteration
// -----------------------------------------------------------------------------
// Returns true to continue running, false to exit.

static bool mainLoopIteration(MainLoopContext& mlc) {
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
        bool tabKeyPressed = glfwGetKey(mlc.window, GLFW_KEY_TAB) == GLFW_PRESS;
        if (tabKeyPressed && !mlc.tabKeyWasPressed) {
            vivid::imgui::toggleVisible();
        }
        mlc.tabKeyWasPressed = tabKeyPressed;
    }

    // Begin frame (updates time, input, etc.)
    mlc.ctx->beginFrame();
    mlc.ctx->beginDebugFrame();

    // Handle window resize
    if (mlc.ctx->width() != mlc.width || mlc.ctx->height() != mlc.height) {
        mlc.width = mlc.ctx->width();
        mlc.height = mlc.ctx->height();
        if (mlc.width > 0 && mlc.height > 0) {
            mlc.config.width = static_cast<uint32_t>(mlc.width);
            mlc.config.height = static_cast<uint32_t>(mlc.height);
            wgpuSurfaceConfigure(mlc.surface, &mlc.config);
        }
    }

    // Handle vsync change
    if (mlc.ctx->consumeVsyncChange()) {
        mlc.config.presentMode = mlc.ctx->vsync() ? WGPUPresentMode_Fifo : WGPUPresentMode_Immediate;
        wgpuSurfaceConfigure(mlc.surface, &mlc.config);
    }

    // Handle fullscreen change (from ctx.fullscreen() API)
    if (mlc.ctx->consumeFullscreenChange()) {
        if (mlc.ctx->fullscreen() && !mlc.isFullscreen) {
            // Save windowed position and size
            glfwGetWindowPos(mlc.window, &mlc.windowedX, &mlc.windowedY);
            glfwGetWindowSize(mlc.window, &mlc.windowedWidth, &mlc.windowedHeight);

            // Get target monitor (use current or selected)
            int monitorCount = 0;
            GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
            int targetIdx = std::min(mlc.ctx->targetMonitor(), monitorCount - 1);
            targetIdx = std::max(0, targetIdx);
            GLFWmonitor* monitor = monitors[targetIdx];
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);

            // Enter fullscreen
            glfwSetWindowMonitor(mlc.window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
            mlc.isFullscreen = true;
        } else if (!mlc.ctx->fullscreen() && mlc.isFullscreen) {
            // Exit fullscreen - restore windowed mode
            glfwSetWindowMonitor(mlc.window, nullptr, mlc.windowedX, mlc.windowedY, mlc.windowedWidth, mlc.windowedHeight, 0);
            mlc.isFullscreen = false;
        }
    }

    // Handle borderless (decorated) window change
    if (mlc.ctx->consumeBorderlessChange()) {
        glfwSetWindowAttrib(mlc.window, GLFW_DECORATED, mlc.ctx->borderless() ? GLFW_FALSE : GLFW_TRUE);
    }

    // Handle always-on-top (floating) change
    if (mlc.ctx->consumeAlwaysOnTopChange()) {
        glfwSetWindowAttrib(mlc.window, GLFW_FLOATING, mlc.ctx->alwaysOnTop() ? GLFW_TRUE : GLFW_FALSE);
    }

    // Handle cursor visibility change
    if (mlc.ctx->consumeCursorVisibleChange()) {
        glfwSetInputMode(mlc.window, GLFW_CURSOR,
            mlc.ctx->cursorVisible() ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_HIDDEN);
    }

    // Handle monitor change (move window to different display)
    if (mlc.ctx->consumeMonitorChange()) {
        int monitorCount = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
        int targetIdx = std::min(mlc.ctx->targetMonitor(), monitorCount - 1);
        targetIdx = std::max(0, targetIdx);

        if (targetIdx < monitorCount) {
            GLFWmonitor* monitor = monitors[targetIdx];
            const GLFWvidmode* mode = glfwGetVideoMode(monitor);

            if (mlc.isFullscreen) {
                // In fullscreen: switch to fullscreen on target monitor
                glfwSetWindowMonitor(mlc.window, monitor, 0, 0, mode->width, mode->height, mode->refreshRate);
            } else {
                // In windowed: center window on target monitor
                int mx, my;
                glfwGetMonitorPos(monitor, &mx, &my);

                int ww, wh;
                glfwGetWindowSize(mlc.window, &ww, &wh);

                int newX = mx + (mode->width - ww) / 2;
                int newY = my + (mode->height - wh) / 2;
                glfwSetWindowPos(mlc.window, newX, newY);
            }
        }
    }

    // Handle window position change
    if (mlc.ctx->consumeWindowPosChange()) {
        glfwSetWindowPos(mlc.window, mlc.ctx->targetWindowX(), mlc.ctx->targetWindowY());
    }

    // Handle window size change
    if (mlc.ctx->consumeWindowSizeChange()) {
        glfwSetWindowSize(mlc.window, mlc.ctx->targetWindowWidth(), mlc.ctx->targetWindowHeight());
    }

    // Skip frame if minimized
    if (mlc.width == 0 || mlc.height == 0) {
        mlc.ctx->endFrame();
        return true;  // continue
    }

    // Get current texture
    WGPUSurfaceTexture surfaceTexture;
    wgpuSurfaceGetCurrentTexture(mlc.surface, &surfaceTexture);
    if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        mlc.ctx->endFrame();
        return true;  // continue
    }

    // Create view with explicit format matching the surface texture
    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = mlc.surfaceFormat;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_All;
    WGPUTextureView view = wgpuTextureCreateView(surfaceTexture.texture, &viewDesc);

    // Check for hot-reload using safe API
    bool justReloaded = false;
    if (mlc.hotReload->checkNeedsReload()) {
        // Save operator states before destroying chain
        if (mlc.ctx->hasChain()) {
            mlc.ctx->preserveStates(mlc.ctx->chain());
        }
        // Destroy operators BEFORE unloading the library
        mlc.ctx->clearRegisteredOperators();
        mlc.ctx->resetChain();

        // Now safe to reload
        mlc.hotReload->reload();
        mlc.chainNeedsSetup = true;
        justReloaded = true;
    }

    // Update error state from hot-reload
    if (mlc.hotReload->hasError()) {
        mlc.ctx->setError(mlc.hotReload->getError());
    } else if (mlc.hotReload->isLoaded()) {
        mlc.ctx->clearError();
    }

    // Notify connected editors of compile status
    if (justReloaded && mlc.editorBridge->clientCount() > 0) {
        if (mlc.hotReload->hasError()) {
            mlc.editorBridge->sendCompileStatus(false, mlc.hotReload->getError());
        } else {
            mlc.editorBridge->sendCompileStatus(true, "");
        }
    }

    // Call chain functions if loaded
    if (mlc.hotReload->isLoaded()) {
        // Call setup if needed (after reload)
        if (mlc.chainNeedsSetup) {
            mlc.hotReload->getSetupFn()(*mlc.ctx);

            // Auto-initialize the chain
            mlc.ctx->chain().init(*mlc.ctx);

            // Honor chain's window size request
            if (mlc.ctx->chain().hasWindowSize()) {
                int w = mlc.ctx->chain().windowWidth();
                int h = mlc.ctx->chain().windowHeight();
                if (w > 0 && h > 0 && !mlc.isFullscreen) {
                    glfwSetWindowSize(mlc.window, w, h);
                }
            }

            // Update render resolution from chain if set
            if (mlc.ctx->chain().hasResolution()) {
                mlc.ctx->setRenderResolution(mlc.ctx->chain().defaultWidth(), mlc.ctx->chain().defaultHeight());
            }

            // Restore preserved states across hot-reloads
            if (mlc.ctx->hasPreservedStates()) {
                mlc.ctx->restoreStates(mlc.ctx->chain());
            }

            mlc.chainNeedsSetup = false;

            // Update operator source line numbers from chain.cpp
            if (mlc.updateSourceLines) {
                mlc.updateSourceLines(mlc.ctx->chainPath());
            }

            // Send operator list to connected editors
            if (mlc.editorBridge->clientCount() > 0 && mlc.gatherOperatorInfo) {
                mlc.editorBridge->sendOperatorList(mlc.gatherOperatorInfo());
            }

            // Start CLI recording (once, after first chain load)
            if (!mlc.recordPath.empty() && !mlc.cliRecordingStarted) {
                int recW = mlc.renderWidth > 0 ? mlc.renderWidth : mlc.windowWidth;
                int recH = mlc.renderHeight > 0 ? mlc.renderHeight : mlc.windowHeight;
                bool started = false;
                if (mlc.recordAudio) {
                    started = mlc.cliRecorder.startWithAudio(mlc.recordPath, recW, recH,
                                                         mlc.recordFps, mlc.recordCodec,
                                                         AUDIO_SAMPLE_RATE, AUDIO_CHANNELS);
                } else {
                    started = mlc.cliRecorder.start(mlc.recordPath, recW, recH, mlc.recordFps, mlc.recordCodec);
                }
                if (started) {
                    std::cout << "Recording to: " << mlc.recordPath
                              << " (" << recW << "x" << recH << " @ " << mlc.recordFps << "fps";
                    if (mlc.recordDuration > 0) {
                        std::cout << ", " << mlc.recordDuration << "s";
                    }
                    std::cout << ")" << std::endl;
                } else {
                    std::cerr << "Failed to start recording: " << mlc.cliRecorder.error() << std::endl;
                }
                mlc.cliRecordingStarted = true;
            }
        }

        // Start ImGui frame BEFORE user update so user chains can use ImGui
        {
            float xscale, yscale;
            glfwGetWindowContentScale(mlc.window, &xscale, &yscale);

            vivid::imgui::FrameInput frameInput;
            frameInput.width = mlc.ctx->width();
            frameInput.height = mlc.ctx->height();
            frameInput.contentScale = xscale;
            frameInput.dt = static_cast<float>(mlc.ctx->dt());
            frameInput.mousePos = mlc.ctx->mouse();
            frameInput.mouseDown[0] = mlc.ctx->mouseButton(0).held;
            frameInput.mouseDown[1] = mlc.ctx->mouseButton(1).held;
            frameInput.mouseDown[2] = mlc.ctx->mouseButton(2).held;
            frameInput.scroll = mlc.ctx->scroll();
            frameInput.keyCtrl = glfwGetKey(mlc.window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                                 glfwGetKey(mlc.window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
            frameInput.keyShift = glfwGetKey(mlc.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                                  glfwGetKey(mlc.window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
            frameInput.keyAlt = glfwGetKey(mlc.window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                                glfwGetKey(mlc.window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
            frameInput.keySuper = glfwGetKey(mlc.window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
                                  glfwGetKey(mlc.window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;

            // Always begin ImGui frame (even if visualizer not visible) so user chains can use ImGui
            vivid::imgui::beginFrame(frameInput);
        }

        // Call user's update function
        mlc.hotReload->getUpdateFn()(*mlc.ctx);

        // Auto-process the chain
        mlc.ctx->chain().process(*mlc.ctx);

        // Capture frame for video export if recording
        if (mlc.chainVisualizer->exporter().isRecording() && mlc.ctx->outputTexture()) {
            WGPUTexture outputTex = mlc.ctx->chain().outputTexture();
            if (outputTex) {
                mlc.chainVisualizer->exporter().captureFrame(mlc.device, mlc.queue, outputTex);

                // Capture audio if recording with audio
                if (mlc.chainVisualizer->exporter().hasAudio()) {
                    float fps = mlc.chainVisualizer->exporter().fps();
                    uint32_t audioFramesPerVideoFrame = static_cast<uint32_t>(AUDIO_SAMPLE_RATE / fps);

                    static std::vector<float> audioBuffer;
                    if (audioBuffer.size() < audioFramesPerVideoFrame * AUDIO_CHANNELS) {
                        audioBuffer.resize(audioFramesPerVideoFrame * AUDIO_CHANNELS);
                    }

                    mlc.ctx->chain().generateAudioForExport(audioBuffer.data(), audioFramesPerVideoFrame);
                    mlc.chainVisualizer->exporter().pushAudioSamples(
                        audioBuffer.data(), audioFramesPerVideoFrame);
                }
            }
        }

        // CLI video recording capture
        if (mlc.cliRecorder.isRecording() && mlc.ctx->outputTexture()) {
            WGPUTexture outputTex = mlc.ctx->chain().outputTexture();
            if (outputTex) {
                mlc.cliRecorder.captureFrame(mlc.device, mlc.queue, outputTex);

                // Capture audio if enabled
                if (mlc.cliRecorder.hasAudio()) {
                    uint32_t audioFramesPerVideoFrame = static_cast<uint32_t>(AUDIO_SAMPLE_RATE / mlc.recordFps);
                    static std::vector<float> cliAudioBuffer;
                    if (cliAudioBuffer.size() < audioFramesPerVideoFrame * AUDIO_CHANNELS) {
                        cliAudioBuffer.resize(audioFramesPerVideoFrame * AUDIO_CHANNELS);
                    }
                    mlc.ctx->chain().generateAudioForExport(cliAudioBuffer.data(), audioFramesPerVideoFrame);
                    mlc.cliRecorder.pushAudioSamples(cliAudioBuffer.data(), audioFramesPerVideoFrame);
                }

                // Check duration limit
                if (mlc.recordDuration > 0 && mlc.cliRecorder.duration() >= mlc.recordDuration) {
                    std::cout << "Recording complete: " << mlc.cliRecorder.frameCount() << " frames, "
                              << mlc.cliRecorder.duration() << "s" << std::endl;
                    mlc.cliRecorder.stop();
                    glfwSetWindowShouldClose(mlc.window, GLFW_TRUE);
                }
            }
        }

        // Save snapshot if requested (interactive UI)
        if (mlc.chainVisualizer->snapshotRequested()) {
            WGPUTexture outputTex = mlc.ctx->chain().outputTexture();
            if (outputTex) {
                mlc.chainVisualizer->saveSnapshot(mlc.device, mlc.queue, outputTex, *mlc.ctx);
            }
        }

        // Track total frames for --snapshot and --frames options
        mlc.snapshotFrameCounter++;

        // Automated snapshot mode (CLI --snapshot flag)
        if (!mlc.snapshotPath.empty() && !mlc.snapshotSaved) {
            if (mlc.snapshotFrameCounter >= mlc.snapshotFrame) {
                WGPUTexture outputTex = mlc.ctx->chain().outputTexture();
                if (outputTex) {
                    std::cout << "Saving snapshot to: " << mlc.snapshotPath << std::endl;
                    if (VideoExporter::saveSnapshot(mlc.device, mlc.queue, outputTex, mlc.snapshotPath)) {
                        std::cout << "Snapshot saved successfully" << std::endl;
                        mlc.snapshotSaved = true;
                        // Exit after saving (unless --frames is also set)
                        if (mlc.maxFrames == 0) {
                            glfwSetWindowShouldClose(mlc.window, GLFW_TRUE);
                        }
                    } else {
                        std::cerr << "Failed to save snapshot" << std::endl;
                        mlc.snapshotSaved = true;  // Don't retry
                    }
                }
            }
        }

        // Frame limit mode (CLI --frames flag)
        if (mlc.maxFrames > 0 && mlc.snapshotFrameCounter >= mlc.maxFrames) {
            std::cout << "Rendered " << mlc.maxFrames << " frames, exiting." << std::endl;
            glfwSetWindowShouldClose(mlc.window, GLFW_TRUE);
        }
    }

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(mlc.device, &encoderDesc);

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
    mlc.display->setScreenSize(mlc.width, mlc.height);

    // Build frame input for ImGui
    float xscale, yscale;
    glfwGetWindowContentScale(mlc.window, &xscale, &yscale);

    vivid::imgui::FrameInput frameInput;
    frameInput.width = mlc.ctx->width();
    frameInput.height = mlc.ctx->height();
    frameInput.contentScale = xscale;
    frameInput.dt = static_cast<float>(mlc.ctx->dt());
    frameInput.mousePos = mlc.ctx->mouse();
    frameInput.mouseDown[0] = mlc.ctx->mouseButton(0).held;
    frameInput.mouseDown[1] = mlc.ctx->mouseButton(1).held;
    frameInput.mouseDown[2] = mlc.ctx->mouseButton(2).held;
    frameInput.scroll = mlc.ctx->scroll();
    frameInput.keyCtrl = glfwGetKey(mlc.window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
                         glfwGetKey(mlc.window, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS;
    frameInput.keyShift = glfwGetKey(mlc.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                          glfwGetKey(mlc.window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
    frameInput.keyAlt = glfwGetKey(mlc.window, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
                        glfwGetKey(mlc.window, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS;
    frameInput.keySuper = glfwGetKey(mlc.window, GLFW_KEY_LEFT_SUPER) == GLFW_PRESS ||
                          glfwGetKey(mlc.window, GLFW_KEY_RIGHT_SUPER) == GLFW_PRESS;
    frameInput.surfaceFormat = mlc.surfaceFormat;

    // Toggle between imnodes and new NodeGraph (press 'G' key)
    static bool lastGKeyPressed = false;
    bool gKeyPressed = glfwGetKey(mlc.window, GLFW_KEY_G) == GLFW_PRESS;
    if (gKeyPressed && !lastGKeyPressed) {
        mlc.chainVisualizer->setUseNodeGraph(!mlc.chainVisualizer->useNodeGraph());
        std::cout << "[Vivid] NodeGraph mode: " << (mlc.chainVisualizer->useNodeGraph() ? "ON" : "OFF") << std::endl;
    }
    lastGKeyPressed = gKeyPressed;

    // Run chain visualizer BEFORE blit so solo mode can override output texture
    // (beginFrame was already called before user update)
    if (vivid::imgui::isVisible() && !mlc.chainVisualizer->useNodeGraph()) {
        mlc.chainVisualizer->render(frameInput, *mlc.ctx);
    }

    // Blit output texture (may have been modified by solo mode)
    if (mlc.ctx->outputTexture() && mlc.display->isValid()) {
        mlc.display->blit(pass, mlc.ctx->outputTexture());
    }

    // Render new NodeGraph if enabled (after blit, so it overlays the output)
    if (vivid::imgui::isVisible() && mlc.chainVisualizer->useNodeGraph()) {
        mlc.chainVisualizer->renderNodeGraph(pass, frameInput, *mlc.ctx);
    }

    // Always render ImGui (ends the frame started before user update)
    // This renders both user ImGui widgets and chain visualizer (if visible)
    vivid::imgui::render(pass);

    // Render error message if present
    if (mlc.ctx->hasError() && mlc.display->isValid()) {
        mlc.display->renderText(pass, mlc.ctx->errorMessage(), 20.0f, 20.0f, 2.0f);
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    // Submit
    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    wgpuQueueSubmit(mlc.queue, 1, &cmdBuffer);

    // Release command resources
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    // Present BEFORE releasing the texture view
    wgpuSurfacePresent(mlc.surface);

    // Present to secondary windows (span/multi-output)
    if (mlc.windowManager->windowCount() > 1) {
        mlc.windowManager->presentAll(&mlc.ctx->chain(), mlc.ctx->outputTexture());
    }

    // Poll device to process pending GPU work
    wgpuDevicePoll(mlc.device, false, nullptr);

    // Release texture view after present
    wgpuTextureViewRelease(view);
    wgpuTextureRelease(surfaceTexture.texture);

    // End frame
    mlc.ctx->endFrame();

    // FPS counter and title update
    mlc.frameCount++;
    double currentTime = glfwGetTime();

    // Track frame time for performance stats
    double frameTimeMs = (currentTime - mlc.lastFrameTime) * 1000.0;
    mlc.lastFrameTime = currentTime;

    // Update frame time history
    mlc.perfStats.frameTimeHistory.push_back(static_cast<float>(frameTimeMs));
    if (mlc.perfStats.frameTimeHistory.size() > mlc.kHistorySize) {
        mlc.perfStats.frameTimeHistory.pop_front();
    }
    mlc.perfStats.frameTimeMs = static_cast<float>(frameTimeMs);

    // Update FPS every second
    if (currentTime - mlc.lastFpsTime >= 1.0) {
        float fps = mlc.frameCount / static_cast<float>(currentTime - mlc.lastFpsTime);
        mlc.perfStats.fps = fps;

        if (!mlc.headless) {
            // Update window title with FPS
            std::string title = mlc.projectName.empty() ? "Vivid" : mlc.projectName;
            title += " - " + std::to_string(static_cast<int>(fps)) + " fps";
            glfwSetWindowTitle(mlc.window, title.c_str());
        }

        mlc.lastFpsTime = currentTime;
        mlc.frameCount = 0;

        // Send performance stats to connected editors
        if (mlc.editorBridge->clientCount() > 0 && mlc.gatherParamValues) {
            // Estimate GPU memory (rough: count texture operators)
            size_t textureOpCount = 0;
            if (mlc.ctx->hasChain()) {
                for (const auto& name : mlc.ctx->chain().operatorNames()) {
                    Operator* op = mlc.ctx->chain().getByName(name);
                    if (op && op->outputKind() == OutputKind::Texture) {
                        textureOpCount++;
                    }
                }
            }
            mlc.perfStats.textureMemoryBytes = textureOpCount * mlc.ctx->width() * mlc.ctx->height() * 4;

            mlc.editorBridge->sendPerformanceStats(mlc.perfStats);
        }
    }

    return true;  // continue running
}

// -----------------------------------------------------------------------------
// Application Implementation
// -----------------------------------------------------------------------------

struct Application::Impl {
    // Owned objects
    std::unique_ptr<WindowManager> windowManager;
    std::unique_ptr<Context> ctx;
    std::unique_ptr<Display> display;
    std::unique_ptr<HotReload> hotReload;
    std::unique_ptr<vivid::imgui::ChainVisualizer> chainVisualizer;
    std::unique_ptr<EditorBridge> editorBridge;

    // WebGPU objects
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUSurface surface = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_BGRA8Unorm;
    WGPUSurfaceConfigurationExtras configExtras = {};
    WGPUSurfaceConfiguration config = {};

    // Window
    GLFWwindow* window = nullptr;
    int width = 0;
    int height = 0;

    // Main loop context
    MainLoopContext mlc;

    // Config copy
    AppConfig appConfig;
};

Application::~Application() {
    shutdown();
}

int Application::init(const AppConfig& config) {
    if (m_initialized) {
        return 0;  // Already initialized
    }

    m_impl = new Impl();
    m_impl->appConfig = config;

    // Extract project name for window title
    std::string initialWindowTitle = "Vivid";
    if (!config.projectPath.empty()) {
        fs::path pp(config.projectPath);
        if (fs::is_directory(pp)) {
            initialWindowTitle = pp.filename().string();
        } else if (fs::is_regular_file(pp)) {
            initialWindowTitle = pp.parent_path().filename().string();
        }
    }

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return 1;
    }

    // No OpenGL context - we're using WebGPU
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    // Headless mode: create invisible window
    if (config.headless) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    // Create window
    m_impl->window = glfwCreateWindow(config.windowWidth, config.windowHeight,
                                       initialWindowTitle.c_str(), nullptr, nullptr);
    if (!m_impl->window) {
        std::cerr << "Failed to create window" << std::endl;
        glfwTerminate();
        return 1;
    }

    // Create WebGPU instance
    WGPUInstanceDescriptor instanceDesc = {};
    m_impl->instance = wgpuCreateInstance(&instanceDesc);
    if (!m_impl->instance) {
        std::cerr << "Failed to create WebGPU instance" << std::endl;
        glfwDestroyWindow(m_impl->window);
        glfwTerminate();
        return 1;
    }

    // Create surface from GLFW window
    m_impl->surface = glfwCreateWindowWGPUSurface(m_impl->instance, m_impl->window);
    if (!m_impl->surface) {
        std::cerr << "Failed to create surface" << std::endl;
        wgpuInstanceRelease(m_impl->instance);
        glfwDestroyWindow(m_impl->window);
        glfwTerminate();
        return 1;
    }

    // Request adapter
    std::cout << "Requesting adapter..." << std::endl;
    WGPURequestAdapterOptions adapterOpts = {};
    adapterOpts.compatibleSurface = m_impl->surface;
    adapterOpts.powerPreference = WGPUPowerPreference_HighPerformance;

    AdapterUserData adapterData;
    WGPURequestAdapterCallbackInfo adapterCallback = {};
    adapterCallback.mode = WGPUCallbackMode_AllowSpontaneous;
    adapterCallback.callback = onAdapterRequestEnded;
    adapterCallback.userdata1 = &adapterData;

    wgpuInstanceRequestAdapter(m_impl->instance, &adapterOpts, adapterCallback);

    while (!adapterData.done) {
        // Spin
    }

    if (!adapterData.adapter) {
        std::cerr << "Failed to get adapter" << std::endl;
        wgpuSurfaceRelease(m_impl->surface);
        wgpuInstanceRelease(m_impl->instance);
        glfwDestroyWindow(m_impl->window);
        glfwTerminate();
        return 1;
    }

    m_impl->adapter = adapterData.adapter;

    // Print adapter info
    WGPUAdapterInfo info = {};
    wgpuAdapterGetInfo(m_impl->adapter, &info);
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

    // Set error callbacks
    deviceDesc.deviceLostCallbackInfo.callback = onDeviceLost;
    deviceDesc.uncapturedErrorCallbackInfo.callback = onDeviceError;

    DeviceUserData deviceData;
    WGPURequestDeviceCallbackInfo deviceCallback = {};
    deviceCallback.mode = WGPUCallbackMode_AllowSpontaneous;
    deviceCallback.callback = onDeviceRequestEnded;
    deviceCallback.userdata1 = &deviceData;

    wgpuAdapterRequestDevice(m_impl->adapter, &deviceDesc, deviceCallback);

    while (!deviceData.done) {
        // Spin
    }

    if (!deviceData.device) {
        std::cerr << "Failed to get device" << std::endl;
        wgpuAdapterRelease(m_impl->adapter);
        wgpuSurfaceRelease(m_impl->surface);
        wgpuInstanceRelease(m_impl->instance);
        glfwDestroyWindow(m_impl->window);
        glfwTerminate();
        return 1;
    }

    m_impl->device = deviceData.device;

    // Get queue
    m_impl->queue = wgpuDeviceGetQueue(m_impl->device);

    // Configure surface
    glfwGetFramebufferSize(m_impl->window, &m_impl->width, &m_impl->height);

    // Query surface capabilities to get preferred format
    WGPUSurfaceCapabilities capabilities = {};
    wgpuSurfaceGetCapabilities(m_impl->surface, m_impl->adapter, &capabilities);

    m_impl->surfaceFormat = WGPUTextureFormat_BGRA8Unorm;  // Default
    if (capabilities.formatCount > 0) {
        m_impl->surfaceFormat = capabilities.formats[0];
        std::cout << "Using surface format: " << m_impl->surfaceFormat << std::endl;
    }

    WGPUPresentMode presentMode = WGPUPresentMode_Fifo;  // Default (vsync)
    if (config.headless) {
        presentMode = WGPUPresentMode_Immediate;
    } else if (capabilities.presentModeCount > 0) {
        presentMode = capabilities.presentModes[0];
        std::cout << "Using present mode: " << presentMode << std::endl;
    }

    wgpuSurfaceCapabilitiesFreeMembers(capabilities);

    // Configure surface with frame latency limit
    m_impl->configExtras = {};
    m_impl->configExtras.chain.sType = static_cast<WGPUSType>(WGPUSType_SurfaceConfigurationExtras);
    m_impl->configExtras.desiredMaximumFrameLatency = 2;

    m_impl->config = {};
    m_impl->config.nextInChain = &m_impl->configExtras.chain;
    m_impl->config.device = m_impl->device;
    m_impl->config.format = m_impl->surfaceFormat;
    m_impl->config.width = static_cast<uint32_t>(m_impl->width);
    m_impl->config.height = static_cast<uint32_t>(m_impl->height);
    m_impl->config.presentMode = presentMode;
    m_impl->config.alphaMode = WGPUCompositeAlphaMode_Auto;
    m_impl->config.usage = WGPUTextureUsage_RenderAttachment;
    wgpuSurfaceConfigure(m_impl->surface, &m_impl->config);

    std::cout << "WebGPU initialized successfully!" << std::endl;
    std::cout << "Window size: " << m_impl->width << "x" << m_impl->height << std::endl;

    // Create WindowManager and adopt primary window
    m_impl->windowManager = std::make_unique<WindowManager>(
        m_impl->instance, m_impl->adapter, m_impl->device, m_impl->queue);
    m_impl->windowManager->adoptPrimaryWindow(m_impl->window, m_impl->surface,
                                               m_impl->width, m_impl->height);

    // Create context
    m_impl->ctx = std::make_unique<Context>(m_impl->window, m_impl->device, m_impl->queue);
    m_impl->ctx->setWindowManager(m_impl->windowManager.get());

    // Set render resolution from command-line (or default to window size)
    if (config.renderWidth > 0 && config.renderHeight > 0) {
        m_impl->ctx->setRenderResolution(config.renderWidth, config.renderHeight);
    } else {
        m_impl->ctx->setRenderResolution(config.windowWidth, config.windowHeight);
    }

    // Start in fullscreen if requested
    if (config.startFullscreen) {
        m_impl->ctx->fullscreen(true);
    }

    // Set up scroll callback
    glfwSetWindowUserPointer(m_impl->window, m_impl->ctx.get());
    glfwSetScrollCallback(m_impl->window, [](GLFWwindow* w, double xoffset, double yoffset) {
        Context* c = static_cast<Context*>(glfwGetWindowUserPointer(w));
        if (c) c->addScroll(static_cast<float>(xoffset), static_cast<float>(yoffset));
    });

    // Create display
    m_impl->display = std::make_unique<Display>(m_impl->device, m_impl->queue, m_impl->surfaceFormat);
    if (!m_impl->display->isValid()) {
        std::cerr << "Warning: Display initialization failed (shaders may be missing)" << std::endl;
    }

    // Initialize ImGui
    vivid::imgui::init(m_impl->device, m_impl->queue, m_impl->surfaceFormat);

    // Show UI immediately if requested (for --show-ui flag)
    if (config.showUI) {
        vivid::imgui::setVisible(true);
    }

    // Create chain visualizer
    m_impl->chainVisualizer = std::make_unique<vivid::imgui::ChainVisualizer>();

    // Create hot-reload system
    m_impl->hotReload = std::make_unique<HotReload>();

    // Create editor bridge
    m_impl->editorBridge = std::make_unique<EditorBridge>();
    m_impl->editorBridge->start();
    m_impl->editorBridge->onReloadCommand([this](const std::string&) {
        std::cout << "[EditorBridge] Force reload triggered by editor\n";
        m_impl->hotReload->forceReload();
    });
    m_impl->editorBridge->onParamChange([this](const std::string& opName, const std::string& paramName, const float value[4]) {
        if (!m_impl->ctx->hasChain()) return;
        Operator* op = m_impl->ctx->chain().getByName(opName);
        if (op) {
            op->setParam(paramName, value);
        }
    });
    m_impl->editorBridge->onSoloNode([this](const std::string& opName) {
        if (!m_impl->ctx->hasChain()) return;
        Operator* op = m_impl->ctx->chain().getByName(opName);
        if (op) {
            m_impl->chainVisualizer->enterSoloMode(op, opName);
            m_impl->editorBridge->sendSoloState(true, opName);
        }
    });
    m_impl->editorBridge->onSelectNode([this](const std::string& opName) {
        m_impl->chainVisualizer->selectNodeFromEditor(opName);
    });
    m_impl->editorBridge->onSoloExit([this]() {
        m_impl->chainVisualizer->exitSoloMode();
        m_impl->editorBridge->sendSoloState(false, "");
    });
    m_impl->editorBridge->onFocusedNode([this](const std::string& opName) {
        if (opName.empty()) {
            m_impl->chainVisualizer->clearFocusedNode();
        } else {
            m_impl->chainVisualizer->setFocusedNode(opName);
        }
    });
    m_impl->editorBridge->onWindowControl([this](const std::string& setting, int value) {
        if (setting == "fullscreen") {
            m_impl->ctx->fullscreen(value != 0);
        } else if (setting == "borderless") {
            m_impl->ctx->borderless(value != 0);
        } else if (setting == "alwaysOnTop") {
            m_impl->ctx->alwaysOnTop(value != 0);
        } else if (setting == "cursorVisible") {
            m_impl->ctx->cursorVisible(value != 0);
        } else if (setting == "monitor") {
            m_impl->ctx->moveToMonitor(value);
        }
    });

    // Helper lambdas for editor bridge callbacks
    auto updateSourceLines = [this](const std::string& chainFilePath) {
        if (!m_impl->ctx->hasChain() || chainFilePath.empty()) return;

        fs::path chainFile(chainFilePath);
        if (!fs::exists(chainFile)) return;

        std::ifstream file(chainFile);
        if (!file) return;

        std::regex addPattern("chain\\.add<\\w+>\\s*\\(\\s*\"(\\w+)\"");
        std::string lineStr;
        int lineNum = 0;
        Chain& chain = m_impl->ctx->chain();

        while (std::getline(file, lineStr)) {
            lineNum++;
            std::smatch match;
            if (std::regex_search(lineStr, match, addPattern)) {
                std::string opName = match[1].str();
                Operator* op = chain.getByName(opName);
                if (op) {
                    op->sourceLine = lineNum;
                }
            }
        }
    };

    auto gatherOperatorInfo = [this]() -> std::vector<EditorOperatorInfo> {
        std::vector<EditorOperatorInfo> result;
        if (!m_impl->ctx->hasChain()) return result;

        Chain& chain = m_impl->ctx->chain();
        for (const auto& name : chain.operatorNames()) {
            Operator* op = chain.getByName(name);
            if (!op) continue;

            EditorOperatorInfo info;
            info.chainName = name;
            info.displayName = op->name();
            info.outputType = outputKindName(op->outputKind());
            info.sourceLine = op->sourceLine;

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

    auto gatherParamValues = [this]() -> std::vector<EditorParamInfo> {
        std::vector<EditorParamInfo> result;
        if (!m_impl->ctx->hasChain()) return result;

        Chain& chain = m_impl->ctx->chain();
        for (const auto& name : chain.operatorNames()) {
            Operator* op = chain.getByName(name);
            if (!op) continue;

            for (const auto& decl : op->params()) {
                EditorParamInfo info;
                info.operatorName = name;
                info.paramName = decl.name;
                info.minVal = decl.minVal;
                info.maxVal = decl.maxVal;

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

                op->getParam(decl.name, info.value);
                result.push_back(info);
            }
        }
        return result;
    };

    auto gatherWindowState = [this]() -> EditorWindowState {
        EditorWindowState state;
        state.fullscreen = m_impl->ctx->fullscreen();
        state.borderless = m_impl->ctx->borderless();
        state.alwaysOnTop = m_impl->ctx->alwaysOnTop();
        state.cursorVisible = m_impl->ctx->cursorVisible();
        state.currentMonitor = m_impl->ctx->currentMonitor();

        int monitorCount = 0;
        GLFWmonitor** monitors = glfwGetMonitors(&monitorCount);
        for (int i = 0; i < monitorCount; ++i) {
            EditorMonitorInfo mInfo;
            mInfo.index = i;
            const char* name = glfwGetMonitorName(monitors[i]);
            mInfo.name = name ? name : ("Monitor " + std::to_string(i + 1));
            const GLFWvidmode* mode = glfwGetVideoMode(monitors[i]);
            if (mode) {
                mInfo.width = mode->width;
                mInfo.height = mode->height;
            }
            state.monitors.push_back(mInfo);
        }
        return state;
    };

    m_impl->editorBridge->onRequestOperators([this, gatherOperatorInfo, gatherParamValues, gatherWindowState]() {
        m_impl->editorBridge->sendOperatorList(gatherOperatorInfo());
        m_impl->editorBridge->sendParamValues(gatherParamValues());
        m_impl->editorBridge->sendWindowState(gatherWindowState());
    });

    // Extract project name and set up chain path
    std::string projectName;
    fs::path projectDir;
    if (!config.projectPath.empty()) {
        fs::path chainPath;
        if (fs::is_directory(config.projectPath)) {
            chainPath = config.projectPath / "chain.cpp";
            projectName = config.projectPath.filename().string();
            projectDir = config.projectPath;
        } else if (fs::is_regular_file(config.projectPath)) {
            chainPath = config.projectPath;
            projectName = config.projectPath.parent_path().filename().string();
            projectDir = config.projectPath.parent_path();
        }

        if (!projectDir.empty()) {
            vivid::imgui::setIniDirectory(projectDir.string().c_str());
            AssetLoader::instance().setProjectDir(projectDir);
        }

        if (fs::exists(chainPath)) {
            std::cout << "Loading chain: " << chainPath << std::endl;
            m_impl->hotReload->setSourceFile(chainPath);
            m_impl->ctx->setChainPath(chainPath.string());
        } else {
            m_impl->ctx->setError("Chain file not found: " + chainPath.string());
        }
    } else {
        m_impl->ctx->setError("No chain specified. Usage: vivid <path/to/chain.cpp>");
    }

    // Initialize MainLoopContext
    MainLoopContext& mlc = m_impl->mlc;

    // WebGPU infrastructure
    mlc.instance = m_impl->instance;
    mlc.adapter = m_impl->adapter;
    mlc.surface = m_impl->surface;
    mlc.device = m_impl->device;
    mlc.queue = m_impl->queue;
    mlc.surfaceFormat = m_impl->surfaceFormat;
    mlc.configExtras = m_impl->configExtras;
    mlc.config = m_impl->config;
    mlc.config.nextInChain = &mlc.configExtras.chain;

    // Window state
    mlc.window = m_impl->window;
    mlc.width = m_impl->width;
    mlc.height = m_impl->height;
    mlc.isFullscreen = false;
    glfwGetWindowPos(m_impl->window, &mlc.windowedX, &mlc.windowedY);
    glfwGetWindowSize(m_impl->window, &mlc.windowedWidth, &mlc.windowedHeight);
    mlc.windowManager = m_impl->windowManager.get();

    // Timing
    mlc.lastFpsTime = glfwGetTime();
    mlc.frameCount = 0;
    mlc.lastFrameTime = glfwGetTime();

    // Core runtime objects
    mlc.ctx = m_impl->ctx.get();
    mlc.display = m_impl->display.get();
    mlc.hotReload = m_impl->hotReload.get();
    mlc.chainVisualizer = m_impl->chainVisualizer.get();
    mlc.editorBridge = m_impl->editorBridge.get();

    // CLI args
    mlc.snapshotPath = config.snapshotPath;
    mlc.snapshotFrame = config.snapshotFrame;
    mlc.headless = config.headless;
    mlc.renderWidth = config.renderWidth;
    mlc.renderHeight = config.renderHeight;
    mlc.recordPath = config.recordPath;
    mlc.recordFps = config.recordFps;
    mlc.recordDuration = config.recordDuration;
    mlc.recordAudio = config.recordAudio;
    mlc.recordCodec = config.recordCodec;
    mlc.maxFrames = config.maxFrames;
    mlc.windowWidth = config.windowWidth;
    mlc.windowHeight = config.windowHeight;
    mlc.showUI = config.showUI;

    // Project info
    mlc.projectName = projectName;

    // Assign helper lambdas
    mlc.updateSourceLines = updateSourceLines;
    mlc.gatherOperatorInfo = gatherOperatorInfo;
    mlc.gatherParamValues = gatherParamValues;
    mlc.gatherWindowState = gatherWindowState;

    m_initialized = true;
    return 0;
}

int Application::run() {
    if (!m_initialized || !m_impl) {
        return 1;
    }

    MainLoopContext& mlc = m_impl->mlc;

    // Main loop
    while (!glfwWindowShouldClose(mlc.window)) {
        bool shouldContinue = true;
        platform::withAutoreleasePool([&]() {
            if (!mainLoopIteration(mlc)) {
                shouldContinue = false;
            }
        });
        if (!shouldContinue) break;
    }

    return 0;
}

void Application::shutdown() {
    if (!m_impl) {
        return;
    }

    std::cout << "Shutting down..." << std::endl;

    MainLoopContext& mlc = m_impl->mlc;

    // Stop CLI recording if active
    if (mlc.cliRecorder.isRecording()) {
        std::cout << "Stopping recording: " << mlc.cliRecorder.frameCount() << " frames, "
                  << mlc.cliRecorder.duration() << "s" << std::endl;
        mlc.cliRecorder.stop();
    }

    // Stop editor bridge
    if (m_impl->editorBridge) {
        m_impl->editorBridge->stop();
    }

    // Release chain operators before WebGPU cleanup
    if (m_impl->ctx) {
        m_impl->ctx->resetChain();
    }

    // Shutdown ImGui
    if (m_impl->chainVisualizer) {
        m_impl->chainVisualizer->shutdown();
    }
    vivid::imgui::shutdown();

    // Release display resources
    if (m_impl->display) {
        m_impl->display->shutdown();
    }

    // WebGPU cleanup
    if (m_impl->surface) {
        wgpuSurfaceUnconfigure(m_impl->surface);
    }
    if (m_impl->queue) {
        wgpuQueueRelease(m_impl->queue);
    }
    if (m_impl->device) {
        wgpuDeviceRelease(m_impl->device);
    }
    if (m_impl->adapter) {
        wgpuAdapterRelease(m_impl->adapter);
    }
    if (m_impl->surface) {
        wgpuSurfaceRelease(m_impl->surface);
    }
    if (m_impl->instance) {
        wgpuInstanceRelease(m_impl->instance);
    }
    if (m_impl->window) {
        glfwDestroyWindow(m_impl->window);
    }
    glfwTerminate();

    delete m_impl;
    m_impl = nullptr;
    m_initialized = false;
}

} // namespace vivid
