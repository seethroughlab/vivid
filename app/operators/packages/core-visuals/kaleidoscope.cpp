// Core visual package operator: Kaleidoscope — 1-in/1-out radial mirror-symmetry fold.
// Migrated verbatim from the built-in KaleidoscopeOp; behaviour unchanged. Shares the
// UvFilterOp body (uv_filter_op.h). Classic mirror.wgsl.
#include "uv_filter_op.h"

#include <array>

namespace {
const char* kKaleidoWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
struct U { res: vec2f, time: f32, segments: f32, cx: f32, cy: f32, angle: f32, zoom: f32 };
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var in_tex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let ar = u.res.x / max(u.res.y, 1.0);
    var p = (inp.uv - vec2f(u.cx, u.cy)) * vec2f(ar, 1.0);
    let seg = 2.0 + floor(u.segments * 14.0);            // 2..16 wedges
    let wedge = 6.2831853 / seg;
    var ang = atan2(p.y, p.x) + u.angle * 6.2831853;
    ang = ang - wedge * floor(ang / wedge);
    ang = abs(ang - wedge * 0.5);                        // mirror within the wedge
    let r = length(p) * (0.6 + u.zoom * 0.8);
    var uv2 = vec2f(cos(ang), sin(ang)) * r / vec2f(ar, 1.0) + vec2f(u.cx, u.cy);
    uv2 = clamp(uv2, vec2f(0.0), vec2f(1.0));
    return textureSample(in_tex, samp, uv2);
}
)";
}  // namespace

struct KaleidoscopeOp : core_visuals::UvFilterOp<5, 32> {
    static constexpr const char* kName = "Kaleidoscope";
    static constexpr const char* kDisplayName = "Kaleidoscope";
    static constexpr const char* kSummary = "Radial mirror-symmetry (kaleidoscope) fold of the input.";
    static constexpr std::array<const char*, 3> kKeywords = {"effect", "kaleidoscope", "mirror"};
    vivid::Param<float> segments{"segments", 0.3f, 0.f, 1.f}, cx{"cx", 0.5f, 0.f, 1.f}, cy{"cy", 0.5f, 0.f, 1.f};
    vivid::Param<float> angle{"angle", 0.f, 0.f, 1.f}, zoom{"zoom", 0.5f, 0.f, 1.f};
    KaleidoscopeOp() : UvFilterOp(kKaleidoWGSL, "Kaleidoscope") {}
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        o.push_back(&segments); o.push_back(&cx); o.push_back(&cy); o.push_back(&angle); o.push_back(&zoom);
    }
    void fill(const VividGpuContext*, const float* p, float* u) override {
        auto pv = [&](int i, float d) { return p ? p[i] : d; };
        u[3] = pv(0, segments.value); u[4] = pv(1, cx.value); u[5] = pv(2, cy.value); u[6] = pv(3, angle.value); u[7] = pv(4, zoom.value);
    }
};

VIVID_REGISTER(KaleidoscopeOp)
