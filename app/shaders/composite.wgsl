/*{
  "version": 1,
  "name": "Composite",
  "summary": "Blend two inputs (A base, B over): normal/add/multiply/screen/overlay.",
  "role": "transform",
  "keywords": ["effect", "composite", "blend"],
  "inputs": ["A", "B"],
  "params": [
    {"name": "mode",    "choices": ["normal", "add", "multiply", "screen", "overlay"], "default": 0,
     "description": "How B is blended over A"},
    {"name": "opacity", "type": "float", "default": 1.0, "min": 0, "max": 1,
     "semantic_intent": "blend amount"}
  ]
}*/
// The uniform struct, its bindings and the fullscreen vertex stage are GENERATED from the
// header above (ADR-0016). Declare a param, then use it as u.<name>; each declared input is
// bound as a texture of that name.
fn blend_overlay(a: vec3f, b: vec3f) -> vec3f {
    return select(2.0 * a * b, 1.0 - 2.0 * (1.0 - a) * (1.0 - b), a > vec3f(0.5));
}
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let a = textureSample(A, samp, inp.uv);
    let b = textureSample(B, samp, inp.uv);
    let o = clamp(u.opacity, 0.0, 1.0);
    var c: vec3f;
    switch (u.mode) {                                    // a real enum: the choice IS the value
        case 1:  { c = a.rgb + b.rgb * o; }                                       // add
        case 2:  { c = mix(a.rgb, a.rgb * b.rgb, o); }                            // multiply
        case 3:  { c = mix(a.rgb, 1.0 - (1.0 - a.rgb) * (1.0 - b.rgb), o); }      // screen
        case 4:  { c = mix(a.rgb, blend_overlay(a.rgb, b.rgb), o); }              // overlay
        default: { c = mix(a.rgb, b.rgb, o); }                                    // normal
    }
    return vec4f(c, 1.0);
}
