#version 450
// CustomShader source for the song-sketch demo: a warm horizon-glow gradient with a
// pulsing sun. Follows the ShaderOp contract (v_uv/o_color + the standard uniform block).
// The 4 CustomShader params drive u_warp/u_hue/u_density/u_glow (wire them from audio).
layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 o_color;
layout(set = 0, binding = 0) uniform U {
    vec2 u_res; float u_time; float u_warp; float u_hue; float u_density; float u_glow; };
void main() {
    vec2 uv = v_uv;
    // sun position drifts with warp; sun radius pulses with density
    vec2 sun = vec2(0.5 + 0.2 * sin(u_time * 0.2 + u_warp * 3.0), 0.62);
    float d = distance(uv, sun);
    float glow = exp(-d * (5.0 - u_density * 3.0));
    float pulse = 0.85 + 0.15 * sin(u_time * 2.0);
    // sky gradient: warm at horizon, cool up top
    vec3 sky = mix(vec3(0.9, 0.45, 0.2), vec3(0.1, 0.12, 0.35), smoothstep(0.35, 1.0, uv.y));
    vec3 sunc = 0.5 + 0.5 * cos(vec3(0.0, 1.6, 3.4) + u_hue * 6.2831853);
    vec3 col = sky + sunc * glow * pulse * (0.6 + u_glow);
    o_color = vec4(col, 1.0);
}
