// Vivid Effects 2D - HSV Operator Implementation

#include <vivid/effects/hsv.h>

namespace vivid::effects {

const char* HSV::fragmentShader() const {
    return R"(
struct Uniforms {
    hueShift: f32,
    saturation: f32,
    value: f32,
    _pad: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

fn rgb2hsv(rgb: vec3f) -> vec3f {
    let r = rgb.r;
    let g = rgb.g;
    let b = rgb.b;

    let maxC = max(max(r, g), b);
    let minC = min(min(r, g), b);
    let delta = maxC - minC;

    var h = 0.0;
    if (delta > 0.0001) {
        if (maxC == r) {
            h = (g - b) / delta;
            if (h < 0.0) { h += 6.0; }
        } else if (maxC == g) {
            h = 2.0 + (b - r) / delta;
        } else {
            h = 4.0 + (r - g) / delta;
        }
        h /= 6.0;
    }

    var s = 0.0;
    if (maxC > 0.0001) {
        s = delta / maxC;
    }

    return vec3f(h, s, maxC);
}

fn hsv2rgb(hsv: vec3f) -> vec3f {
    let h = hsv.x * 6.0;
    let s = hsv.y;
    let v = hsv.z;

    let i = floor(h);
    let f = h - i;
    let p = v * (1.0 - s);
    let q = v * (1.0 - s * f);
    let t = v * (1.0 - s * (1.0 - f));

    let idx = i32(i) % 6;
    if (idx == 0) { return vec3f(v, t, p); }
    if (idx == 1) { return vec3f(q, v, p); }
    if (idx == 2) { return vec3f(p, v, t); }
    if (idx == 3) { return vec3f(p, q, v); }
    if (idx == 4) { return vec3f(t, p, v); }
    return vec3f(v, p, q);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(inputTex, texSampler, input.uv);

    // Convert to HSV
    var hsv = rgb2hsv(color.rgb);

    // Apply adjustments
    hsv.x = fract(hsv.x + uniforms.hueShift);  // Hue shift (wraps around)
    hsv.y *= uniforms.saturation;               // Saturation multiply
    hsv.z *= uniforms.value;                    // Value multiply

    // Convert back to RGB
    let rgb = hsv2rgb(hsv);

    return vec4f(rgb, color.a);
}
)";
}

} // namespace vivid::effects
