// Ramp shader - generates animated HSV color gradients

struct Uniforms {
    resolution: vec2f,
    time: f32,
    rampType: i32,      // 0=Linear, 1=Radial, 2=Angular, 3=Diamond
    angle: f32,
    offsetX: f32,
    offsetY: f32,
    scale: f32,
    repeat: f32,
    hueOffset: f32,
    hueSpeed: f32,
    hueRange: f32,
    saturation: f32,
    brightness: f32,
    _pad: vec2f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f( 3.0, -1.0),
        vec2f(-1.0,  3.0)
    );
    var out: VertexOutput;
    let pos = positions[vertexIndex];
    out.position = vec4f(pos, 0.0, 1.0);
    out.uv = pos * 0.5 + 0.5;
    out.uv.y = 1.0 - out.uv.y;
    return out;
}

// HSV to RGB conversion
fn hsv2rgb(hsv: vec3f) -> vec3f {
    let h = hsv.x;
    let s = hsv.y;
    let v = hsv.z;

    let c = v * s;
    let hp = h * 6.0;
    let x = c * (1.0 - abs(hp % 2.0 - 1.0));
    let m = v - c;

    var rgb: vec3f;
    if (hp < 1.0) {
        rgb = vec3f(c, x, 0.0);
    } else if (hp < 2.0) {
        rgb = vec3f(x, c, 0.0);
    } else if (hp < 3.0) {
        rgb = vec3f(0.0, c, x);
    } else if (hp < 4.0) {
        rgb = vec3f(0.0, x, c);
    } else if (hp < 5.0) {
        rgb = vec3f(x, 0.0, c);
    } else {
        rgb = vec3f(c, 0.0, x);
    }

    return rgb + vec3f(m, m, m);
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Apply offset and scale
    var uv = (in.uv - vec2f(0.5, 0.5) + vec2f(u.offsetX, u.offsetY)) * u.scale + vec2f(0.5, 0.5);

    // Rotate around center based on angle
    let center = vec2f(0.5, 0.5);
    let rotated = uv - center;
    let cosA = cos(u.angle);
    let sinA = sin(u.angle);
    uv = vec2f(
        rotated.x * cosA - rotated.y * sinA,
        rotated.x * sinA + rotated.y * cosA
    ) + center;

    // Apply aspect ratio correction for radial/diamond modes
    let aspect = u.resolution.x / u.resolution.y;
    let aspectCorrectedUV = vec2f((uv.x - 0.5) * aspect + 0.5, uv.y);

    // Calculate ramp value based on type
    var t: f32;
    if (u.rampType == 0) {
        // Linear (horizontal)
        t = uv.x;
    } else if (u.rampType == 1) {
        // Radial (aspect corrected for circular shape)
        t = length(aspectCorrectedUV - center) * 2.0;
    } else if (u.rampType == 2) {
        // Angular (aspect corrected)
        let d = aspectCorrectedUV - center;
        t = (atan2(d.y, d.x) + 3.14159265) / (2.0 * 3.14159265);
    } else {
        // Diamond (aspect corrected for square shape)
        let d = abs(aspectCorrectedUV - center);
        t = (d.x + d.y) * 2.0;
    }

    // Apply repeat
    t = fract(t * u.repeat);

    // Calculate animated hue
    let hue = fract(u.hueOffset + u.time * u.hueSpeed + t * u.hueRange);

    // Convert HSV to RGB
    let rgb = hsv2rgb(vec3f(hue, u.saturation, u.brightness));

    return vec4f(rgb, 1.0);
}
