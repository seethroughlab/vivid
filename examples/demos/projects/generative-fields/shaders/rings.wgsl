/*{
  "version": 1,
  "name": "Rings",
  "summary": "Concentric pulsing rings — a source you can drive from the beat.",
  "role": "source",
  "keywords": ["generator", "rings", "pulse", "circles"],
  "inputs": [],
  "params": [
    {"name": "count",  "type": "float", "default": 8.0, "min": 1.0, "max": 40.0,
     "display": "knob", "description": "how many rings fit across the frame"},
    {"name": "speed",  "type": "float", "default": 0.4, "min": -2.0, "max": 2.0,
     "display": "knob", "description": "outward drift; negative pulls inward"},
    {"name": "width",  "type": "float", "default": 0.35, "min": 0.02, "max": 1.0,
     "display": "knob", "description": "thickness of each ring"},
    {"name": "glow",   "type": "float", "default": 0.5, "min": 0.0, "max": 1.0,
     "display": "knob", "description": "brightness — wire this to a kick"},
    {"name": "center", "type": "point2", "default": [0.5, 0.5],
     "description": "where the rings radiate from"},
    {"name": "tint",   "type": "color",  "default": [0.35, 0.75, 1.0],
     "description": "ring colour"}
  ]
}*/
// The host generated everything above the body from that header: the uniform struct `u`
// (u.res, u.time, u.count, u.speed, u.width, u.glow, u.center, u.tint), its bindings and
// the fullscreen vertex stage. Declare a param, use it — there is nothing to pack by hand.
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let ar = u.res.x / max(u.res.y, 1.0);
    let p  = (inp.uv - u.center) * vec2f(ar, 1.0);
    let r  = length(p);

    // Ring field: a sine in radius, marched outward by time.
    let phase = r * u.count - u.time * u.speed * 2.0;
    let wave  = 0.5 + 0.5 * cos(phase * 6.2831853);
    let ring  = pow(wave, max(1.0, 12.0 * (1.0 - u.width)));

    // Fade at the corners so the frame reads as a source, not a texture swatch.
    let vignette = smoothstep(1.1, 0.15, r);
    let col = u.tint * ring * vignette * (0.35 + u.glow * 1.3);
    return vec4f(col, 1.0);
}
