// Core visual package operator: Feedback — frame feedback / trails: blends the input
// with a decaying, slightly-zoomed history texture. Migrated from the built-in
// FeedbackOp; GLSL translated to WGSL. Self-contained: the op owns its history
// texture and copies output->history each frame (no host ABI needed). Behaviour preserved.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"

#include <array>
#include <string>

namespace {
VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}
const char* kFeedbackWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
struct U { res: vec2f, time: f32, decay: f32, pad: vec4f };
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var u_gen: texture_2d<f32>;
@group(0) @binding(2) var u_samp: sampler;
@group(0) @binding(3) var u_prev: texture_2d<f32>;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let c = inp.uv - vec2f(0.5);
    let puv = vec2f(0.5) + c * 0.985;
    let gen = textureSample(u_gen, u_samp, inp.uv);
    let prev = textureSample(u_prev, u_samp, puv);
    return max(gen, prev * u.decay);
}
)";
}  // namespace

struct FeedbackOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Feedback";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_TRANSFORM;   // ADR-0046
    static constexpr const char* kDisplayName = "Feedback";
    static constexpr const char* kSummary = "Frame feedback / trails: blends the input with a decaying history texture.";
    static constexpr std::array<const char*, 3> kKeywords = {"effect", "feedback", "trails"};
    vivid::Param<float> decay{"decay", 0.5f, 0.f, 1.f};
    bool tried_ = false; std::string err_;   // ADR-0019: surfaced per-frame via report_if_no_pipeline
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUSampler samp_ = nullptr; WGPUBindGroup bg_ = nullptr;
    WGPUTexture hist_ = nullptr; WGPUTextureView hist_view_ = nullptr; uint32_t hw_ = 0, hh_ = 0;
    ~FeedbackOp() override {
        if (hist_view_) wgpuTextureViewRelease(hist_view_); if (hist_) wgpuTextureRelease(hist_);
        if (bg_) wgpuBindGroupRelease(bg_); if (samp_) wgpuSamplerRelease(samp_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override { o.push_back(&decay); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(tex_port("input", VIVID_PORT_INPUT));
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }
    void ensure_hist(const VividGpuContext* c) {
        if (hist_ && hw_ == c->output_width && hh_ == c->output_height) return;
        if (hist_view_) wgpuTextureViewRelease(hist_view_);
        if (hist_) wgpuTextureRelease(hist_);
        WGPUTextureDescriptor td{};
        td.size = { c->output_width, c->output_height, 1 };
        td.mipLevelCount = 1; td.sampleCount = 1; td.dimension = WGPUTextureDimension_2D;
        td.format = c->output_format;
        td.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        hist_ = wgpuDeviceCreateTexture(c->device, &td);
        WGPUTextureViewDescriptor vd{};
        vd.format = c->output_format; vd.dimension = WGPUTextureViewDimension_2D;
        vd.mipLevelCount = 1; vd.arrayLayerCount = 1; vd.aspect = WGPUTextureAspect_All;
        hist_view_ = wgpuTextureCreateView(hist_, &vd);
        hw_ = c->output_width; hh_ = c->output_height;
    }
    bool lazy_init(const VividGpuContext* c) {
        std::string err; sh_ = vivid::gpu::create_shader_checked(c->device, kFeedbackWGSL, "Feedback", err);
        if (!sh_ || !err.empty()) { err_ = vivid::gpu::concise_gpu_error(err); return false; }
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 32, "Feedback U");
        WGPUBindGroupLayoutEntry e[4]{};
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e[0].buffer.type = WGPUBufferBindingType_Uniform; e[0].buffer.minBindingSize = 32;
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment;
        e[1].texture.sampleType = WGPUTextureSampleType_Float; e[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[2].binding = 2; e[2].visibility = WGPUShaderStage_Fragment; e[2].sampler.type = WGPUSamplerBindingType_Filtering;
        e[3].binding = 3; e[3].visibility = WGPUShaderStage_Fragment;
        e[3].texture.sampleType = WGPUTextureSampleType_Float; e[3].texture.viewDimension = WGPUTextureViewDimension_2D;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 4; ld.entries = e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "Feedback Pipeline");
        WGPUSamplerDescriptor sd{}; sd.magFilter = WGPUFilterMode_Linear; sd.minFilter = WGPUFilterMode_Linear;
        sd.addressModeU = WGPUAddressMode_ClampToEdge; sd.addressModeV = WGPUAddressMode_ClampToEdge; sd.maxAnisotropy = 1;
        samp_ = wgpuDeviceCreateSampler(c->device, &sd);
        return pipe_ != nullptr;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (vivid::gpu::report_if_no_pipeline(c, pipe_, err_)) return;
        ensure_hist(c);
        const float d = 0.82f + (c->param_values ? c->param_values[0] : decay.value) * 0.16f;   // 0.82..0.98
        float u[8] = { float(c->output_width), float(c->output_height), float(c->time), d, 0.f, 0.f, 0.f, 0.f };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        const WGPUTextureView gen = (c->input_texture_count > 0) ? c->input_texture_views[0] : c->output_texture_view;
        if (bg_) { wgpuBindGroupRelease(bg_); bg_ = nullptr; }
        WGPUBindGroupEntry be[4]{};
        be[0].binding = 0; be[0].buffer = ubo_; be[0].size = 32;
        be[1].binding = 1; be[1].textureView = gen;
        be[2].binding = 2; be[2].sampler = samp_;
        be[3].binding = 3; be[3].textureView = hist_view_;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 4; bd.entries = be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "Feedback");
        // Copy output -> history for next frame.
        WGPUTexelCopyTextureInfo src{}; src.texture = c->output_texture;
        WGPUTexelCopyTextureInfo dst{}; dst.texture = hist_;
        WGPUExtent3D ext = { c->output_width, c->output_height, 1 };
        wgpuCommandEncoderCopyTextureToTexture(c->command_encoder, &src, &dst, &ext);
    }
};

VIVID_REGISTER(FeedbackOp)
