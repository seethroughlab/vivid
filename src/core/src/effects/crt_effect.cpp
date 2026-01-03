// Vivid Effects 2D - CRT Effect Operator Implementation

#include <vivid/effects/crt_effect.h>

namespace vivid::effects {

const char* CRTEffect::fragmentShader() const {
    return R"(
struct Uniforms {
    curvature: f32,
    vignette: f32,
    scanlines: f32,
    bloom: f32,
    chromatic: f32,
    aspect: f32,
    _pad1: f32,
    _pad2: f32,
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
    // Apply barrel distortion (CRT curvature)
    let distortedUV = barrelDistortion(input.uv, uniforms.curvature);

    // Check if outside screen bounds
    if (distortedUV.x < 0.0 || distortedUV.x > 1.0 || distortedUV.y < 0.0 || distortedUV.y > 1.0) {
        return vec4f(0.0, 0.0, 0.0, 1.0);
    }

    // Chromatic aberration
    let chromOffset = uniforms.chromatic * 0.01;
    let r = textureSample(inputTex, texSampler, distortedUV + vec2f(chromOffset, 0.0)).r;
    let g = textureSample(inputTex, texSampler, distortedUV).g;
    let b = textureSample(inputTex, texSampler, distortedUV - vec2f(chromOffset, 0.0)).b;
    var color = vec3f(r, g, b);

    // Scanlines
    let scanlineY = input.position.y;
    let scanline = sin(scanlineY * 3.14159 * 2.0) * 0.5 + 0.5;
    color = color * (1.0 - uniforms.scanlines * 0.5 * scanline);

    // Phosphor bloom (simple glow)
    let bloom = textureSample(inputTex, texSampler, distortedUV).rgb;
    color = color + bloom * uniforms.bloom * 0.3;

    // Vignette
    let centered = distortedUV * 2.0 - 1.0;
    let vignetteFactor = 1.0 - dot(centered, centered) * uniforms.vignette;
    color = color * max(vignetteFactor, 0.0);

    return vec4f(color, 1.0);
}
)";
}

// Explicit template instantiation for Windows hot-reload
template class SimpleTextureEffect<CRTEffect, CRTEffectUniforms>;

} // namespace vivid::effects
