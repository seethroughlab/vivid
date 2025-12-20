// Vivid - Core Runtime
// Minimal core: window, timing, input, hot-reload, display

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
#include "platform_macos.h"

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
// Main Loop Context
// -----------------------------------------------------------------------------
// Encapsulates all state needed for the main loop iteration.
// This enables future Emscripten web export via emscripten_set_main_loop_arg().

struct MainLoopContext {
    // WebGPU infrastructure
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUSurface surface = nullptr;
    WGPUDevice device = nullptr;
    WGPUQueue queue = nullptr;
    WGPUTextureFormat surfaceFormat = WGPUTextureFormat_BGRA8Unorm;
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

bool mainLoopIteration(MainLoopContext& mlc) {
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

    // Check for hot-reload using safe API:
    // 1. Check if reload needed (without unloading)
    // 2. Destroy chain operators while old code is still loaded
    // 3. Then reload (which unloads old library)
    bool justReloaded = false;
    if (mlc.hotReload->checkNeedsReload()) {
        // Save operator states before destroying chain
        if (mlc.ctx->hasChain()) {
            mlc.ctx->preserveStates(mlc.ctx->chain());
        }
        // Destroy operators BEFORE unloading the library
        // (their destructors are in the dylib code)
        mlc.ctx->clearRegisteredOperators();
        mlc.ctx->resetChain();

        // Now safe to reload (unloads old library, loads new one)
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
            // Chain was already reset before reload (to safely destroy operators)
            // Just call user's setup function
            mlc.hotReload->getSetupFn()(*mlc.ctx);

            // Auto-initialize the chain
            mlc.ctx->chain().init(*mlc.ctx);

            // Honor chain's window size request (if not overridden by command-line)
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

            // Send operator list to connected editors (Phase 2)
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

        // Call user's update function (parameter tweaks)
        mlc.hotReload->getUpdateFn()(*mlc.ctx);

        // Auto-process the chain
        mlc.ctx->chain().process(*mlc.ctx);

        // Capture frame for video export if recording
        if (mlc.chainVisualizer->exporter().isRecording() && mlc.ctx->outputTexture()) {
            // Get the underlying texture from the texture view
            // The output texture should be the chain's final output
            WGPUTexture outputTex = mlc.ctx->chain().outputTexture();
            if (outputTex) {
                mlc.chainVisualizer->exporter().captureFrame(mlc.device, mlc.queue, outputTex);

                // Capture audio if recording with audio
                if (mlc.chainVisualizer->exporter().hasAudio()) {
                    // Generate audio synchronously for this video frame
                    // Calculate audio frames per video frame: 48000Hz / fps
                    float fps = mlc.chainVisualizer->exporter().fps();
                    uint32_t audioFramesPerVideoFrame = static_cast<uint32_t>(AUDIO_SAMPLE_RATE / fps);

                    // Use a static buffer for efficiency
                    static std::vector<float> audioBuffer;
                    if (audioBuffer.size() < audioFramesPerVideoFrame * AUDIO_CHANNELS) {
                        audioBuffer.resize(audioFramesPerVideoFrame * AUDIO_CHANNELS);
                    }

                    // Generate audio deterministically (no race condition)
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

    // Build frame input for ImGui (needed for chain visualizer even if UI hidden)
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

    // Run chain visualizer BEFORE blit so solo mode can override output texture
    if (vivid::imgui::isVisible()) {
        vivid::imgui::beginFrame(frameInput);
        mlc.chainVisualizer->render(frameInput, *mlc.ctx);
    }

    // Blit output texture (may have been modified by solo mode)
    if (mlc.ctx->outputTexture() && mlc.display->isValid()) {
        mlc.display->blit(pass, mlc.ctx->outputTexture());
    }

    // Render ImGui on top of the blit
    if (vivid::imgui::isVisible()) {
        vivid::imgui::render(pass);
    }

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
    // Note: Primary window (0) was already presented above
    if (mlc.windowManager->windowCount() > 1) {
        mlc.windowManager->presentAll(&mlc.ctx->chain(), mlc.ctx->outputTexture());
    }

    // Poll device to process pending GPU work and prevent command buffer accumulation
    // This is critical when multiple operators submit command buffers per frame
    wgpuDevicePoll(mlc.device, false, nullptr);

    // Release texture view after present
    wgpuTextureViewRelease(view);
    // For wgpu-native, release the surface texture AFTER presenting
    // (different from Dawn which releases before present)
    wgpuTextureRelease(surfaceTexture.texture);

    // End frame
    mlc.ctx->endFrame();

    // FPS counter and title update
    mlc.frameCount++;
    double currentTime = glfwGetTime();

    // Track frame time for performance stats
    double frameTimeMs = (currentTime - mlc.lastFrameTime) * 1000.0;
    mlc.lastFrameTime = currentTime;

    // Update frame time history (rolling buffer using deque for O(1) pop_front)
    mlc.perfStats.frameTimeHistory.push_back(static_cast<float>(frameTimeMs));
    if (mlc.perfStats.frameTimeHistory.size() > mlc.kHistorySize) {
        mlc.perfStats.frameTimeHistory.pop_front();
    }
    mlc.perfStats.frameTimeMs = static_cast<float>(frameTimeMs);

    if (currentTime - mlc.lastFpsTime >= 1.0) {
        std::string title;
        if (!mlc.projectName.empty()) {
            title = mlc.projectName + " - Vivid (" + std::to_string(mlc.frameCount) + " fps)";
        } else {
            title = "Vivid (" + std::to_string(mlc.frameCount) + " fps)";
        }
        glfwSetWindowTitle(mlc.window, title.c_str());

        // Update FPS in performance stats (using deque for O(1) pop_front)
        mlc.perfStats.fps = static_cast<float>(mlc.frameCount);
        mlc.perfStats.fpsHistory.push_back(mlc.perfStats.fps);
        if (mlc.perfStats.fpsHistory.size() > mlc.kHistorySize) {
            mlc.perfStats.fpsHistory.pop_front();
        }

        mlc.frameCount = 0;
        mlc.lastFpsTime = currentTime;

        // Send param values, window state, and performance stats to editors once per second
        if (mlc.editorBridge->clientCount() > 0 && mlc.hotReload->isLoaded()) {
            if (mlc.gatherParamValues) {
                mlc.editorBridge->sendParamValues(mlc.gatherParamValues());
            }
            if (mlc.gatherWindowState) {
                mlc.editorBridge->sendWindowState(mlc.gatherWindowState());
            }

            // Gather operator count and estimate texture memory
            mlc.perfStats.operatorCount = mlc.ctx->hasChain() ? mlc.ctx->chain().operatorNames().size() : 0;

            // Estimate texture memory: count operators and assume average texture size
            // More accurate would require querying each texture operator
            size_t textureOpCount = 0;
            if (mlc.ctx->hasChain()) {
                for (const auto& name : mlc.ctx->chain().operatorNames()) {
                    Operator* op = mlc.ctx->chain().getByName(name);
                    if (op && op->outputKind() == OutputKind::Texture) {
                        textureOpCount++;
                    }
                }
            }
            // Estimate: each texture is width*height*4 bytes (RGBA8)
            mlc.perfStats.textureMemoryBytes = textureOpCount * mlc.ctx->width() * mlc.ctx->height() * 4;

            mlc.editorBridge->sendPerformanceStats(mlc.perfStats);
        }
    }

    return true;  // continue running
}

// -----------------------------------------------------------------------------
// Main
// -----------------------------------------------------------------------------

int main(int argc, char** argv) {
    // Handle CLI commands first (vivid new, --help, --version, addons)
    // These don't require GPU initialization
    int cliResult = vivid::cli::handleCommand(argc, argv);
    if (cliResult >= 0) {
        return cliResult;  // CLI command was handled
    }

    std::cout << "Vivid - Starting..." << std::endl;

    // Load user-installed addons from ~/.vivid/addons/
    // This must happen before chain loading so operators are registered
    AddonManager::instance().loadUserAddons();

    // Parse arguments
    fs::path projectPath;
    std::string snapshotPath;
    int snapshotFrame = 5;  // Default: capture after 5 frames to allow warm-up
    bool headless = false;
    int windowWidth = 1280;
    int windowHeight = 720;
    int renderWidth = 0;   // 0 = use window size
    int renderHeight = 0;
    bool startFullscreen = false;

    // Video recording options
    std::string recordPath;
    float recordFps = 60.0f;
    float recordDuration = 0.0f;  // 0 = unlimited
    bool recordAudio = false;
    ExportCodec recordCodec = ExportCodec::H264;

    // Frame limit option
    int maxFrames = 0;  // 0 = unlimited

    // Helper to parse WxH format
    auto parseSize = [](const std::string& s, int& w, int& h) {
        size_t x = s.find('x');
        if (x != std::string::npos) {
            w = std::atoi(s.substr(0, x).c_str());
            h = std::atoi(s.substr(x + 1).c_str());
        }
    };

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--snapshot" && i + 1 < argc) {
            snapshotPath = argv[++i];
        } else if (arg.rfind("--snapshot=", 0) == 0) {
            snapshotPath = arg.substr(11);
        } else if (arg == "--snapshot-frame" && i + 1 < argc) {
            snapshotFrame = std::atoi(argv[++i]);
        } else if (arg.rfind("--snapshot-frame=", 0) == 0) {
            snapshotFrame = std::atoi(arg.substr(17).c_str());
        } else if (arg == "--headless") {
            headless = true;
        } else if (arg == "--window" && i + 1 < argc) {
            parseSize(argv[++i], windowWidth, windowHeight);
        } else if (arg.rfind("--window=", 0) == 0) {
            parseSize(arg.substr(9), windowWidth, windowHeight);
        } else if (arg == "--render" && i + 1 < argc) {
            parseSize(argv[++i], renderWidth, renderHeight);
        } else if (arg.rfind("--render=", 0) == 0) {
            parseSize(arg.substr(9), renderWidth, renderHeight);
        } else if (arg == "--fullscreen") {
            startFullscreen = true;
        } else if (arg == "--record" && i + 1 < argc) {
            recordPath = argv[++i];
        } else if (arg.rfind("--record=", 0) == 0) {
            recordPath = arg.substr(9);
        } else if (arg == "--record-fps" && i + 1 < argc) {
            recordFps = std::stof(argv[++i]);
        } else if (arg.rfind("--record-fps=", 0) == 0) {
            recordFps = std::stof(arg.substr(13));
        } else if (arg == "--record-duration" && i + 1 < argc) {
            recordDuration = std::stof(argv[++i]);
        } else if (arg.rfind("--record-duration=", 0) == 0) {
            recordDuration = std::stof(arg.substr(18));
        } else if (arg == "--record-audio") {
            recordAudio = true;
        } else if (arg == "--record-codec" && i + 1 < argc) {
            std::string codec = argv[++i];
            if (codec == "h265" || codec == "hevc") recordCodec = ExportCodec::H265;
            else if (codec == "prores" || codec == "animation") recordCodec = ExportCodec::Animation;
            else recordCodec = ExportCodec::H264;
        } else if (arg.rfind("--record-codec=", 0) == 0) {
            std::string codec = arg.substr(15);
            if (codec == "h265" || codec == "hevc") recordCodec = ExportCodec::H265;
            else if (codec == "prores" || codec == "animation") recordCodec = ExportCodec::Animation;
            else recordCodec = ExportCodec::H264;
        } else if (arg == "--frames" && i + 1 < argc) {
            maxFrames = std::atoi(argv[++i]);
        } else if (arg.rfind("--frames=", 0) == 0) {
            maxFrames = std::atoi(arg.substr(9).c_str());
        } else if (arg[0] != '-') {
            // Non-flag argument is the project path
            projectPath = arg;
        }
    }

    // Headless mode validation
    if (headless) {
        if (snapshotPath.empty() && recordPath.empty() && maxFrames == 0) {
            std::cerr << "Warning: --headless without --snapshot, --record, or --frames will run indefinitely.\n";
            std::cerr << "         Use Ctrl+C to stop or add one of these options to capture output.\n";
        }
        std::cout << "Running in headless mode" << std::endl;
    }

    // Initialize GLFW
    if (!glfwInit()) {
        std::cerr << "Failed to initialize GLFW" << std::endl;
        return 1;
    }

    // No OpenGL context - we're using WebGPU
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    // Headless mode: create invisible window
    if (headless) {
        glfwWindowHint(GLFW_VISIBLE, GLFW_FALSE);
    }

    // Create window
    GLFWwindow* window = glfwCreateWindow(windowWidth, windowHeight, "Vivid", nullptr, nullptr);
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

    WGPUPresentMode presentMode = WGPUPresentMode_Fifo;  // Default (vsync)
    if (headless) {
        // Headless: disable vsync for maximum speed
        presentMode = WGPUPresentMode_Immediate;
    } else if (capabilities.presentModeCount > 0) {
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

    // Create WindowManager and adopt primary window
    WindowManager windowManager(instance, adapter, device, queue);
    windowManager.adoptPrimaryWindow(window, surface, width, height);

    // Create context
    Context ctx(window, device, queue);
    ctx.setWindowManager(&windowManager);

    // Set render resolution from command-line (or default to window size)
    if (renderWidth > 0 && renderHeight > 0) {
        ctx.setRenderResolution(renderWidth, renderHeight);
    } else {
        ctx.setRenderResolution(windowWidth, windowHeight);
    }

    // Start in fullscreen if requested
    if (startFullscreen) {
        ctx.fullscreen(true);
    }

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

    editorBridge.onSoloNode([&ctx, &chainVisualizer, &editorBridge](const std::string& opName) {
        if (!ctx.hasChain()) return;
        Operator* op = ctx.chain().getByName(opName);
        if (op) {
            chainVisualizer.enterSoloMode(op, opName);
            editorBridge.sendSoloState(true, opName);
        }
    });

    editorBridge.onSelectNode([&chainVisualizer](const std::string& opName) {
        chainVisualizer.selectNodeFromEditor(opName);
    });

    editorBridge.onSoloExit([&chainVisualizer, &editorBridge]() {
        chainVisualizer.exitSoloMode();
        editorBridge.sendSoloState(false, "");
    });

    editorBridge.onFocusedNode([&chainVisualizer](const std::string& opName) {
        if (opName.empty()) {
            chainVisualizer.clearFocusedNode();
        } else {
            chainVisualizer.setFocusedNode(opName);
        }
    });

    // Handle window control commands from editor (Phase 14)
    editorBridge.onWindowControl([&ctx](const std::string& setting, int value) {
        if (setting == "fullscreen") {
            ctx.fullscreen(value != 0);
        } else if (setting == "borderless") {
            ctx.borderless(value != 0);
        } else if (setting == "alwaysOnTop") {
            ctx.alwaysOnTop(value != 0);
        } else if (setting == "cursorVisible") {
            ctx.cursorVisible(value != 0);
        } else if (setting == "monitor") {
            ctx.moveToMonitor(value);
        }
    });

    // Helper to parse chain.cpp and update operator source lines
    auto updateSourceLines = [&ctx](const std::string& chainFilePath) {
        if (!ctx.hasChain() || chainFilePath.empty()) return;

        fs::path chainFile(chainFilePath);
        if (!fs::exists(chainFile)) return;

        std::ifstream file(chainFile);
        if (!file) return;

        // Regex to match: chain.add<Type>("name") or auto& name = chain.add<Type>("name")
        std::regex addPattern("chain\\.add<\\w+>\\s*\\(\\s*\"(\\w+)\"");

        std::string lineStr;
        int lineNum = 0;
        Chain& chain = ctx.chain();

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

    // Helper to gather operator info from chain
    auto gatherOperatorInfo = [&ctx]() -> std::vector<EditorOperatorInfo> {
        std::vector<EditorOperatorInfo> result;
        if (!ctx.hasChain()) {
            std::cout << "[EditorBridge] gatherOperatorInfo: no chain\n";
            return result;
        }

        Chain& chain = ctx.chain();
        std::cout << "[EditorBridge] gatherOperatorInfo: " << chain.operatorNames().size() << " operators\n";
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
        std::cout << "[EditorBridge] gatherOperatorInfo: returning " << result.size() << " operators\n";
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

    // Helper to gather window state (Phase 14)
    auto gatherWindowState = [&ctx, window]() -> EditorWindowState {
        EditorWindowState state;
        state.fullscreen = ctx.fullscreen();
        state.borderless = ctx.borderless();
        state.alwaysOnTop = ctx.alwaysOnTop();
        state.cursorVisible = ctx.cursorVisible();
        state.currentMonitor = ctx.currentMonitor();

        // Gather monitor info
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

    // Handle request_operators from editor (sends current operator list on demand)
    editorBridge.onRequestOperators([&editorBridge, &gatherOperatorInfo, &gatherParamValues, &gatherWindowState]() {
        editorBridge.sendOperatorList(gatherOperatorInfo());
        editorBridge.sendParamValues(gatherParamValues());
        editorBridge.sendWindowState(gatherWindowState());
    });

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

    // Initialize MainLoopContext
    MainLoopContext mlc;

    // WebGPU infrastructure
    mlc.instance = instance;
    mlc.adapter = adapter;
    mlc.surface = surface;
    mlc.device = device;
    mlc.queue = queue;
    mlc.surfaceFormat = surfaceFormat;
    mlc.config = config;

    // Window state
    mlc.window = window;
    mlc.width = width;
    mlc.height = height;
    mlc.isFullscreen = false;
    glfwGetWindowPos(window, &mlc.windowedX, &mlc.windowedY);
    glfwGetWindowSize(window, &mlc.windowedWidth, &mlc.windowedHeight);
    mlc.windowManager = &windowManager;

    // Timing
    mlc.lastFpsTime = glfwGetTime();
    mlc.frameCount = 0;
    mlc.lastFrameTime = glfwGetTime();

    // Core runtime objects
    mlc.ctx = &ctx;
    mlc.display = &display;
    mlc.hotReload = &hotReload;
    mlc.chainVisualizer = &chainVisualizer;
    mlc.editorBridge = &editorBridge;

    // CLI args
    mlc.snapshotPath = snapshotPath;
    mlc.snapshotFrame = snapshotFrame;
    mlc.headless = headless;
    mlc.renderWidth = renderWidth;
    mlc.renderHeight = renderHeight;
    mlc.recordPath = recordPath;
    mlc.recordFps = recordFps;
    mlc.recordDuration = recordDuration;
    mlc.recordAudio = recordAudio;
    mlc.recordCodec = recordCodec;
    mlc.maxFrames = maxFrames;
    mlc.windowWidth = windowWidth;
    mlc.windowHeight = windowHeight;

    // Project info
    mlc.projectName = projectName;

    // Assign helper lambdas to MainLoopContext
    mlc.updateSourceLines = updateSourceLines;
    mlc.gatherOperatorInfo = gatherOperatorInfo;
    mlc.gatherParamValues = gatherParamValues;
    mlc.gatherWindowState = gatherWindowState;

    // Main loop
    // Each frame is wrapped in an autoreleasepool to prevent Metal/WebGPU memory leaks on macOS
    while (!glfwWindowShouldClose(mlc.window)) {
        bool shouldContinue = true;
        platform::withAutoreleasePool([&]() {
            if (!mainLoopIteration(mlc)) {
                shouldContinue = false;
            }
        });
        if (!shouldContinue) break;
    }

    // Cleanup
    std::cout << "Shutting down..." << std::endl;

    // Stop CLI recording if active
    if (mlc.cliRecorder.isRecording()) {
        std::cout << "Stopping recording: " << mlc.cliRecorder.frameCount() << " frames, "
                  << mlc.cliRecorder.duration() << "s" << std::endl;
        mlc.cliRecorder.stop();
    }

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
