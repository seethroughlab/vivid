/*{
  "name": "Color Space",
  "params": [
    {"name": "source",     "default": 0, "type": "int", "choices": ["sRGB / Rec.709", "DCI-P3", "Rec.2020"]},
    {"name": "target_cs",  "default": 0, "type": "int", "choices": ["sRGB / Rec.709", "DCI-P3", "Rec.2020"]},
    {"name": "adapt",      "default": 1, "type": "int", "choices": ["None", "Bradford"]},
    {"name": "gamut_clip", "default": 1, "type": "int", "choices": ["Off", "Clip", "Compress"]}
  ]
}*/

// RGB ↔ XYZ conversions using explicit dot products.
// Note: param is named `target_cs` (not `target`) because `target` is a
// WGSL reserved keyword that naga rejects at shader module creation.

fn rgb_to_xyz(rgb: vec3f, space: i32) -> vec3f {
    if (space == 1) {
        // DCI-P3 (D65 display variant)
        return vec3f(
            dot(rgb, vec3f(0.4865709, 0.2656677, 0.1982173)),
            dot(rgb, vec3f(0.2289746, 0.6917385, 0.0792869)),
            dot(rgb, vec3f(0.0000000, 0.0451134, 1.0439444))
        );
    }
    if (space == 2) {
        // Rec.2020
        return vec3f(
            dot(rgb, vec3f(0.6369580, 0.1446169, 0.1688810)),
            dot(rgb, vec3f(0.2627002, 0.6779981, 0.0593017)),
            dot(rgb, vec3f(0.0000000, 0.0280727, 1.0609851))
        );
    }
    // sRGB / Rec.709
    return vec3f(
        dot(rgb, vec3f(0.4124564, 0.3575761, 0.1804375)),
        dot(rgb, vec3f(0.2126729, 0.7151522, 0.0721750)),
        dot(rgb, vec3f(0.0193339, 0.1191920, 0.9503041))
    );
}

fn xyz_to_rgb(xyz: vec3f, space: i32) -> vec3f {
    if (space == 1) {
        // DCI-P3 (D65 display variant)
        return vec3f(
            dot(xyz, vec3f( 2.4934969, -0.9313836, -0.4027108)),
            dot(xyz, vec3f(-0.8294890,  1.7626641,  0.0236247)),
            dot(xyz, vec3f( 0.0358458, -0.0761724,  0.9568845))
        );
    }
    if (space == 2) {
        // Rec.2020
        return vec3f(
            dot(xyz, vec3f( 1.7166512, -0.3556708, -0.2533663)),
            dot(xyz, vec3f(-0.6666844,  1.6164812,  0.0157685)),
            dot(xyz, vec3f( 0.0176399, -0.0427706,  0.9421031))
        );
    }
    // sRGB / Rec.709
    return vec3f(
        dot(xyz, vec3f( 3.2404542, -1.5371385, -0.4985314)),
        dot(xyz, vec3f(-0.9692660,  1.8760108,  0.0415560)),
        dot(xyz, vec3f( 0.0556434, -0.2040259,  1.0572252))
    );
}

// Soft-compress values outside [0,1] toward the boundary.
fn gamut_compress_channel(v: f32) -> f32 {
    if (v >= 0.0 && v <= 1.0) { return v; }
    let abs_v = abs(v);
    let excess = abs_v - 1.0;
    let threshold = 0.2;
    let compressed = 1.0 + threshold * (1.0 - exp(-excess / threshold));
    if (v < 0.0) { return -compressed; }
    return compressed;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let col = textureSample(inputTex, texSampler, input.uv);

    let src_space = i32(u.source);
    let dst_space = i32(u.target_cs);

    // Identity: same source and target, skip conversion
    if (src_space == dst_space) {
        return col;
    }

    // Convert: source RGB → XYZ → target RGB
    let xyz = rgb_to_xyz(col.rgb, src_space);
    var rgb = xyz_to_rgb(xyz, dst_space);

    // Gamut handling
    let mode = i32(u.gamut_clip);
    if (mode == 1) {
        // Hard clip
        rgb = clamp(rgb, vec3f(0.0), vec3f(1.0));
    } else if (mode == 2) {
        // Soft compress
        rgb = vec3f(
            gamut_compress_channel(rgb.x),
            gamut_compress_channel(rgb.y),
            gamut_compress_channel(rgb.z)
        );
    }

    return vec4f(rgb, col.a);
}
