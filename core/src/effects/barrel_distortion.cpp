// Vivid Effects 2D - Barrel Distortion Operator Implementation

#include <vivid/effects/barrel_distortion.h>

namespace vivid::effects {

const char* BarrelDistortion::fragmentShader() const {
    return R"(
struct Uniforms {
    curvature: f32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

fn barrelDistortion(uv: vec2f, amount: f32) -> vec2f {
    let centered = uv * 2.0 - 1.0;
    let offset = centered.yx * centered.yx * centered.xy * amount;
    return (centered + offset) * 0.5 + 0.5;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let distortedUV = barrelDistortion(input.uv, uniforms.curvature);

    // Return black if outside screen bounds
    if (distortedUV.x < 0.0 || distortedUV.x > 1.0 || distortedUV.y < 0.0 || distortedUV.y > 1.0) {
        return vec4f(0.0, 0.0, 0.0, 1.0);
    }

    return textureSample(inputTex, texSampler, distortedUV);
}
)";
}

} // namespace vivid::effects
