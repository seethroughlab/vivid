/*{
  "name": "Test Pattern",
  "inputs": [],
  "params": [
    {"name": "pattern",    "default": 0.0, "min": 0.0, "max": 5.0, "type": "int", "choices": ["Grid", "Crosshatch", "Color Bars", "Gradient", "Checkerboard", "Circle Grid"]},
    {"name": "density",    "default": 16.0, "min": 1.0, "max": 64.0},
    {"name": "line_width", "default": 2.0, "min": 0.5, "max": 10.0},
    {"name": "r",          "default": 1.0, "min": 0.0, "max": 1.0},
    {"name": "g",          "default": 1.0, "min": 0.0, "max": 1.0},
    {"name": "b",          "default": 1.0, "min": 0.0, "max": 1.0}
  ]
}*/

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let uv = input.uv;
    let color = vec3f(u.r, u.g, u.b);
    let px = u.line_width / u.resolution.y; // line width in UV space
    let pattern = i32(u.pattern);

    if (pattern == 0) {
        // Grid
        let gx = abs(fract(uv.x * u.density) - 0.5);
        let gy = abs(fract(uv.y * u.density) - 0.5);
        let line = 1.0 - smoothstep(0.0, px * u.density, min(gx, gy));
        // Border lines
        let bx = min(uv.x, 1.0 - uv.x);
        let by = min(uv.y, 1.0 - uv.y);
        let border = 1.0 - smoothstep(0.0, px, min(bx, by));
        let intensity = max(line, border);
        return vec4f(color * intensity, intensity);
    }

    if (pattern == 1) {
        // Crosshatch — diagonal lines both directions
        let scale = u.density;
        let d1 = abs(fract((uv.x + uv.y) * scale) - 0.5);
        let d2 = abs(fract((uv.x - uv.y) * scale) - 0.5);
        let line = 1.0 - smoothstep(0.0, px * scale, min(d1, d2));
        return vec4f(color * line, line);
    }

    if (pattern == 2) {
        // Color Bars (SMPTE-inspired 8 bars)
        let bar = i32(floor(uv.x * 8.0));
        var bar_color: vec3f;
        switch bar {
            case 0 { bar_color = vec3f(0.75, 0.75, 0.75); } // White
            case 1 { bar_color = vec3f(0.75, 0.75, 0.0);  } // Yellow
            case 2 { bar_color = vec3f(0.0,  0.75, 0.75); } // Cyan
            case 3 { bar_color = vec3f(0.0,  0.75, 0.0);  } // Green
            case 4 { bar_color = vec3f(0.75, 0.0,  0.75); } // Magenta
            case 5 { bar_color = vec3f(0.75, 0.0,  0.0);  } // Red
            case 6 { bar_color = vec3f(0.0,  0.0,  0.75); } // Blue
            default { bar_color = vec3f(0.0, 0.0, 0.0);   } // Black
        }
        return vec4f(bar_color, 1.0);
    }

    if (pattern == 3) {
        // Gradient — horizontal
        return vec4f(color * uv.x, 1.0);
    }

    if (pattern == 4) {
        // Checkerboard
        let cx = i32(floor(uv.x * u.density));
        let cy = i32(floor(uv.y * u.density));
        let check = f32((cx + cy) % 2);
        return vec4f(color * check, 1.0);
    }

    // pattern == 5: Circle Grid
    let cell = fract(uv * u.density);
    let dist = length(cell - vec2f(0.5));
    let circle = 1.0 - smoothstep(0.3 - px * u.density, 0.3, dist);
    return vec4f(color * circle, circle);
}
