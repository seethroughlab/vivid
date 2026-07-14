/*{
  "version": 1,
  "name": "Gradient",
  "summary": "A clean 2-colour linear/radial gradient (a flat source, not a field).",
  "keywords": ["generator", "gradient", "color"],
  "inputs": [],
  "params": [
    {"name": "mode",  "choices": ["linear", "radial"], "default": 0,
     "description": "Linear sweep, or radial from the centre"},
    {"name": "cx",    "type": "float", "default": 0.5, "min": 0, "max": 1},
    {"name": "cy",    "type": "float", "default": 0.5, "min": 0, "max": 1},
    {"name": "angle", "type": "float", "default": 0.0, "min": 0, "max": 1,
     "semantic_tag": "phase_01", "semantic_intent": "sweep angle"},
    {"name": "scale", "type": "float", "default": 0.5, "min": 0, "max": 1},
    {"name": "ar",    "type": "float", "default": 0.10, "min": 0, "max": 1, "display": "color"},
    {"name": "ag",    "type": "float", "default": 0.10, "min": 0, "max": 1},
    {"name": "ab",    "type": "float", "default": 0.35, "min": 0, "max": 1},
    {"name": "br",    "type": "float", "default": 0.90, "min": 0, "max": 1, "display": "color"},
    {"name": "bg",    "type": "float", "default": 0.20, "min": 0, "max": 1},
    {"name": "bb",    "type": "float", "default": 0.60, "min": 0, "max": 1}
  ]
}*/
// The uniform struct, its bindings and the fullscreen vertex stage are GENERATED from the
// header above (ADR-0016). Declare a param, then use it as u.<name>.
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    var t: f32;
    if (u.mode == 0) {                                  // linear
        let a = u.angle * 6.2831853;
        t = dot(inp.uv - vec2f(u.cx, u.cy), vec2f(cos(a), sin(a))) * (0.5 + u.scale * 2.0) + 0.5;
    } else {                                             // radial
        let d = (inp.uv - vec2f(u.cx, u.cy)) * vec2f(u.res.x / max(u.res.y, 1.0), 1.0);
        t = length(d) * (0.7 + u.scale * 3.0);
    }
    return vec4f(mix(vec3f(u.ar, u.ag, u.ab), vec3f(u.br, u.bg, u.bb), clamp(t, 0.0, 1.0)), 1.0);
}
