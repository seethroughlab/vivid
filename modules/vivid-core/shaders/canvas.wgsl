// Canvas Renderer Shader
// 2D rendering for shapes, text, and images with stencil clipping support

struct Uniforms {
    resolution: vec2f,
    padding: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var texSampler: sampler;
@group(0) @binding(2) var tex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec2f,
    @location(1) uv: vec2f,
    @location(2) color: vec4f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    // Convert pixel coords to clip space (-1 to 1)
    let clipX = (in.position.x / uniforms.resolution.x) * 2.0 - 1.0;
    let clipY = 1.0 - (in.position.y / uniforms.resolution.y) * 2.0;
    out.position = vec4f(clipX, clipY, 0.0, 1.0);
    out.uv = in.uv;
    out.color = in.color;
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let texColor = textureSample(tex, texSampler, in.uv);
    // Use texture alpha * vertex color for text rendering
    // For solid shapes, texture is white (1,1,1,1)
    return vec4f(in.color.rgb, in.color.a * texColor.a);
}

// Image fragment shader - samples full RGBA from texture
@fragment
fn fs_image(in: VertexOutput) -> @location(0) vec4f {
    let texColor = textureSample(tex, texSampler, in.uv);
    // Multiply texture color by vertex color (allows tinting, vertex white = use texture as-is)
    return texColor * in.color;
}
