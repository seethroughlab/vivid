// Depth copy shader - linearizes depth buffer for post-processing

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

struct DepthUniforms {
    near: f32,
    far: f32,
    _pad: vec2f,
};

@group(0) @binding(0) var depthTexture: texture_depth_2d;
@group(0) @binding(1) var depthSampler: sampler;
@group(0) @binding(2) var<uniform> uniforms: DepthUniforms;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var output: VertexOutput;
    // Fullscreen triangle
    let x = f32(i32(vertexIndex & 1u) * 4 - 1);
    let y = f32(i32(vertexIndex >> 1u) * 4 - 1);
    output.position = vec4f(x, y, 0.0, 1.0);
    output.uv = vec2f((x + 1.0) * 0.5, (1.0 - y) * 0.5);
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let depth = textureSample(depthTexture, depthSampler, input.uv);

    // Linearize depth from [0,1] non-linear to [0,1] linear
    let near = uniforms.near;
    let far = uniforms.far;
    let linearZ = near * far / (far - depth * (far - near));

    // Normalize to [0,1] range
    let normalizedDepth = saturate((linearZ - near) / (far - near));

    return vec4f(normalizedDepth, 0.0, 0.0, 1.0);
}
