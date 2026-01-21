// Vivid Render3D - Point Shadow Shader
// Point shadow pass shader (outputs linear depth)

struct PointShadowUniforms {
    lightViewProj: mat4x4f,
    model: mat4x4f,
    lightPosAndFarPlane: vec4f,  // xyz = position, w = farPlane
}

@group(0) @binding(0) var<uniform> uniforms: PointShadowUniforms;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec4f,
    @location(3) uv: vec2f,
    @location(4) color: vec4f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) worldPos: vec3f,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let worldPos = (uniforms.model * vec4f(in.position, 1.0)).xyz;
    out.worldPos = worldPos;
    out.position = uniforms.lightViewProj * vec4f(worldPos, 1.0);
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) f32 {
    // Output linear distance from light, normalized to [0,1]
    let dist = length(in.worldPos - uniforms.lightPosAndFarPlane.xyz);
    return dist / uniforms.lightPosAndFarPlane.w;
}
