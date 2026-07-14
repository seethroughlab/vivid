/*{
  "version": 1,
  "name": "Transform",
  "summary": "Zoom / rotate / translate / tile the input (near-identity at defaults).",
  "keywords": ["effect", "transform", "tile"],
  "inputs": ["input"],
  "params": [
    {"name": "tx",    "type": "float", "default": 0.5, "min": 0, "max": 1},
    {"name": "ty",    "type": "float", "default": 0.5, "min": 0, "max": 1},
    {"name": "rot",   "type": "float", "default": 0.0, "min": 0, "max": 1,
     "semantic_tag": "phase_01", "semantic_intent": "rotation"},
    {"name": "scale", "type": "float", "default": 0.2, "min": 0, "max": 1,
     "description": "0.25x .. 4x zoom; ~0.2 is 1:1"},
    {"name": "tile",  "type": "float", "default": 0.0, "min": 0, "max": 1,
     "description": "Repeat the image N times across the frame"}
  ]
}*/
// The uniform struct, its bindings and the fullscreen vertex stage are GENERATED from the
// header above (ADR-0016). Declare a param, then use it as u.<name>.
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    var p = inp.uv - vec2f(0.5, 0.5);
    let zoom = 0.25 + u.scale * 3.75;                    // scale 0..1 -> 0.25..4x  (~0.2 = 1x)
    p = p / zoom;
    let a = u.rot * 6.2831853;
    p = vec2f(p.x * cos(a) - p.y * sin(a), p.x * sin(a) + p.y * cos(a));
    p = p - (vec2f(u.tx, u.ty) - vec2f(0.5, 0.5));
    var uv2 = p + vec2f(0.5, 0.5);
    let tiles = 1.0 + floor(u.tile * 8.0);               // tile 0 -> 1 (no repeat)
    uv2 = fract(uv2 * tiles);
    return textureSample(input, samp, uv2);
}
