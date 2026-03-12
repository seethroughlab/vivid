/*{
  "name": "Spirograph",
  "inputs": [],
  "params": [
    {"name": "outer_r",    "default": 0.35, "min": 0.05, "max": 0.5},
    {"name": "inner_r",    "default": 0.15, "min": 0.01, "max": 0.49},
    {"name": "pen_dist",   "default": 0.1,  "min": 0.0,  "max": 0.5},
    {"name": "rotation",   "default": 0.0,  "min": 0.0,  "max": 6.28318},
    {"name": "thickness",  "default": 0.003, "min": 0.001, "max": 0.02},
    {"name": "glow",       "default": 0.5,  "min": 0.0,  "max": 2.0},
    {"name": "revolutions","default": 16.0, "min": 1.0,  "max": 64.0},
    {"name": "r",          "default": 0.2,  "min": 0.0,  "max": 1.0},
    {"name": "g",          "default": 0.8,  "min": 0.0,  "max": 1.0},
    {"name": "b",          "default": 1.0,  "min": 0.0,  "max": 1.0}
  ]
}*/

// Spirograph (hypotrochoid) parametric curve:
//   x(t) = (R - r) * cos(t) + d * cos((R - r) / r * t)
//   y(t) = (R - r) * sin(t) - d * sin((R - r) / r * t)
// Distance from pixel to nearest point on curve, sampled densely.

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let aspect = u.resolution.x / u.resolution.y;
    let uv = vec2f((input.uv.x - 0.5) * aspect, input.uv.y - 0.5);

    let R = u.outer_r;
    let rv = u.inner_r;
    let d = u.pen_dist;
    let rot = u.rotation + u.time * 0.2;

    let ca = cos(rot);
    let sa = sin(rot);
    let p = vec2f(ca * uv.x + sa * uv.y, -sa * uv.x + ca * uv.y);

    let diff = R - rv;
    let ratio = diff / max(rv, 0.001);

    // Sample the curve and find minimum distance
    let total_samples = i32(u.revolutions * 8.0);
    let max_samples = min(total_samples, 512);
    let dt = u.revolutions * TAU / f32(max_samples);

    var min_dist = 100.0;

    for (var i = 0; i < max_samples; i++) {
        let t = f32(i) * dt;
        let cx = diff * cos(t) + d * cos(ratio * t);
        let cy = diff * sin(t) - d * sin(ratio * t);
        let dist = length(p - vec2f(cx, cy));
        min_dist = min(min_dist, dist);
    }

    let color = vec3f(u.r, u.g, u.b);

    // Sharp curve line
    let line = 1.0 - smoothstep(0.0, u.thickness, min_dist);

    // Glow falloff
    let glow_amount = u.glow * exp(-min_dist * 80.0);

    let intensity = line + glow_amount;
    return vec4f(color * intensity, intensity);
}
