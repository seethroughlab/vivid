// Feedback shader - blends current input with transformed previous frame

// @include "lib/fullscreen.wgsl"
// @include "lib/coords.wgsl"

struct Uniforms {
    resolution: vec2f,
    decay: f32,
    mix_amount: f32,
    offsetX: f32,
    offsetY: f32,
    zoom: f32,
    rotate: f32,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var inputTexture: texture_2d<f32>;
@group(0) @binding(3) var bufferTexture: texture_2d<f32>;

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
    // Sample current input
    let input_color = textureSample(inputTexture, texSampler, in.uv);

    // Transform UV for feedback buffer sampling
    var feedback_uv = in.uv;
    let center = vec2f(0.5, 0.5);

    // Apply offset (in normalized coordinates)
    let pixel_offset = vec2f(u.offsetX, u.offsetY) / u.resolution;
    feedback_uv = feedback_uv - pixel_offset;

    // Apply zoom around center
    feedback_uv = scaleUv(feedback_uv, center, u.zoom);

    // Apply rotation around center
    feedback_uv = rotateUv(feedback_uv, center, u.rotate);

    // Sample feedback buffer with decay
    let feedback_color = textureSample(bufferTexture, texSampler, feedback_uv) * u.decay;

    // Mix input with feedback
    let result = mix(feedback_color, input_color, u.mix_amount);

    return result;
}
