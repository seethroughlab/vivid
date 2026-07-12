#version 450
// Drift demo — a slow flowing aurora/nebula. Domain-warped fbm noise, cool cinematic
// palette, gentle blooms. Bridge-driven:
//   u_warp    <- master.low    (slow swells bend the flow)
//   u_hue     <- master.mid    (colour drifts with the mids)
//   u_density <- master.level  (loudness thickens the veils)
//   u_glow    <- master.level  (overall bloom)
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform U {
    vec2 u_res; float u_time; float u_warp; float u_hue; float u_density; float u_glow; };

float hash(vec2 p) { return fract(sin(dot(p, vec2(41.3, 289.1))) * 43758.5453); }
float noise(vec2 p) {
    vec2 i = floor(p), f = fract(p);
    float a = hash(i), b = hash(i + vec2(1, 0)), c = hash(i + vec2(0, 1)), d = hash(i + vec2(1, 1));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(mix(a, b, u.x), mix(c, d, u.x), u.y);
}
float fbm(vec2 p) {
    float v = 0.0, a = 0.5;
    for (int i = 0; i < 5; i++) { v += a * noise(p); p = p * 2.0 + 7.0; a *= 0.5; }
    return v;
}

void main() {
    vec2 uv = v_uv;
    uv.x *= u_res.x / max(u_res.y, 1.0);
    float t = u_time * 0.05;
    // two-stage domain warp for a slow, liquid flow
    vec2 q = vec2(fbm(uv * 2.0 + vec2(t, -t)), fbm(uv * 2.0 + vec2(5.2, 1.3) - t));
    vec2 r = uv + (0.4 + u_warp * 0.6) * q;
    float n = fbm(r * 3.0 + t);

    float veil = smoothstep(0.35, 0.75, n) * (0.6 + u_density * 0.6);
    // cool nebula palette drifting with the mids
    vec3 deep = vec3(0.03, 0.05, 0.12);
    vec3 tealc = 0.5 + 0.5 * cos(vec3(0.0, 1.4, 2.6) + u_hue * 6.2831853 + n * 2.0);
    vec3 col = mix(deep, tealc, veil);
    // soft bloom toward center, lifted by loudness
    float d = distance(v_uv, vec2(0.5));
    col += tealc * (0.15 + u_glow * 0.6) * exp(-d * 2.2) * (0.5 + 0.5 * n);
    o_color = vec4(col, 1.0);
}
