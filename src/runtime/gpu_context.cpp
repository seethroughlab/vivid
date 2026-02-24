#include "runtime/gpu_context.h"
#include <glfw3webgpu.h>
#include <cstdio>
#include <cstring>
#include <cassert>

namespace vivid {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static WGPUStringView to_sv(const char* s) {
    return { s, s ? std::strlen(s) : 0 };
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
                         (int)message.length, message.data ? message.data : "");
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
                         (int)message.length, message.data ? message.data : "");
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
        return false;
    }

    // 4. Device
    WGPUDeviceDescriptor device_desc{};
    device_desc.nextInChain = nullptr;
    device_desc.label = to_sv("Vivid Device");
    device_desc.requiredFeatureCount = 0;
    device_desc.requiredFeatures = nullptr;
    device_desc.requiredLimits = nullptr;
    device_desc.defaultQueue.nextInChain = nullptr;
    device_desc.defaultQueue.label = to_sv("Vivid Queue");

    // Device lost callback
    device_desc.deviceLostCallbackInfo.mode = WGPUCallbackMode_AllowSpontaneous;
    device_desc.deviceLostCallbackInfo.callback =
        [](WGPUDevice const*, WGPUDeviceLostReason reason, WGPUStringView message, void*, void*) {
            std::fprintf(stderr, "[vivid] Device lost (reason %d): %.*s\n",
                         (int)reason, (int)message.length, message.data ? message.data : "");
        };

    // Uncaptured error callback
    device_desc.uncapturedErrorCallbackInfo.callback =
        [](WGPUDevice const*, WGPUErrorType type, WGPUStringView message, void*, void*) {
            std::fprintf(stderr, "[vivid] WebGPU error (%d): %.*s\n",
                         (int)type, (int)message.length, message.data ? message.data : "");
        };

    device_ = request_device_sync(adapter_, &device_desc);
    if (!device_) {
        std::fprintf(stderr, "[vivid] Failed to get device\n");
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
    wgpuSurfaceCapabilitiesFreeMembers(caps);
    std::fprintf(stderr, "[vivid] Surface format: %d\n", (int)surface_format_);

    // 7. Configure surface
    WGPUSurfaceConfiguration config{};
    config.nextInChain = nullptr;
    config.device = device_;
    config.format = surface_format_;
    config.usage = WGPUTextureUsage_RenderAttachment;
    config.alphaMode = WGPUCompositeAlphaMode_Auto;
    config.width = width;
    config.height = height;
    config.presentMode = WGPUPresentMode_Fifo;
    config.viewFormatCount = 0;
    config.viewFormats = nullptr;
    wgpuSurfaceConfigure(surface_, &config);

    std::fprintf(stderr, "[vivid] GPU context initialized (%ux%u)\n", width, height);
    return true;
}

bool GpuContext::begin_frame(FrameState& frame) {
    WGPUSurfaceTexture surface_tex{};
    wgpuSurfaceGetCurrentTexture(surface_, &surface_tex);
    if (surface_tex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessOptimal &&
        surface_tex.status != WGPUSurfaceGetCurrentTextureStatus_SuccessSuboptimal) {
        std::fprintf(stderr, "[vivid] Failed to acquire surface texture (status %d)\n",
                     (int)surface_tex.status);
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
    frame.view = wgpuTextureCreateView(surface_tex.texture, &view_desc);

    WGPUCommandEncoderDescriptor enc_desc{};
    enc_desc.nextInChain = nullptr;
    enc_desc.label = to_sv("Frame Encoder");
    frame.encoder = wgpuDeviceCreateCommandEncoder(device_, &enc_desc);

    return true;
}

void GpuContext::end_frame(const FrameState& frame) {
    WGPUCommandBufferDescriptor cmd_desc{};
    cmd_desc.nextInChain = nullptr;
    cmd_desc.label = to_sv("Frame Commands");
    WGPUCommandBuffer cmd = wgpuCommandEncoderFinish(frame.encoder, &cmd_desc);

    wgpuQueueSubmit(queue_, 1, &cmd);
    wgpuCommandBufferRelease(cmd);
    wgpuCommandEncoderRelease(frame.encoder);

    // Present BEFORE releasing the surface texture/view
    wgpuSurfacePresent(surface_);

    wgpuTextureViewRelease(frame.view);
    wgpuTextureRelease(frame.texture);
}

void GpuContext::shutdown() {
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

} // namespace vivid
