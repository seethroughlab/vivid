// Project-local custom operator for the song-sketch demo: a flowing "aurora" curtain
// generator (GLSL via WGSL). Compiled into the project folder by load_project and
// registered by name. Its 4 float params are bridge-drivable (audio -> visuals):
//   warp    -> curtain sway / turbulence
//   hue     -> colour rotation
//   density -> number of vertical folds
//   glow    -> overall brightness
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
const char* kWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput {
    return fullscreenTriangle(vi, false);
}
struct U { res: vec2f, time: f32, warp: f32, hue: f32, density: f32, glow: f32, p2: f32 };
@group(0) @binding(0) var<uniform> u: U;

fn hash(p: vec2f) -> f32 { return fract(sin(dot(p, vec2f(41.3, 289.1))) * 43758.5453); }
fn noise(p: vec2f) -> f32 {
    let i = floor(p); let f = fract(p);
    let a = hash(i); let b = hash(i + vec2f(1.0, 0.0));
    let c = hash(i + vec2f(0.0, 1.0)); let d = hash(i + vec2f(1.0, 1.0));
    let uu = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, uu.x), mix(c, d, uu.x), uu.y);
}
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let uv = inp.uv;
    let folds = 3.0 + u.density * 14.0;
    let sway = u.warp * 0.35 * sin(uv.y * 3.0 + u.time * 0.7);
    var n = 0.0; var amp = 0.5; var freq = folds;
    for (var i = 0; i < 4; i = i + 1) {
        n = n + amp * noise(vec2f((uv.x + sway) * freq, uv.y * 2.0 - u.time * 0.25));
        amp = amp * 0.5; freq = freq * 2.0;
    }
    // vertical curtains: brightest in bands, fading toward the top
    let curtain = pow(0.5 + 0.5 * sin((uv.x + sway) * folds + n * 4.0), 2.2);
    let vfade = smoothstep(1.0, 0.15, uv.y);
    let intensity = curtain * vfade * (0.4 + n * 0.8);
    let col = 0.5 + 0.5 * cos(vec3f(0.0, 2.2, 4.2) + u.hue * 6.2831853 + n * 1.5 + uv.y * 1.2);
    return vec4f(col * intensity * (0.6 + u.glow), 1.0);
}
)";
}  // namespace

struct AuroraFieldOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "AuroraField";
    static constexpr const char* kDisplayName = "Aurora Field";
    static constexpr const char* kSummary = "Flowing aurora-curtain generator (song-sketch demo op).";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "aurora", "flow"};
    vivid::Param<float> warp   {"warp",    0.5f, 0.f, 1.f};
    vivid::Param<float> hue    {"hue",     0.55f, 0.f, 1.f};
    vivid::Param<float> density{"density", 0.5f, 0.f, 1.f};
    vivid::Param<float> glow   {"glow",    0.4f, 0.f, 1.f};

    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr;
    WGPUPipelineLayout pl_ = nullptr; WGPURenderPipeline pipe_ = nullptr;
    WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;
    ~AuroraFieldOp() override {
        if (bg_) wgpuBindGroupRelease(bg_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        o.push_back(&warp); o.push_back(&hue); o.push_back(&density); o.push_back(&glow);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override { o.push_back(tex_port("texture", VIVID_PORT_OUTPUT)); }

    bool lazy_init(const VividGpuContext* c) {
        std::string err;
        sh_ = vivid::gpu::create_shader_checked(c->device, kWGSL, "AuroraField", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 32, "AuroraField U");
        WGPUBindGroupLayoutEntry e{};
        e.binding = 0; e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 32;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "AuroraField Pipeline");
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = 32;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 1; bd.entries = &be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        return pipe_ != nullptr;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        const float* pv = c->param_values;
        float u[8] = { float(c->output_width), float(c->output_height), float(c->time),
                       pv ? pv[0] : warp.value, pv ? pv[1] : hue.value,
                       pv ? pv[2] : density.value, pv ? pv[3] : glow.value, 0 };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "AuroraField");
    }
};

VIVID_REGISTER(AuroraFieldOp)
