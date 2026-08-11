// Project operator: Tunnel — a NONOTAK-style monochrome grid corridor flying continuously toward
// the camera. Motion is INTRINSIC (the ring walls scroll forward on u.time every frame, reactive or
// not), so the frame is never static; audio drives a big legible full-corridor bloom via `pulse`.
// Fullscreen fragment generator (no vertex geometry) — the fullscreen-triangle preamble + fs_main
// pattern (see cosine_palette / gpu_common.h). Author-your-own-operator per the project north star.
#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_common.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace {
VividPortDescriptor tex_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}

// Uniform: res.xy, time, speed, pulse, warp, spokes, rings, r, g, b, glow — 12 f32 / 48 bytes.
// (vec2f is 8-aligned at offset 0; every following scalar packs tightly; size rounds to 48.)
const char* kTunnelWGSL = R"(
struct U { res: vec2f, time: f32, speed: f32, pulse: f32, warp: f32,
           spokes: f32, rings: f32, r: f32, g: f32, b: f32, glow: f32 };
@group(0) @binding(0) var<uniform> u: U;

@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput {
    return fullscreenTriangle(vi, false);
}

@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    var p = inp.uv * 2.0 - vec2f(1.0, 1.0);
    p.x = p.x * (u.res.x / max(u.res.y, 1.0));   // aspect-correct against the real target
    let rad = length(p);
    let ang = atan2(p.y, p.x);
    // Classic forward-rushing tunnel: 1/rad is the perspective depth; u.time scrolls it toward us.
    // A generous epsilon caps the central depth so the vanishing point doesn't alias into sparkle.
    let depth = u.time * u.speed + 0.30 / (rad + 0.22);
    // Twist the corridor as it recedes.
    let a = ang + depth * u.warp * 0.20;
    // Ring walls (scrolling forward) and radial spokes: 0 exactly on a gridline, 1 in a gap.
    let ring  = abs(fract(depth * u.rings) - 0.5) * 2.0;
    let spoke = abs(fract(a * u.spokes * 0.15915494) - 0.5) * 2.0;   // *1/TAU
    // Bright gridlines: 1 ON a gridline, 0 between. smoothstep(edge0<edge1) — the reversed form is
    // UNDEFINED in WGSL, so build it as 1 - smoothstep(0, w, x). Wide-ish so the corridor reads clearly
    // even downsampled (thin lines average to near-black and become illegible).
    let lw = 0.28;
    let ringL  = 1.0 - smoothstep(0.0, lw, ring);
    let spokeL = 1.0 - smoothstep(0.0, lw, spoke);
    let line = max(ringL, spokeL);
    // Fade the vanishing centre toward black; keep the near walls bright and legible.
    let fade = smoothstep(0.03, 0.62, rad);
    // Clamp pulse: a control edge can drive it past 1, which would blow the whole corridor to solid
    // white. A saturating response keeps a big kick legible without clipping to a featureless frame.
    let pz = clamp(u.pulse, 0.0, 1.0);
    // The corridor is ALWAYS a legible mid-grey grid (never full black, never full white):
    //  - a visible floor so the rest state reads as a structured tunnel, not an empty frame;
    //  - the gridlines carry the form;
    //  - pulse LIFTS brightness on a kick but the ceiling stays below white so structure survives.
    let ambient = fade * 0.22;                         // always-on corridor wash (visible rest)
    let grid    = line * fade;                          // the gridline structure
    let restLum = ambient * (0.5 + u.glow * 0.5) + grid * (0.20 + u.glow * 0.35);
    let bloomLum = (ambient * 0.5 + grid * 0.6) * pz;  // kick lift, bounded
    let lum = clamp(restLum + bloomLum, 0.0, 0.92);    // ceiling < 1 so it never whites out
    let col = vec3f(u.r, u.g, u.b) * lum;
    return vec4f(col, 1.0);
}
)";
}  // namespace

struct TunnelOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "Tunnel";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_SOURCE;   // ADR-0046
    static constexpr const char* kDisplayName = "Tunnel";
    static constexpr const char* kSummary =
        "Monochrome grid corridor flying continuously toward the camera; `pulse` blooms it on audio.";
    static constexpr std::array<const char*, 3> kKeywords = {"generator", "tunnel", "corridor"};

    // 0..1 mappable params (indices match collect_params order below).
    vivid::Param<float> speed{"speed", 0.35f, 0.f, 1.f};    // forward flight rate
    vivid::Param<float> pulse{"pulse", 0.0f, 0.f, 1.f};     // AUDIO: map master.low here for a big flash
    vivid::Param<float> warp{"warp", 0.30f, 0.f, 1.f};      // corridor twist
    vivid::Param<float> density{"density", 0.5f, 0.f, 1.f}; // ring + spoke count
    vivid::Param<float> glow{"glow", 0.40f, 0.f, 1.f};      // base line brightness
    vivid::Param<float> r{"r", 0.85f, 0.f, 1.f}, g{"g", 0.92f, 0.f, 1.f}, b{"b", 1.0f, 0.f, 1.f};

    bool tried_ = false;
    WGPUShaderModule sh_ = nullptr; WGPUBindGroupLayout bgl_ = nullptr; WGPUPipelineLayout pl_ = nullptr;
    WGPURenderPipeline pipe_ = nullptr; WGPUBuffer ubo_ = nullptr; WGPUBindGroup bg_ = nullptr;
    ~TunnelOp() override {
        if (bg_) wgpuBindGroupRelease(bg_); if (ubo_) wgpuBufferRelease(ubo_);
        if (pipe_) wgpuRenderPipelineRelease(pipe_); if (pl_) wgpuPipelineLayoutRelease(pl_);
        if (bgl_) wgpuBindGroupLayoutRelease(bgl_); if (sh_) wgpuShaderModuleRelease(sh_);
    }
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        r.display_hint = VIVID_DISPLAY_COLOR;
        o.push_back(&speed); o.push_back(&pulse); o.push_back(&warp); o.push_back(&density);
        o.push_back(&glow); o.push_back(&r); o.push_back(&g); o.push_back(&b);
    }
    void collect_ports(std::vector<VividPortDescriptor>& o) override {
        o.push_back(tex_port("texture", VIVID_PORT_OUTPUT));
    }
    bool lazy_init(const VividGpuContext* c) {
        std::string err; sh_ = vivid::gpu::create_shader_checked(c->device, kTunnelWGSL, "Tunnel", err);
        if (!sh_ || !err.empty()) return false;
        ubo_ = vivid::gpu::create_uniform_buffer(c->device, 48, "Tunnel U");
        WGPUBindGroupLayoutEntry e{}; e.binding = 0;
        e.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
        e.buffer.type = WGPUBufferBindingType_Uniform; e.buffer.minBindingSize = 48;
        WGPUBindGroupLayoutDescriptor ld{}; ld.entryCount = 1; ld.entries = &e;
        bgl_ = wgpuDeviceCreateBindGroupLayout(c->device, &ld);
        WGPUPipelineLayoutDescriptor pld{}; pld.bindGroupLayoutCount = 1; pld.bindGroupLayouts = &bgl_;
        pl_ = wgpuDeviceCreatePipelineLayout(c->device, &pld);
        pipe_ = vivid::gpu::create_pipeline(c->device, sh_, pl_, c->output_format, "Tunnel Pipeline");
        WGPUBindGroupEntry be{}; be.binding = 0; be.buffer = ubo_; be.size = 48;
        WGPUBindGroupDescriptor bd{}; bd.layout = bgl_; bd.entryCount = 1; bd.entries = &be;
        bg_ = wgpuDeviceCreateBindGroup(c->device, &bd);
        return pipe_ != nullptr;
    }
    void process_gpu(const VividGpuContext* c) override {
        if (!tried_) { tried_ = true; lazy_init(c); }
        if (!pipe_) return;
        const float* p = c->param_values; auto pv = [&](int i, float d) { return p ? p[i] : d; };
        const float spokes = std::round(6.f + pv(3, density.value) * 18.f);   // 6..24 radial spokes
        const float rings  = 1.5f + pv(3, density.value) * 6.f;               // ring frequency
        const float u[12] = {
            float(c->output_width), float(c->output_height),
            float(c->time),
            0.15f + pv(0, speed.value) * 2.5f,     // speed (never fully stops → always in motion)
            pv(1, pulse.value),                    // pulse (audio-mapped)
            pv(2, warp.value),                     // warp
            spokes, rings,
            pv(5, r.value), pv(6, g.value), pv(7, b.value),
            0.15f + pv(4, glow.value) * 0.7f,      // glow
        };
        wgpuQueueWriteBuffer(c->queue, ubo_, 0, u, sizeof(u));
        vivid::gpu::run_pass(c->command_encoder, pipe_, bg_, c->output_texture_view, "Tunnel");
    }
};

VIVID_REGISTER(TunnelOp)
