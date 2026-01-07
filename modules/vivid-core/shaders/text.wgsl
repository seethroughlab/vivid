// Text rendering shader - renders bitmap font characters

struct Uniforms {
    screenSize: vec2f,
    _padding: vec2f,
};

struct VertexInput {
    @location(0) position: vec2f,  // Screen position
    @location(1) uv: vec2f,        // Texture coordinates
    @location(2) color: vec4f,     // Text color
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var fontSampler: sampler;
@group(0) @binding(2) var fontTexture: texture_2d<f32>;

@vertex
fn vs_main(input: VertexInput) -> VertexOutput {
    var output: VertexOutput;

    // Convert from pixel coordinates to NDC (-1 to 1)
    let ndc = vec2f(
        (input.position.x / uniforms.screenSize.x) * 2.0 - 1.0,
        1.0 - (input.position.y / uniforms.screenSize.y) * 2.0
    );

    output.position = vec4f(ndc, 0.0, 1.0);
    output.uv = input.uv;
    output.color = input.color;

    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let alpha = textureSample(fontTexture, fontSampler, input.uv).r;
    return vec4f(input.color.rgb, input.color.a * alpha);
}
