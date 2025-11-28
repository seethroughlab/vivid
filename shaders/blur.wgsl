// Separable Gaussian Blur shader
// Uses uniforms:
//   u.param0 = radius
//   u.vec0 = direction (1,0 for horizontal, 0,1 for vertical)

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let radius = u.param0;
    let direction = u.vec0;
    let pixelSize = 1.0 / u.resolution;
    let dir = direction * pixelSize;

    var color = vec4f(0.0);
    var totalWeight = 0.0;

    let samples = i32(radius * 2.0) + 1;
    let halfSamples = f32(samples) / 2.0;

    for (var i = 0; i < samples; i++) {
        let offset = (f32(i) - halfSamples) * dir * radius / halfSamples;

        // Gaussian weight
        let dist = length(offset * u.resolution);
        let sigma = radius / 3.0;  // Standard deviation
        let weight = exp(-dist * dist / (2.0 * sigma * sigma));

        color += textureSample(inputTexture, inputSampler, in.uv + offset) * weight;
        totalWeight += weight;
    }

    return color / totalWeight;
}
