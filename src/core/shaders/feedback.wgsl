// Feedback shader - blends current input with transformed previous frame

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

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Sample current input
    let input_color = textureSample(inputTexture, texSampler, in.uv);

    // Transform UV for feedback buffer sampling
    var feedback_uv = in.uv;

    // Apply offset (in normalized coordinates)
    let pixel_offset = vec2f(u.offsetX, u.offsetY) / u.resolution;
    feedback_uv = feedback_uv - pixel_offset;

    // Apply zoom around center
    let center = vec2f(0.5, 0.5);
    feedback_uv = (feedback_uv - center) * u.zoom + center;

    // Apply rotation around center
    let rotated = feedback_uv - center;
    let cos_r = cos(u.rotate);
    let sin_r = sin(u.rotate);
    feedback_uv = vec2f(
        rotated.x * cos_r - rotated.y * sin_r,
        rotated.x * sin_r + rotated.y * cos_r
    ) + center;

    // Sample feedback buffer with decay
    let feedback_color = textureSample(bufferTexture, texSampler, feedback_uv) * u.decay;

    // Mix input with feedback
    let result = mix(feedback_color, input_color, u.mix_amount);

    return result;
}
