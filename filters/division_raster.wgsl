/*{
  "name": "DivisionRaster",
  "params": [
    {"name": "depth",     "default": 6.0,  "min": 1.0, "max": 16.0},
    {"name": "gap",       "default": 1.0,  "min": 0.0, "max": 8.0},
    {"name": "threshold", "default": 0.05, "min": 0.0, "max": 1.0}
  ]
}*/

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let res = u.resolution;
    let depth = i32(u.depth);
    let gap = u.gap;
    let threshold = u.threshold;

    // Pixel coordinate
    let px = uv * res;

    // Walk the binary subdivision tree
    var cell_min = vec2f(0.0);
    var cell_max = res;

    for (var i = 0; i < depth; i++) {
        let size = cell_max - cell_min;

        // Content-aware early termination: sample 5 points in the cell
        // (center + 4 quarter-points) and check luminance variance.
        if (threshold > 0.0) {
            let center_uv = ((cell_min + cell_max) * 0.5) / res;
            let quarter = size * 0.25 / res;

            let l0 = dot(textureSample(inputTex, texSampler, center_uv).rgb, vec3f(0.299, 0.587, 0.114));
            let l1 = dot(textureSample(inputTex, texSampler, center_uv + vec2f(-quarter.x, -quarter.y)).rgb, vec3f(0.299, 0.587, 0.114));
            let l2 = dot(textureSample(inputTex, texSampler, center_uv + vec2f( quarter.x, -quarter.y)).rgb, vec3f(0.299, 0.587, 0.114));
            let l3 = dot(textureSample(inputTex, texSampler, center_uv + vec2f(-quarter.x,  quarter.y)).rgb, vec3f(0.299, 0.587, 0.114));
            let l4 = dot(textureSample(inputTex, texSampler, center_uv + vec2f( quarter.x,  quarter.y)).rgb, vec3f(0.299, 0.587, 0.114));

            let min_lum = min(l0, min(min(l1, l2), min(l3, l4)));
            let max_lum = max(l0, max(max(l1, l2), max(l3, l4)));

            if (max_lum - min_lum < threshold) {
                break;
            }
        }

        if (size.x > size.y) {
            // Split vertically
            let mid = (cell_min.x + cell_max.x) * 0.5;
            if (px.x < mid) {
                cell_max.x = mid;
            } else {
                cell_min.x = mid;
            }
        } else {
            // Split horizontally
            let mid = (cell_min.y + cell_max.y) * 0.5;
            if (px.y < mid) {
                cell_max.y = mid;
            } else {
                cell_min.y = mid;
            }
        }
    }

    // Distance from pixel to nearest cell edge
    let within_cell = px - cell_min;
    let cell_size = cell_max - cell_min;
    let dist_to_edge = min(
        min(within_cell.x, within_cell.y),
        min(cell_size.x - within_cell.x, cell_size.y - within_cell.y)
    );

    // Gap: black border between cells
    if (dist_to_edge < gap) {
        return vec4f(0.0, 0.0, 0.0, 1.0);
    }

    // 3x3 grid sample within cell for average color
    let cell_center_uv = ((cell_min + cell_max) * 0.5) / res;
    let step = cell_size / (3.0 * res);

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
