// Vivid Effects 2D - Vignette Operator Implementation

#include <vivid/effects/vignette.h>

namespace vivid::effects {

const char* Vignette::fragmentShader() const {
    return R"(
struct Uniforms {
    intensity: f32,
    softness: f32,
    roundness: f32,
    aspect: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(inputTex, texSampler, input.uv);

    // Center the UV coordinates (-1 to 1)
    var centered = input.uv * 2.0 - 1.0;

    // Correct for aspect ratio
    centered.x *= uniforms.aspect;

    // Calculate distance from center
    // Mix between rectangular (max) and circular (length) based on roundness
    let circularDist = length(centered);
    let rectDist = max(abs(centered.x), abs(centered.y));
    let dist = mix(rectDist, circularDist, uniforms.roundness);

    // Calculate vignette factor with softness
    let softness = max(uniforms.softness, 0.001);  // Avoid division by zero
    let vignette = 1.0 - smoothstep(1.0 - softness, 1.0 + softness * 0.5, dist * (1.0 + uniforms.intensity));

    return vec4f(color.rgb * vignette, color.a);
}
)";
}

} // namespace vivid::effects
