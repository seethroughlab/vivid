/*{
  "version": 1,
  "name": "Shape",
  "summary": "A crisp SDF shape (circle/polygon) drawn over its input. Geometry, not a field.",
  "keywords": ["generator", "shape", "geometry"],
  "inputs": ["input"],
  "params": [
    {"name": "sides",    "type": "float", "default": 0.75, "min": 0, "max": 1,
     "description": "Circle below ~0.15, else a polygon of 3..12 sides"},
    {"name": "x",        "type": "float", "default": 0.5,  "min": 0, "max": 1},
    {"name": "y",        "type": "float", "default": 0.5,  "min": 0, "max": 1},
    {"name": "size",     "type": "float", "default": 0.35, "min": 0, "max": 1},
    {"name": "rotation", "type": "float", "default": 0.0,  "min": 0, "max": 1,
     "semantic_tag": "phase_01", "semantic_intent": "rotation"},
    {"name": "softness", "type": "float", "default": 0.04, "min": 0, "max": 1,
     "description": "Edge feather"},
    {"name": "r",        "type": "float", "default": 1.0,  "min": 0, "max": 1, "display": "color"},
    {"name": "g",        "type": "float", "default": 0.2,  "min": 0, "max": 1},
    {"name": "b",        "type": "float", "default": 0.6,  "min": 0, "max": 1},
    {"name": "a",        "type": "float", "default": 1.0,  "min": 0, "max": 1,
     "description": "Opacity over the input"}
  ]
}*/
// The uniform struct, its bindings and the fullscreen vertex stage are GENERATED from the
// header above (ADR-0016). Declare a param, then use it as u.<name>.
fn sd_shape(pin: vec2f, r: f32, sides: f32) -> f32 {
    if (sides < 2.5) { return length(pin) - r; }
    let n = floor(sides + 0.5);
    let an = 3.14159265 / n;
    let acs = vec2f(cos(an), sin(an));
    var bn = atan2(pin.x, pin.y);
    bn = bn - 2.0 * an * floor((bn + an) / (2.0 * an));
    var p = length(pin) * vec2f(cos(bn), abs(sin(bn)));
    p = p - r * acs;
    p.y = p.y + clamp(-p.y, 0.0, r * acs.y);
    return length(p) * sign(p.x);
}
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let bg = textureSample(input, samp, inp.uv);
    let sides = floor(u.sides * 8.0);
    var p = inp.uv - vec2f(u.x, u.y);
    p.x = p.x * (u.res.x / max(u.res.y, 1.0));
    let a = u.rotation * 6.2831853;
    p = vec2f(p.x * cos(a) - p.y * sin(a), p.x * sin(a) + p.y * cos(a));
    let d = sd_shape(p, max(u.size * 0.7, 0.001), sides);
    let aa = fwidth(d) + u.softness * 0.06 + 0.0015;
    let cov = (1.0 - smoothstep(-aa, aa, d)) * vec4f(u.r, u.g, u.b, u.a).a;
    return vec4f(mix(bg.rgb, vec4f(u.r, u.g, u.b, u.a).rgb, cov), max(bg.a, cov));
}
