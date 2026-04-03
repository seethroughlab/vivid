#include "runtime/gpu/metal_interop.h"

#if defined(__APPLE__)

#include <dlfcn.h>
#include <mutex>

namespace {

using DeviceFn = void* (*)(WGPUDevice);
using QueueFn = void* (*)(WGPUQueue);
using TextureFn = void* (*)(WGPUTexture);

struct MetalInteropFns {
    DeviceFn device_fn = nullptr;
    QueueFn queue_fn = nullptr;
    TextureFn texture_fn = nullptr;
    bool resolved = false;
};

MetalInteropFns& fns() {
    static MetalInteropFns s;
    static std::once_flag once;
    std::call_once(once, [] {
        s.device_fn = reinterpret_cast<DeviceFn>(dlsym(RTLD_DEFAULT, "wgpuDeviceGetNativeMetalDevice"));
        s.queue_fn = reinterpret_cast<QueueFn>(dlsym(RTLD_DEFAULT, "wgpuQueueGetNativeMetalCommandQueue"));
        s.texture_fn = reinterpret_cast<TextureFn>(dlsym(RTLD_DEFAULT, "wgpuTextureGetNativeMetalTexture"));
        s.resolved = true;
    });
    return s;
}

} // namespace

namespace vivid::metal_interop {

bool available() {
    const auto& api = fns();
    return api.resolved && api.device_fn && api.queue_fn && api.texture_fn;
}

void* native_metal_device(WGPUDevice device) {
    const auto& api = fns();
    return (api.device_fn && device) ? api.device_fn(device) : nullptr;
}

void* native_metal_command_queue(WGPUQueue queue) {
    const auto& api = fns();
    return (api.queue_fn && queue) ? api.queue_fn(queue) : nullptr;
}

void* native_metal_texture(WGPUTexture texture) {
    const auto& api = fns();
    return (api.texture_fn && texture) ? api.texture_fn(texture) : nullptr;
}

} // namespace vivid::metal_interop

#else

namespace vivid::metal_interop {

bool available() { return false; }
void* native_metal_device(WGPUDevice) { return nullptr; }
void* native_metal_command_queue(WGPUQueue) { return nullptr; }
void* native_metal_texture(WGPUTexture) { return nullptr; }

} // namespace vivid::metal_interop

#endif
