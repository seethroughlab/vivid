// Vivid Effects 2D - Dither Operator Implementation

#include <vivid/effects/dither.h>

namespace vivid::effects {

const char* Dither::fragmentShader() const {
    return R"(
struct Uniforms {
    pattern: i32,
    levels: i32,
    strength: f32,
    _pad: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var inputTex: texture_2d<f32>;
@group(0) @binding(2) var texSampler: sampler;

// Bayer matrices
fn bayer2x2(p: vec2i) -> f32 {
    let m = array<f32, 4>(0.0, 2.0, 3.0, 1.0);
    let idx = (p.x % 2) + (p.y % 2) * 2;
    return m[idx] / 4.0;
}

fn bayer4x4(p: vec2i) -> f32 {
    let m = array<f32, 16>(
         0.0,  8.0,  2.0, 10.0,
        12.0,  4.0, 14.0,  6.0,
         3.0, 11.0,  1.0,  9.0,
        15.0,  7.0, 13.0,  5.0
    );
    let idx = (p.x % 4) + (p.y % 4) * 4;
    return m[idx] / 16.0;
}

fn bayer8x8(p: vec2i) -> f32 {
    let m = array<f32, 64>(
         0.0, 32.0,  8.0, 40.0,  2.0, 34.0, 10.0, 42.0,
        48.0, 16.0, 56.0, 24.0, 50.0, 18.0, 58.0, 26.0,
        12.0, 44.0,  4.0, 36.0, 14.0, 46.0,  6.0, 38.0,
        60.0, 28.0, 52.0, 20.0, 62.0, 30.0, 54.0, 22.0,
         3.0, 35.0, 11.0, 43.0,  1.0, 33.0,  9.0, 41.0,
        51.0, 19.0, 59.0, 27.0, 49.0, 17.0, 57.0, 25.0,
        15.0, 47.0,  7.0, 39.0, 13.0, 45.0,  5.0, 37.0,
        63.0, 31.0, 55.0, 23.0, 61.0, 29.0, 53.0, 21.0
    );
    let idx = (p.x % 8) + (p.y % 8) * 8;
    return m[idx] / 64.0;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let original = textureSample(inputTex, texSampler, input.uv);
    let pixel = vec2i(input.position.xy);

    var threshold: f32;
    if (uniforms.pattern == 0) {
        threshold = bayer2x2(pixel);
    } else if (uniforms.pattern == 1) {
        threshold = bayer4x4(pixel);
    } else {
        threshold = bayer8x8(pixel);
    }

    // Apply dithering
    let levels = f32(uniforms.levels);
    let step = 1.0 / (levels - 1.0);

    var dithered = original.rgb + (threshold - 0.5) * step;
    dithered = floor(dithered * (levels - 1.0) + 0.5) / (levels - 1.0);
    dithered = clamp(dithered, vec3f(0.0), vec3f(1.0));

    let result = mix(original.rgb, dithered, uniforms.strength);
    return vec4f(result, original.a);
}
)";
}

} // namespace vivid::effects
