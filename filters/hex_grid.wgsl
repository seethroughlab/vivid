/*{
  "name": "HexGrid",
  "params": [
    {"name": "cell_size",   "default": 24.0, "min": 4.0,  "max": 128.0},
    {"name": "top_bright",  "default": 1.5,  "min": 0.5, "max": 3.0},
    {"name": "side_bright", "default": 0.5,  "min": 0.0, "max": 1.5},
    {"name": "gap",         "default": 0.0,  "min": 0.0, "max": 4.0}
  ]
}*/

// Convert pixel coord to nearest hex center (axial coordinates)
// Returns: xy = hex center in pixel space, z = distance to edge
fn hex_nearest(px: vec2f, size: f32) -> vec3f {
    // Hex grid spacing
    let sx = size * 1.7320508;  // sqrt(3) * size
    let sy = size * 1.5;

    // Two candidate hex rows (offset every other row)
    let row = floor(px.y / sy);
    let is_odd = (i32(row) % 2) != 0;
    let offset_x = select(0.0, sx * 0.5, is_odd);

    let col = floor((px.x - offset_x) / sx);
    let cx = col * sx + offset_x + sx * 0.5;
    let cy = row * sy + sy * 0.5;

    // Check this cell and neighbors to find true nearest
    var best_center = vec2f(cx, cy);
    var best_dist = length(px - best_center);

    // Check 6 neighbors + adjacent row candidates
    let offsets = array<vec2f, 5>(
        vec2f(sx, 0.0), vec2f(-sx, 0.0),
        vec2f(sx * 0.5, sy), vec2f(-sx * 0.5, sy),
        vec2f(sx * 0.5, -sy)
    );
    for (var i = 0; i < 5; i++) {
        let candidate = vec2f(cx, cy) + offsets[i];
        let d = length(px - candidate);
        if (d < best_dist) {
            best_dist = d;
            best_center = candidate;
        }
    }
    // Also check the negative offset neighbors
    let neg_offsets = array<vec2f, 2>(
        vec2f(-sx * 0.5, -sy),
        vec2f(0.0, 0.0)
    );
    let c2x = (col + 1.0) * sx + offset_x + sx * 0.5;
    let candidate2 = vec2f(c2x - sx, cy - sy);
    let d2 = length(px - candidate2);
    if (d2 < best_dist) {
        best_dist = d2;
        best_center = candidate2;
    }

    // Approximate edge distance for hex: distance from center vs inscribed radius
    let inscribed = size * 0.8660254;  // sqrt(3)/2 * size
    let edge_dist = inscribed - best_dist;

    return vec3f(best_center, edge_dist);
}

// Determine which isometric face of the hex cube this point is on
// Returns: 0=top, 1=bottom-left, 2=bottom-right
fn hex_face(px: vec2f, center: vec2f) -> i32 {
    let d = px - center;
    // Angle from center
    let angle = atan2(d.y, d.x);

    // Top sector: -30deg to -150deg (negative y = up in screen space)
    // Adjusted for screen coords where y increases downward
    if (angle < -PI / 6.0 && angle > -5.0 * PI / 6.0) {
        return 0;  // top face
    }
    if (d.x < 0.0) {
        return 1;  // bottom-left
    }
    return 2;  // bottom-right
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let res = u.resolution;
    let size = u.cell_size;

    let px = uv * res;
    let hex = hex_nearest(px, size);
    let center = hex.xy;
    let edge_dist = hex.z;

    // Gap: black border at hex edges
    if (u.gap > 0.0 && edge_dist < u.gap) {
        return vec4f(0.0, 0.0, 0.0, 1.0);
    }

    // Sample source at hex center (average 3x3 within hex)
    let center_uv = center / res;
    let step = size * 0.25 / res;
    var avg = vec3f(0.0);
    for (var dy = -1; dy <= 1; dy++) {
        for (var dx = -1; dx <= 1; dx++) {
            let s_uv = center_uv + vec2f(f32(dx), f32(dy)) * step;
            let sample_color = textureSample(inputTex, texSampler, s_uv);
            avg += sample_color.rgb;
        }
    }
    avg /= 9.0;

    // Isometric 3D shading (Q*bert style)
    let face = hex_face(px, center);
    var brightness = 1.0;
    if (face == 0) {
        brightness = u.top_bright;
    } else if (face == 1) {
        brightness = u.side_bright;
    }
    // face == 2 uses brightness = 1.0

    return vec4f(avg * brightness, 1.0);
}
