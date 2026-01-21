// Particle Renderer - Circle Shader
// SDF-based circle rendering with instancing

struct Uniforms {
    resolution: vec2f,
    aspectRatio: f32,
    _pad: f32,
}

struct VertexInput {
    @location(0) localPos: vec2f,
}

struct InstanceInput {
    @location(1) center: vec2f,
    @location(2) radiusPad: vec2f,  // radius in .x, _pad in .y
    @location(3) color: vec4f,
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

    // Scale local position by radius (stored in radiusPad.x)
    var worldPos = vert.localPos * inst.radiusPad.x;

    // Correct for aspect ratio (make circles circular)
    worldPos.x /= uniforms.aspectRatio;

    // Translate to instance center
    worldPos = worldPos + inst.center;

    // Convert from 0-1 to clip space (-1 to 1)
    var clipPos = worldPos * 2.0 - 1.0;
    clipPos.y = -clipPos.y;  // Flip Y for WebGPU

    output.position = vec4f(clipPos, 0.0, 1.0);
    output.localPos = vert.localPos;
    output.color = inst.color;

    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // SDF circle with antialiasing
    let dist = length(input.localPos);

    // Smooth edge with antialiasing
    let edge = smoothstep(1.0, 0.95, dist);

    return vec4f(input.color.rgb, input.color.a * edge);
}
