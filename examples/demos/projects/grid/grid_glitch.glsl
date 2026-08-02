#version 450
// Grid demo — a glitchy digital field: blocky quantized cells, RGB channel-shift, and
// horizontal datamosh tears that burst on transients. Bridge-driven:
//   u_warp    <- master.low       (bass shoves the blocks)
//   u_hue     <- master.mid       (palette cycles)
//   u_density <- master.high      (hats shrink the cells / sharpen)
//   u_glow    <- master.transient (glitch bursts: RGB tear + brightness)
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform U {
    vec2 u_res; float u_time; float u_warp; float u_hue; float u_density; float u_glow; };

float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }

void main() {
    vec2 uv = v_uv;
    // quantize into a block grid; the highs shrink the cells
    float cells = 8.0 + u_density * 40.0;
    vec2 cell = floor(uv * cells);
    float r = hash(cell + floor(u_time * 6.0));

    // horizontal datamosh tear: some rows jump sideways, harder on a transient
    float row = floor(uv.y * cells);
    float tear = (hash(vec2(row, floor(u_time * 8.0))) - 0.5) * (0.06 + u_glow * 0.5);
    vec2 suv = uv + vec2(tear + u_warp * 0.1 * (r - 0.5), 0.0);

    // RGB channel shift (chromatic split), widened by the transient
    float sh = 0.004 + u_glow * 0.03;
    float cr = hash(floor((suv + vec2(sh, 0.0)) * cells) + floor(u_time * 6.0));
    float cg = hash(floor(suv * cells)               + floor(u_time * 6.0));
    float cb = hash(floor((suv - vec2(sh, 0.0)) * cells) + floor(u_time * 6.0));

    // base palette from the mids, modulated by the per-cell noise
    vec3 pal = 0.5 + 0.5 * cos(vec3(0.0, 2.1, 4.2) + u_hue * 6.2831853 + vec3(cr, cg, cb) * 2.0);
    vec3 col = pal * vec3(cr, cg, cb);

    // scanlines + a bright glitch flash on the transient
    col *= 0.75 + 0.25 * step(0.5, fract(uv.y * u_res.y * 0.5));
    col += vec3(cr, cg, cb) * u_glow * 0.6;
    o_color = vec4(col, 1.0);
}
