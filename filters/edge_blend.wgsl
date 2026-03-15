/*{
  "name": "Edge Blend",
  "params": [
    {"name": "left",         "default": 0.0, "min": 0.0, "max": 0.5},
    {"name": "right",        "default": 0.0, "min": 0.0, "max": 0.5},
    {"name": "top",          "default": 0.0, "min": 0.0, "max": 0.5},
    {"name": "bottom",       "default": 0.0, "min": 0.0, "max": 0.5},
    {"name": "gamma",        "default": 2.2, "min": 0.1, "max": 5.0},
    {"name": "curve",        "default": 1.0, "min": 0.0, "max": 2.0, "type": "int", "choices": ["Linear", "Smoothstep", "Raised Cosine"]},
    {"name": "show_overlap", "default": 0.0, "min": 0.0, "max": 1.0},
    {"name": "brightness",   "default": 1.0, "min": 0.0, "max": 2.0}
  ]
}*/

fn blend_curve(t: f32, curve_type: i32) -> f32 {
    let tc = clamp(t, 0.0, 1.0);
    if (curve_type == 2) {
        // Raised cosine
        return 0.5 - 0.5 * cos(tc * PI);
    }
    if (curve_type == 1) {
        // Smoothstep
        return tc * tc * (3.0 - 2.0 * tc);
    }
    // Linear
    return tc;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let tex = textureSample(inputTex, texSampler, uv);

    var factor = 1.0;
    let curve_type = i32(u.curve);

    // Left edge blend
    if (u.left > 0.001) {
        let t = uv.x / u.left;
        factor *= pow(blend_curve(t, curve_type), u.gamma);
    }

    // Right edge blend
    if (u.right > 0.001) {
        let t = (1.0 - uv.x) / u.right;
        factor *= pow(blend_curve(t, curve_type), u.gamma);
    }

    // Top edge blend
    if (u.top > 0.001) {
        let t = uv.y / u.top;
        factor *= pow(blend_curve(t, curve_type), u.gamma);
    }

    // Bottom edge blend
    if (u.bottom > 0.001) {
        let t = (1.0 - uv.y) / u.bottom;
        factor *= pow(blend_curve(t, curve_type), u.gamma);
    }

    var color = tex.rgb * factor * u.brightness;

    // Debug: tint blend regions red
    if (u.show_overlap > 0.001 && factor < 0.999) {
        let tint = vec3f(1.0, 0.0, 0.0) * (1.0 - factor);
        color = mix(color, tint, u.show_overlap * 0.5);
    }

    return vec4f(color, tex.a * factor);
}
