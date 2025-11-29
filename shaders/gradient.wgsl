// Gradient Generator shader
// Uses uniforms:
//   u.mode = gradient type (0=linear, 1=radial, 2=angular, 3=diamond, 4=animated)
//   u.param0 = angle (for linear, in radians)
//   u.param1 = offset (0-1, shifts gradient)
//   u.param2 = scale (repeats)
//   u.vec0 = center (for radial/angular)
//   u.param3 = color1.r, u.param4 = color1.g, u.param5 = color1.b
//   u.param6 = color2.r, u.param7 = color2.g (color2.b uses 1.0 default)
//
// Mode 4 (animated): HSV-based animated gradient that shifts colors over time

// HSV to RGB conversion
fn hsvToRgb(hsv: vec3f) -> vec3f {
    let h = hsv.x * 6.0;
    let s = hsv.y;
    let v = hsv.z;

    let i = floor(h);
    let f = h - i;
    let p = v * (1.0 - s);
    let q = v * (1.0 - s * f);
    let t = v * (1.0 - s * (1.0 - f));

    let ii = i32(i) % 6;

    switch (ii) {
        case 0: { return vec3f(v, t, p); }
        case 1: { return vec3f(q, v, p); }
        case 2: { return vec3f(p, v, t); }
        case 3: { return vec3f(p, q, v); }
        case 4: { return vec3f(t, p, v); }
        default: { return vec3f(v, p, q); }
    }
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let mode = u.mode;
    let angle = u.param0;
    let offset = u.param1;
    let scale = u.param2;
    let center = u.vec0;

    // Colors (pack into available params)
    let color1 = vec3f(u.param3, u.param4, u.param5);
    let color2 = vec3f(u.param6, u.param7, 1.0);

    var t: f32 = 0.0;
    let uv = in.uv;

    if (mode == 4) {
        // Animated HSV gradient mode
        // Creates a moving radial gradient with HSV color cycling
        let animCenter = vec2f(
            0.5 + sin(u.time * 0.3) * 0.2,
            0.5 + cos(u.time * 0.4) * 0.2
        );
        let dist = length(uv - animCenter);

        // Two animated colors using HSV
        let hue1 = fract(u.time * 0.1);
        let hue2 = fract(u.time * 0.1 + 0.5);

        let animColor1 = hsvToRgb(vec3f(hue1, 0.7, 0.8));
        let animColor2 = hsvToRgb(vec3f(hue2, 0.6, 0.4));

        let animT = smoothstep(0.0, 0.8, dist);
        let finalColor = mix(animColor1, animColor2, animT);

        return vec4f(finalColor, 1.0);
    }

    switch (mode) {
        case 0: {  // Linear
            let c = cos(angle);
            let s = sin(angle);
            let rotUv = vec2f(uv.x * c - uv.y * s, uv.x * s + uv.y * c);
            t = rotUv.x;
        }
        case 1: {  // Radial
            t = length(uv - center);
        }
        case 2: {  // Angular/Conical
            let d = uv - center;
            t = (atan2(d.y, d.x) / 3.14159265 + 1.0) * 0.5;
        }
        case 3: {  // Diamond
            let d = abs(uv - center);
            t = d.x + d.y;
        }
        default: {
            t = uv.x;
        }
    }

    // Apply offset and scale
    t = fract((t + offset) * scale);

    // Interpolate between colors
    let color = mix(color1, color2, t);

    return vec4f(color, 1.0);
}
