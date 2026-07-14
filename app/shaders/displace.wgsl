/*{
  "version": 1,
  "name": "Displace",
  "summary": "Warp a source texture's sampling by a displacement texture (RG-vector or luminance).",
  "keywords": ["effect", "displace", "warp"],
  "inputs": ["source", "displace"],
  "params": [
    {"name": "amount", "type": "float", "default": 0.3, "min": 0, "max": 1,
     "semantic_intent": "displacement strength"},
    {"name": "mode",   "choices": ["RG Vector", "Luminance"], "default": 0,
     "description": "How the displacement texture drives the offset"}
  ]
}*/
// The uniform struct, its bindings and the fullscreen vertex stage are GENERATED from the
// header above (ADR-0016). Declare a param, then use it as u.<name>; each declared input is
// bound as a texture of that name.
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let amount = u.amount * 0.5;                      // normalized param -> 0 .. 0.5 UV offset
    let d = textureSample(displace, samp, inp.uv);
    var off: vec2f;
    if (u.mode == 0) {
        off = (d.rg - 0.5) * 2.0 * amount;           // RG vector: R->x, G->y
    } else {
        let luma = dot(d.rgb, vec3f(0.299, 0.587, 0.114));
        off = vec2f((luma - 0.5) * 2.0 * amount);    // luminance: same offset on both axes
    }
    return textureSample(source, samp, inp.uv + off);
}
