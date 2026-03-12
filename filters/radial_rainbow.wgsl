/*{
  "name": "RadialRainbow",
  "inputs": [],
  "params": [
    {"name": "cycles",     "default": 4.0,  "min": 0.5, "max": 10.0},
    {"name": "speed",      "default": 1.0,  "min": 0.0, "max": 5.0},
    {"name": "brightness", "default": 1.0,  "min": 0.0, "max": 2.0},
    {"name": "falloff",    "default": 1.5,  "min": 0.0, "max": 3.0},
    {"name": "center_x",   "default": 0.5,  "min": 0.0, "max": 1.0},
    {"name": "center_y",   "default": 0.5,  "min": 0.0, "max": 1.0}
  ]
}*/

fn hsv2rgb(h: f32, s: f32, v: f32) -> vec3f {
    let c = v * s;
    let hp = fract(h) * 6.0;
    let x = c * (1.0 - abs(hp % 2.0 - 1.0));
    var rgb = vec3f(0.0);
    if (hp < 1.0) { rgb = vec3f(c, x, 0.0); }
    else if (hp < 2.0) { rgb = vec3f(x, c, 0.0); }
    else if (hp < 3.0) { rgb = vec3f(0.0, c, x); }
    else if (hp < 4.0) { rgb = vec3f(0.0, x, c); }
    else if (hp < 5.0) { rgb = vec3f(x, 0.0, c); }
    else { rgb = vec3f(c, 0.0, x); }
    return rgb + (v - c);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let aspect = u.resolution.x / u.resolution.y;
    let center = vec2f(u.center_x, u.center_y);

    // Orbiting highlight offset (Paper.js vector orbit effect)
    let orbit = vec2f(
        sin(u.time * 0.5) * 0.05,
        cos(u.time * 0.5) * 0.05
    );
    let origin = center + orbit;

    // Aspect-corrected distance from origin
    let uv = input.uv;
    let diff = vec2f((uv.x - origin.x) * aspect, uv.y - origin.y);
    let dist = length(diff);

    // HSV hue cycles outward, animated over time
    // 0.33 speed factor: Paper.js shifts ~20deg/frame at 60fps ≈ 2 full cycles/sec
    let hue = fract(dist * u.cycles + u.time * u.speed * 0.33);

    // Brightness fades with distance
    let val = u.brightness * max(0.0, 1.0 - dist * u.falloff);

    let color = hsv2rgb(hue, 1.0, val);
    return vec4f(color, 1.0);
}
