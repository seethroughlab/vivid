/*{
  "version": 1,
  "name": "Shatter",
  "summary": "RGB channel-split + blocky glitch displacement — a hard digital shatter, not a haze.",
  "role": "transform",
  "keywords": ["effect", "glitch", "rgb", "shatter", "datamosh"],
  "inputs": ["input"],
  "params": [
    {"name": "split",  "type": "float", "default": 0.2, "min": 0, "max": 1,
     "description": "chromatic RGB channel separation"},
    {"name": "blocks", "type": "float", "default": 0.3, "min": 0, "max": 1,
     "description": "how many blocks shatter / shove aside"},
    {"name": "jitter", "type": "float", "default": 0.5, "min": 0, "max": 1,
     "description": "per-block offset amount"},
    {"name": "size",   "type": "float", "default": 0.4, "min": 0, "max": 1,
     "description": "block size (many small .. few large)"}
  ]
}*/
// Header-generated: u (res,time,split,blocks,jitter,size), input texture + samp, fullscreen vs.
fn hash21(p: vec2f) -> f32 {
    return fract(sin(dot(p, vec2f(127.1, 311.7))) * 43758.5453);
}
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    let ar = u.res.x / max(u.res.y, 1.0);
    var uv = inp.uv;
    // Blocky shatter: quantize to a grid; a fraction of blocks get a hard horizontal shove,
    // re-rolled a few times a second so it reads as digital tearing rather than smooth motion.
    let cells = mix(44.0, 7.0, u.size);
    let cell  = floor(vec2f(uv.x * cells * ar, uv.y * cells));
    let rnd   = hash21(cell + floor(u.time * 9.0));
    let hit   = step(1.0 - u.blocks * 0.9, rnd);
    let shove = (hash21(cell.yx + 3.7) - 0.5) * u.jitter * 0.28 * hit;
    uv.x = clamp(uv.x + shove, 0.0, 1.0);
    // Chromatic split: R/G/B pulled apart horizontally.
    let s  = u.split * 0.045;
    let cr = textureSample(input, samp, clamp(uv + vec2f(s, 0.0), vec2f(0.0), vec2f(1.0))).r;
    let cg = textureSample(input, samp, uv).g;
    let cb = textureSample(input, samp, clamp(uv - vec2f(s, 0.0), vec2f(0.0), vec2f(1.0))).b;
    return vec4f(cr, cg, cb, 1.0);
}
