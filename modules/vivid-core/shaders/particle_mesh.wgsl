// Vivid Effects - Mesh Particle Shader
// Instanced 3D meshes with velocity alignment

struct Uniforms {
    viewProj: mat4x4f,
}

// Mesh vertex input (position only for simplicity)
struct VertexInput {
    @location(0) position: vec3f,
}

// Instance data: 4 columns of transform matrix + color
struct InstanceInput {
    @location(4) transform0: vec4f,
    @location(5) transform1: vec4f,
    @location(6) transform2: vec4f,
    @location(7) transform3: vec4f,
    @location(8) color: vec4f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
    @location(1) worldNormal: vec3f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@vertex
fn vs_main(vertex: VertexInput, instance: InstanceInput) -> VertexOutput {
    // Reconstruct transform matrix from columns
    let transform = mat4x4f(
        instance.transform0,
        instance.transform1,
        instance.transform2,
        instance.transform3
    );

    var output: VertexOutput;
    let worldPos = transform * vec4f(vertex.position, 1.0);
    output.position = uniforms.viewProj * worldPos;
    output.color = instance.color;

    // Extract rotation for simple lighting (upper-left 3x3)
    let normalMatrix = mat3x3f(
        instance.transform0.xyz,
        instance.transform1.xyz,
        instance.transform2.xyz
    );
    // Assume mesh normal is +Z for elongated cubes
    output.worldNormal = normalize(normalMatrix * vec3f(0.0, 0.0, 1.0));

    return output;
}

@fragment
fn fs_unlit(input: VertexOutput) -> @location(0) vec4f {
    return input.color;
}

@fragment
fn fs_lit(input: VertexOutput) -> @location(0) vec4f {
    // Simple directional lighting
    let lightDir = normalize(vec3f(1.0, 2.0, 1.5));
    let ambient = 0.3;
    let diffuse = max(dot(input.worldNormal, lightDir), 0.0);
    let lighting = ambient + diffuse * 0.7;
    return vec4f(input.color.rgb * lighting, input.color.a);
}
