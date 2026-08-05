/*{
  "version": 1,
  "name": "Blur",
  "summary": "Box blur of the input texture; radius is wire-drivable.",
  "role": "transform",
  "keywords": ["effect", "blur", "soften"],
  "inputs": ["input"],
  "params": [
    {"name": "radius", "type": "float", "default": 0.3, "min": 0, "max": 1,
     "semantic_intent": "blur radius"}
  ]
}*/
// The uniform struct, its bindings and the fullscreen vertex stage are GENERATED from the
// header above (ADR-0016). Declare a param, then use it as u.<name>.
//
// This is a SINGLE-PASS 5-tap box blur — which is what the compiled operator did, so moving it
// changes nothing today. It is also a limitation, not a design: a proper separable gaussian is
// two passes, and a v1 shader file is one. "passes" is RESERVED in the header format (it
// parse-and-rejects with a message), so when multi-pass lands this file gains a field rather
// than the format gaining a version.
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let px = (vec2f(1.0) / u.res) * (1.0 + u.radius * 8.0);
    var s = textureSample(input, samp, inp.uv) * 0.36;
    s += textureSample(input, samp, inp.uv + vec2f( px.x, 0.0)) * 0.16;
    s += textureSample(input, samp, inp.uv + vec2f(-px.x, 0.0)) * 0.16;
    s += textureSample(input, samp, inp.uv + vec2f(0.0,  px.y)) * 0.16;
    s += textureSample(input, samp, inp.uv + vec2f(0.0, -px.y)) * 0.16;
    return s;
}
