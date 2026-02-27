@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let col = textureSample(inputTex, texSampler, input.uv);

    var axis: f32;
    if (u.vertical > 0.5) {
        axis = input.uv.x * u.resolution.x;
    } else {
        axis = input.uv.y * u.resolution.y;
    }

    let line_pos = fract(axis / u.spacing);
    var darkening = 1.0;
    if (line_pos < u.thickness) {
        darkening = mix(1.0, 0.0, u.intensity);
    }

    return vec4f(col.rgb * darkening, col.a);
}
