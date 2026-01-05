// Vivid Effects 2D - Flash Operator Implementation

#include <vivid/effects/flash.h>

namespace vivid::effects {

const char* Flash::fragmentShader() const {
    return R"(
struct Uniforms {
    intensity: f32,
    mode: f32,
    pad0: f32,
    pad1: f32,
    color: vec4f,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let inputColor = textureSample(inputTex, texSampler, input.uv);
    let flashColor = uniforms.color.rgb;
    let intensity = uniforms.intensity;

    var result: vec3f;

    let mode = i32(uniforms.mode);
    if (mode == 0) {
        // Additive: adds light on top
        result = inputColor.rgb + flashColor * intensity;
    } else if (mode == 1) {
        // Screen: softer blend, prevents over-saturation
        let flash = flashColor * intensity;
        result = 1.0 - (1.0 - inputColor.rgb) * (1.0 - flash);
    } else {
        // Replace: solid overlay with alpha
        result = mix(inputColor.rgb, flashColor, intensity);
    }

    return vec4f(result, inputColor.a);
}
)";
}

// Explicit template instantiation for Windows hot-reload
template class SimpleTextureEffect<Flash, FlashUniforms>;

} // namespace vivid::effects
