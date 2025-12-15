// Vivid Effects 2D - Scanlines Operator Implementation

#include <vivid/effects/scanlines.h>

namespace vivid::effects {

const char* Scanlines::fragmentShader() const {
    return R"(
struct Uniforms {
    spacing: i32,
    vertical: i32,
    thickness: f32,
    intensity: f32,
    height: f32,
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
    let pixel = input.position.xy;

    var coord: f32;
    if (uniforms.vertical != 0) {
        coord = pixel.x;
    } else {
        coord = pixel.y;
    }

    let spacing = f32(uniforms.spacing);
    let phase = (coord % spacing) / spacing;

    // Scanline darkening based on position within spacing
    var scanline = 1.0;
    if (phase < uniforms.thickness) {
        scanline = 1.0 - uniforms.intensity;
    }

    return vec4f(color.rgb * scanline, color.a);
}
)";
}

} // namespace vivid::effects
