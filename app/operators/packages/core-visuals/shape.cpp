// Core visual package operator: Shape — a crisp SDF primitive (circle/polygon) drawn
// over its input. Migrated verbatim from the built-in ShapeOp (builtin_ops.cpp);
// behaviour unchanged. 1-in/1-out overlay; SDF math from vivid-classic's Shape2D.
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
const char* kShapeWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput {
    return fullscreenTriangle(vi, false);
}
struct U {
    res: vec2f, time: f32, sides: f32,
    pos: vec2f, size: f32, rotation: f32,
    softness: f32, pad0: f32, pad1: vec2f,
    color: vec4f,
};
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var in_tex: texture_2d<f32>;
@group(0) @binding(2) var in_samp: sampler;

fn sd_shape(pin: vec2f, r: f32, sides: f32) -> f32 {
    if (sides < 2.5) { return length(pin) - r; }
    let n = floor(sides + 0.5);
    let an = 3.14159265 / n;
    let acs = vec2f(cos(an), sin(an));
    var bn = atan2(pin.x, pin.y);
    bn = bn - 2.0 * an * floor((bn + an) / (2.0 * an));
    var p = length(pin) * vec2f(cos(bn), abs(sin(bn)));
    p = p - r * acs;
    p.y = p.y + clamp(-p.y, 0.0, r * acs.y);
    return length(p) * sign(p.x);
}
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let bg = textureSample(in_tex, in_samp, inp.uv);
    let sides = floor(u.sides * 8.0);
    var p = inp.uv - u.pos;
    p.x = p.x * (u.res.x / max(u.res.y, 1.0));
    let a = u.rotation * 6.2831853;
    p = vec2f(p.x * cos(a) - p.y * sin(a), p.x * sin(a) + p.y * cos(a));
    let d = sd_shape(p, max(u.size * 0.7, 0.001), sides);
    let aa = fwidth(d) + u.softness * 0.06 + 0.0015;
    let cov = (1.0 - smoothstep(-aa, aa, d)) * u.color.a;
    return vec4f(mix(bg.rgb, u.color.rgb, cov), max(bg.a, cov));
}
)";
}  // namespace

struct ShapeOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Shape";
    static constexpr const char* kDisplayName = "Shape";
    static constexpr const char* kSummary = "A crisp SDF shape (circle/polygon) drawn over its input. Geometry, not a field.";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "shape", "geometry"};
    vivid::Param<float> sides{"sides", 0.75f, 0.f, 1.f};
    vivid::Param<float> x{"x", 0.5f, 0.f, 1.f}, y{"y", 0.5f, 0.f, 1.f};
    vivid::Param<float> size{"size", 0.35f, 0.f, 1.f};
    vivid::Param<float> rotation{"rotation", 0.f, 0.f, 1.f};
    vivid::Param<float> softness{"softness", 0.04f, 0.f, 1.f};
    vivid::Param<float> r{"r", 1.f, 0.f, 1.f}, g{"g", 0.2f, 0.f, 1.f}, b{"b", 0.6f, 0.f, 1.f}, a{"a", 1.f, 0.f, 1.f};
    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUSampler samp_ = nullptr; WGPUBindGroup bg_ = nullptr;
    ~ShapeOp() override {
        if (bg_) wgpuBindGroupRelease(bg_); if (samp_) wgpuSamplerRelease(samp_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        r.display_hint = VIVID_DISPLAY_COLOR;   // r/g/b/a a colour swatch
        o.push_back(&sides); o.push_back(&x); o.push_back(&y); o.push_back(&size);
        o.push_back(&rotation); o.push_back(&softness);
        o.push_back(&r); o.push_back(&g); o.push_back(&b); o.push_back(&a);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(tex_port("input", VIVID_PORT_INPUT));
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }
    bool lazy_init(const VividGpuContext* c) {
        std::string err;
        sh_ = vivid::gpu::create_shader_checked(c->device, kShapeWGSL, "Shape", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 64, "Shape U");
        WGPUBindGroupLayoutEntry e[3]{};
        e[0].binding = 0; e[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e[0].buffer.type = WGPUBufferBindingType_Uniform; e[0].buffer.minBindingSize = 64;
        e[1].binding = 1; e[1].visibility = WGPUShaderStage_Fragment;
        e[1].texture.sampleType = WGPUTextureSampleType_Float; e[1].texture.viewDimension = WGPUTextureViewDimension_2D;
        e[2].binding = 2; e[2].visibility = WGPUShaderStage_Fragment;
        e[2].sampler.type = WGPUSamplerBindingType_Filtering;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 3; ld.entries = e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "Shape Pipeline");
        WGPUSamplerDescriptor sd{}; sd.magFilter = WGPUFilterMode_Linear; sd.minFilter = WGPUFilterMode_Linear;
        sd.addressModeU = WGPUAddressMode_ClampToEdge; sd.addressModeV = WGPUAddressMode_ClampToEdge; sd.maxAnisotropy = 1;
        samp_ = wgpuDeviceCreateSampler(c->device, &sd);
        return pipe_ != nullptr;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        const float* p = c->param_values;   // sides,x,y,size,rot,softness,r,g,b,a
        auto pv = [&](int i, float d) { return p ? p[i] : d; };
        float u[16] = {
            float(c->output_width), float(c->output_height), float(c->time), pv(0, sides.value),
            pv(1, x.value), pv(2, y.value), pv(3, size.value), pv(4, rotation.value),
            pv(5, softness.value), 0.f, 0.f, 0.f,
            pv(6, r.value), pv(7, g.value), pv(8, b.value), pv(9, a.value),
        };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        const WGPUTextureView in = (c->input_texture_count > 0) ? c->input_texture_views[0] : c->output_texture_view;
        if (bg_) { wgpuBindGroupRelease(bg_); bg_ = nullptr; }
        WGPUBindGroupEntry be[3]{};
        be[0].binding = 0; be[0].buffer = ubo_; be[0].size = 64;
        be[1].binding = 1; be[1].textureView = in;
        be[2].binding = 2; be[2].sampler = samp_;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 3; bd.entries = be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "Shape");
    }
};

VIVID_REGISTER(ShapeOp)
