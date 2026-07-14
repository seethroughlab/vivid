/*{
  "version": 1,
  "name": "Tint",
  "summary": "WGSL example generator: a hue-shifted gradient (shows the WGSL authoring path).",
  "keywords": ["generator", "tint", "wgsl"],
  "inputs": [],
  "params": [
    {"name": "hue", "type": "float", "default": 0.5, "min": 0, "max": 1,
     "semantic_tag": "phase_01", "semantic_intent": "colour hue"},
    {"name": "r",   "type": "float", "default": 1.0, "min": 0, "max": 1, "display": "color"},
    {"name": "g",   "type": "float", "default": 1.0, "min": 0, "max": 1},
    {"name": "b",   "type": "float", "default": 1.0, "min": 0, "max": 1}
  ]
}*/
// The uniform struct, its bindings and the fullscreen vertex stage are GENERATED from the
// header above (ADR-0016). Declare a param, then use it as u.<name>.
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let c = 0.5 + 0.5 * cos(vec3f(0.0, 2.0, 4.0) + inp.uv.x * 6.2831853 + u.time * 0.5 + u.hue * 6.2831853);
    return vec4f(c * vec3f(u.r, u.g, u.b), 1.0);   // r/g/b tint (COLOR compound-widget)
}
