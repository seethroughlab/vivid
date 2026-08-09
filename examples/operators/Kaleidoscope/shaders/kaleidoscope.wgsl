/*{
  "version": 1,
  "name": "Kaleidoscope",
  "summary": "Radial mirror-symmetry (kaleidoscope) fold of the input.",
  "role": "transform",
  "keywords": ["effect", "kaleidoscope", "mirror"],
  "inputs": ["input"],
  "params": [
    {"name": "segments", "type": "float", "default": 0.3, "min": 0, "max": 1,
     "description": "How many mirrored wedges (2..16)"},
    {"name": "cx",       "type": "float", "default": 0.5, "min": 0, "max": 1},
    {"name": "cy",       "type": "float", "default": 0.5, "min": 0, "max": 1},
    {"name": "angle",    "type": "float", "default": 0.0, "min": 0, "max": 1,
     "semantic_tag": "phase_01", "semantic_intent": "rotation of the fold"},
    {"name": "zoom",     "type": "float", "default": 0.5, "min": 0, "max": 1}
  ]
}*/
// The uniform struct, its bindings and the fullscreen vertex stage are GENERATED from the
// header above (ADR-0016). Declare a param, then use it as u.<name>.
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let ar = u.res.x / max(u.res.y, 1.0);
    var p = (inp.uv - vec2f(u.cx, u.cy)) * vec2f(ar, 1.0);
    let seg = 2.0 + floor(u.segments * 14.0);            // 2..16 wedges
    let wedge = 6.2831853 / seg;
    var ang = atan2(p.y, p.x) + u.angle * 6.2831853;
    ang = ang - wedge * floor(ang / wedge);
    ang = abs(ang - wedge * 0.5);                        // mirror within the wedge
    let r = length(p) * (0.6 + u.zoom * 0.8);
    var uv2 = vec2f(cos(ang), sin(ang)) * r / vec2f(ar, 1.0) + vec2f(u.cx, u.cy);
    uv2 = clamp(uv2, vec2f(0.0), vec2f(1.0));
    return textureSample(input, samp, uv2);
}
