// Vivid Effects 2D - ChromaticAberration Operator Implementation

#include <vivid/effects/chromatic_aberration.h>

namespace vivid::effects {

const char* ChromaticAberration::fragmentShader() const {
    return R"(
struct Uniforms {
    amount: f32,
    angle: f32,
    radial: i32,
    aspect: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    var offsetDir: vec2f;

    if (uniforms.radial != 0) {
        // Radial: offset direction points away from center
        // Apply aspect correction for circular pattern
        var p = input.uv - 0.5;
        p.x *= uniforms.aspect;
        offsetDir = normalize(p);
        // Reverse aspect for distance calculation to get circular distance
        let circularP = vec2f(p.x / uniforms.aspect, p.y);
        let dist = length(circularP) * 2.0;
        offsetDir *= dist;
        // Reverse aspect on offset direction so it samples correctly
        offsetDir.x /= uniforms.aspect;
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

// Explicit template instantiation for Windows hot-reload
template class SimpleTextureEffect<ChromaticAberration, ChromaticAberrationUniforms>;

} // namespace vivid::effects
