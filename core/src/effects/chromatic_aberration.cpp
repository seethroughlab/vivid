// Vivid Effects 2D - ChromaticAberration Operator Implementation

#include <vivid/effects/chromatic_aberration.h>

namespace vivid::effects {

const char* ChromaticAberration::fragmentShader() const {
    return R"(
struct Uniforms {
    amount: f32,
    angle: f32,
    radial: i32,
    _pad: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    var offsetDir: vec2f;

    if (uniforms.radial != 0) {
        // Radial: offset direction points away from center
        offsetDir = normalize(input.uv - 0.5);
        // Scale by distance from center
        let dist = length(input.uv - 0.5) * 2.0;
        offsetDir *= dist;
    } else {
        // Linear: fixed direction
        offsetDir = vec2f(cos(uniforms.angle), sin(uniforms.angle));
    }

    let offset = offsetDir * uniforms.amount;

    // Sample each channel at different offsets
    let r = textureSample(inputTex, texSampler, input.uv + offset).r;
    let g = textureSample(inputTex, texSampler, input.uv).g;
    let b = textureSample(inputTex, texSampler, input.uv - offset).b;
    let a = textureSample(inputTex, texSampler, input.uv).a;

    return vec4f(r, g, b, a);
}
)";
}

} // namespace vivid::effects
