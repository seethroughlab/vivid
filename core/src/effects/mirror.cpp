// Vivid Effects 2D - Mirror Operator Implementation

#include <vivid/effects/mirror.h>

namespace vivid::effects {

const char* Mirror::fragmentShader() const {
    return R"(
struct Uniforms {
    mode: i32,        // 0=horizontal, 1=vertical, 2=quad, 3=kaleidoscope
    segments: i32,
    angle: f32,
    centerX: f32,
    centerY: f32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

const PI: f32 = 3.14159265359;
const TAU: f32 = 6.28318530718;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    var uv = input.uv;
    let center = vec2f(uniforms.centerX, uniforms.centerY);

    if (uniforms.mode == 0) {
        // Horizontal mirror
        if (uv.x > center.x) {
            uv.x = center.x - (uv.x - center.x);
        }
    } else if (uniforms.mode == 1) {
        // Vertical mirror
        if (uv.y > center.y) {
            uv.y = center.y - (uv.y - center.y);
        }
    } else if (uniforms.mode == 2) {
        // Quad mirror (both axes)
        if (uv.x > center.x) {
            uv.x = center.x - (uv.x - center.x);
        }
        if (uv.y > center.y) {
            uv.y = center.y - (uv.y - center.y);
        }
    } else if (uniforms.mode == 3) {
        // Kaleidoscope
        let p = uv - center;
        var a = atan2(p.y, p.x) + uniforms.angle;
        let r = length(p);

        // Number of segments
        let segmentAngle = TAU / f32(uniforms.segments);

        // Fold angle into first segment
        a = a % segmentAngle;
        if (a < 0.0) {
            a += segmentAngle;
        }

        // Mirror within segment
        if (a > segmentAngle * 0.5) {
            a = segmentAngle - a;
        }

        // Convert back to UV
        uv = center + vec2f(cos(a), sin(a)) * r;
    }

    return textureSample(inputTex, texSampler, uv);
}
)";
}

} // namespace vivid::effects
