// Core visual package operator: Plasma — animated plasma colour-field generator.
// Migrated from the built-in PlasmaOp (builtin_ops.cpp); its GLSL is translated to
// WGSL (the built-in used the host-only ShaderOp/GLSL path). Behaviour preserved.
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
const char* kPlasmaWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
struct U { res: vec2f, time: f32, warp: f32, hue: f32, density: f32, glow: f32, pad: f32 };
@group(0) @binding(0) var<uniform> u: U;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let uv = inp.uv;
    let t = u.time;
    let dens = 6.0 + u.density * 18.0;
    let w = uv + u.warp * 0.3 * vec2f(sin(uv.y * 8.0 + t), cos(uv.x * 8.0 + t));
    let v = sin(w.x * dens + t) + sin(w.y * dens + t * 1.3)
          + sin((w.x + w.y) * dens * 0.6 + t * 0.7)
          + sin(length(w - vec2f(0.5)) * dens * 1.8 - t * 2.0);
    let col = 0.5 + 0.5 * cos(vec3f(0.0, 2.0, 4.0) + v + u.hue * 6.2831853);
    return vec4f(col * (0.6 + u.glow), 1.0);
}
)";
}  // namespace

struct PlasmaOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Plasma";
    static constexpr const char* kDisplayName = "Plasma";
    static constexpr const char* kSummary = "Animated plasma colour-field generator. No input; drives a chain.";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "plasma", "color"};
    vivid::Param<float> warp{"warp", 0.5f, 0.f, 1.f};
    vivid::Param<float> hue{"hue", 0.0f, 0.f, 1.f};
    vivid::Param<float> density{"density", 0.5f, 0.f, 1.f};
    vivid::Param<float> glow{"glow", 0.5f, 0.f, 1.f};
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;
    ~PlasmaOp() override {
        if (bg_) wgpuBindGroupRelease(bg_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        vivid::semantic_intent(warp, "domain warp amount"); warp.display_hint = VIVID_DISPLAY_XY_PAD;
        vivid::semantic_tag(hue, "phase_01"); vivid::semantic_intent(hue, "color hue"); hue.display_hint = VIVID_DISPLAY_KNOB;
        vivid::semantic_intent(density, "pattern density"); density.display_hint = VIVID_DISPLAY_KNOB;
        vivid::semantic_intent(glow, "glow intensity"); glow.display_hint = VIVID_DISPLAY_KNOB;
        o.push_back(&warp); o.push_back(&hue); o.push_back(&density); o.push_back(&glow);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }
    bool lazy_init(const VividGpuContext* c) {
        std::string err; sh_ = vivid::gpu::create_shader_checked(c->device, kPlasmaWGSL, "Plasma", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 32, "Plasma U");
        WGPUBindGroupLayoutEntry e{}; e.binding = 0; e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 32;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "Plasma Pipeline");
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = 32;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 1; bd.entries = &be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        return pipe_ != nullptr;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        const float* p = c->param_values; auto pv = [&](int i, float d) { return p ? p[i] : d; };
        float u[8] = { float(c->output_width), float(c->output_height), float(c->time),
                       pv(0, warp.value), pv(1, hue.value), pv(2, density.value), pv(3, glow.value), 0.f };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "Plasma");
    }
};

VIVID_REGISTER(PlasmaOp)
