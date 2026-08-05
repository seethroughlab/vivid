/*{
  "version": 1,
  "name": "CRT",
  "summary": "Broadcast/CRT treatment: barrel curve, scanlines, RGB shadow-mask, aberration, vignette, roll.",
  "role": "transform",
  "keywords": ["effect", "crt", "scanline", "broadcast", "signal", "vhs"],
  "inputs": ["input"],
  "params": [
    {"name": "scan",       "type": "float", "default": 0.5, "min": 0, "max": 1, "description": "scanline depth"},
    {"name": "mask",       "type": "float", "default": 0.4, "min": 0, "max": 1, "description": "RGB shadow-mask strength"},
    {"name": "aberration", "type": "float", "default": 0.3, "min": 0, "max": 1, "description": "chromatic edge fringing"},
    {"name": "vignette",   "type": "float", "default": 0.5, "min": 0, "max": 1, "description": "corner darkening"},
    {"name": "roll",       "type": "float", "default": 0.0, "min": 0, "max": 1, "description": "rolling hum bar"},
    {"name": "curve",      "type": "float", "default": 0.2, "min": 0, "max": 1, "description": "screen barrel curvature"}
  ]
}*/
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    // Barrel-curve the UV so it reads as a tube; black outside the glass.
    let cc = inp.uv - 0.5;
    let c2 = dot(cc, cc);
    let uv = inp.uv + cc * c2 * u.curve * 0.5;
    if (uv.x < 0.0 || uv.x > 1.0 || uv.y < 0.0 || uv.y > 1.0) { return vec4f(0.0, 0.0, 0.0, 1.0); }
    // Radial chromatic aberration.
    let ab  = u.aberration * 0.012;
    let dir = normalize(cc + vec2f(1.0e-4, 1.0e-4));
    let cr = textureSample(input, samp, clamp(uv + dir * ab, vec2f(0.0), vec2f(1.0))).r;
    let cg = textureSample(input, samp, uv).g;
    let cb = textureSample(input, samp, clamp(uv - dir * ab, vec2f(0.0), vec2f(1.0))).b;
    var col = vec3f(cr, cg, cb);
    // Scanlines locked to output pixels.
    let sl = 0.5 + 0.5 * sin(uv.y * u.res.y * 3.14159265);
    col = col * (1.0 - u.scan * 0.6 * (1.0 - sl));
    // RGB shadow-mask: per-column triad tint.
    let mcol = i32(floor(uv.x * u.res.x)) % 3;
    var m = vec3f(0.7, 0.7, 1.0);
    if (mcol == 0) { m = vec3f(1.0, 0.7, 0.7); } else if (mcol == 1) { m = vec3f(0.7, 1.0, 0.7); }
    col = col * mix(vec3f(1.0), m, u.mask);
    // Rolling hum bar.
    if (u.roll > 0.001) {
        let bar = fract(uv.y + u.time * (0.15 + u.roll));
        let band = smoothstep(0.0, 0.08, bar) * (1.0 - smoothstep(0.08, 0.18, bar));
        col = col * (1.0 - u.roll * 0.35 * band);
    }
    // Vignette.
    col = col * (1.0 - u.vignette * c2 * 2.2);
    return vec4f(max(col, vec3f(0.0)), 1.0);
}
