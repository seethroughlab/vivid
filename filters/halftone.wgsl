/*{
  "name": "Halftone",
  "params": [
    {"name": "dot_size", "default": 8.0, "min": 1.0, "max": 50.0},
    {"name": "angle",    "default": 0.4, "min": 0.0, "max": 6.28318}
  ]
}*/
@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(inputTex, texSampler, input.uv);
    let luma = dot(color.rgb, vec3f(0.299, 0.587, 0.114));

    let size = u.dot_size;
    let a = u.angle;
    let ca = cos(a);
    let sa = sin(a);

    // Rotate UV into halftone grid space
    let px = input.uv * u.resolution;
    let rotated = vec2f(
        px.x * ca - px.y * sa,
        px.x * sa + px.y * ca
    );

    // Distance from center of nearest grid cell
    let cell = floor(rotated / size) * size + size * 0.5;
    let dist = length(rotated - cell);

    // Dot radius proportional to brightness
    let radius = size * 0.5 * sqrt(1.0 - luma);
    let dot = select(1.0, 0.0, dist < radius);

    return vec4f(vec3f(dot), color.a);
}
