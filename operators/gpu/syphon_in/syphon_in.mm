#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"

#ifdef __APPLE__

#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <simd/simd.h>
#include <algorithm>
#include <atomic>
#import "SyphonMetalClient.h"
#import "SyphonServerDirectory.h"
#import "SyphonSubclassing.h"

extern "C" void* wgpuDeviceGetNativeMetalDevice(WGPUDevice device);
extern "C" void* wgpuQueueGetNativeMetalCommandQueue(WGPUQueue queue);
extern "C" void* wgpuTextureGetNativeMetalTexture(WGPUTexture texture);

struct SyphonIn : vivid::GpuOperatorBase {
    static constexpr const char* kName = "SyphonIn";
    static constexpr bool kTimeDependent = true;

    SyphonIn()
        : server("server", 0, build_server_choices(list_servers())) {}

    vivid::Param<bool> active{"active", true};
    vivid::Param<int> server;
    vivid::Param<vivid::TextValue> server_name{"server_name", ""};

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&active);
        out.push_back(&server);
        out.push_back(&server_name);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }

    void process_gpu(const VividGpuContext* ctx) override {
        if (!ctx->device || !ctx->queue || !ctx->output_texture) return;

        if (!active.bool_value()) {
            disconnect_client();
            return;
        }

        id<MTLDevice> metal_device = (__bridge id<MTLDevice>)wgpuDeviceGetNativeMetalDevice(ctx->device);
        id<MTLCommandQueue> metal_queue = (__bridge id<MTLCommandQueue>)wgpuQueueGetNativeMetalCommandQueue(ctx->queue);
        id<MTLTexture> output_texture = (__bridge id<MTLTexture>)wgpuTextureGetNativeMetalTexture(ctx->output_texture);
        if (!metal_device || !metal_queue || !output_texture) return;

        const std::string wanted_server = resolve_wanted_server_name();
        if (!ensure_client(metal_device, wanted_server, ctx->time)) return;

        const uint64_t pending = frame_event_counter_.load(std::memory_order_acquire);
        if (pending == consumed_event_counter_) return;

        // Some Syphon senders keep a stable IOSurface ID; force refresh on fetch.
        [client_ invalidateFrame];
        id<MTLTexture> source_texture = [client_ newFrameImage];
        if (!source_texture) return;
        consumed_event_counter_ = pending;

        vivid_request_output_size(ctx,
                                  static_cast<uint32_t>(source_texture.width),
                                  static_cast<uint32_t>(source_texture.height));

        if (!ensure_pipeline(metal_device, output_texture.pixelFormat)) return;

        id<MTLCommandBuffer> command_buffer = [metal_queue commandBuffer];
        if (!command_buffer) return;

        if (source_texture.pixelFormat == output_texture.pixelFormat &&
            source_texture.width == output_texture.width &&
            source_texture.height == output_texture.height) {
            id<MTLBlitCommandEncoder> blit = [command_buffer blitCommandEncoder];
            if (blit) {
                [blit copyFromTexture:source_texture
                          sourceSlice:0
                          sourceLevel:0
                         sourceOrigin:MTLOriginMake(0, 0, 0)
                           sourceSize:MTLSizeMake(source_texture.width, source_texture.height, 1)
                            toTexture:output_texture
                     destinationSlice:0
                     destinationLevel:0
                    destinationOrigin:MTLOriginMake(0, 0, 0)];
                [blit endEncoding];
            }
        } else {
            MTLRenderPassDescriptor* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = output_texture;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 0);

            id<MTLRenderCommandEncoder> enc = [command_buffer renderCommandEncoderWithDescriptor:pass];
            if (!enc) return;
            [enc setRenderPipelineState:pipeline_];
            [enc setFragmentTexture:source_texture atIndex:0];
            if (sampler_) {
                [enc setFragmentSamplerState:sampler_ atIndex:0];
            }

            struct QuadVertex {
                simd_float2 pos;
                simd_float2 uv;
            };
            const QuadVertex verts[6] = {
                {{-1.0f, -1.0f}, {0.0f, 1.0f}},
                {{ 1.0f, -1.0f}, {1.0f, 1.0f}},
                {{-1.0f,  1.0f}, {0.0f, 0.0f}},
                {{ 1.0f, -1.0f}, {1.0f, 1.0f}},
                {{ 1.0f,  1.0f}, {1.0f, 0.0f}},
                {{-1.0f,  1.0f}, {0.0f, 0.0f}},
            };
            [enc setVertexBytes:verts length:sizeof(verts) atIndex:0];
            [enc drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6];
            [enc endEncoding];
        }
        [command_buffer commit];
    }

    ~SyphonIn() override {
        disconnect_client();
        pipeline_ = nil;
        sampler_ = nil;
        pipeline_format_ = MTLPixelFormatInvalid;
    }

private:
    SyphonMetalClient* client_ = nil;
    id<MTLRenderPipelineState> pipeline_ = nil;
    id<MTLSamplerState> sampler_ = nil;
    MTLPixelFormat pipeline_format_ = MTLPixelFormatInvalid;
    std::string connected_server_name_;
    double last_lookup_time_ = -1000.0;
    std::atomic<uint64_t> frame_event_counter_{0};
    uint64_t consumed_event_counter_ = 0;

    static std::vector<std::string> list_servers() {
        std::vector<std::string> out;
        NSArray<NSDictionary<NSString*, id<NSCoding>>*>* servers =
            [SyphonServerDirectory sharedDirectory].servers;
        if (!servers) return out;
        for (NSDictionary<NSString*, id<NSCoding>>* desc in servers) {
            id value = desc[SyphonServerDescriptionNameKey];
            if ([value isKindOfClass:[NSString class]]) {
                out.emplace_back([(NSString*)value UTF8String]);
            }
        }
        return out;
    }

    static std::vector<std::string> build_server_choices(const std::vector<std::string>& names) {
        std::vector<std::string> labels;
        labels.reserve(names.size() + 1);
        labels.emplace_back("Auto (First Available)");
        labels.insert(labels.end(), names.begin(), names.end());
        return labels;
    }

    std::string resolve_wanted_server_name() const {
        int idx = server.int_value();
        const auto servers = list_servers();
        if (idx > 0) {
            const size_t name_idx = static_cast<size_t>(idx - 1);
            if (name_idx < servers.size()) {
                return servers[name_idx];
            }
        }
        return server_name.str_value;
    }

    static NSDictionary<NSString*, id<NSCoding>>* find_server(const std::string& wanted_name) {
        NSArray<NSDictionary<NSString*, id<NSCoding>>*>* servers =
            [SyphonServerDirectory sharedDirectory].servers;
        if (!servers || servers.count == 0) return nil;

        if (wanted_name.empty()) {
            return servers.firstObject;
        }

        NSString* wanted = [NSString stringWithUTF8String:wanted_name.c_str()];
        for (NSDictionary<NSString*, id<NSCoding>>* desc in servers) {
            id value = desc[SyphonServerDescriptionNameKey];
            if ([value isKindOfClass:[NSString class]] && [(NSString*)value isEqualToString:wanted]) {
                return desc;
            }
        }
        return nil;
    }

    bool ensure_client(id<MTLDevice> device, const std::string& wanted_name, double now) {
        if (client_ && connected_server_name_ == wanted_name) {
            return true;
        }

        if (now - last_lookup_time_ < 0.5) {
            return client_ != nil;
        }
        last_lookup_time_ = now;

        disconnect_client();
        NSDictionary<NSString*, id<NSCoding>>* desc = find_server(wanted_name);
        if (!desc) return false;

        frame_event_counter_.store(0, std::memory_order_release);
        consumed_event_counter_ = 0;
        client_ = [[SyphonMetalClient alloc] initWithServerDescription:desc
                                                                device:device
                                                               options:nil
                                                       newFrameHandler:^(SyphonMetalClient* c) {
                                                           (void)c;
                                                           frame_event_counter_.fetch_add(1, std::memory_order_release);
                                                       }];
        if (!client_) return false;

        connected_server_name_ = wanted_name;
        return true;
    }

    bool ensure_pipeline(id<MTLDevice> device, MTLPixelFormat format) {
        if (pipeline_ && pipeline_format_ == format) {
            return true;
        }
        pipeline_ = nil;
        sampler_ = nil;
        pipeline_format_ = MTLPixelFormatInvalid;

        static NSString* kShaderSource =
            @"#include <metal_stdlib>\n"
             "using namespace metal;\n"
             "struct VIn { float2 pos; float2 uv; };\n"
             "struct VOut { float4 position [[position]]; float2 uv; };\n"
             "vertex VOut vivid_syphon_in_vs(uint vid [[vertex_id]], constant VIn* v [[buffer(0)]]) {\n"
             "  VOut o; o.position = float4(v[vid].pos, 0.0, 1.0); o.uv = v[vid].uv; return o;\n"
             "}\n"
             "fragment float4 vivid_syphon_in_fs(VOut in [[stage_in]],\n"
             "                                   texture2d<float> src [[texture(0)]],\n"
             "                                   sampler samp [[sampler(0)]]) {\n"
             "  return src.sample(samp, in.uv);\n"
             "}\n";

        NSError* error = nil;
        id<MTLLibrary> lib = [device newLibraryWithSource:kShaderSource options:nil error:&error];
        if (!lib) {
            if (error) NSLog(@"[SyphonIn] Failed to compile Metal shader: %@", error);
            return false;
        }
        id<MTLFunction> vs = [lib newFunctionWithName:@"vivid_syphon_in_vs"];
        id<MTLFunction> fs = [lib newFunctionWithName:@"vivid_syphon_in_fs"];
        if (!vs || !fs) return false;

        MTLRenderPipelineDescriptor* desc = [[MTLRenderPipelineDescriptor alloc] init];
        desc.vertexFunction = vs;
        desc.fragmentFunction = fs;
        desc.colorAttachments[0].pixelFormat = format;
        pipeline_ = [device newRenderPipelineStateWithDescriptor:desc error:&error];
        if (!pipeline_) {
            if (error) NSLog(@"[SyphonIn] Failed to create pipeline: %@", error);
            return false;
        }

        MTLSamplerDescriptor* samp_desc = [[MTLSamplerDescriptor alloc] init];
        samp_desc.minFilter = MTLSamplerMinMagFilterLinear;
        samp_desc.magFilter = MTLSamplerMinMagFilterLinear;
        samp_desc.sAddressMode = MTLSamplerAddressModeClampToEdge;
        samp_desc.tAddressMode = MTLSamplerAddressModeClampToEdge;
        sampler_ = [device newSamplerStateWithDescriptor:samp_desc];

        pipeline_format_ = format;
        return true;
    }

    void disconnect_client() {
        if (client_) {
            [client_ stop];
            client_ = nil;
        }
        connected_server_name_.clear();
        frame_event_counter_.store(0, std::memory_order_release);
        consumed_event_counter_ = 0;
    }
};

VIVID_REGISTER(SyphonIn)

#endif
