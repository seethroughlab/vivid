/*{
  "name": "RasterGrid",
  "params": [
    {"name": "cell_size", "default": 32.0, "min": 2.0, "max": 256.0},
    {"name": "gap",       "default": 2.0,  "min": 0.0, "max": 8.0}
  ]
}*/

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let res = u.resolution;
    let cell = u.cell_size;
    let gap = u.gap;

    // Pixel coordinate
    let px = uv * res;

    // Cell coordinate (which cell this pixel belongs to)
    let cell_idx = floor(px / cell);
    let cell_origin = cell_idx * cell;

    // Distance from pixel to nearest cell boundary
    let within_cell = px - cell_origin;
    let dist_to_edge = min(
        min(within_cell.x, within_cell.y),
        min(cell - within_cell.x, cell - within_cell.y)
    );

    // Gap: black border between cells
    if (dist_to_edge < gap) {
        return vec4f(0.0, 0.0, 0.0, 1.0);
    }

    // 3x3 grid sample within cell for average color
    let cell_center_uv = (cell_origin + vec2f(cell * 0.5)) / res;
    let step = cell / (3.0 * res);

    var avg = vec3f(0.0);
    for (var dy = -1; dy <= 1; dy++) {
        for (var dx = -1; dx <= 1; dx++) {
            let offset = vec2f(f32(dx), f32(dy)) * step;
            let sample_color = textureSample(inputTex, texSampler, cell_center_uv + offset);
            avg += sample_color.rgb;
        }
    }
    avg /= 9.0;

    return vec4f(avg, 1.0);
}
