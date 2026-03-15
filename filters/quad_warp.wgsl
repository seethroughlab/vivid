/*{
  "name": "Quad Warp",
  "params": [
    {"name": "tl_x", "default": 0.0, "min": -0.5, "max": 1.5, "display": "xy_pad"},
    {"name": "tl_y", "default": 0.0, "min": -0.5, "max": 1.5, "display": "xy_pad"},
    {"name": "tr_x", "default": 1.0, "min": -0.5, "max": 1.5, "display": "xy_pad"},
    {"name": "tr_y", "default": 0.0, "min": -0.5, "max": 1.5, "display": "xy_pad"},
    {"name": "bl_x", "default": 0.0, "min": -0.5, "max": 1.5, "display": "xy_pad"},
    {"name": "bl_y", "default": 1.0, "min": -0.5, "max": 1.5, "display": "xy_pad"},
    {"name": "br_x", "default": 1.0, "min": -0.5, "max": 1.5, "display": "xy_pad"},
    {"name": "br_y", "default": 1.0, "min": -0.5, "max": 1.5, "display": "xy_pad"},
    {"name": "crop",         "default": 0.0, "min": 0.0, "max": 1.0},
    {"name": "grid_overlay", "default": 0.0, "min": 0.0, "max": 1.0}
  ]
}*/

// Inverse bilinear interpolation: given output UV and four corner positions,
// solve for (s,t) in the original texture space.
//
// Q(s,t) = (1-s)(1-t)*P00 + s*(1-t)*P10 + (1-s)*t*P01 + s*t*P11
// We need to find (s,t) given Q = output UV.

fn inverse_bilinear(p: vec2f, p00: vec2f, p10: vec2f, p01: vec2f, p11: vec2f) -> vec2f {
    let e = p10 - p00;
    let f = p01 - p00;
    let g = p00 - p10 + p11 - p01;
    let h = p - p00;

    let k2 = cross2d(g, f);
    let k1 = cross2d(e, f) + cross2d(h, g);
    let k0 = cross2d(h, e);

    // Solve quadratic k2*t^2 + k1*t + k0 = 0
    var t: f32;
    var s: f32;

    if (abs(k2) < 1e-6) {
        // Linear case
        t = -k0 / k1;
        let denom = e.x + g.x * t;
        if (abs(denom) > abs(e.y + g.y * t)) {
            s = (h.x - f.x * t) / denom;
        } else {
            s = (h.y - f.y * t) / (e.y + g.y * t);
        }
    } else {
        let disc = k1 * k1 - 4.0 * k0 * k2;
        if (disc < 0.0) {
            return vec2f(-1.0);
        }
        let sqrt_disc = sqrt(disc);
        let t0 = (-k1 - sqrt_disc) / (2.0 * k2);
        let t1 = (-k1 + sqrt_disc) / (2.0 * k2);

        // Pick the root in [0,1] range (prefer t0)
        if (t0 >= -0.001 && t0 <= 1.001) {
            t = t0;
        } else {
            t = t1;
        }

        let denom = e.x + g.x * t;
        if (abs(denom) > abs(e.y + g.y * t)) {
            s = (h.x - f.x * t) / denom;
        } else {
            s = (h.y - f.y * t) / (e.y + g.y * t);
        }
    }

    return vec2f(s, t);
}

fn cross2d(a: vec2f, b: vec2f) -> f32 {
    return a.x * b.y - a.y * b.x;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;

    // Corner positions (where the original texture corners map to in output space)
    let p00 = vec2f(u.tl_x, u.tl_y); // top-left
    let p10 = vec2f(u.tr_x, u.tr_y); // top-right
    let p01 = vec2f(u.bl_x, u.bl_y); // bottom-left
    let p11 = vec2f(u.br_x, u.br_y); // bottom-right

    // Find source UV via inverse bilinear interpolation
    let st = inverse_bilinear(uv, p00, p10, p01, p11);

    // Grid overlay for alignment
    var grid_color = vec4f(0.0);
    if (u.grid_overlay > 0.001) {
        let grid_lines = 8.0;
        let gx = abs(fract(uv.x * grid_lines) - 0.5);
        let gy = abs(fract(uv.y * grid_lines) - 0.5);
        let line = 1.0 - smoothstep(0.0, 0.02, min(gx, gy));
        // Crosshair at center
        let cx = 1.0 - smoothstep(0.0, 0.003, abs(uv.x - 0.5));
        let cy = 1.0 - smoothstep(0.0, 0.003, abs(uv.y - 0.5));
        let cross = max(cx, cy);
        let g = max(line * 0.5, cross);
        grid_color = vec4f(0.0, g, 0.0, g) * u.grid_overlay;
    }

    // Out-of-bounds check
    if (st.x < 0.0 || st.x > 1.0 || st.y < 0.0 || st.y > 1.0) {
        if (u.crop > 0.5) {
            // Clamp mode
            let clamped = clamp(st, vec2f(0.0), vec2f(1.0));
            let tex = textureSample(inputTex, texSampler, clamped);
            return max(tex, grid_color);
        }
        // Transparent mode — still show grid if enabled
        return grid_color;
    }

    let tex = textureSample(inputTex, texSampler, st);
    return max(tex, grid_color);
}
