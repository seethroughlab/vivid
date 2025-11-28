// Gradient Generator shader
// Uses uniforms:
//   u.mode = gradient type (0=linear, 1=radial, 2=angular, 3=diamond)
//   u.param0 = angle (for linear, in radians)
//   u.param1 = offset (0-1, shifts gradient)
//   u.param2 = scale (repeats)
//   u.vec0 = center (for radial/angular)
//   u.param3 = color1.r, u.param4 = color1.g, u.param5 = color1.b
//   u.param6 = color2.r, u.param7 = color2.g (color2.b uses 1.0 default)

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
