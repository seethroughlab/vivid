// Vivid Effects 2D - Pixelate Operator Implementation

#include <vivid/effects/pixelate.h>

namespace vivid::effects {

const char* Pixelate::fragmentShader() const {
    return R"(
struct Uniforms {
    sizeX: f32,
    sizeY: f32,
    texWidth: f32,
    texHeight: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // Calculate pixel grid size in UV space
    let pixelSizeX = uniforms.sizeX / uniforms.texWidth;
    let pixelSizeY = uniforms.sizeY / uniforms.texHeight;

    // Snap UV to pixel grid center
    let pixelUV = vec2f(
        floor(input.uv.x / pixelSizeX) * pixelSizeX + pixelSizeX * 0.5,
        floor(input.uv.y / pixelSizeY) * pixelSizeY + pixelSizeY * 0.5
    );

    return textureSample(inputTex, texSampler, pixelUV);
}
)";
}

} // namespace vivid::effects
