#include "gpu_context.h"
#include "gpu_util.h"
#include "app/perf_stats.h"     // publish GPU timing for get_perf
#include <glfw3webgpu.h>
#include <webgpu/wgpu.h>        // WGPUNativeFeature_TimestampQueryInsideEncoders, wgpuDevicePoll
#include <cstdio>
#include <cstdlib>              // getenv
#include <cstring>
#include <cassert>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

namespace vivid {

// --- Present mode: default Fifo (vsync); VIVID_PRESENT=immediate/off/0 selects Immediate to uncap
// the frame rate (so the true render ceiling is measurable). Falls back to Fifo if unsupported. ---
WGPUPresentMode vivid_present_mode(const WGPUSurfaceCapabilities& caps) {
    const char* env = std::getenv("VIVID_PRESENT");
    bool want_immediate = env && (std::strcmp(env, "immediate") == 0 ||
                                  std::strcmp(env, "off") == 0 || std::strcmp(env, "0") == 0);
    if (want_immediate) {
        for (size_t i = 0; i < caps.presentModeCount; ++i)
            if (caps.presentModes[i] == WGPUPresentMode_Immediate) return WGPUPresentMode_Immediate;
        std::fprintf(stderr, "[vivid] VIVID_PRESENT=immediate requested but surface lacks Immediate; using Fifo\n");
    }
    return WGPUPresentMode_Fifo;
}

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
    // Build the required-feature list: optional TextureCompressionBC (HAP) + the timestamp-query pair
    // (GPU timing). Each is requested only if the adapter advertises it, so a missing feature disables
    // that capability rather than failing device creation.
    std::vector<WGPUFeatureName> features;
    bc_texture_compression_enabled_ =
        wgpuAdapterHasFeature(adapter_, WGPUFeatureName_TextureCompressionBC);
    if (bc_texture_compression_enabled_) {
        features.push_back(WGPUFeatureName_TextureCompressionBC);
        std::fprintf(stderr, "[vivid] GPU feature enabled: TextureCompressionBC (HAP direct BC path available)\n");
    } else {
        std::fprintf(stderr, "[vivid] GPU feature unavailable: TextureCompressionBC (HAP direct BC path disabled)\n");
    }
    WGPUFeatureName ts_feats[2];
    const uint32_t n_ts = GpuTimer::required_features(adapter_, ts_feats);
    for (uint32_t i = 0; i < n_ts; ++i) features.push_back(ts_feats[i]);
    device_desc.requiredFeatureCount = features.size();
    device_desc.requiredFeatures = features.empty() ? nullptr : features.data();
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
            self->error_count_.fetch_add(1, std::memory_order_relaxed);  // health signal (P4.3)
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

    present_mode_ = vivid_present_mode(caps);   // VIVID_PRESENT (needs caps.presentModes; before free)
    perf::g_present_uncapped.store(present_mode_ == WGPUPresentMode_Immediate, std::memory_order_relaxed);
    wgpuSurfaceCapabilitiesFreeMembers(caps);
    std::fprintf(stderr, "[vivid] Surface format: %d (CopySrc: %s), present: %s\n",
                 static_cast<int>(surface_format_), surface_copy_src_ ? "yes" : "no",
                 present_mode_ == WGPUPresentMode_Immediate ? "Immediate (vsync OFF)" : "Fifo (vsync)");

    // 7. Configure surface
    WGPUSurfaceConfiguration config{};
    config.nextInChain = nullptr;
    config.device = device_;
    config.format = surface_format_;
    config.usage = usage;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.width = width;
    config.height = height;
    config.presentMode = present_mode_;
    config.viewFormatCount = 0;
    config.viewFormats = nullptr;
    wgpuSurfaceConfigure(surface_, &config);

    width_ = width;
    height_ = height;
    ensure_msaa(width, height);   // 4x MSAA color target (resolves to the surface)

    timer_.init(device_, adapter_);   // GPU-side frame timing (opt-in via VIVID_GPU_TIMING)
    std::fprintf(stderr, "[vivid] GPU context initialized (%ux%u, %ux MSAA), gpu-timing: %s\n",
                 width, height, kMsaaSamples,
                 timer_.enabled() ? "on" : "off (set VIVID_GPU_TIMING=1)");
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
    config.presentMode = present_mode_;
    wgpuSurfaceConfigure(surface_, &config);
    width_ = width;
    height_ = height;
    ensure_msaa(width, height);   // resize the MSAA target to match the surface
}

// ---- Secondary (pop-out) surface: shares device_/queue_/surface_format_ ----
// --- AuxSurface: one secondary swap-chain surface + MSAA target (shared device/queue/format) ---
void AuxSurface::ensure_msaa(WGPUDevice dev, WGPUTextureFormat fmt, uint32_t width, uint32_t height, const char* label) {
    if (!dev || width == 0 || height == 0) return;
    if (msaa_view && width == msaa_w && height == msaa_h) return;
    if (msaa_view) { wgpuTextureViewRelease(msaa_view); msaa_view = nullptr; }
    if (msaa_tex)  { wgpuTextureRelease(msaa_tex);       msaa_tex  = nullptr; }
    WGPUTextureDescriptor td{};
    td.label = to_sv(label);
    td.usage = WGPUTextureUsage_RenderAttachment;
    td.dimension = WGPUTextureDimension_2D;
    td.size = WGPUExtent3D{ width, height, 1 };
    td.format = fmt;
    td.mipLevelCount = 1;
    td.sampleCount = kMsaaSamples;
    msaa_tex = wgpuDeviceCreateTexture(dev, &td);
    if (!msaa_tex) return;
    WGPUTextureViewDescriptor vd{};
    vd.label = to_sv(label);
    vd.format = fmt;
    vd.dimension = WGPUTextureViewDimension_2D;
    vd.baseMipLevel = 0; vd.mipLevelCount = 1; vd.baseArrayLayer = 0; vd.arrayLayerCount = 1; vd.aspect = WGPUTextureAspect_All;
    msaa_view = wgpuTextureCreateView(msaa_tex, &vd);
    msaa_w = width; msaa_h = height;
}

bool AuxSurface::open(WGPUInstance inst, WGPUDevice dev, WGPUTextureFormat fmt, GLFWwindow* win,
                      uint32_t width, uint32_t height, const char* label) {
    if (!dev) return false;
    if (surface) return true;   // already open
    surface = glfwCreateWindowWGPUSurface(inst, win);
    if (!surface) { std::fprintf(stderr, "[vivid] %s: surface create failed\n", label); return false; }
    WGPUSurfaceConfiguration config{};
    config.device = dev; config.format = fmt;
    config.usage = WGPUTextureUsage_RenderAttachment; config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.width = width; config.height = height; config.presentMode = WGPUPresentMode_Fifo;  // aux windows stay vsync'd
    wgpuSurfaceConfigure(surface, &config);
    w = width; h = height;
    ensure_msaa(dev, fmt, width, height, label);
    std::fprintf(stderr, "[vivid] %s surface open (%ux%u)\n", label, width, height);
    return true;
}

void AuxSurface::resize(WGPUDevice dev, WGPUTextureFormat fmt, uint32_t width, uint32_t height) {
    if (!surface || width == 0 || height == 0 || (width == w && height == h)) return;
    wgpuSurfaceUnconfigure(surface);
    WGPUSurfaceConfiguration config{};
    config.device = dev; config.format = fmt;
    config.usage = WGPUTextureUsage_RenderAttachment; config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.width = width; config.height = height; config.presentMode = WGPUPresentMode_Fifo;  // aux windows stay vsync'd
    wgpuSurfaceConfigure(surface, &config);
    w = width; h = height;
    ensure_msaa(dev, fmt, width, height, "Aux MSAA");
}

void AuxSurface::close() {
    if (msaa_view) { wgpuTextureViewRelease(msaa_view); msaa_view = nullptr; }
    if (msaa_tex)  { wgpuTextureRelease(msaa_tex);       msaa_tex  = nullptr; }
    if (surface)   { wgpuSurfaceUnconfigure(surface); wgpuSurfaceRelease(surface); surface = nullptr; }
    w = h = msaa_w = msaa_h = 0;
}

bool AuxSurface::begin(WGPUDevice dev, WGPUTextureFormat fmt, bool device_lost, FrameState& frame, const char* label) {
    if (device_lost || !surface) return false;
    WGPUSurfaceTexture st{};
    wgpuSurfaceGetCurrentTexture(surface, &st);
    if (st.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal) {
        if (st.texture) wgpuTextureRelease(st.texture);
        return false;
    }
    WGPUTextureViewDescriptor vd{};
    vd.label = to_sv(label); vd.format = fmt;
    vd.dimension = WGPUTextureViewDimension_2D; vd.mipLevelCount = 1; vd.arrayLayerCount = 1; vd.aspect = WGPUTextureAspect_All;
    frame.texture = st.texture;
    WGPUTextureView sv = wgpuTextureCreateView(st.texture, &vd);
    if (!sv || !msaa_view) { if (sv) wgpuTextureViewRelease(sv); wgpuTextureRelease(frame.texture); frame.texture = nullptr; return false; }
    WGPUCommandEncoderDescriptor ed{}; ed.label = to_sv(label);
    frame.encoder = wgpuDeviceCreateCommandEncoder(dev, &ed);
    if (!frame.encoder) { wgpuTextureViewRelease(sv); wgpuTextureRelease(frame.texture); frame.texture = nullptr; return false; }
    frame.resolve_view = sv;
    frame.view = msaa_view;
    return true;
}

bool AuxSurface::end(WGPUDevice dev, WGPUQueue queue, bool device_lost, const FrameState& frame, const char* label) {
    if (device_lost) {
        if (frame.encoder) wgpuCommandEncoderRelease(frame.encoder);
        if (frame.resolve_view) wgpuTextureViewRelease(frame.resolve_view);
        if (frame.texture) wgpuTextureRelease(frame.texture);
        return false;
    }
    {
        WGPURenderPassColorAttachment att{};
        att.view = frame.view; att.resolveTarget = frame.resolve_view;
        att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED; att.loadOp = WGPULoadOp_Load; att.storeOp = WGPUStoreOp_Store;
        WGPURenderPassDescriptor rp{}; rp.colorAttachmentCount = 1; rp.colorAttachments = &att;
        WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(frame.encoder, &rp);
        wgpuRenderPassEncoderEnd(pass); wgpuRenderPassEncoderRelease(pass);
    }
    if (!gpu_submit(dev, queue, frame.encoder, label)) {
        if (frame.resolve_view) wgpuTextureViewRelease(frame.resolve_view);
        wgpuTextureRelease(frame.texture);
        return false;
    }
    wgpuSurfacePresent(surface);
    if (frame.resolve_view) wgpuTextureViewRelease(frame.resolve_view);
    wgpuTextureRelease(frame.texture);
    return true;
}

// --- GpuContext secondary (pop-out): thin delegators over aux_popout_ ---
bool GpuContext::open_secondary(GLFWwindow* window, uint32_t width, uint32_t height) {
    return aux_popout_.open(instance_, device_, surface_format_, window, width, height, "popout");
}
void GpuContext::resize_secondary(uint32_t width, uint32_t height) {
    aux_popout_.resize(device_, surface_format_, width, height);
}
void GpuContext::close_secondary() { aux_popout_.close(); }
bool GpuContext::begin_secondary(FrameState& frame) {
    return aux_popout_.begin(device_, surface_format_, device_lost_, frame, "Popout Surface");
}
bool GpuContext::end_secondary(const FrameState& frame) {
    return aux_popout_.end(device_, queue_, device_lost_, frame, "Popout Commands");
}

// --- GpuContext editor float-out (UI-5): thin delegators over aux_editor_ ---
bool GpuContext::open_editor_surface(GLFWwindow* window, uint32_t width, uint32_t height) {
    return aux_editor_.open(instance_, device_, surface_format_, window, width, height, "editor");
}
void GpuContext::resize_editor_surface(uint32_t width, uint32_t height) {
    aux_editor_.resize(device_, surface_format_, width, height);
}
void GpuContext::close_editor_surface() { aux_editor_.close(); }
bool GpuContext::begin_editor_surface(FrameState& frame) {
    return aux_editor_.begin(device_, surface_format_, device_lost_, frame, "Editor Surface");
}
bool GpuContext::end_editor_surface(const FrameState& frame) {
    return aux_editor_.end(device_, queue_, device_lost_, frame, "Editor Commands");
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

    timer_.begin(frame.encoder);         // GPU timing: opens a slot + writes the frame-start mark
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

    timer_.resolve(frame.encoder);   // GPU timing: final mark + resolve/copy (before finish/submit)

    if (!gpu_submit(device_, queue_, frame.encoder, "Frame Commands")) {
        // Encoder was in an error state (e.g. surface texture invalidated mid-frame
        // during resize, fullscreen, or macOS drag-tracking transitions).
        std::fprintf(stderr, "[vivid] Frame submit failed — dropping frame"
                     " (last error: %s)\n", last_error_.c_str());
        if (frame.resolve_view) wgpuTextureViewRelease(frame.resolve_view);
        wgpuTextureRelease(frame.texture);
        return false;
    }

    timer_.after_submit();   // GPU timing: map the readback buffer (submit succeeded)

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
    close_secondary();
    close_editor_surface();
    timer_.shutdown();
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

// ---------------------------------------------------------------------------
// GpuTimer — GPU-side frame timing via encoder-level timestamp queries.
// ---------------------------------------------------------------------------

uint32_t GpuTimer::required_features(WGPUAdapter adapter, WGPUFeatureName* out) {
    // We timestamp via render-pass descriptor timestampWrites (beginningOfPassWriteIndex). That is
    // standard WebGPU and needs ONLY the TimestampQuery feature — NOT the wgpu-native InsidePasses
    // extension (which is for mid-pass wgpuRenderPassEncoderWriteTimestamp, absent on this Metal
    // backend). The encoder-level writeTimestamp path reads back as 0 here, so we use passes instead.
    if (wgpuAdapterHasFeature(adapter, WGPUFeatureName_TimestampQuery)) {
        out[0] = WGPUFeatureName_TimestampQuery;
        return 1;
    }
    return 0;
}

// Timestamp the current point in the command stream: a 1x1 render pass whose beginningOfPassWriteIndex
// samples the GPU counter. The pass does no draws and clears/discards a 1x1 target, so it's ~free.
void GpuTimer::write_mark(WGPUCommandEncoder enc, uint32_t qidx) {
    WGPUPassTimestampWrites tw{};
    tw.querySet                 = qset_;
    tw.beginningOfPassWriteIndex = qidx;
    tw.endOfPassWriteIndex       = WGPU_QUERY_SET_INDEX_UNDEFINED;
    WGPURenderPassColorAttachment att{};
    att.view       = dummy_view_;
    att.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    att.loadOp     = WGPULoadOp_Clear;
    att.storeOp    = WGPUStoreOp_Discard;
    att.clearValue = WGPUColor{ 0, 0, 0, 0 };
    WGPURenderPassDescriptor rp{};
    rp.colorAttachmentCount = 1;
    rp.colorAttachments     = &att;
    rp.timestampWrites      = &tw;
    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(enc, &rp);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
}

bool GpuTimer::init(WGPUDevice device, WGPUAdapter adapter) {
    // Opt-in: GPU timing adds a few tiny passes + a non-blocking poll per frame, so it's off unless
    // profiling (VIVID_GPU_TIMING=1). Normal runs pay nothing.
    const char* env = std::getenv("VIVID_GPU_TIMING");
    if (!(env && env[0] && env[0] != '0')) { enabled_ = false; return false; }
    WGPUFeatureName tmp[2];
    if (required_features(adapter, tmp) == 0) { enabled_ = false; return false; }
    device_ = device;

    WGPUQuerySetDescriptor qd{};
    qd.type  = WGPUQueryType_Timestamp;
    qd.count = kRing * kMaxMarks;
    qset_ = wgpuDeviceCreateQuerySet(device_, &qd);
    if (!qset_) { enabled_ = false; return false; }

    const uint64_t bytes = static_cast<uint64_t>(kMaxMarks) * sizeof(uint64_t);
    for (uint32_t r = 0; r < kRing; ++r) {
        WGPUBufferDescriptor rd{};
        rd.usage = WGPUBufferUsage_QueryResolve | WGPUBufferUsage_CopySrc;
        rd.size  = bytes;
        slots_[r].resolve_buf = wgpuDeviceCreateBuffer(device_, &rd);
        WGPUBufferDescriptor md{};
        md.usage = WGPUBufferUsage_MapRead | WGPUBufferUsage_CopyDst;
        md.size  = bytes;
        slots_[r].read_buf = wgpuDeviceCreateBuffer(device_, &md);
        if (!slots_[r].resolve_buf || !slots_[r].read_buf) { shutdown(); return false; }
    }

    // 1x1 render target for the timing passes.
    WGPUTextureDescriptor td{};
    td.usage         = WGPUTextureUsage_RenderAttachment;
    td.dimension     = WGPUTextureDimension_2D;
    td.size          = WGPUExtent3D{ 1, 1, 1 };
    td.format        = WGPUTextureFormat_RGBA8Unorm;
    td.mipLevelCount = 1;
    td.sampleCount   = 1;
    dummy_tex_ = wgpuDeviceCreateTexture(device_, &td);
    if (!dummy_tex_) { shutdown(); return false; }
    dummy_view_ = wgpuTextureCreateView(dummy_tex_, nullptr);
    if (!dummy_view_) { shutdown(); return false; }

    enabled_ = true;
    return true;
}

void GpuTimer::begin(WGPUCommandEncoder enc) {
    if (!enabled_) return;
    drain();                                  // consume finished readbacks, publish to perf
    cur_ = (cur_ + 1) % kRing;
    if (slots_[cur_].inflight) { active_ = false; return; }   // readback not yet consumed — skip this frame
    active_ = true;
    nmarks_cur_ = 0;
    write_mark(enc, cur_ * kMaxMarks + nmarks_cur_);   // mark 0 = frame start
    slots_[cur_].labels[nmarks_cur_] = "frame";        // labels[0] is the start (not itself a segment)
    nmarks_cur_++;
}

void GpuTimer::mark(WGPUCommandEncoder enc, const char* label) {
    if (!enabled_ || !active_) return;
    if (nmarks_cur_ >= kMaxMarks - 1) return;   // keep one slot for the final (resolve) mark
    write_mark(enc, cur_ * kMaxMarks + nmarks_cur_);
    slots_[cur_].labels[nmarks_cur_] = label;   // names the segment ending at this mark
    nmarks_cur_++;
}

void GpuTimer::resolve(WGPUCommandEncoder enc) {
    if (!enabled_ || !active_) return;
    // Final mark: everything after the last app mark (UI + composite + MSAA resolve) up to submit.
    write_mark(enc, cur_ * kMaxMarks + nmarks_cur_);
    slots_[cur_].labels[nmarks_cur_] = "ui";
    nmarks_cur_++;
    Slot& s = slots_[cur_];
    s.nmarks = nmarks_cur_;
    wgpuCommandEncoderResolveQuerySet(enc, qset_, cur_ * kMaxMarks, s.nmarks, s.resolve_buf, 0);
    wgpuCommandEncoderCopyBufferToBuffer(enc, s.resolve_buf, 0, s.read_buf, 0,
                                         static_cast<uint64_t>(s.nmarks) * sizeof(uint64_t));
}

void GpuTimer::after_submit() {
    if (!enabled_ || !active_) return;
    Slot& s = slots_[cur_];
    s.inflight = true;
    s.mapped.store(0, std::memory_order_release);
    WGPUBufferMapCallbackInfo ci{};
    ci.mode = WGPUCallbackMode_AllowProcessEvents;   // fires during wgpuDevicePoll
    ci.callback = [](WGPUMapAsyncStatus status, WGPUStringView, void* u1, void*) {
        auto* slot = static_cast<Slot*>(u1);
        slot->mapped.store(status == WGPUMapAsyncStatus_Success ? 1 : -1, std::memory_order_release);
    };
    ci.userdata1 = &s;
    wgpuBufferMapAsync(s.read_buf, WGPUMapMode_Read, 0,
                       static_cast<size_t>(s.nmarks) * sizeof(uint64_t), ci);
    active_ = false;
}

void GpuTimer::drain() {
    if (!enabled_) return;
    wgpuDevicePoll(device_, false, nullptr);   // non-blocking: let pending map callbacks fire
    for (uint32_t r = 0; r < kRing; ++r) {
        Slot& s = slots_[r];
        if (!s.inflight) continue;
        const int m = s.mapped.load(std::memory_order_acquire);
        if (m == 0) continue;                  // still pending
        if (m == 1) {
            const size_t n = static_cast<size_t>(s.nmarks) * sizeof(uint64_t);
            const uint64_t* ts = static_cast<const uint64_t*>(
                wgpuBufferGetConstMappedRange(s.read_buf, 0, n));
            if (ts && s.nmarks >= 2) {
                auto ms = [](uint64_t a, uint64_t b) { return b >= a ? static_cast<double>(b - a) / 1e6 : 0.0; };
                std::vector<std::pair<std::string, double>> regions;
                for (uint32_t i = 1; i < s.nmarks; ++i)
                    regions.emplace_back(s.labels[i] ? s.labels[i] : "?", ms(ts[i - 1], ts[i]));
                perf::g_gpu_ms.store(ms(ts[0], ts[s.nmarks - 1]), std::memory_order_relaxed);
                perf::set_gpu_regions(std::move(regions));
            }
            wgpuBufferUnmap(s.read_buf);
        }
        s.inflight = false;
        s.mapped.store(0, std::memory_order_release);
    }
}

void GpuTimer::shutdown() {
    for (uint32_t r = 0; r < kRing; ++r) {
        Slot& s = slots_[r];
        if (s.inflight && s.mapped.load(std::memory_order_acquire) == 1 && s.read_buf)
            wgpuBufferUnmap(s.read_buf);
        if (s.resolve_buf) { wgpuBufferRelease(s.resolve_buf); s.resolve_buf = nullptr; }
        if (s.read_buf)    { wgpuBufferRelease(s.read_buf);    s.read_buf    = nullptr; }
        s.inflight = false;
        s.mapped.store(0, std::memory_order_relaxed);
    }
    if (dummy_view_) { wgpuTextureViewRelease(dummy_view_); dummy_view_ = nullptr; }
    if (dummy_tex_)  { wgpuTextureRelease(dummy_tex_);      dummy_tex_  = nullptr; }
    if (qset_) { wgpuQuerySetRelease(qset_); qset_ = nullptr; }
    enabled_ = false;
}

} // namespace vivid
