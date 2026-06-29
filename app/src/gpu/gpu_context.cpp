#include "gpu_context.h"
#include "gpu_util.h"
#include <glfw3webgpu.h>
#include <cstdio>
#include <cstring>
#include <cassert>

namespace vivid {

// ---------------------------------------------------------------------------
// Synchronous request wrappers (wgpu-native v24 / WebGPU spec 2024+)
// ---------------------------------------------------------------------------

static WGPUAdapter request_adapter_sync(WGPUInstance instance, const WGPURequestAdapterOptions* opts) {
    struct UserData { WGPUAdapter adapter = nullptr; bool done = false; };
    UserData data;

    WGPURequestAdapterCallbackInfo cb{};
    cb.mode = WGPUCallbackMode_AllowSpontaneous;
    cb.callback = [](WGPURequestAdapterStatus status, WGPUAdapter adapter,
                     WGPUStringView message, void* ud1, void* /*ud2*/) {
        auto* d = static_cast<UserData*>(ud1);
        if (status == WGPURequestAdapterStatus_Success) {
            d->adapter = adapter;
        } else {
            std::fprintf(stderr, "[vivid] Adapter request failed: %.*s\n",
                         static_cast<int>(message.length), message.data ? message.data : "");
        }
        d->done = true;
    };
    cb.userdata1 = &data;
    cb.userdata2 = nullptr;

    wgpuInstanceRequestAdapter(instance, opts, cb);
    assert(data.done && "Adapter request did not complete synchronously");
    return data.adapter;
}

static WGPUDevice request_device_sync(WGPUAdapter adapter, const WGPUDeviceDescriptor* desc) {
    struct UserData { WGPUDevice device = nullptr; bool done = false; };
    UserData data;

    WGPURequestDeviceCallbackInfo cb{};
    cb.mode = WGPUCallbackMode_AllowSpontaneous;
    cb.callback = [](WGPURequestDeviceStatus status, WGPUDevice device,
                     WGPUStringView message, void* ud1, void* /*ud2*/) {
        auto* d = static_cast<UserData*>(ud1);
        if (status == WGPURequestDeviceStatus_Success) {
            d->device = device;
        } else {
            std::fprintf(stderr, "[vivid] Device request failed: %.*s\n",
                         static_cast<int>(message.length), message.data ? message.data : "");
        }
        d->done = true;
    };
    cb.userdata1 = &data;
    cb.userdata2 = nullptr;

    wgpuAdapterRequestDevice(adapter, desc, cb);
    assert(data.done && "Device request did not complete synchronously");
    return data.device;
}

// ---------------------------------------------------------------------------
// GpuContext
// ---------------------------------------------------------------------------

GpuContext::~GpuContext() {
    shutdown();
}

bool GpuContext::init(GLFWwindow* window, uint32_t width, uint32_t height) {
    // 1. Instance
    WGPUInstanceDescriptor instance_desc{};
    instance_desc.nextInChain = nullptr;
    instance_ = wgpuCreateInstance(&instance_desc);
    if (!instance_) {
        std::fprintf(stderr, "[vivid] Failed to create WebGPU instance\n");
        return false;
    }

    // 2. Surface (via glfw3webgpu)
    surface_ = glfwCreateWindowWGPUSurface(instance_, window);
    if (!surface_) {
        std::fprintf(stderr, "[vivid] Failed to create surface\n");
        shutdown();
        return false;
    }

    // 3. Adapter
    WGPURequestAdapterOptions adapter_opts{};
    adapter_opts.nextInChain = nullptr;
    adapter_opts.compatibleSurface = surface_;
    adapter_opts.powerPreference = WGPUPowerPreference_HighPerformance;
    adapter_ = request_adapter_sync(instance_, &adapter_opts);
    if (!adapter_) {
        std::fprintf(stderr, "[vivid] Failed to get adapter\n");
        shutdown();
        return false;
    }

    // 4. Device
    WGPUDeviceDescriptor device_desc{};
    device_desc.nextInChain = nullptr;
    device_desc.label = to_sv("Vivid Device");
    bc_texture_compression_enabled_ =
        wgpuAdapterHasFeature(adapter_, WGPUFeatureName_TextureCompressionBC);
    if (bc_texture_compression_enabled_) {
        static WGPUFeatureName kRequiredFeatures[] = {
            WGPUFeatureName_TextureCompressionBC
        };
        device_desc.requiredFeatureCount = 1;
        device_desc.requiredFeatures = kRequiredFeatures;
        std::fprintf(stderr, "[vivid] GPU feature enabled: TextureCompressionBC (HAP direct BC path available)\n");
    } else {
        device_desc.requiredFeatureCount = 0;
        device_desc.requiredFeatures = nullptr;
        std::fprintf(stderr, "[vivid] GPU feature unavailable: TextureCompressionBC (HAP direct BC path disabled)\n");
    }
    device_desc.requiredLimits = nullptr;
    device_desc.defaultQueue.nextInChain = nullptr;
    device_desc.defaultQueue.label = to_sv("Vivid Queue");

    // Device lost callback
    device_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    device_desc.deviceLostCallbackInfo.callback =
        [](WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView message, void* ud1, void*) {
            auto* self = static_cast<GpuContext*>(ud1);
            self->device_lost_ = true;
            std::fprintf(stderr, "[vivid] Device lost (reason %d): %.*s\n",
                         static_cast<int>(reason), static_cast<int>(message.length),
                         message.data ? message.data : "");
        };
    device_desc.deviceLostCallbackInfo.userdata1 = this;

    // Uncaptured error callback — capture last error for crash diagnostics
    device_desc.uncapturedErrorCallbackInfo.callback =
        [](WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void* ud1, void*) {
            auto* self = static_cast<GpuContext*>(ud1);
            self->last_error_ = std::string(message.data ? message.data : "", message.length);
            self->last_error_type_ = type;
            std::fprintf(stderr, "[vivid] WebGPU error (%d): %.*s\n",
                         static_cast<int>(type), static_cast<int>(message.length),
                         message.data ? message.data : "");
        };
    device_desc.uncapturedErrorCallbackInfo.userdata1 = this;

    device_ = request_device_sync(adapter_, &device_desc);
    if (!device_) {
        std::fprintf(stderr, "[vivid] Failed to get device\n");
        shutdown();
        return false;
    }

    // 5. Queue
    queue_ = wgpuDeviceGetQueue(device_);

    // 6. Surface format — first from capabilities (preferred order)
    WGPUSurfaceCapabilities caps{};
    wgpuSurfaceGetCapabilities(surface_, adapter_, &caps);
    if (caps.formatCount > 0) {
        surface_format_ = caps.formats[0];
    } else {
        surface_format_ = WGPUTextureFormat_BGRA8Unorm;
    }

    // Check if CopySrc is supported for surface textures (needed for screenshot)
    WGPUTextureUsage usage = WGPUTextureUsage_RenderAttachment;
    if (caps.usages & WGPUTextureUsage_CopySrc) {
        surface_copy_src_ = true;
        usage |= WGPUTextureUsage_CopySrc;
    }

    wgpuSurfaceCapabilitiesFreeMembers(caps);
    std::fprintf(stderr, "[vivid] Surface format: %d (CopySrc: %s)\n",
                 static_cast<int>(surface_format_), surface_copy_src_ ? "yes" : "no");

    // 7. Configure surface
    WGPUSurfaceConfiguration config{};
    config.nextInChain = nullptr;
    config.device = device_;
    config.format = surface_format_;
    config.usage = usage;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.width = width;
    config.height = height;
    config.presentMode = WGPUPresentMode_Fifo;
    config.viewFormatCount = 0;
    config.viewFormats = nullptr;
    wgpuSurfaceConfigure(surface_, &config);

    width_ = width;
    height_ = height;
    ensure_msaa(width, height);   // 4x MSAA color target (resolves to the surface)
    std::fprintf(stderr, "[vivid] GPU context initialized (%ux%u, %ux MSAA)\n",
                 width, height, kMsaaSamples);
    return true;
}

// (Re)create the multisampled color target that the whole frame renders into.
// Same size/format as the surface; resolved to the surface view in end_frame.
void GpuContext::ensure_msaa(uint32_t width, uint32_t height) {
    if (!device_ || width == 0 || height == 0) return;
    if (msaa_view_ && width == msaa_w_ && height == msaa_h_) return;  // size unchanged
    if (msaa_view_) { wgpuTextureViewRelease(msaa_view_); msaa_view_ = nullptr; }
    if (msaa_tex_)  { wgpuTextureRelease(msaa_tex_);      msaa_tex_  = nullptr; }

    WGPUTextureDescriptor td{};
    td.label = to_sv("Frame MSAA Color");
    td.usage = WGPUTextureUsage_RenderAttachment;
    td.dimension = WGPUTextureDimension_2D;
    td.size = WGPUExtent3D{ width, height, 1 };
    td.format = surface_format_;
    td.mipLevelCount = 1;
    td.sampleCount = kMsaaSamples;
    msaa_tex_ = wgpuDeviceCreateTexture(device_, &td);
    if (!msaa_tex_) { std::fprintf(stderr, "[vivid] Failed to create MSAA texture\n"); return; }

    WGPUTextureViewDescriptor vd{};
    vd.label = to_sv("Frame MSAA View");
    vd.format = surface_format_;
    vd.dimension = WGPUTextureViewDimension_2D;
    vd.baseMipLevel = 0;  vd.mipLevelCount = 1;
    vd.baseArrayLayer = 0; vd.arrayLayerCount = 1;
    vd.aspect = WGPUTextureAspect_All;
    msaa_view_ = wgpuTextureCreateView(msaa_tex_, &vd);
    msaa_w_ = width;
    msaa_h_ = height;
}

void GpuContext::resize(uint32_t width, uint32_t height) {
    if (width == 0 || height == 0) return;
    wgpuSurfaceUnconfigure(surface_);
    WGPUSurfaceConfiguration config{};
    config.device = device_;
    config.format = surface_format_;
    config.usage = WGPUTextureUsage_RenderAttachment | (surface_copy_src_ ? WGPUTextureUsage_CopySrc : 0);
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.width = width;
    config.height = height;
    config.presentMode = WGPUPresentMode_Fifo;
    wgpuSurfaceConfigure(surface_, &config);
    width_ = width;
    height_ = height;
    ensure_msaa(width, height);   // resize the MSAA target to match the surface
}

bool GpuContext::begin_frame(FrameState& frame) {
    if (device_lost_) return false;

    WGPUSurfaceTexture surface_tex{};
    wgpuSurfaceGetCurrentTexture(surface_, &surface_tex);
    if (surface_tex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal) {
        if (surface_tex.status == WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
            // Treat suboptimal as transiently invalid during monitor/fullscreen transitions.
            // The main loop will keep ticking offscreen and retry acquire on subsequent frames.
            if (surface_tex.texture) {
                wgpuTextureRelease(surface_tex.texture);
            }
            return false;
        }
        // Transient (Timeout / Outdated / Lost) — common during window resize or
        // when offscreen. Retry next frame; warn only once so the log isn't spammed.
        static bool warned_acquire = false;
        if (!warned_acquire) {
            std::fprintf(stderr, "[vivid] surface texture unavailable (status %d) — retrying (silenced)\n",
                         static_cast<int>(surface_tex.status));
            warned_acquire = true;
        }
        if (surface_tex.texture) wgpuTextureRelease(surface_tex.texture);
        return false;
    }

    WGPUTextureViewDescriptor view_desc{};
    view_desc.nextInChain = nullptr;
    view_desc.label = to_sv("Surface View");
    view_desc.format = surface_format_;
    view_desc.dimension = WGPUTextureViewDimension_2D;
    view_desc.baseMipLevel = 0;
    view_desc.mipLevelCount = 1;
    view_desc.baseArrayLayer = 0;
    view_desc.arrayLayerCount = 1;
    view_desc.aspect = WGPUTextureAspect_All;
    frame.texture = surface_tex.texture;
    // The surface view is the MSAA *resolve* target; the app renders into the
    // multisampled view (frame.view), which gpu_context owns and reuses per frame.
    WGPUTextureView surface_view = wgpuTextureCreateView(surface_tex.texture, &view_desc);
    if (!surface_view) {
        std::fprintf(stderr, "[vivid] Failed to create surface texture view\n");
        wgpuTextureRelease(frame.texture);
        frame.texture = nullptr;
        return false;
    }
    if (!msaa_view_) {  // MSAA target missing (alloc failed) — can't render this frame
        std::fprintf(stderr, "[vivid] MSAA target unavailable — dropping frame\n");
        wgpuTextureViewRelease(surface_view);
        wgpuTextureRelease(frame.texture);
        frame.texture = nullptr;
        return false;
    }

    WGPUCommandEncoderDescriptor enc_desc{};
    enc_desc.nextInChain = nullptr;
    enc_desc.label = to_sv("Frame Encoder");
    frame.encoder = wgpuDeviceCreateCommandEncoder(device_, &enc_desc);
    if (!frame.encoder) {
        std::fprintf(stderr, "[vivid] Failed to create frame encoder\n");
        wgpuTextureViewRelease(surface_view);
        wgpuTextureRelease(frame.texture);
        frame.texture = nullptr;
        return false;
    }

    frame.resolve_view = surface_view;   // resolves here in end_frame, then presents
    frame.view = msaa_view_;             // gpu_context-owned; NOT released per frame
    return true;
}

bool GpuContext::end_frame(const FrameState& frame) {
    if (device_lost_) {
        discard_frame(frame);
        return false;
    }

    // Resolve the multisampled frame into the swap-chain surface. A draw-less
    // render pass whose resolveTarget is the surface view performs the MSAA
    // downsample at endPass; everything the app drew lives in frame.view (MSAA).
    {
        WGPURenderPassColorAttachment att{};
        att.view = frame.view;                    // 4x MSAA color
        att.resolveTarget = frame.resolve_view;   // -> surface (1x)
        att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        att.loadOp = WGPULoadOp_Load;             // keep what the frame drew
        att.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor rp{};
        rp.colorAttachmentCount = 1;
        rp.colorAttachments = &att;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(frame.encoder, &rp);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
    }

    if (!gpu_submit(device_, queue_, frame.encoder, "Frame Commands")) {
        // Encoder was in an error state (e.g. surface texture invalidated mid-frame
        // during resize, fullscreen, or macOS drag-tracking transitions).
        std::fprintf(stderr, "[vivid] Frame submit failed — dropping frame"
                     " (last error: %s)\n", last_error_.c_str());
        if (frame.resolve_view) wgpuTextureViewRelease(frame.resolve_view);
        wgpuTextureRelease(frame.texture);
        return false;
    }

    // Present BEFORE releasing the surface texture/view. frame.view (MSAA) is
    // gpu_context-owned and reused — only the per-frame surface view/texture go.
    wgpuSurfacePresent(surface_);

    if (frame.resolve_view) wgpuTextureViewRelease(frame.resolve_view);
    wgpuTextureRelease(frame.texture);
    return true;
}

void GpuContext::discard_frame(const FrameState& frame) {
    // Drop acquired surface frame without submit/present when the surface is
    // transiently invalid (resize, fullscreen transition, drag-tracking runloop).
    if (frame.encoder) wgpuCommandEncoderRelease(frame.encoder);
    if (frame.resolve_view) wgpuTextureViewRelease(frame.resolve_view);  // frame.view is MSAA (owned)
    if (frame.texture) wgpuTextureRelease(frame.texture);
}

void GpuContext::shutdown() {
    if (msaa_view_) { wgpuTextureViewRelease(msaa_view_); msaa_view_ = nullptr; }
    if (msaa_tex_)  { wgpuTextureRelease(msaa_tex_);      msaa_tex_  = nullptr; }
    if (surface_) {
        wgpuSurfaceUnconfigure(surface_);
        wgpuSurfaceRelease(surface_);
        surface_ = nullptr;
    }
    if (queue_) {
        wgpuQueueRelease(queue_);
        queue_ = nullptr;
    }
    if (device_) {
        wgpuDeviceRelease(device_);
        device_ = nullptr;
    }
    if (adapter_) {
        wgpuAdapterRelease(adapter_);
        adapter_ = nullptr;
    }
    if (instance_) {
        wgpuInstanceRelease(instance_);
        instance_ = nullptr;
    }
    surface_format_ = WGPUTextureFormat_Undefined;
}

// ---------------------------------------------------------------------------
// Safe queue submit with error-scope protection
// ---------------------------------------------------------------------------

bool gpu_submit(WGPUDevice device, WGPUQueue queue, WGPUCommandEncoder encoder,
                const char* label) {
    WGPUCommandBufferDescriptor cmd_desc{};
    cmd_desc.nextInChain = nullptr;
    cmd_desc.label = to_sv(label);
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(encoder, &cmd_desc);

    if (!cmd) {
        // Encoder was in an error state — release it and bail.
        wgpuCommandEncoderRelease(encoder);
        return false;
    }

    wgpuQueueSubmit(queue, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(encoder);

    // GPU errors (validation, OOM, internal) are handled by the uncaptured
    // error callback configured on the device.  We intentionally avoid
    // PushErrorScope/PopErrorScope here because wgpu-native panics (abort)
    // if PushErrorScope is called on a lost device.
    return true;
}

} // namespace vivid
