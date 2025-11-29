// Scanlines shader
// Adds CRT-style horizontal lines for retro monitor effect
// Uses uniforms:
//   u.param0 = density (lines per screen height, 100-1000)
//   u.param1 = intensity (darkness of lines, 0-1)
//   u.param2 = scroll speed (optional animation)
//   u.mode = 0: simple dark lines, 1: alternating bright/dark, 2: RGB sub-pixel

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let density = u.param0;
    let intensity = u.param1;
    let scroll = u.param2 * u.time;
    let mode = u.mode;

    var color = textureSample(inputTexture, inputSampler, in.uv).rgb;

    // Calculate scanline position with optional scrolling
    let lineY = in.uv.y * density + scroll;
    let linePos = fract(lineY);

    if (mode == 0) {
        // Simple dark lines
        let scanline = smoothstep(0.3, 0.5, linePos) * smoothstep(0.7, 0.5, linePos);
        color = color * (1.0 - intensity * scanline);
    } else if (mode == 1) {
        // Alternating bright/dark (CRT phosphor style)
        let brightLine = smoothstep(0.0, 0.2, linePos) * smoothstep(0.4, 0.2, linePos);
        let darkLine = smoothstep(0.5, 0.7, linePos) * smoothstep(1.0, 0.7, linePos);
        let scanline = brightLine * 0.3 - darkLine * intensity;
        color = color * (1.0 + scanline);
    } else {
        // RGB sub-pixel simulation (vertical stripes + horizontal scanlines)
        let subPixelX = fract(in.uv.x * u.resolution.x / 3.0);
        var subPixelMask = vec3f(1.0);

        // RGB stripe pattern
        if (subPixelX < 0.333) {
            subPixelMask = vec3f(1.2, 0.8, 0.8);
        } else if (subPixelX < 0.666) {
            subPixelMask = vec3f(0.8, 1.2, 0.8);
        } else {
            subPixelMask = vec3f(0.8, 0.8, 1.2);
        }

        // Horizontal scanline
        let scanline = smoothstep(0.3, 0.5, linePos) * smoothstep(0.7, 0.5, linePos);
        color = color * subPixelMask * (1.0 - intensity * scanline * 0.5);
    }

    return vec4f(clamp(color, vec3f(0.0), vec3f(1.0)), 1.0);
}
