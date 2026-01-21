// Fluid Simulation - Render Shader
// Output dye field to texture with clear color background

// @include "lib/fullscreen.wgsl"

struct Uniforms {
    clearR: f32,
    clearG: f32,
    clearB: f32,
    clearA: f32,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;
@group(0) @binding(1) var dyeTex: texture_2d<f32>;
@group(0) @binding(2) var linearSampler: sampler;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var positions = array<vec2f, 3>(
        vec2f(-1.0, -1.0),
        vec2f(3.0, -1.0),
        vec2f(-1.0, 3.0)
    );
    var output: VertexOutput;
    output.position = vec4f(positions[vertexIndex], 0.0, 1.0);
    output.uv = (positions[vertexIndex] + 1.0) * 0.5;
    output.uv.y = 1.0 - output.uv.y;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let dye = textureSample(dyeTex, linearSampler, input.uv);
    let clear = vec4f(u.clearR, u.clearG, u.clearB, u.clearA);

    // Blend dye over clear color
    let result = vec4f(
        mix(clear.rgb, dye.rgb, dye.a),
        max(clear.a, dye.a)
    );
    return result;
}
