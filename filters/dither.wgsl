/*{
  "name": "Dither",
  "params": [
    {"name": "pattern",  "type": "int", "default": 1, "min": 0, "max": 2,
     "choices": ["Bayer 2x2", "Bayer 4x4", "Bayer 8x8"]},
    {"name": "levels",   "default": 4.0, "min": 2.0, "max": 256.0},
    {"name": "strength", "default": 1.0, "min": 0.0, "max": 1.0}
  ]
}*/
// Bayer 2x2 (normalized to 0–1)
const bayer2: array<f32, 4> = array<f32, 4>(
    0.0 / 4.0, 2.0 / 4.0,
    3.0 / 4.0, 1.0 / 4.0
);

// Bayer 4x4 (normalized to 0–1)
const bayer4: array<f32, 16> = array<f32, 16>(
     0.0 / 16.0,  8.0 / 16.0,  2.0 / 16.0, 10.0 / 16.0,
    12.0 / 16.0,  4.0 / 16.0, 14.0 / 16.0,  6.0 / 16.0,
     3.0 / 16.0, 11.0 / 16.0,  1.0 / 16.0,  9.0 / 16.0,
    15.0 / 16.0,  7.0 / 16.0, 13.0 / 16.0,  5.0 / 16.0
);

// Bayer 8x8 (normalized to 0–1)
const bayer8: array<f32, 64> = array<f32, 64>(
     0.0 / 64.0, 32.0 / 64.0,  8.0 / 64.0, 40.0 / 64.0,  2.0 / 64.0, 34.0 / 64.0, 10.0 / 64.0, 42.0 / 64.0,
    48.0 / 64.0, 16.0 / 64.0, 56.0 / 64.0, 24.0 / 64.0, 50.0 / 64.0, 18.0 / 64.0, 58.0 / 64.0, 26.0 / 64.0,
    12.0 / 64.0, 44.0 / 64.0,  4.0 / 64.0, 36.0 / 64.0, 14.0 / 64.0, 46.0 / 64.0,  6.0 / 64.0, 38.0 / 64.0,
    60.0 / 64.0, 28.0 / 64.0, 52.0 / 64.0, 20.0 / 64.0, 62.0 / 64.0, 30.0 / 64.0, 54.0 / 64.0, 22.0 / 64.0,
     3.0 / 64.0, 35.0 / 64.0, 11.0 / 64.0, 43.0 / 64.0,  1.0 / 64.0, 33.0 / 64.0,  9.0 / 64.0, 41.0 / 64.0,
    51.0 / 64.0, 19.0 / 64.0, 59.0 / 64.0, 27.0 / 64.0, 49.0 / 64.0, 17.0 / 64.0, 57.0 / 64.0, 25.0 / 64.0,
    15.0 / 64.0, 47.0 / 64.0,  7.0 / 64.0, 39.0 / 64.0, 13.0 / 64.0, 45.0 / 64.0,  5.0 / 64.0, 37.0 / 64.0,
    63.0 / 64.0, 31.0 / 64.0, 55.0 / 64.0, 23.0 / 64.0, 61.0 / 64.0, 29.0 / 64.0, 53.0 / 64.0, 21.0 / 64.0
);

fn get_threshold(px: vec2i, pat: i32) -> f32 {
    if (pat == 0) {
        let idx = (px.y % 2) * 2 + (px.x % 2);
        return bayer2[idx];
    } else if (pat == 1) {
        let idx = (px.y % 4) * 4 + (px.x % 4);
        return bayer4[idx];
    } else {
        let idx = (px.y % 8) * 8 + (px.x % 8);
        return bayer8[idx];
    }
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let col = textureSample(inputTex, texSampler, input.uv);
    let px = vec2i(input.uv * u.resolution);
    let pat = i32(u.pattern);

    let threshold = get_threshold(px, pat);
    let dither_offset = (threshold - 0.5) * u.strength;
    let levels = u.levels;

    let r = floor((col.r + dither_offset) * levels) / levels;
    let g = floor((col.g + dither_offset) * levels) / levels;
    let b = floor((col.b + dither_offset) * levels) / levels;

    return vec4f(clamp(r, 0.0, 1.0), clamp(g, 0.0, 1.0), clamp(b, 0.0, 1.0), col.a);
}
