/**
 * @file vivid_c.cpp
 * @brief C API implementation for Vivid Core
 */

#include <vivid/vivid_c.h>
#include <vivid/context.h>
#include <vivid/chain.h>
#include <vivid/operator.h>
#include <vivid/hot_reload.h>
#include <vivid/asset_loader.h>
#include <vivid/video_exporter.h>
#include <vivid/operator_registry.h>
#include <vivid/chain_visualizer.h>
#include <vivid/display.h>
#include <vivid/frame_input.h>
#include <vivid/render_lock.h>

#include <string>
#include <mutex>
#include <cstring>
#include <chrono>

// Platform-specific window functions (implemented in vivid_c_window.mm for macOS)
#ifdef __APPLE__
extern "C" {
    WGPUSurface vivid_create_surface_from_nswindow(WGPUInstance instance, void* ns_window);
    float vivid_get_window_scale_factor(void* ns_window);
    void vivid_get_window_size(void* ns_window, int* out_width, int* out_height);
    void vivid_begin_frame_transaction();
    void vivid_commit_frame_transaction();
}
#endif

// Thread-local error message
static thread_local std::string s_lastError;

// =============================================================================
// Internal helper functions
// =============================================================================

static void setError(const char* msg) {
    s_lastError = msg ? msg : "";
}

static void setError(const std::string& msg) {
    s_lastError = msg;
}

static vivid::Context* toContext(VividContext* ctx) {
    return reinterpret_cast<vivid::Context*>(ctx);
}

static VividContext* fromContext(vivid::Context* ctx) {
    return reinterpret_cast<VividContext*>(ctx);
}

static vivid::Chain* toChain(VividChain* chain) {
    return reinterpret_cast<vivid::Chain*>(chain);
}

static VividChain* fromChain(vivid::Chain* chain) {
    return reinterpret_cast<VividChain*>(chain);
}

static vivid::Operator* toOperator(VividOperator* op) {
    return reinterpret_cast<vivid::Operator*>(op);
}

static VividOperator* fromOperator(vivid::Operator* op) {
    return reinterpret_cast<VividOperator*>(op);
}

// Convert C++ OutputKind to C enum
static VividOutputKind convertOutputKind(vivid::OutputKind kind) {
    switch (kind) {
        case vivid::OutputKind::Texture:    return VIVID_OUTPUT_TEXTURE;
        case vivid::OutputKind::CpuPixels:  return VIVID_OUTPUT_CPU_PIXELS;
        case vivid::OutputKind::Value:      return VIVID_OUTPUT_VALUE;
        case vivid::OutputKind::ValueArray: return VIVID_OUTPUT_VALUE_ARRAY;
        case vivid::OutputKind::Geometry:   return VIVID_OUTPUT_GEOMETRY;
        case vivid::OutputKind::Camera:     return VIVID_OUTPUT_CAMERA;
        case vivid::OutputKind::Light:      return VIVID_OUTPUT_LIGHT;
        case vivid::OutputKind::Audio:      return VIVID_OUTPUT_AUDIO;
        case vivid::OutputKind::AudioValue: return VIVID_OUTPUT_AUDIO_VALUE;
        case vivid::OutputKind::Event:      return VIVID_OUTPUT_EVENT;
        default:                            return VIVID_OUTPUT_TEXTURE;
    }
}

// Convert C++ ParamType to C enum
static VividParamType convertParamType(vivid::ParamType type) {
    switch (type) {
        case vivid::ParamType::Float:      return VIVID_PARAM_FLOAT;
        case vivid::ParamType::Int:        return VIVID_PARAM_INT;
        case vivid::ParamType::Bool:       return VIVID_PARAM_BOOL;
        case vivid::ParamType::Vec2:       return VIVID_PARAM_VEC2;
        case vivid::ParamType::Vec3:       return VIVID_PARAM_VEC3;
        case vivid::ParamType::Vec4:       return VIVID_PARAM_VEC4;
        case vivid::ParamType::Color:      return VIVID_PARAM_COLOR;
        case vivid::ParamType::String:     return VIVID_PARAM_STRING;
        case vivid::ParamType::FilePath:   return VIVID_PARAM_FILE_PATH;
        case vivid::ParamType::Enum:       return VIVID_PARAM_ENUM;
        case vivid::ParamType::ADSR:       return VIVID_PARAM_ADSR;
        case vivid::ParamType::DeviceList: return VIVID_PARAM_DEVICE_LIST;
        default:                           return VIVID_PARAM_FLOAT;
    }
}

// =============================================================================
// Internal context wrapper (holds HotReload for project loading)
// =============================================================================

struct VividContextInternal {
    vivid::Context* context = nullptr;
    std::unique_ptr<vivid::HotReload> hotReload;
    std::string projectPath;
    std::string compileError;
    bool hasProject = false;

    // Window-based context state
    bool ownsGpuResources = false;     // True if we created the instance/device/surface
    void* nativeWindow = nullptr;       // Native window handle (NSWindow*, HWND, etc.)
    WGPUInstance instance = nullptr;
    WGPUAdapter adapter = nullptr;
    WGPUSurface surface = nullptr;
    WGPUSurfaceConfiguration surfaceConfig = {};

    // Visualizer and display
    std::unique_ptr<vivid::ChainVisualizer> visualizer;
    std::unique_ptr<vivid::Display> display;
    bool visualizerVisible = true;

    // Frame timing
    std::chrono::steady_clock::time_point lastFrameTime;
    bool firstFrame = true;

    // Re-entrancy guard for rendering
    bool isRendering = false;

    ~VividContextInternal() {
        // Clean up visualizer first
        if (visualizer) {
            visualizer->shutdown();
            visualizer.reset();
        }

        delete context;

        // Clean up GPU resources if we own them
        if (ownsGpuResources) {
            if (surface) {
                wgpuSurfaceUnconfigure(surface);
                wgpuSurfaceRelease(surface);
            }
            if (adapter) {
                wgpuAdapterRelease(adapter);
            }
            if (instance) {
                wgpuInstanceRelease(instance);
            }
        }
    }
};

static VividContextInternal* toInternal(VividContext* ctx) {
    return reinterpret_cast<VividContextInternal*>(ctx);
}

static VividContext* fromInternal(VividContextInternal* internal) {
    return reinterpret_cast<VividContext*>(internal);
}

// =============================================================================
// Error Handling
// =============================================================================

VIVID_C_API const char* vivid_get_last_error(void) {
    return s_lastError.empty() ? nullptr : s_lastError.c_str();
}

VIVID_C_API void vivid_clear_error(void) {
    s_lastError.clear();
}

// =============================================================================
// Context Lifecycle
// =============================================================================

VIVID_C_API VividResult vivid_context_create_external(
    VividWGPUDevice device,
    VividWGPUQueue queue,
    const VividContextConfig* config,
    VividContext** out_ctx
) {
    if (!device || !queue || !out_ctx) {
        setError("Invalid argument: device, queue, and out_ctx must not be NULL");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    if (!config) {
        setError("Invalid argument: config must not be NULL");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    try {
        auto* internal = new VividContextInternal();
        internal->context = new vivid::Context(
            static_cast<WGPUDevice>(device),
            static_cast<WGPUQueue>(queue),
            config->width,
            config->height
        );
        internal->hotReload = std::make_unique<vivid::HotReload>();

        *out_ctx = fromInternal(internal);
        return VIVID_OK;
    } catch (const std::exception& e) {
        setError(std::string("Failed to create context: ") + e.what());
        return VIVID_ERROR_INTERNAL;
    }
}

// =============================================================================
// Window-based Context Creation
// =============================================================================

// Adapter request callback state
struct AdapterRequestData {
    WGPUAdapter adapter = nullptr;
    bool done = false;
};

static void onAdapterRequestEnded(WGPURequestAdapterStatus status,
                                   WGPUAdapter adapter,
                                   WGPUStringView message,
                                   void* userdata1,
                                   void* userdata2) {
    (void)userdata2;
    auto* data = static_cast<AdapterRequestData*>(userdata1);
    if (status == WGPURequestAdapterStatus_Success) {
        data->adapter = adapter;
    }
    data->done = true;
}

// Device request callback state
struct DeviceRequestData {
    WGPUDevice device = nullptr;
    bool done = false;
};

static void onDeviceRequestEnded(WGPURequestDeviceStatus status,
                                  WGPUDevice device,
                                  WGPUStringView message,
                                  void* userdata1,
                                  void* userdata2) {
    (void)userdata2;
    auto* data = static_cast<DeviceRequestData*>(userdata1);
    if (status == WGPURequestDeviceStatus_Success) {
        data->device = device;
    }
    data->done = true;
}

VIVID_C_API VividResult vivid_context_create_with_window(
    void* native_window,
    const VividContextConfig* config,
    VividContext** out_ctx
) {
    if (!native_window || !out_ctx) {
        setError("Invalid argument: native_window and out_ctx must not be NULL");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    if (!config) {
        setError("Invalid argument: config must not be NULL");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

#ifdef __APPLE__
    try {
        auto* internal = new VividContextInternal();
        internal->nativeWindow = native_window;
        internal->ownsGpuResources = true;

        // Create WebGPU instance
        WGPUInstanceDescriptor instanceDesc = {};
        internal->instance = wgpuCreateInstance(&instanceDesc);
        if (!internal->instance) {
            delete internal;
            setError("Failed to create WebGPU instance");
            return VIVID_ERROR_INTERNAL;
        }

        // Create surface from native window
        internal->surface = vivid_create_surface_from_nswindow(internal->instance, native_window);
        if (!internal->surface) {
            delete internal;
            setError("Failed to create surface from native window");
            return VIVID_ERROR_INTERNAL;
        }

        // Request adapter
        WGPURequestAdapterOptions adapterOpts = {};
        adapterOpts.compatibleSurface = internal->surface;
        adapterOpts.powerPreference = WGPUPowerPreference_HighPerformance;

        AdapterRequestData adapterData;
        WGPURequestAdapterCallbackInfo adapterCallback = {};
        adapterCallback.mode = WGPUCallbackMode_AllowSpontaneous;
        adapterCallback.callback = onAdapterRequestEnded;
        adapterCallback.userdata1 = &adapterData;
        wgpuInstanceRequestAdapter(internal->instance, &adapterOpts, adapterCallback);

        // Wait for adapter (wgpu-native is synchronous on native platforms)
        while (!adapterData.done) {
            // Busy wait - on native platforms this returns immediately
        }

        internal->adapter = adapterData.adapter;
        if (!internal->adapter) {
            delete internal;
            setError("Failed to get WebGPU adapter");
            return VIVID_ERROR_INTERNAL;
        }

        // Request device
        WGPUDeviceDescriptor deviceDesc = {};
        deviceDesc.label = (WGPUStringView){ "Vivid Device", WGPU_STRLEN };

        DeviceRequestData deviceData;
        WGPURequestDeviceCallbackInfo deviceCallback = {};
        deviceCallback.mode = WGPUCallbackMode_AllowSpontaneous;
        deviceCallback.callback = onDeviceRequestEnded;
        deviceCallback.userdata1 = &deviceData;
        wgpuAdapterRequestDevice(internal->adapter, &deviceDesc, deviceCallback);

        while (!deviceData.done) {
            // Busy wait
        }

        WGPUDevice device = deviceData.device;
        if (!device) {
            delete internal;
            setError("Failed to create WebGPU device");
            return VIVID_ERROR_INTERNAL;
        }

        WGPUQueue queue = wgpuDeviceGetQueue(device);

        // Get window size
        int width, height;
        vivid_get_window_size(native_window, &width, &height);
        if (width == 0 || height == 0) {
            width = config->width > 0 ? config->width : 1280;
            height = config->height > 0 ? config->height : 720;
        }

        // Configure surface
        WGPUSurfaceCapabilities capabilities = {};
        wgpuSurfaceGetCapabilities(internal->surface, internal->adapter, &capabilities);

        // Choose format (prefer BGRA8Unorm for macOS)
        WGPUTextureFormat surfaceFormat = WGPUTextureFormat_BGRA8UnormSrgb;
        for (size_t i = 0; i < capabilities.formatCount; ++i) {
            if (capabilities.formats[i] == WGPUTextureFormat_BGRA8UnormSrgb) {
                surfaceFormat = capabilities.formats[i];
                break;
            } else if (capabilities.formats[i] == WGPUTextureFormat_BGRA8Unorm) {
                surfaceFormat = capabilities.formats[i];
            }
        }

        internal->surfaceConfig.device = device;
        internal->surfaceConfig.format = surfaceFormat;
        internal->surfaceConfig.usage = WGPUTextureUsage_RenderAttachment;
        internal->surfaceConfig.width = static_cast<uint32_t>(width);
        internal->surfaceConfig.height = static_cast<uint32_t>(height);
        // Use Fifo (vsync) - standard present mode that's always supported
        internal->surfaceConfig.presentMode = WGPUPresentMode_Fifo;
        internal->surfaceConfig.alphaMode = WGPUCompositeAlphaMode_Auto;

        wgpuSurfaceCapabilitiesFreeMembers(capabilities);
        wgpuSurfaceConfigure(internal->surface, &internal->surfaceConfig);

        // Create vivid Context
        internal->context = new vivid::Context(device, queue, width, height);
        internal->hotReload = std::make_unique<vivid::HotReload>();

        // Create display for blitting chain output
        internal->display = std::make_unique<vivid::Display>(
            internal->context->device(),
            internal->context->queue(),
            surfaceFormat
        );
        internal->display->setScreenSize(config->width, config->height);
        internal->display->setTextureSize(config->width, config->height);

        // Create visualizer
        internal->visualizer = std::make_unique<vivid::ChainVisualizer>();
        internal->visualizer->init();
        internal->visualizer->initNodeGraph(*internal->context, surfaceFormat);

        internal->lastFrameTime = std::chrono::steady_clock::now();
        internal->firstFrame = true;

        *out_ctx = fromInternal(internal);
        return VIVID_OK;
    } catch (const std::exception& e) {
        setError(std::string("Failed to create window context: ") + e.what());
        return VIVID_ERROR_INTERNAL;
    }
#else
    setError("Window-based context not implemented for this platform");
    return VIVID_ERROR_INTERNAL;
#endif
}

VIVID_C_API VividResult vivid_context_render_frame(VividContext* ctx) {
    if (!ctx) {
        setError("Invalid argument");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    auto* internal = toInternal(ctx);

    if (!internal->ownsGpuResources) {
        setError("render_frame only valid for window-based contexts");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    // Re-entrancy guard - skip if already rendering
    if (internal->isRendering) {
        return VIVID_OK;
    }

    // Check global render lock (set by video during loop transitions)
    if (vivid::RenderLock::instance().isLocked()) {
        return VIVID_OK;
    }

    internal->isRendering = true;

    // RAII guard to reset isRendering on exit
    struct RenderGuard {
        VividContextInternal* ctx;
        ~RenderGuard() { ctx->isRendering = false; }
    } guard{internal};

    try {
        // Calculate delta time
        auto now = std::chrono::steady_clock::now();
        float dt = 0.016f; // Default to ~60fps
        if (!internal->firstFrame) {
            auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
                now - internal->lastFrameTime);
            dt = duration.count() / 1000000.0f;
        }
        internal->lastFrameTime = now;
        internal->firstFrame = false;

        // Check surface is valid before accessing
        if (!internal->surface) {
            setError("Surface is null");
            return VIVID_ERROR_INTERNAL;
        }

        // Get current surface texture
        WGPUSurfaceTexture surfaceTexture = {};
        surfaceTexture.texture = nullptr;
        surfaceTexture.status = WGPUSurfaceGetCurrentTextureStatus_Error;

        wgpuSurfaceGetCurrentTexture(internal->surface, &surfaceTexture);

        // Check for various failure states
        if (surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_Error ||
            surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_Lost ||
            surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_OutOfMemory ||
            surfaceTexture.status == WGPUSurfaceGetCurrentTextureStatus_DeviceLost) {
            // Surface in error state - try to reconfigure
            std::cerr << "[vivid_c] Surface texture error: status=" << surfaceTexture.status << std::endl;
            wgpuSurfaceConfigure(internal->surface, &internal->surfaceConfig);
            return VIVID_OK;
        }

        if (surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
            surfaceTexture.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
            // Surface not ready, skip frame
            return VIVID_OK;
        }

        if (!surfaceTexture.texture) {
            // No texture returned even with success status
            return VIVID_OK;
        }

        WGPUTextureViewDescriptor viewDesc = {};
        viewDesc.format = internal->surfaceConfig.format;
        viewDesc.dimension = WGPUTextureViewDimension_2D;
        viewDesc.baseMipLevel = 0;
        viewDesc.mipLevelCount = 1;
        viewDesc.baseArrayLayer = 0;
        viewDesc.arrayLayerCount = 1;
        viewDesc.aspect = WGPUTextureAspect_All;
        WGPUTextureView targetView = wgpuTextureCreateView(surfaceTexture.texture, &viewDesc);

        // Build frame input for visualizer BEFORE processing
        // (Need to capture scroll before endFrame clears it)
        vivid::FrameInput frameInput;
        frameInput.width = static_cast<int>(internal->surfaceConfig.width);
        frameInput.height = static_cast<int>(internal->surfaceConfig.height);
        frameInput.contentScale = 1.0f;
#ifdef __APPLE__
        if (internal->nativeWindow) {
            frameInput.contentScale = vivid_get_window_scale_factor(internal->nativeWindow);
        }
#endif
        // Copy input state from context (before endFrame clears scroll)
        frameInput.mousePos = internal->context->mouse();
        frameInput.mouseDown[0] = internal->context->mouseButton(0).held;
        frameInput.mouseDown[1] = internal->context->mouseButton(1).held;
        frameInput.mouseDown[2] = internal->context->mouseButton(2).held;
        frameInput.scroll = internal->context->scroll();
        frameInput.dt = dt;
        frameInput.time = static_cast<float>(internal->context->time());
        frameInput.surfaceFormat = internal->surfaceConfig.format;

        // Process chain if project loaded
        if (internal->hasProject && internal->context->hasChain()) {
            internal->context->injectDeltaTime(dt);
            internal->context->beginFrame();

            // Call user's update function
            auto updateFn = internal->hotReload->getUpdateFn();
            if (updateFn) {
                updateFn(*internal->context);
            }

            // Process the chain (runs all operators)
            internal->context->chain().process(*internal->context);

            internal->context->endFrame();

            // Update solo mode output if active
            if (internal->visualizer) {
                internal->visualizer->updateSoloOutput(*internal->context);
            }
        }

        // Create render pass for final output
        WGPUCommandEncoderDescriptor encoderDesc = {};
        WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(
            internal->context->device(), &encoderDesc);

        // Clear and blit chain output to surface
        WGPURenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = targetView;
        colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = {0.1, 0.1, 0.12, 1.0};

        WGPURenderPassDescriptor passDesc = {};
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;

        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

        // Update display dimensions
        if (internal->display) {
            internal->display->setScreenSize(
                static_cast<int>(internal->surfaceConfig.width),
                static_cast<int>(internal->surfaceConfig.height)
            );
        }

        // Update solo mode output before blit
        if (internal->visualizerVisible && internal->visualizer) {
            internal->visualizer->updateSoloOutput(*internal->context);
        }

        // Blit chain output texture to surface
        WGPUTextureView outputView = internal->context->outputTexture();

        // Debug: log state on first few frames
        static int debugFrameCount = 0;
        if (debugFrameCount < 5) {
            bool hasChain = internal->context->hasChain();
            std::cout << "[vivid_c] Frame " << debugFrameCount
                      << " hasProject=" << internal->hasProject
                      << " hasChain=" << hasChain
                      << " outputView=" << (outputView ? "yes" : "null")
                      << " display=" << (internal->display ? "yes" : "null")
                      << std::endl;
            if (hasChain) {
                WGPUTexture chainOut = internal->context->chain().outputTexture();
                std::cout << "  chainOutputTex=" << (chainOut ? "yes" : "null") << std::endl;
            }
            debugFrameCount++;
        }

        if (internal->hasProject && outputView && internal->display) {
            internal->display->setTextureSize(
                internal->context->renderWidth(),
                internal->context->renderHeight()
            );
            internal->display->blit(pass, outputView);
        }

        // Render visualizer if visible
        if (internal->visualizerVisible && internal->visualizer) {
            internal->visualizer->renderNodeGraph(pass, frameInput, *internal->context);
        }

        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);

        // Submit
        WGPUCommandBufferDescriptor cmdDesc = {};
        WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
        wgpuQueueSubmit(internal->context->queue(), 1, &cmdBuffer);
        wgpuCommandBufferRelease(cmdBuffer);
        wgpuCommandEncoderRelease(encoder);

        // Present the frame
        wgpuSurfacePresent(internal->surface);

        wgpuTextureViewRelease(targetView);
        wgpuTextureRelease(surfaceTexture.texture);

        return VIVID_OK;
    } catch (const std::exception& e) {
        setError(e.what());
        return VIVID_ERROR_INTERNAL;
    } catch (...) {
        // Catch any other C++ exceptions (Objective-C exceptions, etc.)
        setError("Unknown exception in render_frame");
        return VIVID_ERROR_INTERNAL;
    }
}

VIVID_C_API VividResult vivid_context_resize_surface(VividContext* ctx, int width, int height) {
    if (!ctx) {
        setError("Invalid argument");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    auto* internal = toInternal(ctx);

    if (!internal->ownsGpuResources) {
        setError("resize_surface only valid for window-based contexts");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    if (width <= 0 || height <= 0) {
        return VIVID_OK; // Ignore zero-size
    }

    internal->surfaceConfig.width = static_cast<uint32_t>(width);
    internal->surfaceConfig.height = static_cast<uint32_t>(height);
    wgpuSurfaceConfigure(internal->surface, &internal->surfaceConfig);

    // Update context resolution too
    internal->context->setRenderResolution(width, height);

    // Update display dimensions
    if (internal->display) {
        internal->display->setScreenSize(width, height);
        internal->display->setTextureSize(width, height);
    }

    return VIVID_OK;
}

VIVID_C_API void vivid_context_set_visualizer_visible(VividContext* ctx, bool visible) {
    if (ctx) {
        toInternal(ctx)->visualizerVisible = visible;
    }
}

VIVID_C_API bool vivid_context_is_visualizer_visible(VividContext* ctx) {
    if (!ctx) return false;
    return toInternal(ctx)->visualizerVisible;
}

VIVID_C_API const char* vivid_context_get_selected_operator(VividContext* ctx) {
    if (!ctx) return nullptr;
    auto internal = toInternal(ctx);
    if (!internal->visualizer) return nullptr;

    const std::string& name = internal->visualizer->getSelectedOperatorName();
    if (name.empty()) return nullptr;
    return name.c_str();
}

VIVID_C_API void vivid_context_select_operator(VividContext* ctx, const char* name) {
    if (!ctx || !name) return;
    auto internal = toInternal(ctx);
    if (!internal->visualizer) return;

    internal->visualizer->selectNodeFromEditor(name);
}

VIVID_C_API void vivid_context_destroy(VividContext* ctx) {
    if (ctx) {
        delete toInternal(ctx);
    }
}

// =============================================================================
// Project Loading
// =============================================================================

VIVID_C_API VividResult vivid_context_load_project(VividContext* ctx, const char* path) {
    if (!ctx || !path) {
        setError("Invalid argument");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    auto* internal = toInternal(ctx);

    try {
        // Find chain.cpp in the project directory
        std::string projectPath = path;
        std::string chainPath = projectPath + "/chain.cpp";

        // CRITICAL: Reset the chain BEFORE reloading the dylib!
        // The old chain's operators have vtables pointing to the old dylib.
        // If we unload the dylib first, the operator destructors will crash
        // when trying to call virtual functions in unloaded memory.
        if (internal->hasProject) {
            internal->context->resetChain();
            internal->context->clearRegisteredOperators();
        }

        internal->projectPath = projectPath;
        internal->hotReload->setSourceFile(chainPath);

        // Set project directory for asset resolution (relative paths like "assets/file.mp4")
        vivid::AssetLoader::instance().setProjectDir(projectPath);

        if (!internal->hotReload->reload()) {
            internal->compileError = internal->hotReload->getError();
            internal->hasProject = false;
            setError(internal->compileError);
            return VIVID_ERROR_COMPILE_FAILED;
        }

        // Reset chain again to clear any state (it's now safe since new dylib is loaded)
        internal->context->resetChain();
        internal->context->setChainPath(chainPath);

        auto setupFn = internal->hotReload->getSetupFn();
        if (setupFn) {
            setupFn(*internal->context);
        }

        internal->compileError.clear();
        internal->hasProject = true;
        return VIVID_OK;
    } catch (const std::exception& e) {
        internal->compileError = e.what();
        internal->hasProject = false;
        setError(e.what());
        return VIVID_ERROR_LOAD_FAILED;
    }
}

VIVID_C_API VividResult vivid_configure_asset_paths(const char* vivid_root) {
    if (!vivid_root) {
        setError("Invalid argument");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    std::filesystem::path rootDir(vivid_root);
    auto& assets = vivid::AssetLoader::instance();

    // Add build directory (contains shaders/)
    std::filesystem::path buildDir = rootDir / "build";
    if (std::filesystem::exists(buildDir)) {
        assets.addSearchPath(buildDir);
    }

    // Add modules directory for module-specific assets
    std::filesystem::path modulesDir = rootDir / "modules";
    if (std::filesystem::exists(modulesDir)) {
        assets.addSearchPath(modulesDir);
        // Add vivid-core assets specifically (fonts)
        std::filesystem::path coreAssets = modulesDir / "vivid-core" / "assets";
        if (std::filesystem::exists(coreAssets)) {
            assets.addSearchPath(coreAssets);
        }
    }

    return VIVID_OK;
}

VIVID_C_API VividResult vivid_context_set_root_dir(VividContext* ctx, const char* path) {
    if (!ctx || !path) {
        setError("Invalid argument");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    auto* internal = toInternal(ctx);
    std::filesystem::path rootDir(path);

    // Configure hot-reload to find headers
    internal->hotReload->setRootDir(rootDir);

    // Configure asset loader to find shaders, fonts, etc.
    auto& assets = vivid::AssetLoader::instance();

    // Add build directory (contains shaders/, fonts from build process)
    std::filesystem::path buildDir = rootDir / "build";
    if (std::filesystem::exists(buildDir)) {
        assets.addSearchPath(buildDir);
    }

    // Add modules directory for module-specific assets
    std::filesystem::path modulesDir = rootDir / "modules";
    if (std::filesystem::exists(modulesDir)) {
        assets.addSearchPath(modulesDir);
        // Add vivid-core assets specifically
        std::filesystem::path coreAssets = modulesDir / "vivid-core" / "assets";
        if (std::filesystem::exists(coreAssets)) {
            assets.addSearchPath(coreAssets);
        }
    }

    return VIVID_OK;
}

VIVID_C_API VividResult vivid_context_reload(VividContext* ctx) {
    if (!ctx) {
        setError("Invalid argument");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    auto* internal = toInternal(ctx);

    if (!internal->hasProject) {
        setError("No project loaded");
        return VIVID_ERROR_NO_CHAIN;
    }

    try {
        // Preserve states from current chain
        if (internal->context->hasChain()) {
            internal->context->preserveStates(internal->context->chain());
        }

        // Force reload
        internal->hotReload->forceReload();
        if (!internal->hotReload->reload()) {
            internal->compileError = internal->hotReload->getError();
            setError(internal->compileError);
            return VIVID_ERROR_COMPILE_FAILED;
        }

        // Reset and setup
        internal->context->resetChain();
        internal->context->clearRegisteredOperators();

        auto setupFn = internal->hotReload->getSetupFn();
        if (setupFn) {
            setupFn(*internal->context);
        }

        // Restore states
        if (internal->context->hasChain()) {
            internal->context->restoreStates(internal->context->chain());
        }

        internal->compileError.clear();
        return VIVID_OK;
    } catch (const std::exception& e) {
        internal->compileError = e.what();
        setError(e.what());
        return VIVID_ERROR_LOAD_FAILED;
    }
}

VIVID_C_API VividResult vivid_context_unload_project(VividContext* ctx) {
    if (!ctx) {
        setError("Invalid argument");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    auto* internal = toInternal(ctx);
    internal->context->resetChain();
    internal->context->clearRegisteredOperators();
    internal->hotReload = std::make_unique<vivid::HotReload>();
    internal->projectPath.clear();
    internal->compileError.clear();
    internal->hasProject = false;
    return VIVID_OK;
}

VIVID_C_API VividCompileStatus vivid_context_get_compile_status(VividContext* ctx) {
    VividCompileStatus status = {true, nullptr, 0, 0};

    if (!ctx) {
        return status;
    }

    auto* internal = toInternal(ctx);

    if (!internal->compileError.empty()) {
        status.success = false;
        status.message = internal->compileError.c_str();

        // Try to extract line/column from error
        auto& errors = internal->hotReload->getCompileErrors();
        if (!errors.empty()) {
            status.error_line = errors[0].line;
            status.error_column = errors[0].column;
        }
    }

    return status;
}

VIVID_C_API bool vivid_context_has_project(VividContext* ctx) {
    if (!ctx) return false;
    return toInternal(ctx)->hasProject;
}

VIVID_C_API const char* vivid_context_get_project_path(VividContext* ctx) {
    if (!ctx) return nullptr;
    auto* internal = toInternal(ctx);
    return internal->projectPath.empty() ? nullptr : internal->projectPath.c_str();
}

// =============================================================================
// Frame Processing
// =============================================================================

VIVID_C_API VividResult vivid_context_process_frame(VividContext* ctx, double dt) {
    if (!ctx) {
        setError("Invalid argument");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    auto* internal = toInternal(ctx);

    if (!internal->hasProject) {
        setError("No project loaded");
        return VIVID_ERROR_NO_CHAIN;
    }

    try {
        // Inject time delta (headless mode)
        internal->context->injectDeltaTime(dt);
        internal->context->beginFrame();

        // Call update function
        auto updateFn = internal->hotReload->getUpdateFn();
        if (updateFn) {
            updateFn(*internal->context);
        }

        internal->context->endFrame();
        return VIVID_OK;
    } catch (const std::exception& e) {
        setError(e.what());
        return VIVID_ERROR_INTERNAL;
    }
}

VIVID_C_API uint64_t vivid_context_get_frame(VividContext* ctx) {
    if (!ctx) return 0;
    return toInternal(ctx)->context->frame();
}

VIVID_C_API double vivid_context_get_time(VividContext* ctx) {
    if (!ctx) return 0.0;
    return toInternal(ctx)->context->time();
}

VIVID_C_API void vivid_context_reset_time(VividContext* ctx) {
    if (ctx) {
        toInternal(ctx)->context->resetTime();
    }
}

// =============================================================================
// Resolution Management
// =============================================================================

VIVID_C_API VividResult vivid_context_set_resolution(VividContext* ctx, int width, int height) {
    if (!ctx) {
        setError("Invalid argument");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    toInternal(ctx)->context->setRenderResolution(width, height);
    return VIVID_OK;
}

VIVID_C_API int vivid_context_get_width(VividContext* ctx) {
    if (!ctx) return 0;
    return toInternal(ctx)->context->renderWidth();
}

VIVID_C_API int vivid_context_get_height(VividContext* ctx) {
    if (!ctx) return 0;
    return toInternal(ctx)->context->renderHeight();
}

// =============================================================================
// Input Injection
// =============================================================================

VIVID_C_API void vivid_context_set_mouse_position(VividContext* ctx, float x, float y) {
    if (ctx) {
        toInternal(ctx)->context->injectMousePosition(x, y);
    }
}

VIVID_C_API void vivid_context_set_mouse_button(VividContext* ctx, int button, bool pressed) {
    if (ctx) {
        toInternal(ctx)->context->injectMouseButton(button, pressed);
    }
}

VIVID_C_API void vivid_context_set_key(VividContext* ctx, int keycode, bool pressed) {
    if (ctx) {
        toInternal(ctx)->context->injectKeyState(keycode, pressed);
    }
}

VIVID_C_API void vivid_context_add_scroll(VividContext* ctx, float dx, float dy) {
    if (ctx) {
        toInternal(ctx)->context->injectScroll(dx, dy);
    }
}

// =============================================================================
// Chain Access
// =============================================================================

VIVID_C_API VividChain* vivid_context_get_chain(VividContext* ctx) {
    if (!ctx) return nullptr;
    auto* internal = toInternal(ctx);
    if (!internal->hasProject || !internal->context->hasChain()) {
        return nullptr;
    }
    return fromChain(&internal->context->chain());
}

VIVID_C_API VividWGPUTextureView vivid_context_get_output_view(VividContext* ctx) {
    if (!ctx) return nullptr;
    return toInternal(ctx)->context->outputTexture();
}

VIVID_C_API VividWGPUTexture vivid_context_get_output_texture(VividContext* ctx) {
    if (!ctx) return nullptr;
    auto* internal = toInternal(ctx);
    if (!internal->hasProject || !internal->context->hasChain()) {
        return nullptr;
    }
    return internal->context->chain().outputTexture();
}

// =============================================================================
// Operator Iteration
// =============================================================================

VIVID_C_API int vivid_chain_get_operator_count(VividChain* chain) {
    if (!chain) return 0;
    return static_cast<int>(toChain(chain)->operatorNames().size());
}

VIVID_C_API VividOperator* vivid_chain_get_operator_by_index(VividChain* chain, int index) {
    if (!chain || index < 0) return nullptr;
    auto* c = toChain(chain);
    const auto& names = c->operatorNames();
    if (index >= static_cast<int>(names.size())) return nullptr;
    return fromOperator(c->getByName(names[index]));
}

VIVID_C_API VividOperator* vivid_chain_get_operator_by_name(VividChain* chain, const char* name) {
    if (!chain || !name) return nullptr;
    return fromOperator(toChain(chain)->getByName(name));
}

VIVID_C_API VividOperator* vivid_chain_get_output_operator(VividChain* chain) {
    if (!chain) return nullptr;
    return fromOperator(toChain(chain)->getOutput());
}

// =============================================================================
// Operator Information
// =============================================================================

// Thread-local storage for operator names (to return stable pointers)
static thread_local std::string s_operatorName;
static thread_local std::string s_operatorTypeName;
static thread_local std::string s_inputName;

VIVID_C_API const char* vivid_operator_get_name(VividOperator* op) {
    // We need access to the chain to get the name - but we don't have it here
    // Return the type name instead (which the operator knows)
    if (!op) return "";
    s_operatorName = toOperator(op)->name();
    return s_operatorName.c_str();
}

VIVID_C_API const char* vivid_operator_get_type_name(VividOperator* op) {
    if (!op) return "";
    s_operatorTypeName = toOperator(op)->name();
    return s_operatorTypeName.c_str();
}

VIVID_C_API VividOutputKind vivid_operator_get_output_kind(VividOperator* op) {
    if (!op) return VIVID_OUTPUT_TEXTURE;
    return convertOutputKind(toOperator(op)->outputKind());
}

VIVID_C_API bool vivid_operator_is_bypassed(VividOperator* op) {
    if (!op) return false;
    return toOperator(op)->isBypassed();
}

VIVID_C_API void vivid_operator_set_bypassed(VividOperator* op, bool bypassed) {
    if (op) {
        toOperator(op)->setBypassed(bypassed);
    }
}

// =============================================================================
// Operator Outputs (Textures)
// =============================================================================

VIVID_C_API VividWGPUTextureView vivid_operator_get_output_view(VividOperator* op) {
    if (!op) return nullptr;
    return toOperator(op)->outputView();
}

VIVID_C_API VividWGPUTexture vivid_operator_get_output_texture(VividOperator* op) {
    if (!op) return nullptr;
    return toOperator(op)->outputTexture();
}

VIVID_C_API bool vivid_operator_get_texture_info(VividOperator* op, VividTextureInfo* out_info) {
    if (!op || !out_info) return false;

    auto* cppOp = toOperator(op);
    WGPUTexture tex = cppOp->outputTexture();
    if (!tex) return false;

    // Get texture info from WebGPU
    out_info->width = static_cast<int>(wgpuTextureGetWidth(tex));
    out_info->height = static_cast<int>(wgpuTextureGetHeight(tex));
    out_info->format = static_cast<int>(wgpuTextureGetFormat(tex));
    out_info->has_alpha = true;  // Assume RGBA

    return true;
}

VIVID_C_API float vivid_operator_get_output_value(VividOperator* op) {
    if (!op) return 0.0f;
    return toOperator(op)->outputValue();
}

// =============================================================================
// Operator Parameters
// =============================================================================

// Thread-local storage for parameter info
// We need to keep the strings alive because the ParamDecl returned from params() is temporary
struct CachedParamInfo {
    std::vector<vivid::ParamDecl> decls;
    std::vector<std::vector<std::string>> enumLabelStrings;  // Own the strings
    std::vector<std::vector<const char*>> enumLabelPtrs;     // Pointers to strings
    VividOperator* cachedOp = nullptr;
};
static thread_local CachedParamInfo s_paramCache;

// Helper to ensure param cache is valid for this operator
static void ensureParamCache(VividOperator* op) {
    if (s_paramCache.cachedOp == op) return;  // Already cached

    auto* cppOp = toOperator(op);
    s_paramCache.decls = cppOp->params();
    s_paramCache.enumLabelStrings.resize(s_paramCache.decls.size());
    s_paramCache.enumLabelPtrs.resize(s_paramCache.decls.size());

    for (size_t i = 0; i < s_paramCache.decls.size(); ++i) {
        const auto& p = s_paramCache.decls[i];
        s_paramCache.enumLabelStrings[i].clear();
        s_paramCache.enumLabelPtrs[i].clear();
        for (const auto& label : p.enumLabels) {
            s_paramCache.enumLabelStrings[i].push_back(label);
        }
        for (const auto& label : s_paramCache.enumLabelStrings[i]) {
            s_paramCache.enumLabelPtrs[i].push_back(label.c_str());
        }
    }

    s_paramCache.cachedOp = op;
}

VIVID_C_API int vivid_operator_get_param_count(VividOperator* op) {
    if (!op) return 0;
    return static_cast<int>(toOperator(op)->params().size());
}

VIVID_C_API bool vivid_operator_get_param_decl(VividOperator* op, int index, VividParamDecl* out_decl) {
    if (!op || !out_decl || index < 0) return false;

    ensureParamCache(op);

    if (index >= static_cast<int>(s_paramCache.decls.size())) return false;

    const auto& p = s_paramCache.decls[index];

    out_decl->name = p.name.c_str();
    out_decl->type = convertParamType(p.type);
    out_decl->min_val = p.minVal;
    out_decl->max_val = p.maxVal;
    std::memcpy(out_decl->default_val, p.defaultVal, sizeof(float) * 4);
    out_decl->string_default = p.stringDefault.c_str();

    // Handle enum labels - use cached strings
    if (!s_paramCache.enumLabelPtrs[index].empty()) {
        out_decl->enum_count = static_cast<int>(s_paramCache.enumLabelPtrs[index].size());
        out_decl->enum_labels = s_paramCache.enumLabelPtrs[index].data();
    } else {
        out_decl->enum_count = 0;
        out_decl->enum_labels = nullptr;
    }

    return true;
}

VIVID_C_API bool vivid_operator_get_param(VividOperator* op, const char* name, float out_value[4]) {
    if (!op || !name || !out_value) return false;
    return toOperator(op)->getParam(name, out_value);
}

VIVID_C_API bool vivid_operator_set_param(VividOperator* op, const char* name, const float value[4]) {
    if (!op || !name || !value) return false;
    return toOperator(op)->setParam(name, value);
}

VIVID_C_API const char* vivid_operator_get_param_string(VividOperator* op, const char* name) {
    // String parameters are not yet implemented in the base Operator API
    // Would need extension to support this
    (void)op;
    (void)name;
    return nullptr;
}

VIVID_C_API bool vivid_operator_set_param_string(VividOperator* op, const char* name, const char* value) {
    // String parameters are not yet implemented in the base Operator API
    (void)op;
    (void)name;
    (void)value;
    return false;
}

// =============================================================================
// Operator Inputs
// =============================================================================

VIVID_C_API int vivid_operator_get_input_count(VividOperator* op) {
    if (!op) return 0;
    return static_cast<int>(toOperator(op)->inputCount());
}

VIVID_C_API VividOperator* vivid_operator_get_input(VividOperator* op, int index) {
    if (!op || index < 0) return nullptr;
    return fromOperator(toOperator(op)->getInput(index));
}

VIVID_C_API const char* vivid_operator_get_input_name(VividOperator* op, int index) {
    if (!op || index < 0) return "";
    s_inputName = toOperator(op)->getInputName(index);
    return s_inputName.c_str();
}

// =============================================================================
// Operator Registry
// =============================================================================

// Thread-local storage for registry names
static thread_local std::string s_registryName;
static thread_local std::string s_registryCategory;

VIVID_C_API int vivid_registry_get_operator_count(void) {
    return static_cast<int>(vivid::OperatorRegistry::instance().operators().size());
}

VIVID_C_API const char* vivid_registry_get_operator_name(int index) {
    const auto& ops = vivid::OperatorRegistry::instance().operators();
    if (index < 0 || index >= static_cast<int>(ops.size())) return "";
    s_registryName = ops[index].name;
    return s_registryName.c_str();
}

VIVID_C_API const char* vivid_registry_get_operator_category(int index) {
    const auto& ops = vivid::OperatorRegistry::instance().operators();
    if (index < 0 || index >= static_cast<int>(ops.size())) return "";
    s_registryCategory = ops[index].category;
    return s_registryCategory.c_str();
}

// =============================================================================
// Snapshot/Capture
// =============================================================================

VIVID_C_API VividResult vivid_context_capture_snapshot(VividContext* ctx, const char* path) {
    if (!ctx || !path) {
        setError("Invalid argument");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    auto* internal = toInternal(ctx);

    if (!internal->hasProject || !internal->context->hasChain()) {
        setError("No project loaded");
        return VIVID_ERROR_NO_CHAIN;
    }

    try {
        WGPUTexture tex = internal->context->chain().outputTexture();
        if (!tex) {
            setError("No output texture");
            return VIVID_ERROR_INTERNAL;
        }

        if (vivid::VideoExporter::saveSnapshot(
            internal->context->device(),
            internal->context->queue(),
            tex,
            path
        )) {
            return VIVID_OK;
        } else {
            setError("Failed to save snapshot");
            return VIVID_ERROR_INTERNAL;
        }
    } catch (const std::exception& e) {
        setError(e.what());
        return VIVID_ERROR_INTERNAL;
    }
}

VIVID_C_API VividResult vivid_operator_capture_snapshot(VividOperator* op, const char* path) {
    // This requires access to the context for device/queue
    // For now, return error - would need to track context in operator
    (void)op;
    (void)path;
    setError("Not implemented: operator snapshot requires context access");
    return VIVID_ERROR_INTERNAL;
}

// =============================================================================
// Version Information
// =============================================================================

VIVID_C_API const char* vivid_get_version(void) {
    return VIVID_VERSION_STRING;
}

VIVID_C_API int vivid_get_api_version(void) {
    return VIVID_API_VERSION;  // From vivid_c.h
}
