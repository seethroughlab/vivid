/*{
  "name": "Edge",
  "params": [
    {"name": "strength",  "default": 1.0, "min": 0.0, "max": 10.0},
    {"name": "threshold", "default": 0.0, "min": 0.0, "max": 1.0},
    {"name": "invert",    "type": "bool", "default": false}
  ]
}*/
fn luminance(c: vec4f) -> f32 {
    return dot(c.rgb, vec3f(0.299, 0.587, 0.114));
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let texel = 1.0 / u.resolution;
    let uv = input.uv;

    // Sample 3x3 neighborhood luminance
    let tl = luminance(textureSample(inputTex, texSampler, uv + vec2f(-texel.x,  texel.y)));
    let tc = luminance(textureSample(inputTex, texSampler, uv + vec2f(     0.0,  texel.y)));
    let tr = luminance(textureSample(inputTex, texSampler, uv + vec2f( texel.x,  texel.y)));
    let ml = luminance(textureSample(inputTex, texSampler, uv + vec2f(-texel.x,      0.0)));
    let mr = luminance(textureSample(inputTex, texSampler, uv + vec2f( texel.x,      0.0)));
    let bl = luminance(textureSample(inputTex, texSampler, uv + vec2f(-texel.x, -texel.y)));
    let bc = luminance(textureSample(inputTex, texSampler, uv + vec2f(     0.0, -texel.y)));
    let br = luminance(textureSample(inputTex, texSampler, uv + vec2f( texel.x, -texel.y)));

    // Sobel kernels
    let gx = -tl + tr - 2.0 * ml + 2.0 * mr - bl + br;
    let gy = -tl - 2.0 * tc - tr + bl + 2.0 * bc + br;

    var edge = sqrt(gx * gx + gy * gy) * u.strength;
    edge = step(u.threshold, edge) * edge;

    if (u.invert > 0.5) {
        edge = 1.0 - edge;
    }

    return vec4f(edge, edge, edge, 1.0);
}
