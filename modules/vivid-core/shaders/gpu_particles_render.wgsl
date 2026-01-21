// GPU Particles - Render Shader
// Draws circles from particle buffer with lifetime-based color and size interpolation

struct Particle {
    posX: f32, posY: f32,
    velX: f32, velY: f32,
    life: f32, maxLife: f32,
    size: f32, rotation: f32,
    colorR: f32, colorG: f32, colorB: f32, colorA: f32,
    seed: f32,
    _pad0: f32, _pad1: f32, _pad2: f32,
}

struct RenderUniforms {
    aspectRatio: f32,
    sizeStart: f32,
    sizeEnd: f32,
    fadeOut: f32,
    colorStartR: f32, colorStartG: f32, colorStartB: f32, colorStartA: f32,
    colorEndR: f32, colorEndG: f32, colorEndB: f32, colorEndA: f32,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) localPos: vec2f,
    @location(1) color: vec4f,
}

@group(0) @binding(0) var<uniform> u: RenderUniforms;
@group(0) @binding(1) var<storage, read> particles: array<Particle>;

@vertex
fn vs_main(
    @location(0) localPos: vec2f,
    @builtin(instance_index) instanceIdx: u32
) -> VertexOutput {
    let p = particles[instanceIdx];

    // Skip dead particles (move off screen)
    if (p.life <= 0.0) {
        var output: VertexOutput;
        output.position = vec4f(-10.0, -10.0, 0.0, 1.0);
        output.localPos = vec2f(0.0);
        output.color = vec4f(0.0);
        return output;
    }

    // Age ratio (0 = just born, 1 = about to die)
    let age = 1.0 - (p.life / p.maxLife);

    // Interpolate size over lifetime
    let size = mix(u.sizeStart, u.sizeEnd, age);

    // Interpolate color over lifetime
    let colorStart = vec4f(u.colorStartR, u.colorStartG, u.colorStartB, u.colorStartA);
    let colorEnd = vec4f(u.colorEndR, u.colorEndG, u.colorEndB, u.colorEndA);
    var color = mix(colorStart, colorEnd, age);

    // Apply particle's own color (tint)
    color *= vec4f(p.colorR, p.colorG, p.colorB, p.colorA);

    // Fade out near death
    if (u.fadeOut > 0.5) {
        let fadeStart = 0.7;
        if (age > fadeStart) {
            color.a *= 1.0 - (age - fadeStart) / (1.0 - fadeStart);
        }
    }

    // Convert position from 0-1 to clip space (-1 to 1)
    let clipPos = vec2f(p.posX, p.posY) * 2.0 - 1.0;

    // Scale local position by size and aspect ratio
    var offset = localPos * size;
    offset.x /= u.aspectRatio;

    var output: VertexOutput;
    output.position = vec4f(clipPos + offset, 0.0, 1.0);
    output.localPos = localPos;
    output.color = color;
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // SDF circle with soft edge
    let dist = length(input.localPos);
    let alpha = 1.0 - smoothstep(0.8, 1.0, dist);

    if (alpha < 0.01) { discard; }

    return vec4f(input.color.rgb, input.color.a * alpha);
}
