// Vivid Render3D - Shadow Depth Shader
// Shadow pass shader (depth only) for directional/spot lights

struct ShadowUniforms {
    lightViewProj: mat4x4f,
    model: mat4x4f,
}

@group(0) @binding(0) var<uniform> uniforms: ShadowUniforms;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec4f,
    @location(3) uv: vec2f,
    @location(4) color: vec4f,
}

@vertex
fn vs_main(in: VertexInput) -> @builtin(position) vec4f {
    return uniforms.lightViewProj * uniforms.model * vec4f(in.position, 1.0);
}

@fragment
fn fs_main() {}
