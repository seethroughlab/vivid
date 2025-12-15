// Vivid Effects 2D - Transform Operator Implementation

#include <vivid/effects/transform.h>

namespace vivid::effects {

const char* Transform::fragmentShader() const {
    return R"(
struct Uniforms {
    scaleX: f32,
    scaleY: f32,
    rotation: f32,
    translateX: f32,
    translateY: f32,
    pivotX: f32,
    pivotY: f32,
    _pad: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // Transform UV coordinates (inverse transform)
    var uv = input.uv;

    // Move to pivot
    uv -= vec2f(uniforms.pivotX, uniforms.pivotY);

    // Rotate (inverse)
    let c = cos(-uniforms.rotation);
    let s = sin(-uniforms.rotation);
    uv = vec2f(uv.x * c - uv.y * s, uv.x * s + uv.y * c);

    // Scale (inverse)
    uv /= vec2f(uniforms.scaleX, uniforms.scaleY);

    // Move back from pivot
    uv += vec2f(uniforms.pivotX, uniforms.pivotY);

    // Translate (inverse)
    uv -= vec2f(uniforms.translateX, -uniforms.translateY);

    return textureSample(inputTex, texSampler, uv);
}
)";
}

} // namespace vivid::effects
