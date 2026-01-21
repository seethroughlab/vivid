// Ramp shader - generates animated HSV color gradients

// @include "lib/constants.wgsl"
// @include "lib/fullscreen.wgsl"
// @include "lib/color.wgsl"
// @include "lib/coords.wgsl"

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
    let fs = fullscreenTriangle(vertexIndex, true);
    var out: VertexOutput;
    out.position = fs.position;
    out.uv = fs.uv;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Apply offset and scale
    var uv = (in.uv - vec2f(0.5, 0.5) + vec2f(u.offsetX, u.offsetY)) * u.scale + vec2f(0.5, 0.5);

    // Rotate around center based on angle
    uv = rotateUv(uv, vec2f(0.5, 0.5), u.angle);

    // Apply aspect ratio correction for radial/diamond modes
    let aspectCorrectedUV = correctAspect(uv, u.resolution);
    let center = vec2f(0.5, 0.5);

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
        t = (atan2(d.y, d.x) + PI) / TWO_PI;
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
