/*{
  "version": 1,
  "name": "Fold",
  "summary": "Crisp axis-aligned mirror reflection of the input — architectural symmetry, not a busy radial kaleidoscope.",
  "role": "transform",
  "keywords": ["effect", "mirror", "fold", "symmetry", "reflect"],
  "inputs": ["input"],
  "params": [
    {"name": "axes",  "choices": ["1 (L-R)", "2 (quad)", "3 (+diag)"], "default": 1,
     "description": "how many mirror axes"},
    {"name": "angle", "type": "float", "default": 0.0, "min": 0, "max": 1,
     "semantic_tag": "phase_01", "description": "rotation of the fold"},
    {"name": "cx",    "type": "float", "default": 0.5, "min": 0, "max": 1},
    {"name": "cy",    "type": "float", "default": 0.5, "min": 0, "max": 1},
    {"name": "zoom",  "type": "float", "default": 0.5, "min": 0, "max": 1}
  ]
}*/
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let ar = u.res.x / max(u.res.y, 1.0);
    var p = (inp.uv - vec2f(u.cx, u.cy)) * vec2f(ar, 1.0);
    let a = u.angle * 6.2831853;
    p = vec2f(p.x * cos(a) - p.y * sin(a), p.x * sin(a) + p.y * cos(a));
    p = p / (0.6 + u.zoom * 0.9);
    // Cartesian mirror folds — clean, axis-aligned (paper-fold), not a radial pinwheel.
    p.x = abs(p.x);                                   // axis 1: left-right
    if (u.axes >= 1) { p.y = abs(p.y); }              // axis 2: top-bottom (quad symmetry)
    if (u.axes >= 2) { if (p.x < p.y) { p = p.yx; } } // axis 3: diagonal
    var uv2 = p / vec2f(ar, 1.0) + vec2f(u.cx, u.cy);
    uv2 = clamp(uv2, vec2f(0.0), vec2f(1.0));
    return textureSample(input, samp, uv2);
}
