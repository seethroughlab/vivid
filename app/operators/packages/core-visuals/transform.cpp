// Core visual package operator: Transform — 1-in/1-out UV warp (zoom/rotate/translate/
// tile), near-identity at defaults. Migrated verbatim from the built-in TransformOp;
// behaviour unchanged. Shares the UvFilterOp body (uv_filter_op.h).
#include "uv_filter_op.h"

#include <array>

namespace {
const char* kTransformWGSL = R"(
@vertex fn vs_main(@builtin(vertex_index) vi: u32) -> FullscreenOutput { return fullscreenTriangle(vi, false); }
struct U { res: vec2f, time: f32, tx: f32, ty: f32, rot: f32, scale: f32, tile: f32 };
@group(0) @binding(0) var<uniform> u: U;
@group(0) @binding(1) var in_tex: texture_2d<f32>;
@group(0) @binding(2) var samp: sampler;
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    var p = inp.uv - vec2f(0.5, 0.5);
    let zoom = 0.25 + u.scale * 3.75;                    // scale 0..1 -> 0.25..4x  (~0.2 = 1x)
    p = p / zoom;
    let a = u.rot * 6.2831853;
    p = vec2f(p.x * cos(a) - p.y * sin(a), p.x * sin(a) + p.y * cos(a));
    p = p - (vec2f(u.tx, u.ty) - vec2f(0.5, 0.5));
    var uv2 = p + vec2f(0.5, 0.5);
    let tiles = 1.0 + floor(u.tile * 8.0);               // tile 0 -> 1 (no repeat)
    uv2 = fract(uv2 * tiles);
    return textureSample(in_tex, samp, uv2);
}
)";
}  // namespace

struct TransformOp : core_visuals::UvFilterOp<5, 32> {
    static constexpr const char* kName = "Transform";
    static constexpr const char* kDisplayName = "Transform";
    static constexpr const char* kSummary = "Zoom / rotate / translate / tile the input (near-identity at defaults).";
    static constexpr std::array<const char*, 3> kKeywords = {"effect", "transform", "tile"};
    vivid::Param<float> tx{"tx", 0.5f, 0.f, 1.f}, ty{"ty", 0.5f, 0.f, 1.f}, rot{"rot", 0.f, 0.f, 1.f};
    vivid::Param<float> scale{"scale", 0.2f, 0.f, 1.f}, tile{"tile", 0.f, 0.f, 1.f};
    TransformOp() : UvFilterOp(kTransformWGSL, "Transform") {}
    void collect_params(std::vector<vivid::ParamBase*>& o) override {
        o.push_back(&tx); o.push_back(&ty); o.push_back(&rot); o.push_back(&scale); o.push_back(&tile);
    }
    void fill(const VividGpuContext*, const float* p, float* u) override {
        auto pv = [&](int i, float d) { return p ? p[i] : d; };
        u[3] = pv(0, tx.value); u[4] = pv(1, ty.value); u[5] = pv(2, rot.value); u[6] = pv(3, scale.value); u[7] = pv(4, tile.value);
    }
};

VIVID_REGISTER(TransformOp)
