/*{
  "name": "Color Space",
  "params": [
    {"name": "source",     "default": 0, "type": "int", "choices": ["sRGB / Rec.709", "DCI-P3", "Rec.2020"]},
    {"name": "target",     "default": 0, "type": "int", "choices": ["sRGB / Rec.709", "DCI-P3", "Rec.2020"]},
    {"name": "adapt",      "default": 1, "type": "int", "choices": ["None", "Bradford"]},
    {"name": "gamut_clip", "default": 1, "type": "int", "choices": ["Off", "Clip", "Compress"]}
  ]
}*/

// ---- RGB ↔ XYZ matrices (D65 white point) ----
// sRGB / Rec.709
const srgb_to_xyz = mat3x3<f32>(
    vec3f(0.4124564, 0.3575761, 0.1804375),
    vec3f(0.2126729, 0.7151522, 0.0721750),
    vec3f(0.0193339, 0.1191920, 0.9503041)
);
const xyz_to_srgb = mat3x3<f32>(
    vec3f( 3.2404542, -1.5371385, -0.4985314),
    vec3f(-0.9692660,  1.8760108,  0.0415560),
    vec3f( 0.0556434, -0.2040259,  1.0572252)
);

// DCI-P3 (D65 display variant, a.k.a. Display P3)
const p3_to_xyz = mat3x3<f32>(
    vec3f(0.4865709, 0.2656677, 0.1982173),
    vec3f(0.2289746, 0.6917385, 0.0792869),
    vec3f(0.0000000, 0.0451134, 1.0439444)
);
const xyz_to_p3 = mat3x3<f32>(
    vec3f( 2.4934969, -0.9313836, -0.4027108),
    vec3f(-0.8294890,  1.7626641,  0.0236247),
    vec3f( 0.0358458, -0.0761724,  0.9568845)
);

// Rec.2020
const r2020_to_xyz = mat3x3<f32>(
    vec3f(0.6369580, 0.1446169, 0.1688810),
    vec3f(0.2627002, 0.6779981, 0.0593017),
    vec3f(0.0000000, 0.0280727, 1.0609851)
);
const xyz_to_r2020 = mat3x3<f32>(
    vec3f( 1.7166512, -0.3556708, -0.2533663),
    vec3f(-0.6666844,  1.6164812,  0.0157685),
    vec3f( 0.0176399, -0.0427706,  0.9421031)
);

fn rgb_to_xyz(rgb: vec3f, space: i32) -> vec3f {
    if (space == 1) { return p3_to_xyz * rgb; }
    if (space == 2) { return r2020_to_xyz * rgb; }
    return srgb_to_xyz * rgb;
}

fn xyz_to_rgb(xyz: vec3f, space: i32) -> vec3f {
    if (space == 1) { return xyz_to_p3 * xyz; }
    if (space == 2) { return xyz_to_r2020 * xyz; }
    return xyz_to_srgb * xyz;
}

// ---- Gamut mapping ----

// Soft-compress values outside [0,1] toward the boundary.
// Uses a smooth shoulder curve so near-boundary colors stay accurate
// while far-out-of-gamut colors are pulled in gracefully.
fn gamut_compress_channel(v: f32) -> f32 {
    if (v >= 0.0 && v <= 1.0) { return v; }
    if (v < 0.0) {
        // Mirror: compress negative values symmetrically
        return -gamut_compress_channel(-v);
    }
    // v > 1.0: soft knee — asymptotically approaches 1.0 + threshold
    let excess = v - 1.0;
    let threshold = 0.2;
    return 1.0 + threshold * (1.0 - exp(-excess / threshold));
}

fn gamut_compress(rgb: vec3f) -> vec3f {
    return vec3f(
        gamut_compress_channel(rgb.x),
        gamut_compress_channel(rgb.y),
        gamut_compress_channel(rgb.z)
    );
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let col = textureSample(inputTex, texSampler, input.uv);

    let source = i32(u.source);
    let target = i32(u.target);

    // Identity: same source and target, skip conversion
    if (source == target) {
        return col;
    }

    // Convert: source RGB → XYZ → target RGB
    let xyz = rgb_to_xyz(col.rgb, source);
    var rgb = xyz_to_rgb(xyz, target);

    // Gamut handling
    let mode = i32(u.gamut_clip);
    if (mode == 1) {
        // Hard clip
        rgb = clamp(rgb, vec3f(0.0), vec3f(1.0));
    } else if (mode == 2) {
        // Soft compress
        rgb = gamut_compress(rgb);
    }

    return vec4f(rgb, col.a);
}
