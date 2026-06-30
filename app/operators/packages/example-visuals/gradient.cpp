// Example package operator: a vertical hue gradient generator authored in WGSL.
// Compiled at install time by the package compiler (clang++) into a loadable .dylib
// — no app rebuild. Self-contained against operator_api/ + gpu_common.h.
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
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput {
    return fullscreenTriangle(vi, false);
}
struct U { res: vec2f, time: f32, hue: f32, tilt: f32, p: f32 };
@group(0) @binding(0) var<uniform> u: U;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let t = mix(inp.uv.y, inp.uv.x, u.tilt);
    let c = 0.5 + 0.5 * cos(vec3f(0.0, 2.0, 4.0) + t * 6.2831853 + u.hue * 6.2831853 + u.time * 0.3);
    return vec4f(c, 1.0);
}
)";
}  // namespace

struct GradientOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Gradient";
    static constexpr const char* kDisplayName = "Gradient";
    static constexpr const char* kSummary = "Vertical/horizontal hue gradient generator (example package operator).";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "gradient", "color"};
    vivid::Param<float> hue {"hue",  0.0f, 0.f, 1.f};
    vivid::Param<float> tilt{"tilt", 0.0f, 0.f, 1.f};   // 0 = vertical, 1 = horizontal

    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr;
    WGPUPipelineLayout pl_ = nullptr; WGPURenderPipeline pipe_ = nullptr;
    WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;
    ~GradientOp() override {
        if (bg_) wgpuBindGroupRelease(bg_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override { o.push_back(&hue); o.push_back(&tilt); }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }

    bool lazy_init(const VividGpuContext* c) {
        std::string err;
        sh_ = vivid::gpu::create_shader_checked(c->device, kGradientWGSL, "Gradient", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 32, "Gradient U");
        WGPUBindGroupLayoutEntry e{};
        e.binding = 0; e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 32;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "Gradient Pipeline");
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = 32;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 1; bd.entries = &be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        return pipe_ != nullptr;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        const float h = c->param_values ? c->param_values[0] : hue.value;
        const float t = c->param_values ? c->param_values[1] : tilt.value;
        float u[8] = { float(c->output_width), float(c->output_height), float(c->time), h, t, 0, 0, 0 };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "Gradient");
    }
};

VIVID_REGISTER(GradientOp)
