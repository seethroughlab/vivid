// Vivid Effects 2D - Downsample Operator Implementation

#include <vivid/effects/downsample.h>

namespace vivid::effects {

const char* Downsample::fragmentShader() const {
    return R"(
struct Uniforms {
    targetW: f32,
    targetH: f32,
    sourceW: f32,
    sourceH: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // Snap UV to low-res pixel grid
    let pixelX = floor(input.uv.x * uniforms.targetW) / uniforms.targetW;
    let pixelY = floor(input.uv.y * uniforms.targetH) / uniforms.targetH;
    let snappedUV = vec2f(pixelX, pixelY);

    return textureSample(inputTex, texSampler, snappedUV);
}
)";
}

} // namespace vivid::effects
