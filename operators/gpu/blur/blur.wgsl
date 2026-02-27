@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let r = u.radius;
    let q = max(u.quality, 1.0);
    let pixel = vec2f(1.0) / u.resolution;

    var color = vec4f(0.0);
    var total = 0.0;
    let steps = i32(q);

    for (var x = -steps; x <= steps; x++) {
        for (var y = -steps; y <= steps; y++) {
            let offset = vec2f(f32(x), f32(y)) * pixel * (r / q);
            let w = 1.0 - length(vec2f(f32(x), f32(y))) / (q * 1.414);
            if (w > 0.0) {
                color += textureSample(inputTex, texSampler, input.uv + offset) * w;
                total += w;
            }
        }
    }

    return color / total;
}
