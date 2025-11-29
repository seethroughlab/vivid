// Chromatic Aberration shader
// Separates RGB channels with offset for VHS/glitch aesthetic
// Uses uniforms:
//   u.param0 = amount (offset strength, 0.0-0.1)
//   u.param1 = angle (direction in radians)
//   u.mode = 0: directional, 1: radial from center, 2: barrel distortion

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let amount = u.param0;
    let angle = u.param1;
    let mode = u.mode;

    var rOffset: vec2f;
    var bOffset: vec2f;

    if (mode == 0) {
        // Directional: RGB split along angle
        let dir = vec2f(cos(angle), sin(angle)) * amount;
        rOffset = dir;
        bOffset = -dir;
    } else if (mode == 1) {
        // Radial: RGB split from center outward
        let toCenter = in.uv - vec2f(0.5);
        let dist = length(toCenter);
        let dir = normalize(toCenter) * amount * dist * 2.0;
        rOffset = dir;
        bOffset = -dir;
    } else {
        // Barrel distortion style: stronger at edges
        let toCenter = in.uv - vec2f(0.5);
        let dist = length(toCenter);
        let distSq = dist * dist;
        let dir = toCenter * amount * distSq * 4.0;
        rOffset = dir;
        bOffset = -dir;
    }

    // Sample each channel at different positions
    let r = textureSample(inputTexture, inputSampler, in.uv + rOffset).r;
    let g = textureSample(inputTexture, inputSampler, in.uv).g;
    let b = textureSample(inputTexture, inputSampler, in.uv + bOffset).b;
    let a = textureSample(inputTexture, inputSampler, in.uv).a;

    return vec4f(r, g, b, a);
}
