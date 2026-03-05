#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"

#ifdef __APPLE__

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <cstdio>
#import "SyphonMetalServer.h"

extern "C" void* wgpuDeviceGetNativeMetalDevice(WGPUDevice device);
extern "C" void* wgpuQueueGetNativeMetalCommandQueue(WGPUQueue queue);
extern "C" void* wgpuTextureGetNativeMetalTexture(WGPUTexture texture);

struct SyphonOut : vivid::OperatorBase {
    static constexpr const char* kName = "SyphonOut";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_GPU;
    static constexpr bool kTimeDependent = true;

    vivid::Param<bool> active{"active", true};
    vivid::Param<vivid::TextValue> server_name{"server_name", "Vivid Output"};
    double last_log_time_ = -1000.0;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&active);
        out.push_back(&server_name);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT});
    }

    void process(const VividProcessContext* ctx) override {
        VividGpuState* gpu = vivid_gpu(ctx);
        if (!gpu || !gpu->device || !gpu->queue) return;

        if (!active.bool_value()) {
            stop_server();
            return;
        }
        if (!gpu->input_textures || gpu->input_texture_count < 1) return;
        WGPUTexture in_tex = gpu->input_textures[0];
        if (!in_tex) return;

        id<MTLDevice> metal_device = (__bridge id<MTLDevice>)wgpuDeviceGetNativeMetalDevice(gpu->device);
        id<MTLCommandQueue> metal_queue = (__bridge id<MTLCommandQueue>)wgpuQueueGetNativeMetalCommandQueue(gpu->queue);
        id<MTLTexture> metal_texture = (__bridge id<MTLTexture>)wgpuTextureGetNativeMetalTexture(in_tex);
        if (!metal_device || !metal_queue || !metal_texture) return;
        const uint32_t in_w = static_cast<uint32_t>(metal_texture.width);
        const uint32_t in_h = static_cast<uint32_t>(metal_texture.height);
        if (in_w == 0 || in_h == 0) return;

        const std::string effective_name = server_name.str_value.empty() ? std::string("Vivid Output")
                                                                          : server_name.str_value;
        if (!ensure_server(metal_device, effective_name)) {
            if (ctx->time - last_log_time_ > 1.0) {
                last_log_time_ = ctx->time;
                std::fprintf(stderr, "[vivid] SyphonOut: failed to ensure server '%s'\n",
                    effective_name.c_str());
            }
            return;
        }

        if (!ensure_copy_pipeline(metal_device)) return;

        id<MTLCommandBuffer> command_buffer = [metal_queue commandBuffer];
        if (!command_buffer) {
            if (ctx->time - last_log_time_ > 1.0) {
                last_log_time_ = ctx->time;
                std::fprintf(stderr, "[vivid] SyphonOut: failed to create command buffer\n");
            }
            return;
        }

        id<MTLTexture> publish_tex = metal_texture;
        if (metal_texture.pixelFormat != MTLPixelFormatBGRA8Unorm) {
            if (!ensure_publish_texture(metal_device, in_w, in_h)) {
                if (ctx->time - last_log_time_ > 1.0) {
                    last_log_time_ = ctx->time;
                    std::fprintf(stderr,
                        "[vivid] SyphonOut: failed to allocate BGRA8 publish texture (%ux%u)\n",
                        in_w, in_h);
                }
                return;
            }
            if (!convert_texture(command_buffer, metal_texture, publish_texture_)) {
                if (ctx->time - last_log_time_ > 1.0) {
                    last_log_time_ = ctx->time;
                    std::fprintf(stderr, "[vivid] SyphonOut: GPU convert to BGRA8 failed\n");
                }
                return;
            }
            publish_tex = publish_texture_;
        }

        NSRect region = NSMakeRect(0, 0, static_cast<CGFloat>(publish_tex.width), static_cast<CGFloat>(publish_tex.height));
        [server_ publishFrameTexture:publish_tex
                     onCommandBuffer:command_buffer
                         imageRegion:region
                             flipped:NO];
        [command_buffer commit];
    }

    ~SyphonOut() override {
        stop_server();
    }

private:
    SyphonMetalServer* server_ = nil;
    id<MTLDevice> server_device_ = nil;
    std::string server_name_active_;
    id<MTLRenderPipelineState> copy_pipeline_ = nil;
    id<MTLSamplerState> copy_sampler_ = nil;
    id<MTLTexture> publish_texture_ = nil;
    uint32_t publish_w_ = 0;
    uint32_t publish_h_ = 0;

    bool ensure_server(id<MTLDevice> device, const std::string& name) {
        const bool needs_recreate = (server_ == nil) || (server_device_ != device);
        if (needs_recreate) {
            stop_server();
            server_device_ = device;
            server_ = [[SyphonMetalServer alloc] initWithName:[NSString stringWithUTF8String:name.c_str()]
                                                       device:device
                                                      options:nil];
            if (!server_) return false;
            server_name_active_ = name;
            return true;
        }
        if (server_name_active_ != name) {
            server_name_active_ = name;
            server_.name = [NSString stringWithUTF8String:name.c_str()];
        }
        return true;
    }

    bool ensure_copy_pipeline(id<MTLDevice> device) {
        if (copy_pipeline_ && copy_sampler_) return true;
        NSError* error = nil;
        NSString* src = @R"(
            #include <metal_stdlib>
            using namespace metal;
            struct VSOut { float4 position [[position]]; float2 uv; };
            vertex VSOut vs_main(uint vid [[vertex_id]]) {
                float2 p[3] = { float2(-1.0, -1.0), float2(3.0, -1.0), float2(-1.0, 3.0) };
                float2 uv[3] = { float2(0.0, 1.0), float2(2.0, 1.0), float2(0.0, -1.0) };
                VSOut o; o.position = float4(p[vid], 0.0, 1.0); o.uv = uv[vid]; return o;
            }
            fragment float4 fs_main(VSOut in [[stage_in]],
                                    texture2d<float> tex [[texture(0)]],
                                    sampler s [[sampler(0)]]) {
                return tex.sample(s, in.uv);
            }
        )";
        id<MTLLibrary> lib = [device newLibraryWithSource:src options:nil error:&error];
        if (!lib) {
            if (error) NSLog(@"[SyphonOut] Failed to compile Metal shader: %@", error);
            return false;
        }
        id<MTLFunction> vs = [lib newFunctionWithName:@"vs_main"];
        id<MTLFunction> fs = [lib newFunctionWithName:@"fs_main"];
        if (!vs || !fs) return false;

        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = vs;
        desc.fragmentFunction = fs;
        desc.colorAttachments[0].pixelFormat = MTLPixelFormatBGRA8Unorm;
        copy_pipeline_ = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!copy_pipeline_) {
            if (error) NSLog(@"[SyphonOut] Failed to create pipeline: %@", error);
            return false;
        }
        MTLSamplerDescriptor* sdesc = [[MTLSamplerDescriptor alloc] init];
        sdesc.minFilter = MTLSamplerMinMagFilterLinear;
        sdesc.magFilter = MTLSamplerMinMagFilterLinear;
        sdesc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        sdesc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        copy_sampler_ = [device newSamplerStateWithDescriptor:sdesc];
        return copy_sampler_ != nil;
    }

    bool ensure_publish_texture(id<MTLDevice> device, uint32_t w, uint32_t h) {
        if (publish_texture_ && publish_w_ == w && publish_h_ == h) return true;
        MTLTextureDescriptor* desc =
            [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                               width:w
                                                              height:h
                                                           mipmapped:NO];
        desc.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
        desc.storageMode = MTLStorageModePrivate;
        desc.resourceOptions = MTLResourceStorageModePrivate;
        publish_texture_ = [device newTextureWithDescriptor:desc];
        if (!publish_texture_) return false;
        publish_w_ = w;
        publish_h_ = h;
        return true;
    }

    bool convert_texture(id<MTLCommandBuffer> cb, id<MTLTexture> src, id<MTLTexture> dst) {
        if (!cb || !src || !dst || !copy_pipeline_ || !copy_sampler_) return false;
        MTLRenderPassDescriptor* rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = dst;
        rp.colorAttachments[0].loadAction = MTLLoadActionDontCare;
        rp.colorAttachments[0].storeAction = MTLStoreActionStore;
        id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
        if (!enc) return false;
        [enc setRenderPipelineState:copy_pipeline_];
        [enc setFragmentTexture:src atIndex:0];
        [enc setFragmentSamplerState:copy_sampler_ atIndex:0];
        [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:3];
        [enc endEncoding];
        return true;
    }

    void stop_server() {
        if (server_) {
            [server_ stop];
            server_ = nil;
        }
        server_device_ = nil;
        server_name_active_.clear();
        copy_pipeline_ = nil;
        copy_sampler_ = nil;
        publish_texture_ = nil;
        publish_w_ = 0;
        publish_h_ = 0;
    }
};

VIVID_REGISTER(SyphonOut)

#endif
