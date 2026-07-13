// Core visual package operator: Gradient — a clean 2-colour linear/radial gradient
// generator (a flat source to composite over / tint a chain). Migrated verbatim from
// the built-in GradientOp (builtin_ops.cpp); behaviour unchanged. Classic gradient.wgsl.
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
const char* kGradientWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
struct U { res: vec2f, time: f32, mode: f32, center: vec2f, angle: f32, scale: f32, colA: vec4f, colB: vec4f };
@group(0) @binding(0) var<uniform> u: U;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    var t: f32;
    if (u.mode < 0.5) {                                  // linear
        let a = u.angle * 6.2831853;
        t = dot(inp.uv - u.center, vec2f(cos(a), sin(a))) * (0.5 + u.scale * 2.0) + 0.5;
    } else {                                             // radial
        let d = (inp.uv - u.center) * vec2f(u.res.x / max(u.res.y, 1.0), 1.0);
        t = length(d) * (0.7 + u.scale * 3.0);
    }
    return vec4f(mix(u.colA.rgb, u.colB.rgb, clamp(t, 0.0, 1.0)), 1.0);
}
)";
}  // namespace

struct GradientOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Gradient";
    static constexpr const char* kDisplayName = "Gradient";
    static constexpr const char* kSummary = "A clean 2-colour linear/radial gradient (a flat source, not a field).";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "gradient", "color"};
    vivid::Param<float> mode{"mode", 0.f, 0.f, 1.f};
    vivid::Param<float> cx{"cx", 0.5f, 0.f, 1.f}, cy{"cy", 0.5f, 0.f, 1.f};
    vivid::Param<float> angle{"angle", 0.f, 0.f, 1.f}, scale{"scale", 0.5f, 0.f, 1.f};
    vivid::Param<float> ar{"ar", 0.1f, 0.f, 1.f}, ag{"ag", 0.1f, 0.f, 1.f}, ab{"ab", 0.35f, 0.f, 1.f};
    vivid::Param<float> br{"br", 0.9f, 0.f, 1.f}, bg{"bg", 0.2f, 0.f, 1.f}, bb{"bb", 0.6f, 0.f, 1.f};
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;
    ~GradientOp() override {
        if (bg_) wgpuBindGroupRelease(bg_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        ar.display_hint = VIVID_DISPLAY_COLOR; br.display_hint = VIVID_DISPLAY_COLOR;
        o.push_back(&mode); o.push_back(&cx); o.push_back(&cy); o.push_back(&angle); o.push_back(&scale);
        o.push_back(&ar); o.push_back(&ag); o.push_back(&ab); o.push_back(&br); o.push_back(&bg); o.push_back(&bb);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }
    bool lazy_init(const VividGpuContext* c) {
        std::string err; sh_ = vivid::gpu::create_shader_checked(c->device, kGradientWGSL, "Gradient", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 64, "Gradient U");
        WGPUBindGroupLayoutEntry e{}; e.binding = 0; e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 64;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "Gradient Pipeline");
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = 64;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 1; bd.entries = &be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        return pipe_ != nullptr;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        const float* p = c->param_values; auto pv = [&](int i, float d) { return p ? p[i] : d; };
        float u[16] = { float(c->output_width), float(c->output_height), float(c->time), pv(0, mode.value),
                        pv(1, cx.value), pv(2, cy.value), pv(3, angle.value), pv(4, scale.value),
                        pv(5, ar.value), pv(6, ag.value), pv(7, ab.value), 1.f,
                        pv(8, br.value), pv(9, bg.value), pv(10, bb.value), 1.f };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "Gradient");
    }
};

VIVID_REGISTER(GradientOp)
