// Vivid Render3D - Billboard Particle Shader
// GPU billboard particle system with spritesheet animation support

struct Uniforms {
    viewProj: mat4x4f,
    cameraRight: vec3f,
    _pad1: f32,
    cameraUp: vec3f,
    _pad2: f32,
    spriteSheetCols: f32,
    spriteSheetRows: f32,
    spriteFrameCount: f32,
    _pad3: f32,
}

struct ParticleInstance {
    @location(0) position: vec3f,
    @location(1) size: f32,
    @location(2) color: vec4f,
    @location(3) rotation: f32,
    @location(4) frameIndex: f32,
    @location(5) _pad: vec2f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

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
    instance: ParticleInstance
) -> VertexOutput {
    var output: VertexOutput;

    let localPos = quadPositions[vertexIndex];

    // Apply rotation around Z (screen-space)
    let c = cos(instance.rotation);
    let s = sin(instance.rotation);
    let rotatedPos = vec2f(
        localPos.x * c - localPos.y * s,
        localPos.x * s + localPos.y * c
    );

    // Billboard: expand quad in camera plane
    let worldOffset = uniforms.cameraRight * rotatedPos.x * instance.size
                    + uniforms.cameraUp * rotatedPos.y * instance.size;
    let worldPos = instance.position + worldOffset;

    output.position = uniforms.viewProj * vec4f(worldPos, 1.0);

    // Compute spritesheet UV offset
    let baseUV = quadUVs[vertexIndex];
    let cols = uniforms.spriteSheetCols;
    let rows = uniforms.spriteSheetRows;

    if (cols > 1.0 || rows > 1.0) {
        // Spritesheet mode: compute frame position
        let frame = u32(instance.frameIndex) % u32(uniforms.spriteFrameCount);
        let col = f32(frame % u32(cols));
        let row = f32(frame / u32(cols));

        let cellWidth = 1.0 / cols;
        let cellHeight = 1.0 / rows;

        output.uv = vec2f(
            (col + baseUV.x) * cellWidth,
            (row + baseUV.y) * cellHeight
        );
    } else {
        output.uv = baseUV;
    }

    output.color = instance.color;

    return output;
}

@group(0) @binding(1) var particleSampler: sampler;
@group(0) @binding(2) var particleTexture: texture_2d<f32>;

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    // Sample texture
    let texColor = textureSample(particleTexture, particleSampler, input.uv);
    return texColor * input.color;
}

@fragment
fn fs_circle(input: VertexOutput) -> @location(0) vec4f {
    // Draw antialiased circle using SDF
    let dist = length(input.uv - vec2f(0.5, 0.5)) * 2.0;
    let alpha = 1.0 - smoothstep(0.9, 1.0, dist);
    return vec4f(input.color.rgb, input.color.a * alpha);
}
