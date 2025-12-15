// Vivid Effects 2D - Edge Operator Implementation

#include <vivid/effects/edge.h>

namespace vivid::effects {

const char* Edge::fragmentShader() const {
    return R"(
struct Uniforms {
    strength: f32,
    threshold: f32,
    texelW: f32,
    texelH: f32,
    invert: i32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

fn luminance(c: vec3f) -> f32 {
    return dot(c, vec3f(0.299, 0.587, 0.114));
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let texel = vec2f(uniforms.texelW, uniforms.texelH);

    // Sample 3x3 neighborhood
    let tl = luminance(textureSample(inputTex, texSampler, input.uv + vec2f(-texel.x, -texel.y)).rgb);
    let tc = luminance(textureSample(inputTex, texSampler, input.uv + vec2f(0.0, -texel.y)).rgb);
    let tr = luminance(textureSample(inputTex, texSampler, input.uv + vec2f(texel.x, -texel.y)).rgb);
    let ml = luminance(textureSample(inputTex, texSampler, input.uv + vec2f(-texel.x, 0.0)).rgb);
    let mr = luminance(textureSample(inputTex, texSampler, input.uv + vec2f(texel.x, 0.0)).rgb);
    let bl = luminance(textureSample(inputTex, texSampler, input.uv + vec2f(-texel.x, texel.y)).rgb);
    let bc = luminance(textureSample(inputTex, texSampler, input.uv + vec2f(0.0, texel.y)).rgb);
    let br = luminance(textureSample(inputTex, texSampler, input.uv + vec2f(texel.x, texel.y)).rgb);

    // Sobel operators
    let gx = -tl - 2.0*ml - bl + tr + 2.0*mr + br;
    let gy = -tl - 2.0*tc - tr + bl + 2.0*bc + br;

    // Edge magnitude
    var edge = sqrt(gx*gx + gy*gy) * uniforms.strength;

    // Apply threshold
    edge = max(edge - uniforms.threshold, 0.0) / (1.0 - uniforms.threshold + 0.0001);

    // Invert if requested
    if (uniforms.invert != 0) {
        edge = 1.0 - edge;
    }

    return vec4f(edge, edge, edge, 1.0);
}
)";
}

} // namespace vivid::effects
