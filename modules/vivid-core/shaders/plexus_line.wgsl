// Plexus - Line Shader
// 3D line rendering with screen-space width and distance-based alpha

struct Uniforms {
    viewProj: mat4x4f,
    resolution: vec2f,
    lineWidth: f32,
    _pad: f32,
    color: vec4f,
}

struct VertexInput {
    @location(0) localPos: vec2f,
    @location(1) alongAcross: vec2f,
}

struct InstanceInput {
    @location(2) start: vec4f,   // xyz + pad
    @location(3) endAlpha: vec4f, // xyz + alpha
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) alpha: f32,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@vertex
fn vs_main(vert: VertexInput, inst: InstanceInput) -> VertexOutput {
    var output: VertexOutput;

    let start3d = inst.start.xyz;
    let end3d = inst.endAlpha.xyz;
    let lineAlpha = inst.endAlpha.w;

    // Project start and end points
    let startClip = uniforms.viewProj * vec4f(start3d, 1.0);
    let endClip = uniforms.viewProj * vec4f(end3d, 1.0);

    // Convert to NDC
    let startNdc = startClip.xy / startClip.w;
    let endNdc = endClip.xy / endClip.w;

    // Direction in screen space
    let dir = endNdc - startNdc;
    let len = length(dir);
    let tangent = select(vec2f(1.0, 0.0), dir / len, len > 0.0001);
    let normal = vec2f(-tangent.y, tangent.x);

    // Interpolate along line
    let t = vert.alongAcross.x;
    let clipPos = mix(startClip, endClip, t);

    // Offset perpendicular to line for width (in screen space)
    let halfWidth = uniforms.lineWidth / uniforms.resolution.y;
    let offset = normal * vert.alongAcross.y * halfWidth * clipPos.w;

    output.position = vec4f(clipPos.xy + offset, clipPos.z, clipPos.w);
    output.alpha = lineAlpha;

    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    return vec4f(uniforms.color.rgb, uniforms.color.a * input.alpha);
}
