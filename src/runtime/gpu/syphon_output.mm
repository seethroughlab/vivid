#include "runtime/gpu/syphon_output.h"

#include "runtime/gpu/metal_interop.h"

#if defined(__APPLE__)

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#import "SyphonMetalServer.h"

namespace vivid {

struct SyphonOutput::Impl {
    SyphonMetalServer* server = nil;
    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    std::string server_name;
};

SyphonOutput::SyphonOutput() : impl_(new Impl()) {}

SyphonOutput::~SyphonOutput() {
    shutdown();
    delete impl_;
    impl_ = nullptr;
}

bool SyphonOutput::publish(bool enabled,
                           const std::string& server_name,
                           WGPUDevice device,
                           WGPUQueue queue,
                           WGPUTexture texture,
                           uint32_t width,
                           uint32_t height) {
    if (!impl_) return false;

    if (!enabled || !device || !queue || !texture || width == 0 || height == 0) {
        if (impl_->server) {
            [impl_->server stop];
            impl_->server = nil;
            impl_->device = nil;
            impl_->queue = nil;
            impl_->server_name.clear();
        }
        return false;
    }

    if (!metal_interop::available()) {
        return false;
    }

    auto* metal_device = (__bridge id<MTLDevice>)metal_interop::native_metal_device(device);
    auto* metal_queue = (__bridge id<MTLCommandQueue>)metal_interop::native_metal_command_queue(queue);
    auto* metal_texture = (__bridge id<MTLTexture>)metal_interop::native_metal_texture(texture);
    if (!metal_device || !metal_queue || !metal_texture) {
        return false;
    }

    const std::string effective_name = server_name.empty() ? std::string("Vivid Output") : server_name;
    const bool needs_recreate = (impl_->server == nil) || (impl_->device != metal_device);
    if (needs_recreate) {
        if (impl_->server) {
            [impl_->server stop];
            impl_->server = nil;
        }
        impl_->device = metal_device;
        impl_->server_name = effective_name;
        impl_->server = [[SyphonMetalServer alloc] initWithName:[NSString stringWithUTF8String:effective_name.c_str()]
                                                         device:metal_device
                                                        options:nil];
        if (!impl_->server) {
            return false;
        }
    } else if (impl_->server_name != effective_name) {
        impl_->server_name = effective_name;
        impl_->server.name = [NSString stringWithUTF8String:effective_name.c_str()];
    }
    impl_->queue = metal_queue;

    id<MTLCommandBuffer> command_buffer = [impl_->queue commandBuffer];
    if (!command_buffer) {
        return false;
    }

    NSRect region = NSMakeRect(0, 0, static_cast<CGFloat>(width), static_cast<CGFloat>(height));
    [impl_->server publishFrameTexture:metal_texture
                       onCommandBuffer:command_buffer
                           imageRegion:region
                               flipped:NO];
    [command_buffer commit];
    return true;
}

void SyphonOutput::shutdown() {
    if (!impl_) return;
    if (impl_->server) {
        [impl_->server stop];
        impl_->server = nil;
    }
    impl_->device = nil;
    impl_->queue = nil;
    impl_->server_name.clear();
}

} // namespace vivid

#else

namespace vivid {

struct SyphonOutput::Impl {};

SyphonOutput::SyphonOutput() = default;
SyphonOutput::~SyphonOutput() = default;

bool SyphonOutput::publish(bool,
                           const std::string&,
                           WGPUDevice,
                           WGPUQueue,
                           WGPUTexture,
                           uint32_t,
                           uint32_t) {
    return false;
}

void SyphonOutput::shutdown() {}

} // namespace vivid

#endif
