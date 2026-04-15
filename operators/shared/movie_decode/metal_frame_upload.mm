#include "metal_frame_upload.h"

#ifdef __APPLE__

#include "operator_api/gpu_common.h"

#import <CoreVideo/CoreVideo.h>
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <chrono>

extern "C" void* wgpuDeviceGetNativeMetalDevice(WGPUDevice device);
extern "C" void* wgpuQueueGetNativeMetalCommandQueue(WGPUQueue queue);
extern "C" void* wgpuTextureGetNativeMetalTexture(WGPUTexture texture);

namespace {

static const char* kMovieMetalShader = R"(
#include <metal_stdlib>
using namespace metal;

struct VertexOut {
    float4 position [[position]];
    float2 uv;
};

vertex VertexOut vs_main(uint vertex_id [[vertex_id]]) {
    const float2 pos[6] = {
        float2(-1.0, -1.0), float2( 1.0, -1.0), float2(-1.0,  1.0),
        float2( 1.0, -1.0), float2( 1.0,  1.0), float2(-1.0,  1.0)
    };
    const float2 uv[6] = {
        float2(0.0, 1.0), float2(1.0, 1.0), float2(0.0, 0.0),
        float2(1.0, 1.0), float2(1.0, 0.0), float2(0.0, 0.0)
    };
    VertexOut out;
    out.position = float4(pos[vertex_id], 0.0, 1.0);
    out.uv = uv[vertex_id];
    return out;
}

fragment float4 fs_main(VertexOut in [[stage_in]],
                        texture2d<float> tex [[texture(0)]],
                        sampler samp [[sampler(0)]]) {
    return tex.sample(samp, in.uv);
}
)";

template <typename T>
T bridge_obj(void* value) {
    return (__bridge T)value;
}

bool ensure_cache(MovieMetalUploadState& state, id<MTLDevice> device) {
    if (state.texture_cache) return true;
    CVMetalTextureCacheRef cache = nullptr;
    CVReturn status = CVMetalTextureCacheCreate(kCFAllocatorDefault,
                                                nullptr,
                                                device,
                                                nullptr,
                                                &cache);
    if (status != kCVReturnSuccess || !cache) return false;
    state.texture_cache = cache;
    return true;
}

bool ensure_render_state(MovieMetalUploadState& state,
                         id<MTLDevice> device,
                         MTLPixelFormat dst_format) {
    if (state.pipeline && state.sampler && state.pipeline_format == static_cast<uint64_t>(dst_format)) {
        return true;
    }

    if (state.pipeline) {
        CFRelease(state.pipeline);
        state.pipeline = nullptr;
    }
    if (state.sampler) {
        CFRelease(state.sampler);
        state.sampler = nullptr;
    }

    NSError* error = nil;
    NSString* source = [NSString stringWithUTF8String:kMovieMetalShader];
    id<MTLLibrary> library = [device newLibraryWithSource:source options:nil error:&error];
    if (!library) {
        NSLog(@"[movie_metal_upload] Failed to compile shader: %@", error);
        return false;
    }

    id<MTLFunction> vs = [library newFunctionWithName:@"vs_main"];
    id<MTLFunction> fs = [library newFunctionWithName:@"fs_main"];
    if (!vs || !fs) return false;

    MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
    desc.vertexFunction = vs;
    desc.fragmentFunction = fs;
    desc.colorAttachments[0].pixelFormat = dst_format;

    id<MTLRenderPipelineState> pipeline = [device newRenderPipelineStateWithDescriptor:desc error:&error];
    if (!pipeline) {
        NSLog(@"[movie_metal_upload] Failed to create pipeline: %@", error);
        return false;
    }

    MTLSamplerDescriptor* sampler_desc = [[MTLSamplerDescriptor alloc] init];
    sampler_desc.minFilter = MTLSamplerMinMagFilterLinear;
    sampler_desc.magFilter = MTLSamplerMinMagFilterLinear;
    sampler_desc.sAddressMode = MTLSamplerAddressModeClampToEdge;
    sampler_desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
    id<MTLSamplerState> sampler = [device newSamplerStateWithDescriptor:sampler_desc];
    if (!sampler) return false;

    state.pipeline = (__bridge_retained void*)pipeline;
    state.sampler = (__bridge_retained void*)sampler;
    state.pipeline_format = static_cast<uint64_t>(dst_format);
    return true;
}

} // namespace

void movie_metal_upload_release(MovieMetalUploadState& state) {
    if (state.texture_cache) {
        CFRelease(state.texture_cache);
        state.texture_cache = nullptr;
    }
    if (state.pipeline) {
        CFRelease(state.pipeline);
        state.pipeline = nullptr;
    }
    if (state.sampler) {
        CFRelease(state.sampler);
        state.sampler = nullptr;
    }
    state.pipeline_format = 0;
}

bool movie_upload_cv_pixel_buffer_metal(WGPUDevice device,
                                        WGPUQueue queue,
                                        const MovieTextureState& dst,
                                        void* pixel_buffer,
                                        MovieMetalUploadState& state,
                                        float* elapsed_us,
                                        bool* import_failed) {
    if (elapsed_us) *elapsed_us = 0.0f;
    if (import_failed) *import_failed = false;
    if (!device || !queue || !dst.texture || !pixel_buffer) return false;

    auto t0 = std::chrono::steady_clock::now();

    id<MTLDevice> metal_device = bridge_obj<id<MTLDevice>>(wgpuDeviceGetNativeMetalDevice(device));
    id<MTLCommandQueue> metal_queue = bridge_obj<id<MTLCommandQueue>>(wgpuQueueGetNativeMetalCommandQueue(queue));
    id<MTLTexture> dst_texture = bridge_obj<id<MTLTexture>>(wgpuTextureGetNativeMetalTexture(dst.texture));
    if (!metal_device || !metal_queue || !dst_texture) {
        if (import_failed) *import_failed = true;
        return false;
    }

    if (!ensure_cache(state, metal_device)) {
        if (import_failed) *import_failed = true;
        return false;
    }

    CVPixelBufferRef pixel = static_cast<CVPixelBufferRef>(pixel_buffer);
    const size_t width = CVPixelBufferGetWidth(pixel);
    const size_t height = CVPixelBufferGetHeight(pixel);

    CVMetalTextureRef cv_tex = nullptr;
    CVReturn cv_status = CVMetalTextureCacheCreateTextureFromImage(
        kCFAllocatorDefault,
        static_cast<CVMetalTextureCacheRef>(state.texture_cache),
        pixel,
        nullptr,
        MTLPixelFormatBGRA8Unorm,
        width,
        height,
        0,
        &cv_tex);
    if (cv_status != kCVReturnSuccess || !cv_tex) {
        if (import_failed) *import_failed = true;
        return false;
    }

    id<MTLTexture> src_texture = CVMetalTextureGetTexture(cv_tex);
    if (!src_texture) {
        CFRelease(cv_tex);
        if (import_failed) *import_failed = true;
        return false;
    }

    id<MTLCommandBuffer> command_buffer = [metal_queue commandBuffer];
    if (!command_buffer) {
        CFRelease(cv_tex);
        if (import_failed) *import_failed = true;
        return false;
    }

    const bool can_blit =
        src_texture.pixelFormat == dst_texture.pixelFormat &&
        src_texture.width == dst_texture.width &&
        src_texture.height == dst_texture.height;

    if (can_blit) {
        id<MTLBlitCommandEncoder> blit = [command_buffer blitCommandEncoder];
        if (!blit) {
            CFRelease(cv_tex);
            if (import_failed) *import_failed = true;
            return false;
        }
        [blit copyFromTexture:src_texture
                  sourceSlice:0
                  sourceLevel:0
                 sourceOrigin:MTLOriginMake(0, 0, 0)
                   sourceSize:MTLSizeMake(src_texture.width, src_texture.height, 1)
                    toTexture:dst_texture
             destinationSlice:0
             destinationLevel:0
            destinationOrigin:MTLOriginMake(0, 0, 0)];
        [blit endEncoding];
    } else {
        if (!ensure_render_state(state, metal_device, dst_texture.pixelFormat)) {
            CFRelease(cv_tex);
            if (import_failed) *import_failed = true;
            return false;
        }

        MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
        pass.colorAttachments[0].texture = dst_texture;
        pass.colorAttachments[0].loadAction = MTLLoadActionClear;
        pass.colorAttachments[0].storeAction = MTLStoreActionStore;
        pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);

        id<MTLRenderCommandEncoder> enc = [command_buffer renderCommandEncoderWithDescriptor:pass];
        if (!enc) {
            CFRelease(cv_tex);
            if (import_failed) *import_failed = true;
            return false;
        }
        [enc setRenderPipelineState:bridge_obj<id<MTLRenderPipelineState>>(state.pipeline)];
        [enc setFragmentTexture:src_texture atIndex:0];
        [enc setFragmentSamplerState:bridge_obj<id<MTLSamplerState>>(state.sampler) atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
        [enc endEncoding];
    }

    [command_buffer commit];
    [command_buffer waitUntilCompleted];

    auto t1 = std::chrono::steady_clock::now();
    if (elapsed_us) {
        *elapsed_us = std::chrono::duration<float, std::micro>(t1 - t0).count();
    }

    const bool completed = command_buffer.status == MTLCommandBufferStatusCompleted;
    if (!completed) {
        NSError* error = command_buffer.error;
        if (error) {
            NSLog(@"[movie_metal_upload] Metal transfer failed: %@", error);
        } else {
            NSLog(@"[movie_metal_upload] Metal transfer failed with status %lu",
                  static_cast<unsigned long>(command_buffer.status));
        }
        CFRelease(cv_tex);
        if (import_failed) *import_failed = true;
        return false;
    }

    CFRelease(cv_tex);
    return true;
}

#else

void movie_metal_upload_release(MovieMetalUploadState&) {}

bool movie_upload_cv_pixel_buffer_metal(WGPUDevice,
                                        WGPUQueue,
                                        const MovieTextureState&,
                                        void*,
                                        MovieMetalUploadState&,
                                        float* elapsed_us,
                                        bool* import_failed) {
    if (elapsed_us) *elapsed_us = 0.0f;
    if (import_failed) *import_failed = true;
    return false;
}

#endif
