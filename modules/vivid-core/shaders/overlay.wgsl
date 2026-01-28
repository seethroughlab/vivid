// Vivid Core - Overlay Canvas Shader
// Shader for overlay rendering (no stencil, simple alpha blending)

struct Uniforms {
    resolution: vec2f,
    mipLevel: f32,      // Mip level hint for textured rects (0 = auto/base level)
    padding: f32,
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
    // Use textureSampleLevel for explicit mip level control
    // When mipLevel > 0, sample from that specific mip level for smoother thumbnails
    // When mipLevel == 0, sample from base level (equivalent to textureSample at level 0)
    let texColor = textureSampleLevel(tex, texSampler, in.uv, uniforms.mipLevel);
    // For text: texture has alpha in .a channel
    // For solids: texture is white (1,1,1,1)
    return vec4f(in.color.rgb * texColor.rgb, in.color.a * texColor.a);
}
