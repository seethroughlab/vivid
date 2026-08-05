/*{
  "version": 1,
  "name": "Plasma",
  "summary": "Animated plasma colour-field generator. No input; drives a chain.",
  "role": "source",
  "keywords": ["generator", "plasma", "color"],
  "inputs": [],
  "params": [
    {"name": "warp",    "type": "float", "default": 0.5, "min": 0, "max": 1, "display": "xy_pad",
     "semantic_intent": "domain warp amount"},
    {"name": "hue",     "type": "float", "default": 0.0, "min": 0, "max": 1, "display": "knob",
     "semantic_tag": "phase_01", "semantic_intent": "color hue"},
    {"name": "density", "type": "float", "default": 0.5, "min": 0, "max": 1, "display": "knob",
     "semantic_intent": "pattern density"},
    {"name": "glow",    "type": "float", "default": 0.5, "min": 0, "max": 1, "display": "knob",
     "semantic_intent": "glow intensity"}
  ]
}*/
// The uniform struct, its bindings and the fullscreen vertex stage are GENERATED from the
// header above (ADR-0016). Declare a param, then use it as u.<name>.
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let uv = inp.uv;
    let t = u.time;
    let dens = 6.0 + u.density * 18.0;
    let w = uv + u.warp * 0.3 * vec2f(sin(uv.y * 8.0 + t), cos(uv.x * 8.0 + t));
    let v = sin(w.x * dens + t) + sin(w.y * dens + t * 1.3)
          + sin((w.x + w.y) * dens * 0.6 + t * 0.7)
          + sin(length(w - vec2f(0.5)) * dens * 1.8 - t * 2.0);
    let col = 0.5 + 0.5 * cos(vec3f(0.0, 2.0, 4.0) + v + u.hue * 6.2831853);
    return vec4f(col * (0.6 + u.glow), 1.0);
}
