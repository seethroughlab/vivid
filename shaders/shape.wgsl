// Shape Generator using Signed Distance Fields (SDF)
// Uses uniforms:
//   u.mode = shape type (0=circle, 1=rect, 2=triangle, 3=line, 4=ring, 5=star)
//   u.vec0 = center (or start point for line)
//   u.vec1 = size (or end point for line)
//   u.param0 = radius / corner radius / thickness
//   u.param1 = inner radius (ring) / points (star) / rotation
//   u.param2 = stroke width (0 = filled)
//   u.param3, param4, param5 = fill color RGB
//   u.param6 = softness (antialiasing, 0.001 - 0.1)
//   u.param7 = aspect ratio correction (1.0 = square pixels)

// SDF helper functions
fn sdCircle(p: vec2f, r: f32) -> f32 {
    return length(p) - r;
}

fn sdBox(p: vec2f, b: vec2f) -> f32 {
    let d = abs(p) - b;
    return length(max(d, vec2f(0.0))) + min(max(d.x, d.y), 0.0);
}

fn sdRoundedBox(p: vec2f, b: vec2f, r: f32) -> f32 {
    let q = abs(p) - b + r;
    return length(max(q, vec2f(0.0))) + min(max(q.x, q.y), 0.0) - r;
}

fn sdTriangle(p: vec2f, r: f32) -> f32 {
    let k = sqrt(3.0);
    var q = vec2f(abs(p.x) - r, p.y + r / k);
    if (q.x + k * q.y > 0.0) {
        q = vec2f(q.x - k * q.y, -k * q.x - q.y) / 2.0;
    }
    q.x -= clamp(q.x, -2.0 * r, 0.0);
    return -length(q) * sign(q.y);
}

fn sdLine(p: vec2f, a: vec2f, b: vec2f) -> f32 {
    let pa = p - a;
    let ba = b - a;
    let h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
    return length(pa - ba * h);
}

fn sdRing(p: vec2f, outerR: f32, innerR: f32) -> f32 {
    return abs(length(p) - (outerR + innerR) * 0.5) - (outerR - innerR) * 0.5;
}

fn sdStar(p: vec2f, r: f32, n: i32, m: f32) -> f32 {
    // n = number of points, m = inner radius ratio
    let an = 3.141593 / f32(n);
    let en = 3.141593 / m;
    let acs = vec2f(cos(an), sin(an));
    let ecs = vec2f(cos(en), sin(en));

    var q = abs(p);
    let bn = (atan2(q.x, q.y) % (2.0 * an)) - an;
    q = length(q) * vec2f(cos(bn), abs(sin(bn)));
    q = q - r * acs;
    q = q + ecs * clamp(-dot(q, ecs), 0.0, r * acs.y / ecs.y);
    return length(q) * sign(q.x);
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let shapeType = u.mode;
    let center = u.vec0;
    let size = u.vec1;
    let param0 = u.param0;  // radius / corner radius
    let param1 = u.param1;  // inner radius / points / rotation
    let strokeWidth = u.param2;
    let fillColor = vec3f(u.param3, u.param4, u.param5);
    let softness = max(u.param6, 0.002);
    let aspectRatio = u.param7;

    // Aspect-corrected UV (so circles are circular)
    var uv = in.uv - center;
    if (aspectRatio > 0.0) {
        uv.x *= aspectRatio;
    }

    // Apply rotation if specified
    if (param1 != 0.0 && shapeType != 4 && shapeType != 5) {
        let c = cos(param1);
        let s = sin(param1);
        uv = vec2f(uv.x * c - uv.y * s, uv.x * s + uv.y * c);
    }

    var d: f32 = 1.0;

    switch (shapeType) {
        case 0: {  // Circle
            d = sdCircle(uv, param0);
        }
        case 1: {  // Rectangle (with optional rounded corners)
            let halfSize = size * 0.5;
            if (param0 > 0.0) {
                d = sdRoundedBox(uv, halfSize, param0);
            } else {
                d = sdBox(uv, halfSize);
            }
        }
        case 2: {  // Triangle
            d = sdTriangle(uv, param0);
        }
        case 3: {  // Line segment
            let start = center - size * 0.5;
            let end = center + size * 0.5;
            d = sdLine(in.uv, start, end) - param0;  // param0 = thickness
        }
        case 4: {  // Ring
            d = sdRing(uv, param0, param1);
        }
        case 5: {  // Star
            let points = max(i32(param1), 3);
            d = sdStar(uv, param0, points, 2.0);  // m=2 for classic star
        }
        default: {
            d = sdCircle(uv, 0.1);
        }
    }

    // Apply stroke if specified
    if (strokeWidth > 0.0) {
        d = abs(d) - strokeWidth * 0.5;
    }

    // Smooth edge with antialiasing
    let alpha = 1.0 - smoothstep(-softness, softness, d);

    return vec4f(fillColor, alpha);
}
