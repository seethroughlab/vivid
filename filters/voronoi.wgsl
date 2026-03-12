/*{
  "name": "Voronoi",
  "inputs": [{"name": "source"}],
  "params": [
    {"name": "scale",      "default": 8.0,  "min": 1.0, "max": 50.0},
    {"name": "edge_width", "default": 0.02, "min": 0.0, "max": 0.1},
    {"name": "speed",      "default": 0.5,  "min": 0.0, "max": 5.0},
    {"name": "jitter",     "default": 1.0,  "min": 0.0, "max": 1.0},
    {"name": "mode", "type": "int", "default": 0, "min": 0, "max": 3,
     "choices": ["Cells", "Edges", "Distance", "Mosaic"]},
    {"name": "gap",     "default": 0.0, "min": 0.0, "max": 0.2},
    {"name": "color_r", "default": 0.0, "min": 0.0, "max": 1.0},
    {"name": "color_g", "default": 0.0, "min": 0.0, "max": 1.0},
    {"name": "color_b", "default": 0.0, "min": 0.0, "max": 1.0}
  ]
}*/

// Hash functions for cell center generation
fn hash2(p: vec2f) -> vec2f {
    let q = vec2f(dot(p, vec2f(127.1, 311.7)),
                  dot(p, vec2f(269.5, 183.3)));
    return fract(sin(q) * 43758.5453);
}

// Hash for per-cell color
fn hash3(p: vec2f) -> vec3f {
    let q = vec3f(dot(p, vec2f(127.1, 311.7)),
                  dot(p, vec2f(269.5, 183.3)),
                  dot(p, vec2f(419.2, 371.9)));
    return fract(sin(q) * 43758.5453);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let scale = u.scale;
    let mode = i32(u.mode);
    let jitter = u.jitter;
    let t = u.time * u.speed;

    let st = uv * scale;
    let i_st = floor(st);

    var min_dist1 = 100.0;   // nearest
    var min_dist2 = 100.0;   // 2nd nearest
    var nearest_cell = vec2f(0.0);
    var nearest_point = vec2f(0.0);

    // Search 3x3 neighborhood
    for (var y = -1; y <= 1; y++) {
        for (var x = -1; x <= 1; x++) {
            let neighbor = vec2f(f32(x), f32(y));
            let cell = i_st + neighbor;

            // Animated cell center with jitter control
            var point = hash2(cell);
            point = 0.5 + jitter * 0.5 * sin(t + 6.2831 * point);

            let diff = neighbor + point - fract(st);
            let dist = length(diff);

            if (dist < min_dist1) {
                min_dist2 = min_dist1;
                min_dist1 = dist;
                nearest_cell = cell;
                nearest_point = (cell + point) / scale;
            } else if (dist < min_dist2) {
                min_dist2 = dist;
            }
        }
    }

    if (mode == 0) {
        // Gap: black border between cells (Paper.js 95% cell scaling)
        if (u.gap > 0.0) {
            let edge_proximity = (min_dist2 - min_dist1) / scale;
            if (edge_proximity < u.gap) {
                return vec4f(0.0, 0.0, 0.0, 1.0);
            }
        }

        // Solid color override when color_r/g/b are set
        let solid = vec3f(u.color_r, u.color_g, u.color_b);
        if (dot(solid, solid) > 0.0001) {
            return vec4f(solid, 1.0);
        }

        // Default: HSV rainbow color per cell
        let h = hash3(nearest_cell);
        let hue = h.x;
        let sat = 0.6 + 0.4 * h.y;
        let val = 0.7 + 0.3 * h.z;
        let c = val * sat;
        let hp = hue * 6.0;
        let x2 = c * (1.0 - abs(hp % 2.0 - 1.0));
        var rgb = vec3f(0.0);
        if (hp < 1.0) { rgb = vec3f(c, x2, 0.0); }
        else if (hp < 2.0) { rgb = vec3f(x2, c, 0.0); }
        else if (hp < 3.0) { rgb = vec3f(0.0, c, x2); }
        else if (hp < 4.0) { rgb = vec3f(0.0, x2, c); }
        else if (hp < 5.0) { rgb = vec3f(x2, 0.0, c); }
        else { rgb = vec3f(c, 0.0, x2); }
        rgb += val - c;
        return vec4f(rgb, 1.0);
    } else if (mode == 1) {
        // Edges — white where distance to nearest ≈ distance to 2nd nearest
        let edge = 1.0 - smoothstep(0.0, u.edge_width * scale, min_dist2 - min_dist1);
        return vec4f(vec3f(edge), 1.0);
    } else if (mode == 2) {
        // Distance — grayscale distance field
        let d = min_dist1;
        return vec4f(vec3f(d), 1.0);
    } else {
        // Mosaic — sample inputTex at nearest cell center UV
        let color = textureSample(inputTex, texSampler, nearest_point);
        return vec4f(color.rgb, 1.0);
    }
}
