// Mirror / Kaleidoscope Effect
// Modes:
//   0 = Horizontal mirror (left reflects to right)
//   1 = Vertical mirror (top reflects to bottom)
//   2 = Quad mirror (corners reflect)
//   3 = Kaleidoscope (radial segments)
//
// Uniforms:
//   u.mode = mirror mode (0-3)
//   u.param0 = segments (for kaleidoscope, default 6)
//   u.param1 = rotation angle (radians)
//   u.param2 = center X (0-1, default 0.5)
//   u.param3 = center Y (0-1, default 0.5)

const PI: f32 = 3.14159265359;
const TAU: f32 = 6.28318530718;

fn horizontalMirror(uv: vec2f) -> vec2f {
    var result = uv;
    if (uv.x > 0.5) {
        result.x = 1.0 - uv.x;
    }
    return result;
}

fn verticalMirror(uv: vec2f) -> vec2f {
    var result = uv;
    if (uv.y > 0.5) {
        result.y = 1.0 - uv.y;
    }
    return result;
}

fn quadMirror(uv: vec2f) -> vec2f {
    var result = uv;
    if (uv.x > 0.5) {
        result.x = 1.0 - uv.x;
    }
    if (uv.y > 0.5) {
        result.y = 1.0 - uv.y;
    }
    return result;
}

fn kaleidoscope(uv: vec2f, segments: f32, rotation: f32, center: vec2f) -> vec2f {
    // Translate to center
    var p = uv - center;

    // Correct aspect ratio
    let aspect = u.resolution.x / u.resolution.y;
    p.x *= aspect;

    // Convert to polar
    var angle = atan2(p.y, p.x) + rotation;
    let radius = length(p);

    // Create segments by folding the angle
    let segmentAngle = TAU / segments;
    angle = abs(((angle % segmentAngle) + segmentAngle) % segmentAngle);

    // Mirror within segment
    if (angle > segmentAngle * 0.5) {
        angle = segmentAngle - angle;
    }

    // Convert back to cartesian
    p = vec2f(cos(angle), sin(angle)) * radius;

    // Undo aspect correction
    p.x /= aspect;

    // Translate back
    return p + center;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let mode = u.mode;
    var segments = u.param0;
    let rotation = u.param1;
    var centerX = u.param2;
    var centerY = u.param3;

    // Defaults
    if (segments <= 0.0) {
        segments = 6.0;
    }
    if (centerX <= 0.0 && centerY <= 0.0) {
        centerX = 0.5;
        centerY = 0.5;
    }

    var sampleUV: vec2f;

    switch (mode) {
        case 0: {
            // Horizontal mirror
            sampleUV = horizontalMirror(in.uv);
        }
        case 1: {
            // Vertical mirror
            sampleUV = verticalMirror(in.uv);
        }
        case 2: {
            // Quad mirror
            sampleUV = quadMirror(in.uv);
        }
        case 3, default: {
            // Kaleidoscope
            sampleUV = kaleidoscope(in.uv, segments, rotation, vec2f(centerX, centerY));
        }
    }

    // Clamp to valid range
    sampleUV = clamp(sampleUV, vec2f(0.0), vec2f(1.0));

    return textureSample(inputTexture, inputSampler, sampleUV);
}
