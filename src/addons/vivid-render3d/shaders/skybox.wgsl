// Skybox shader - renders environment cubemap as background

struct Uniforms {
    invViewProj: mat4x4f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var envSampler: sampler;
@group(0) @binding(2) var envCubemap: texture_cube<f32>;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) viewDir: vec3f,
}

// Full-screen triangle vertex shader
@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var out: VertexOutput;

    // Generate fullscreen triangle (covers clip space -1 to 1)
    let x = f32((vertexIndex << 1u) & 2u) * 2.0 - 1.0;
    let y = f32(vertexIndex & 2u) * 2.0 - 1.0;

    out.position = vec4f(x, y, 0.9999, 1.0);  // At far plane

    // Calculate view direction from clip space position
    // Use inverse view-projection to get world direction
    let nearPoint = uniforms.invViewProj * vec4f(x, y, -1.0, 1.0);
    let farPoint = uniforms.invViewProj * vec4f(x, y, 1.0, 1.0);

    let nearWorld = nearPoint.xyz / nearPoint.w;
    let farWorld = farPoint.xyz / farPoint.w;

    out.viewDir = normalize(farWorld - nearWorld);

    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Sample environment cubemap
    let color = textureSample(envCubemap, envSampler, in.viewDir).rgb;

    // Apply simple tone mapping for HDR
    let mapped = color / (color + vec3f(1.0));

    return vec4f(mapped, 1.0);
}
