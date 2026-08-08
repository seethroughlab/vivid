// Core visual package operator: TimeMachine — slit-scan / time-displacement. Keeps a ring buffer of
// past source frames (a texture_2d_array cache) and, per output pixel, samples a DIFFERENT point in
// that history chosen by a grayscale retrieval map's luminance. Bright map areas read the present,
// dark areas reach into the past — slit-scan, frozen-time smears, temporal echoes.
// Ported from vivid-classic operators/gpu/time_machine to main's package operator ABI (template:
// feedback.cpp / bloom.cpp). `depth` and `offset` are mappable Params (audio-reactive time travel).
// The `map` input is optional — with nothing wired, the source's own luminance drives the retrieval.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"

#include <algorithm>
#include <array>
#include <string>
#include <vector>

namespace {
VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}

// Passthrough blit: copy the source into one array layer of the history cache.
const char* kBlitWGSL = R"(
@group(0) @binding(0) var samp: sampler;
@group(0) @binding(1) var src: texture_2d<f32>;
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f { return textureSample(src, samp, inp.uv); }
)";

// Slit-scan: per-pixel temporal displacement. disp = luminance(map); age reaches back into the ring.
const char* kSlitWGSL = R"(
struct U { depth: f32, offset: f32, frame_count: u32, write_index: u32, filled: u32, p0: u32, p1: u32, p2: u32 };
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var samp: sampler;
@group(0) @binding(2) var cacheTex: texture_2d_array<f32>;
@group(0) @binding(3) var mapTex: texture_2d<f32>;
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let disp = dot(textureSample(mapTex, samp, inp.uv).rgb, vec3f(0.2126, 0.7152, 0.0722));
    let max_age = f32(max(u.filled, 1u) - 1u);
    let age = clamp((u.offset + u.depth * (1.0 - disp)) * max_age, 0.0, max_age);
    let age_lo = floor(age);
    let age_hi = min(age_lo + 1.0, max_age);
    let t = age - age_lo;
    let ia = (u.write_index + u.frame_count - u32(age_lo)) % u.frame_count;
    let ib = (u.write_index + u.frame_count - u32(age_hi)) % u.frame_count;
    let a = textureSample(cacheTex, samp, inp.uv, i32(ia));
    let b = textureSample(cacheTex, samp, inp.uv, i32(ib));
    return mix(a, b, t);
}
)";
}  // namespace

struct TimeMachineOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "TimeMachine";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_TRANSFORM;
    static constexpr const char* kDisplayName = "Time Machine";
    static constexpr const char* kSummary = "Slit-scan / time-displacement: each pixel samples a different point in a history buffer, chosen by a grayscale map.";
    static constexpr std::array<const char*, 3> kKeywords = {"effect", "slitscan", "time"};

    vivid::Param<float> depth  {"depth",  1.0f, 0.f, 1.f};   // max temporal reach (fraction of history)
    vivid::Param<int>   frames {"frames", 30, 2, 120};       // history length
    vivid::Param<float> offset {"offset", 0.f, 0.f, 1.f};    // shift the whole read-head back in time

    bool tried_ = false;
    WGPUShaderModule sh_blit_ = nullptr, sh_slit_ = nullptr;
    WGPUBindGroupLayout bgl_blit_ = nullptr, bgl_slit_ = nullptr;
    WGPUPipelineLayout pl_blit_ = nullptr, pl_slit_ = nullptr;
    WGPURenderPipeline pipe_blit_ = nullptr, pipe_slit_ = nullptr;
    WGPUBuffer ubo_ = nullptr; WGPUSampler samp_ = nullptr;
    WGPUTexture cache_ = nullptr; WGPUTextureView cache_array_view_ = nullptr;
    std::vector<WGPUTextureView> layer_views_;
    uint32_t cw_ = 0, ch_ = 0, cn_ = 0;   // cache width/height/layers
    uint32_t write_index_ = 0, filled_ = 0;
    std::vector<WGPUBindGroup> frame_bgs_;

    static constexpr uint32_t kCacheMaxEdge = 640;   // bound history-cache memory regardless of output res

    ~TimeMachineOp() override {
        release_frame_bgs();
        release_cache();
        if (samp_) wgpuSamplerRelease(samp_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_blit_) wgpuRenderPipelineRelease(pipe_blit_); if (pipe_slit_) wgpuRenderPipelineRelease(pipe_slit_);
        if (pl_blit_) wgpuPipelineLayoutRelease(pl_blit_); if (pl_slit_) wgpuPipelineLayoutRelease(pl_slit_);
        if (bgl_blit_) wgpuBindGroupLayoutRelease(bgl_blit_); if (bgl_slit_) wgpuBindGroupLayoutRelease(bgl_slit_);
        if (sh_blit_) wgpuShaderModuleRelease(sh_blit_); if (sh_slit_) wgpuShaderModuleRelease(sh_slit_);
    }
    void release_frame_bgs() { for (auto bg : frame_bgs_) if (bg) wgpuBindGroupRelease(bg); frame_bgs_.clear(); }
    void release_cache() {
        for (auto v : layer_views_) if (v) wgpuTextureViewRelease(v);
        layer_views_.clear();
        if (cache_array_view_) { wgpuTextureViewRelease(cache_array_view_); cache_array_view_ = nullptr; }
        if (cache_) { wgpuTextureRelease(cache_); cache_ = nullptr; }
    }

    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        o.push_back(&depth); o.push_back(&frames); o.push_back(&offset);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(tex_port("input", VIVID_PORT_INPUT));
        o.push_back(tex_port("map", VIVID_PORT_INPUT));   // optional grayscale retrieval map
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }

    void ensure_cache(const VividGpuContext* c, uint32_t layers) {
        const uint32_t longest = std::max(c->output_width, c->output_height);
        uint32_t w = c->output_width, h = c->output_height;
        if (longest > kCacheMaxEdge) {
            const double s = static_cast<double>(kCacheMaxEdge) / longest;
            w = std::max<uint32_t>(1, static_cast<uint32_t>(c->output_width * s));
            h = std::max<uint32_t>(1, static_cast<uint32_t>(c->output_height * s));
        }
        if (cache_ && cw_ == w && ch_ == h && cn_ == layers) return;
        release_cache();
        WGPUTextureDescriptor td{};
        td.size = { w, h, layers };
        td.mipLevelCount = 1; td.sampleCount = 1; td.dimension = WGPUTextureDimension_2D;
        td.format = c->output_format;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_RenderAttachment;
        cache_ = wgpuDeviceCreateTexture(c->device, &td);
        WGPUTextureViewDescriptor av{};
        av.format = c->output_format; av.dimension = WGPUTextureViewDimension_2DArray;
        av.mipLevelCount = 1; av.baseArrayLayer = 0; av.arrayLayerCount = layers; av.aspect = WGPUTextureAspect_All;
        cache_array_view_ = wgpuTextureCreateView(cache_, &av);
        layer_views_.resize(layers);
        for (uint32_t i = 0; i < layers; ++i) {
            WGPUTextureViewDescriptor lv{};
            lv.format = c->output_format; lv.dimension = WGPUTextureViewDimension_2D;
            lv.mipLevelCount = 1; lv.baseArrayLayer = i; lv.arrayLayerCount = 1; lv.aspect = WGPUTextureAspect_All;
            layer_views_[i] = wgpuTextureCreateView(cache_, &lv);
        }
        cw_ = w; ch_ = h; cn_ = layers; write_index_ = 0; filled_ = 0;
    }

    bool lazy_init(const VividGpuContext* c) {
        std::string err;
        sh_blit_ = vivid::gpu::create_shader_checked(c->device, kBlitWGSL, "TimeMachine.blit", err);
        sh_slit_ = vivid::gpu::create_shader_checked(c->device, kSlitWGSL, "TimeMachine.slit", err);
        if (!sh_blit_ || !sh_slit_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 32, "TimeMachine U");
        // blit BGL: sampler(0), tex(1)
        WGPUBindGroupLayoutEntry be[2]{};
        be[0].binding = 0; be[0].visibility = WGPUShaderStage_Fragment; be[0].sampler.type = WGPUSamplerBindingType_Filtering;
        be[1].binding = 1; be[1].visibility = WGPUShaderStage_Fragment;
        be[1].texture.sampleType = WGPUTextureSampleType_Float; be[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        WGPUBindGroupLayoutDescriptor bld{}; bld.entryCount = 2; bld.entries = be;
        bgl_blit_ = wgpuDeviceCreateBindGroupLayout(c->device, &bld);
        // slit BGL: ubo(0), sampler(1), cache 2DArray(2), map 2D(3)
        WGPUBindGroupLayoutEntry se[4]{};
        se[0].binding = 0; se[0].visibility = WGPUShaderStage_Fragment;
        se[0].buffer.type = WGPUBufferBindingType_Uniform; se[0].buffer.minBindingSize = 32;
        se[1].binding = 1; se[1].visibility = WGPUShaderStage_Fragment; se[1].sampler.type = WGPUSamplerBindingType_Filtering;
        se[2].binding = 2; se[2].visibility = WGPUShaderStage_Fragment;
        se[2].texture.sampleType = WGPUTextureSampleType_Float; se[2].texture.viewDimension = WGPUTextureViewDimension_2DArray;
        se[3].binding = 3; se[3].visibility = WGPUShaderStage_Fragment;
        se[3].texture.sampleType = WGPUTextureSampleType_Float; se[3].texture.viewDimension = WGPUTextureViewDimension_2D;
        WGPUBindGroupLayoutDescriptor sld{}; sld.entryCount = 4; sld.entries = se;
        bgl_slit_ = wgpuDeviceCreateBindGroupLayout(c->device, &sld);
        WGPUPipelineLayoutDescriptor pb{}; pb.bindGroupLayoutCount = 1; pb.bindGroupLayouts = &bgl_blit_;
        pl_blit_ = wgpuDeviceCreatePipelineLayout(c->device, &pb);
        WGPUPipelineLayoutDescriptor ps{}; ps.bindGroupLayoutCount = 1; ps.bindGroupLayouts = &bgl_slit_;
        pl_slit_ = wgpuDeviceCreatePipelineLayout(c->device, &ps);
        pipe_blit_ = vivid::gpu::create_pipeline(c->device, sh_blit_, pl_blit_, c->output_format, "TimeMachine.blit");
        pipe_slit_ = vivid::gpu::create_pipeline(c->device, sh_slit_, pl_slit_, c->output_format, "TimeMachine.slit");
        WGPUSamplerDescriptor sd{}; sd.magFilter = WGPUFilterMode_Linear; sd.minFilter = WGPUFilterMode_Linear;
        sd.addressModeU = WGPUAddressMode_ClampToEdge; sd.addressModeV = WGPUAddressMode_ClampToEdge; sd.maxAnisotropy = 1;
        samp_ = wgpuDeviceCreateSampler(c->device, &sd);
        return pipe_blit_ && pipe_slit_;
    }

    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_slit_) return;
        auto pv = [&](int i, float def) { return c->param_values ? c->param_values[i] : def; };
        const int frames_req = c->param_values ? static_cast<int>(c->param_values[1])
                                                : static_cast<int>(frames.value);
        const uint32_t layers = static_cast<uint32_t>(std::clamp(frames_req, 2, 120));
        ensure_cache(c, layers);
        release_frame_bgs();

        const WGPUTextureView src = (c->input_texture_count > 0) ? c->input_texture_views[0] : c->output_texture_view;
        const WGPUTextureView map = (c->input_texture_count > 1) ? c->input_texture_views[1] : src;

        // 1) blit source into the current write layer, then account it as filled.
        WGPUBindGroupEntry bbe[2]{};
        bbe[0].binding = 0; bbe[0].sampler = samp_;
        bbe[1].binding = 1; bbe[1].textureView = src;
        WGPUBindGroupDescriptor bbd{}; bbd.layout = bgl_blit_; bbd.entryCount = 2; bbd.entries = bbe;
        WGPUBindGroup bg_blit = wgpuDeviceCreateBindGroup(c->device, &bbd); frame_bgs_.push_back(bg_blit);
        vivid::gpu::run_pass(c->command_encoder, pipe_blit_, bg_blit, layer_views_[write_index_], "TimeMachine.blit");
        filled_ = std::min(filled_ + 1, cn_);

        // 2) slit-scan read across the ring into the output.
        struct U { float depth, offset; uint32_t frame_count, write_index, filled, p0, p1, p2; };
        U u{ pv(0, depth.value), pv(2, offset.value), cn_, write_index_, filled_, 0, 0, 0 };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, &u, sizeof(u));
        WGPUBindGroupEntry sbe[4]{};
        sbe[0].binding = 0; sbe[0].buffer = ubo_; sbe[0].size = 32;
        sbe[1].binding = 1; sbe[1].sampler = samp_;
        sbe[2].binding = 2; sbe[2].textureView = cache_array_view_;
        sbe[3].binding = 3; sbe[3].textureView = map;
        WGPUBindGroupDescriptor sbd{}; sbd.layout = bgl_slit_; sbd.entryCount = 4; sbd.entries = sbe;
        WGPUBindGroup bg_slit = wgpuDeviceCreateBindGroup(c->device, &sbd); frame_bgs_.push_back(bg_slit);
        vivid::gpu::run_pass(c->command_encoder, pipe_slit_, bg_slit, c->output_texture_view, "TimeMachine.slit");

        // advance the ring write head for next frame.
        write_index_ = (write_index_ + 1) % cn_;
    }
};

VIVID_REGISTER(TimeMachineOp)
