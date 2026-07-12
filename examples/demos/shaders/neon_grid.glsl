#version 450
// Neon demo — a retro synthwave perspective grid with a setting sun and scanlines.
// Bridge-driven:
//   u_warp    <- master.low       (bass rolls the grid horizon)
//   u_hue     <- master.mid       (neon colour shifts)
//   u_density <- master.high      (hi-hats sharpen the scanlines)
//   u_glow    <- master.transient (snare/kick flash the sun + grid)
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform U {
    vec2 u_res; float u_time; float u_warp; float u_hue; float u_density; float u_glow; };

void main() {
    vec2 uv = v_uv;
    float aspect = u_res.x / max(u_res.y, 1.0);
    vec3 neon = 0.5 + 0.5 * cos(vec3(0.0, 2.0, 4.0) + u_hue * 6.2831853 + 1.5);  // magenta/cyan family
    vec3 col;

    float horizon = 0.5;
    if (uv.y < horizon) {
        // --- lower half: the perspective grid rushing toward the horizon ---
        vec2 p = vec2((uv.x - 0.5) * aspect, uv.y);
        float depth = horizon - p.y;                     // 0 at horizon .. 0.5 at bottom
        float persp = 1.0 / (depth + 0.03);
        float roll = u_time * 1.2 + u_warp * 3.0;
        float rows = fract(persp * 0.35 - roll);
        float cols = fract(p.x * persp * 0.5);
        float line = smoothstep(0.06, 0.0, min(rows, 1.0 - rows))
                   + smoothstep(0.04, 0.0, min(cols, 1.0 - cols));
        float fade = smoothstep(0.0, 0.35, depth);
        col = neon * line * fade * (1.2 + u_glow);
        col += vec3(0.35, 0.05, 0.25) * fade;            // grid haze
    } else {
        // --- upper half: sky gradient + a banded neon sun ---
        float sky = (uv.y - horizon) / (1.0 - horizon);
        col = mix(vec3(0.35, 0.08, 0.32), vec3(0.04, 0.02, 0.12), sky);
        vec2 sc = vec2((uv.x - 0.5) * aspect, uv.y - 0.72);
        float sun = smoothstep(0.28, 0.24, length(sc));
        float bands = step(0.02, fract(sc.y * 22.0));     // horizontal sun bands
        vec3 sunc = mix(vec3(1.0, 0.75, 0.25), neon, 0.4);
        col += sunc * sun * bands * (1.0 + u_glow);
    }
    // scanlines, sharpened by the highs
    float scan = 0.85 + 0.15 * sin(uv.y * u_res.y * (0.6 + u_density * 1.4));
    o_color = vec4(col * scan, 1.0);
}
