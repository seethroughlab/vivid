// Vivid Effects - GPU Billboard Particle Render Shader
// Reads directly from particle storage buffer for 3D billboard rendering

struct Particle {
    posX: f32, posY: f32, posZ: f32,
    velX: f32, velY: f32, velZ: f32,
    life: f32, maxLife: f32,
    size: f32, rotation: f32,
    colorR: f32, colorG: f32, colorB: f32, colorA: f32,
    seed: f32, _pad: f32,
}

struct BillboardUniforms {
    viewProj: mat4x4f,
    cameraRight: vec3f,
    _pad1: f32,
    cameraUp: vec3f,
    _pad2: f32,
    sizeStart: f32,
    sizeEnd: f32,
    fadeOut: f32,
    _pad3: f32,
    colorStartR: f32, colorStartG: f32, colorStartB: f32, colorStartA: f32,
    colorEndR: f32, colorEndG: f32, colorEndB: f32, colorEndA: f32,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
}

@group(0) @binding(0) var<uniform> u: BillboardUniforms;
@group(0) @binding(1) var<storage, read> particles: array<Particle>;

// Quad vertices (2 triangles)
const quadPositions = array<vec2f, 6>(
    vec2f(-0.5, -0.5),
    vec2f( 0.5, -0.5),
    vec2f( 0.5,  0.5),
    vec2f(-0.5, -0.5),
    vec2f( 0.5,  0.5),
    vec2f(-0.5,  0.5),
);

const quadUVs = array<vec2f, 6>(
    vec2f(0.0, 1.0),
    vec2f(1.0, 1.0),
    vec2f(1.0, 0.0),
    vec2f(0.0, 1.0),
    vec2f(1.0, 0.0),
    vec2f(0.0, 0.0),
);

@vertex
fn vs_main(
    @builtin(vertex_index) vertexIndex: u32,
    @builtin(instance_index) instanceIdx: u32
) -> VertexOutput {
    let p = particles[instanceIdx];

    // Skip dead particles
    if (p.life <= 0.0) {
        var output: VertexOutput;
        output.position = vec4f(-10.0, -10.0, -10.0, 1.0);
        output.uv = vec2f(0.0);
        output.color = vec4f(0.0);
        return output;
    }

    // Age ratio
    let age = 1.0 - (p.life / p.maxLife);

    // Interpolate size over lifetime
    let size = mix(u.sizeStart, u.sizeEnd, age);

    // Interpolate color
    let colorStart = vec4f(u.colorStartR, u.colorStartG, u.colorStartB, u.colorStartA);
    let colorEnd = vec4f(u.colorEndR, u.colorEndG, u.colorEndB, u.colorEndA);
    var color = mix(colorStart, colorEnd, age);
    color *= vec4f(p.colorR, p.colorG, p.colorB, p.colorA);

    // Fade out near death
    if (u.fadeOut > 0.5) {
        let fadeStart = 0.7;
        if (age > fadeStart) {
            color.a *= 1.0 - (age - fadeStart) / (1.0 - fadeStart);
        }
    }

    let localPos = quadPositions[vertexIndex];

    // Apply rotation
    let c = cos(p.rotation);
    let s = sin(p.rotation);
    let rotatedPos = vec2f(
        localPos.x * c - localPos.y * s,
        localPos.x * s + localPos.y * c
    );

    // Billboard: expand quad in camera plane
    let worldOffset = u.cameraRight * rotatedPos.x * size
                    + u.cameraUp * rotatedPos.y * size;
    let position = vec3f(p.posX, p.posY, p.posZ);
    let worldPos = position + worldOffset;

    var output: VertexOutput;
    output.position = u.viewProj * vec4f(worldPos, 1.0);
    output.uv = quadUVs[vertexIndex];
    output.color = color;
    return output;
}

@fragment
fn fs_circle(input: VertexOutput) -> @location(0) vec4f {
    let dist = length(input.uv - vec2f(0.5, 0.5)) * 2.0;
    // Soft radial gradient: opaque center, transparent edges
    let alpha = max(0.0, 1.0 - dist * dist);  // Quadratic falloff
    if (alpha < 0.01) { discard; }
    return vec4f(input.color.rgb, input.color.a * alpha);
}
