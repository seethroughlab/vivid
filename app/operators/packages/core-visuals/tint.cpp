// Core visual package operator: Tint — a hue-shifted gradient generator (WGSL).
// Migrated verbatim from the built-in TintOp (builtin_ops.cpp) to the auto-discovered
// package path; behaviour unchanged.
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
const char* kTintWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput {
    return fullscreenTriangle(vi, false);
}
struct U { res: vec2f, time: f32, hue: f32, tint: vec3f };
@group(0) @binding(0) var<uniform> u: U;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let c = 0.5 + 0.5 * cos(vec3f(0.0, 2.0, 4.0) + inp.uv.x * 6.2831853 + u.time * 0.5 + u.hue * 6.2831853);
    return vec4f(c * u.tint, 1.0);   // r/g/b tint (COLOR compound-widget)
}
)";
}  // namespace

struct TintOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Tint";
    static constexpr const char* kDisplayName = "Tint";
    static constexpr const char* kSummary = "WGSL example generator: a hue-shifted gradient (shows the WGSL authoring path).";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "tint", "wgsl"};
    vivid::Param<float> hue{"hue", 0.5f, 0.f, 1.f};
    vivid::Param<float> r{"r", 1.f, 0.f, 1.f}, g{"g", 1.f, 0.f, 1.f}, b{"b", 1.f, 0.f, 1.f};
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;
    ~TintOp() override {
        if (bg_) wgpuBindGroupRelease(bg_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        r.display_hint = VIVID_DISPLAY_COLOR;   // r starts an r/g/b COLOR group (swatch + channels)
        o.push_back(&hue); o.push_back(&r); o.push_back(&g); o.push_back(&b);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }
    bool lazy_init(const VividGpuContext* c) {
        std::string err;
        sh_ = vivid::gpu::create_shader_checked(c->device, kTintWGSL, "Tint", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 32, "Tint U");
        WGPUBindGroupLayoutEntry e{};
        e.binding = 0; e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 32;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "Tint Pipeline");
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = 32;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 1; bd.entries = &be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        return pipe_ != nullptr;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        const float* p = c->param_values;   // order: hue, r, g, b
        float u[8] = { float(c->output_width), float(c->output_height), float(c->time),
                       p ? p[0] : hue.value, 0.f, 0.f, 0.f, 0.f };
        u[4] = p ? p[1] : r.value;   // tint.r (vec3 aligns at offset 16 = u[4])
        u[5] = p ? p[2] : g.value;   // tint.g
        u[6] = p ? p[3] : b.value;   // tint.b
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "Tint");
    }
};

VIVID_REGISTER(TintOp)
