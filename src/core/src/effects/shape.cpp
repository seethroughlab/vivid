// Vivid Effects 2D - Shape Operator Implementation
// SDF-based shape generator

#include <vivid/effects/shape.h>

namespace vivid::effects {

const char* Shape::fragmentShader() const {
    return R"(
struct Uniforms {
    shapeType: i32,
    sizeX: f32,
    sizeY: f32,
    posX: f32,
    posY: f32,
    rotation: f32,
    sides: i32,
    cornerRadius: f32,
    thickness: f32,
    softness: f32,
    colorR: f32,
    colorG: f32,
    colorB: f32,
    colorA: f32,
    aspect: f32,
    _pad: f32,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

const PI: f32 = 3.14159265359;
const TAU: f32 = 6.28318530718;

fn rotate2d(p: vec2f, a: f32) -> vec2f {
    let c = cos(a);
    let s = sin(a);
    return vec2f(p.x * c - p.y * s, p.x * s + p.y * c);
}

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

fn sdEquilateralTriangle(p: vec2f, r: f32) -> f32 {
    let k = sqrt(3.0);
    var q = vec2f(abs(p.x) - r, p.y + r / k);
    if (q.x + k * q.y > 0.0) {
        q = vec2f(q.x - k * q.y, -k * q.x - q.y) / 2.0;
    }
    q.x -= clamp(q.x, -2.0 * r, 0.0);
    return -length(q) * sign(q.y);
}

fn sdStar(p: vec2f, r: f32, n: i32, m: f32) -> f32 {
    let an = PI / f32(n);
    let en = PI / m;
    let acs = vec2f(cos(an), sin(an));
    let ecs = vec2f(cos(en), sin(en));

    var q = vec2f(abs(p.x), p.y);
    let bn = (atan2(q.x, q.y) % (2.0 * an)) - an;
    q = length(q) * vec2f(cos(bn), abs(sin(bn)));
    q = q - r * acs;
    q = q + ecs * clamp(-dot(q, ecs), 0.0, r * acs.y / ecs.y);
    return length(q) * sign(q.x);
}

fn sdPolygon(p: vec2f, r: f32, n: i32) -> f32 {
    let an = TAU / f32(n);
    let he = r * tan(an * 0.5);
    var q = vec2f(abs(p.x), p.y);
    let bn = (atan2(q.x, q.y) % an) - an * 0.5;
    q = length(q) * vec2f(cos(bn), abs(sin(bn)));
    return q.x - r;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // Transform UV to centered coordinates
    var p = (input.uv - vec2f(uniforms.posX, uniforms.posY)) * 2.0;
    p.x *= uniforms.aspect;

    // Apply rotation
    p = rotate2d(p, uniforms.rotation);

    // Calculate SDF based on shape type
    var d: f32;

    if (uniforms.shapeType == 0) {
        // Circle
        d = sdCircle(p, uniforms.sizeX);
    } else if (uniforms.shapeType == 1) {
        // Rectangle
        d = sdBox(p, vec2f(uniforms.sizeX, uniforms.sizeY));
    } else if (uniforms.shapeType == 2) {
        // Rounded Rectangle
        d = sdRoundedBox(p, vec2f(uniforms.sizeX, uniforms.sizeY), uniforms.cornerRadius);
    } else if (uniforms.shapeType == 3) {
        // Triangle
        d = sdEquilateralTriangle(p, uniforms.sizeX);
    } else if (uniforms.shapeType == 4) {
        // Star
        d = sdStar(p, uniforms.sizeX, uniforms.sides, 2.0);
    } else if (uniforms.shapeType == 5) {
        // Ring
        d = abs(sdCircle(p, uniforms.sizeX)) - uniforms.thickness;
    } else {
        // Polygon
        d = sdPolygon(p, uniforms.sizeX, uniforms.sides);
    }

    // Apply softness
    let alpha = 1.0 - smoothstep(-uniforms.softness, uniforms.softness, d);

    let color = vec4f(uniforms.colorR, uniforms.colorG, uniforms.colorB, uniforms.colorA * alpha);
    return color;
}
)";
}

// Explicit template instantiation for Windows hot-reload
template class SimpleGeneratorEffect<Shape, ShapeUniforms>;

} // namespace vivid::effects
