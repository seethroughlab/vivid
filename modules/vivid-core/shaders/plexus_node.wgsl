// Plexus - Node Shader
// 3D billboard node rendering with SDF circles

struct Uniforms {
    viewProj: mat4x4f,
    resolution: vec2f,
    aspectRatio: f32,
    _pad: f32,
}

struct VertexInput {
    @location(0) localPos: vec2f,
}

struct InstanceInput {
    @location(1) posSize: vec4f,  // xyz + size
    @location(2) color: vec4f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) localPos: vec2f,
    @location(1) color: vec4f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@vertex
fn vs_main(vert: VertexInput, inst: InstanceInput) -> VertexOutput {
    var output: VertexOutput;

    let worldPos = inst.posSize.xyz;
    let size = inst.posSize.w;

    // Project center to clip space
    let clipPos = uniforms.viewProj * vec4f(worldPos, 1.0);

    // Billboard offset in screen space (size scales with distance for consistency)
    var offset = vert.localPos * size;
    offset.x /= uniforms.aspectRatio;

    output.position = vec4f(clipPos.xy + offset * clipPos.w, clipPos.z, clipPos.w);
    output.localPos = vert.localPos;
    output.color = inst.color;

    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let dist = length(input.localPos);
    let edge = smoothstep(1.0, 0.9, dist);
    return vec4f(input.color.rgb, input.color.a * edge);
}
