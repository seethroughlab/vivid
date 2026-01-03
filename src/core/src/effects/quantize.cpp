// Vivid Effects 2D - Quantize Operator Implementation

#include <vivid/effects/quantize.h>

namespace vivid::effects {

const char* Quantize::fragmentShader() const {
    return R"(
struct Uniforms {
    levels: i32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(inputTex, texSampler, input.uv);
    let levels = f32(uniforms.levels);

    // Quantize each channel
    let quantized = floor(color.rgb * levels) / levels;

    return vec4f(quantized, color.a);
}
)";
}

// Explicit template instantiation for Windows hot-reload
template class SimpleTextureEffect<Quantize, QuantizeUniforms>;

} // namespace vivid::effects
