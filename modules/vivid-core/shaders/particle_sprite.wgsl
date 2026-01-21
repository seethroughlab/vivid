// Particle Renderer - Sprite Shader
// Textured quad rendering with instancing, rotation, and UV atlas support

struct Uniforms {
    resolution: vec2f,
    aspectRatio: f32,
    _pad: f32,
}

struct VertexInput {
    @location(0) localPos: vec2f,
    @location(1) uv: vec2f,
}

struct InstanceInput {
    @location(2) center: vec2f,
    @location(3) sizeRot: vec2f,  // size in .x, rotation in .y
    @location(4) color: vec4f,
    @location(5) uvOffset: vec2f,
    @location(6) uvScale: vec2f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var spriteSampler: sampler;
@group(0) @binding(2) var spriteTexture: texture_2d<f32>;

@vertex
fn vs_main(vert: VertexInput, inst: InstanceInput) -> VertexOutput {
    var output: VertexOutput;

    // Rotate local position (rotation stored in sizeRot.y)
    let c = cos(inst.sizeRot.y);
    let s = sin(inst.sizeRot.y);
    var rotated = vec2f(
        vert.localPos.x * c - vert.localPos.y * s,
        vert.localPos.x * s + vert.localPos.y * c
    );

    // Scale by instance size (stored in sizeRot.x)
    var worldPos = rotated * inst.sizeRot.x;

    // Correct for aspect ratio
    worldPos.x /= uniforms.aspectRatio;

    // Translate to instance center
    worldPos = worldPos + inst.center;

    // Convert from 0-1 to clip space (-1 to 1)
    var clipPos = worldPos * 2.0 - 1.0;
    clipPos.y = -clipPos.y;

    output.position = vec4f(clipPos, 0.0, 1.0);
    output.uv = inst.uvOffset + vert.uv * inst.uvScale;
    output.color = inst.color;

    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let texColor = textureSample(spriteTexture, spriteSampler, input.uv);
    return texColor * input.color;
}
