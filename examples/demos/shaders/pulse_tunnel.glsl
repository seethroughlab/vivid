#version 450
// Pulse demo — an "acid tunnel": concentric rings rushing outward with a rotating
// hue and a hard glow on the beat. Follows the CustomShader contract (v_uv/o_color +
// the standard uniform block); the 4 params are bridge-driven from the audio:
//   u_warp    <- master.low       (bass pumps / bends the tunnel)
//   u_hue     <- master.mid       (colour rotates with the mids)
//   u_density <- master.level     (ring count swells with loudness)
//   u_glow    <- master.transient (kick punches the brightness)
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform U {
    vec2 u_res; float u_time; float u_warp; float u_hue; float u_density; float u_glow; };

void main() {
    vec2 p = v_uv * 2.0 - 1.0;          // -1..1, centered
    p.x *= u_res.x / max(u_res.y, 1.0); // aspect-correct
    float r = length(p);
    float a = atan(p.y, p.x);

    // domain warp: the bass bends the radial field
    r += u_warp * 0.35 * sin(a * 6.0 + u_time * 1.5);

    // tunnel: rings rushing toward the viewer, count swells with density
    float rings = 6.0 + u_density * 22.0;
    float z = 1.0 / (r + 0.15);                       // perspective depth
    float bands = sin((z * rings) - u_time * 4.0);
    float ring = smoothstep(0.0, 0.9, bands) * smoothstep(1.6, 0.2, r);

    // spokes rotating around the tunnel
    float spokes = 0.5 + 0.5 * sin(a * 8.0 + u_time * 0.8 + z * 2.0);

    // acid palette, hue rotates with the mids
    vec3 col = 0.5 + 0.5 * cos(vec3(0.0, 2.1, 4.2) + u_hue * 6.2831853 + z * 1.5 + a);
    float intensity = ring * (0.35 + 0.65 * spokes);

    // transient punch: a bright core flash on the kick
    float core = exp(-r * 3.0) * u_glow;
    vec3 outc = col * intensity * (0.7 + u_glow * 1.3) + vec3(0.9, 0.95, 1.0) * core * 0.6;
    o_color = vec4(outc, 1.0);
}
