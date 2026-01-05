// Vivid Effects 2D - Gradient Operator Implementation

#include <vivid/effects/gradient.h>

namespace vivid::effects {

const char* Gradient::fragmentShader() const {
    return R"(
struct Uniforms {
    mode: i32,
    angle: f32,
    centerX: f32,
    centerY: f32,
    scale: f32,
    offset: f32,
    aspect: f32,
    _pad: f32,
    colorA: vec4f,
    colorB: vec4f,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

const PI: f32 = 3.14159265359;
const TAU: f32 = 6.28318530718;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let center = vec2f(uniforms.centerX, uniforms.centerY);
    var p = input.uv - center;
    p.x *= uniforms.aspect;

    var t: f32;

    if (uniforms.mode == 0) {
        // Linear gradient
        let c = cos(uniforms.angle);
        let s = sin(uniforms.angle);
        let rotated = vec2f(p.x * c + p.y * s, -p.x * s + p.y * c);
        t = rotated.x * uniforms.scale + 0.5 + uniforms.offset;
    } else if (uniforms.mode == 1) {
        // Radial gradient
        t = length(p) * uniforms.scale * 2.0 + uniforms.offset;
    } else if (uniforms.mode == 2) {
        // Angular gradient
        t = (atan2(p.y, p.x) + PI) / TAU + uniforms.offset;
        t = fract(t * uniforms.scale);
    } else {
        // Diamond gradient
        t = (abs(p.x) + abs(p.y)) * uniforms.scale * 2.0 + uniforms.offset;
    }

    t = clamp(t, 0.0, 1.0);
    return mix(uniforms.colorA, uniforms.colorB, t);
}
)";
}

// Explicit template instantiation for Windows hot-reload
template class SimpleGeneratorEffect<Gradient, GradientUniforms>;

} // namespace vivid::effects
