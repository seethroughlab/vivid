// Vivid Render3D - Shadow Pass Shader
// Depth-only rendering from light's perspective
// No fragment shader needed - WebGPU writes depth automatically

struct ShadowUniforms {
    lightViewProj: mat4x4f,  // Light's view-projection matrix
    model: mat4x4f,          // Object model matrix
};

@group(0) @binding(0) var<uniform> uniforms: ShadowUniforms;

// Only need position for depth rendering
struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec4f,
    @location(3) uv: vec2f,
    @location(4) color: vec4f,
};

@vertex
fn vs_main(in: VertexInput) -> @builtin(position) vec4f {
    return uniforms.lightViewProj * uniforms.model * vec4f(in.position, 1.0);
}

// Empty fragment shader - we only care about depth
@fragment
fn fs_main() {
    // Depth is written automatically by the depth test
}
