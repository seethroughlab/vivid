// Vivid Effects - Unified Particle System Implementation
// CPU simulation with multiple render modes

#include <vivid/effects/particle_system.h>
#include <vivid/effects/particle_renderer.h>
#include <vivid/effects/gpu_common.h>
#include <vivid/effects/types.h>
#include <vivid/context.h>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <cstring>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace vivid::effects {

// =============================================================================
// Billboard Shader
// =============================================================================

static const char* BILLBOARD_SHADER = R"(
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
};

struct ParticleInstance {
    @location(0) position: vec3f,
    @location(1) size: f32,
    @location(2) color: vec4f,
    @location(3) rotation: f32,
    @location(4) frameIndex: f32,
    @location(5) _pad: vec2f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
    @location(1) color: vec4f,
};

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
fn fs_textured(input: VertexOutput) -> @location(0) vec4f {
    let texColor = textureSample(particleTexture, particleSampler, input.uv);
    return texColor * input.color;
}

@fragment
fn fs_circle(input: VertexOutput) -> @location(0) vec4f {
    let dist = length(input.uv - vec2f(0.5, 0.5)) * 2.0;
    let alpha = 1.0 - smoothstep(0.9, 1.0, dist);
    return vec4f(input.color.rgb, input.color.a * alpha);
}
)";

// Billboard uniform data
struct BillboardUniforms {
    float viewProj[16];
    float cameraRight[3];
    float _pad1;
    float cameraUp[3];
    float _pad2;
    float spriteSheetCols;
    float spriteSheetRows;
    float spriteFrameCount;
    float _pad3;
};

// Billboard instance data (48 bytes)
struct BillboardInstance {
    float position[3];
    float size;
    float color[4];
    float rotation;
    float frameIndex;
    float _pad[2];
};

// =============================================================================
// Mesh Shader (instanced 3D meshes with velocity alignment)
// =============================================================================

static const char* MESH_SHADER = R"(
struct Uniforms {
    viewProj: mat4x4f,
};

// Mesh vertex input (position only for simplicity)
struct VertexInput {
    @location(0) position: vec3f,
};

// Instance data: 4 columns of transform matrix + color
struct InstanceInput {
    @location(4) transform0: vec4f,
    @location(5) transform1: vec4f,
    @location(6) transform2: vec4f,
    @location(7) transform3: vec4f,
    @location(8) color: vec4f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
    @location(1) worldNormal: vec3f,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

@vertex
fn vs_main(vertex: VertexInput, instance: InstanceInput) -> VertexOutput {
    // Reconstruct transform matrix from columns
    let transform = mat4x4f(
        instance.transform0,
        instance.transform1,
        instance.transform2,
        instance.transform3
    );

    var output: VertexOutput;
    let worldPos = transform * vec4f(vertex.position, 1.0);
    output.position = uniforms.viewProj * worldPos;
    output.color = instance.color;

    // Extract rotation for simple lighting (upper-left 3x3)
    let normalMatrix = mat3x3f(
        instance.transform0.xyz,
        instance.transform1.xyz,
        instance.transform2.xyz
    );
    // Assume mesh normal is +Z for elongated cubes
    output.worldNormal = normalize(normalMatrix * vec3f(0.0, 0.0, 1.0));

    return output;
}

@fragment
fn fs_unlit(input: VertexOutput) -> @location(0) vec4f {
    return input.color;
}

@fragment
fn fs_lit(input: VertexOutput) -> @location(0) vec4f {
    // Simple directional lighting
    let lightDir = normalize(vec3f(1.0, 2.0, 1.5));
    let ambient = 0.3;
    let diffuse = max(dot(input.worldNormal, lightDir), 0.0);
    let lighting = ambient + diffuse * 0.7;
    return vec4f(input.color.rgb * lighting, input.color.a);
}
)";

// Mesh uniform data (just viewProj for now)
struct MeshUniforms {
    float viewProj[16];
};

// Mesh instance data (80 bytes - transform matrix + color)
struct MeshInstance {
    float transform[16];  // 4x4 matrix, column-major
    float color[4];
};

// =============================================================================
// GPU Compute Shader - 3D Particle Simulation
// =============================================================================

static const char* GPU_SIMULATE_SHADER = R"(
// GPU particle data (64 bytes, cache-aligned)
struct Particle {
    posX: f32, posY: f32, posZ: f32,   // 12 bytes - position
    velX: f32, velY: f32, velZ: f32,   // 12 bytes - velocity (total: 24)
    life: f32, maxLife: f32,            // 8 bytes - lifetime (total: 32)
    size: f32, rotation: f32,           // 8 bytes - visual (total: 40)
    colorR: f32, colorG: f32, colorB: f32, colorA: f32,  // 16 bytes - color (total: 56)
    seed: f32, _pad: f32,               // 8 bytes (total: 64)
}

struct SimulateUniforms {
    dt: f32,
    time: f32,
    particleCount: u32,
    is3D: u32,  // 0 = 2D, 1 = 3D

    // Curl noise
    curlStrength: f32,
    curlScale: f32,
    curlSpeed: f32,
    curlOctaves: i32,

    // Gravity
    gravityX: f32,
    gravityY: f32,
    gravityZ: f32,
    drag: f32,

    // Turbulence
    turbulence: f32,
    turbulenceSeed: f32,
    _pad0: f32,
    _pad1: f32,

    // Attractor
    attractorX: f32,
    attractorY: f32,
    attractorZ: f32,
    attractorStrength: f32,
}

@group(0) @binding(0) var<uniform> u: SimulateUniforms;
@group(0) @binding(1) var<storage, read> particlesIn: array<Particle>;
@group(0) @binding(2) var<storage, read_write> particlesOut: array<Particle>;

// =============================================================================
// 3D Simplex Noise
// =============================================================================

fn mod289_3(x: vec3f) -> vec3f { return x - floor(x * (1.0 / 289.0)) * 289.0; }
fn mod289_4(x: vec4f) -> vec4f { return x - floor(x * (1.0 / 289.0)) * 289.0; }
fn permute(x: vec4f) -> vec4f { return mod289_4(((x * 34.0) + 1.0) * x); }
fn taylorInvSqrt(r: vec4f) -> vec4f { return 1.79284291400159 - 0.85373472095314 * r; }

fn snoise(v: vec3f) -> f32 {
    let C = vec2f(1.0/6.0, 1.0/3.0);
    let D = vec4f(0.0, 0.5, 1.0, 2.0);

    var i = floor(v + dot(v, vec3f(C.y)));
    let x0 = v - i + dot(i, vec3f(C.x));

    let g = step(x0.yzx, x0.xyz);
    let l = 1.0 - g;
    let i1 = min(g.xyz, l.zxy);
    let i2 = max(g.xyz, l.zxy);

    let x1 = x0 - i1 + C.x;
    let x2 = x0 - i2 + C.y;
    let x3 = x0 - D.yyy;

    i = mod289_3(i);
    let p = permute(permute(permute(
        i.z + vec4f(0.0, i1.z, i2.z, 1.0))
        + i.y + vec4f(0.0, i1.y, i2.y, 1.0))
        + i.x + vec4f(0.0, i1.x, i2.x, 1.0));

    let n_ = 0.142857142857;
    let ns = n_ * D.wyz - D.xzx;

    let j = p - 49.0 * floor(p * ns.z * ns.z);

    let x_ = floor(j * ns.z);
    let y_ = floor(j - 7.0 * x_);

    let x = x_ * ns.x + ns.yyyy;
    let y = y_ * ns.x + ns.yyyy;
    let h = 1.0 - abs(x) - abs(y);

    let b0 = vec4f(x.xy, y.xy);
    let b1 = vec4f(x.zw, y.zw);

    let s0 = floor(b0) * 2.0 + 1.0;
    let s1 = floor(b1) * 2.0 + 1.0;
    let sh = -step(h, vec4f(0.0));

    let a0 = b0.xzyw + s0.xzyw * sh.xxyy;
    let a1 = b1.xzyw + s1.xzyw * sh.zzww;

    var p0 = vec3f(a0.xy, h.x);
    var p1 = vec3f(a0.zw, h.y);
    var p2 = vec3f(a1.xy, h.z);
    var p3 = vec3f(a1.zw, h.w);

    let norm = taylorInvSqrt(vec4f(dot(p0,p0), dot(p1,p1), dot(p2,p2), dot(p3,p3)));
    p0 *= norm.x;
    p1 *= norm.y;
    p2 *= norm.z;
    p3 *= norm.w;

    var m = max(0.6 - vec4f(dot(x0,x0), dot(x1,x1), dot(x2,x2), dot(x3,x3)), vec4f(0.0));
    m = m * m;
    return 42.0 * dot(m*m, vec4f(dot(p0,x0), dot(p1,x1), dot(p2,x2), dot(p3,x3)));
}

// FBM (fractal brownian motion) noise
fn fbm(p: vec3f, octaves: i32) -> f32 {
    var result = 0.0;
    var amplitude = 1.0;
    var frequency = 1.0;
    var maxAmp = 0.0;

    for (var i = 0; i < octaves; i++) {
        result += amplitude * snoise(p * frequency);
        maxAmp += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return result / maxAmp;
}

// 3D Curl noise - divergence-free velocity field
fn curlNoise3D(pos: vec3f, z: f32, scale: f32, octaves: i32) -> vec3f {
    let eps = 0.001;
    let p = pos * scale;

    // Sample noise at offset positions for finite differences
    let n = vec3f(
        fbm(vec3f(p.x, p.y + 100.0, p.z + z), octaves),
        fbm(vec3f(p.x + 200.0, p.y, p.z + z + 100.0), octaves),
        fbm(vec3f(p.x + 100.0, p.y + 300.0, p.z + z + 200.0), octaves)
    );

    let nx = vec3f(
        fbm(vec3f(p.x + eps, p.y + 100.0, p.z + z), octaves),
        fbm(vec3f(p.x + eps + 200.0, p.y, p.z + z + 100.0), octaves),
        fbm(vec3f(p.x + eps + 100.0, p.y + 300.0, p.z + z + 200.0), octaves)
    );

    let ny = vec3f(
        fbm(vec3f(p.x, p.y + eps + 100.0, p.z + z), octaves),
        fbm(vec3f(p.x + 200.0, p.y + eps, p.z + z + 100.0), octaves),
        fbm(vec3f(p.x + 100.0, p.y + eps + 300.0, p.z + z + 200.0), octaves)
    );

    let nz = vec3f(
        fbm(vec3f(p.x, p.y + 100.0, p.z + eps + z), octaves),
        fbm(vec3f(p.x + 200.0, p.y, p.z + eps + z + 100.0), octaves),
        fbm(vec3f(p.x + 100.0, p.y + 300.0, p.z + eps + z + 200.0), octaves)
    );

    // Curl = nabla x F
    let d = 2.0 * eps;
    return vec3f(
        (ny.z - n.z) / d - (nz.y - n.y) / d,
        (nz.x - n.x) / d - (nx.z - n.z) / d,
        (nx.y - n.y) / d - (ny.x - n.x) / d
    );
}

// 2D Curl noise (for Screen2D mode)
fn curlNoise2D(pos: vec2f, z: f32, scale: f32, octaves: i32) -> vec2f {
    let eps = 0.001;
    let p = vec3f(pos * scale, z);

    let n = fbm(p, octaves);
    let nx = fbm(vec3f(p.x + eps, p.y, p.z), octaves);
    let ny = fbm(vec3f(p.x, p.y + eps, p.z), octaves);

    let d = 2.0 * eps;
    // Perpendicular to gradient (divergence-free)
    return vec2f((ny - n) / d, -(nx - n) / d);
}

// Simple hash for turbulence
fn hash31(p: vec3f) -> f32 {
    var p3 = fract(p * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return fract((p3.x + p3.y) * p3.z);
}

// =============================================================================
// Main Compute Kernel
// =============================================================================

@compute @workgroup_size(256)
fn main(@builtin(global_invocation_id) id: vec3u) {
    let idx = id.x;
    if (idx >= u.particleCount) { return; }

    var p = particlesIn[idx];

    // Skip dead particles
    if (p.life <= 0.0) {
        particlesOut[idx] = p;
        return;
    }

    let is3D = u.is3D == 1u;
    var pos = vec3f(p.posX, p.posY, p.posZ);
    var vel = vec3f(p.velX, p.velY, p.velZ);

    // === Apply Forces ===

    // Curl noise
    if (u.curlStrength > 0.001) {
        let curlTime = u.time * u.curlSpeed + p.seed * 10.0;
        if (is3D) {
            let curl = curlNoise3D(pos, curlTime, u.curlScale, u.curlOctaves);
            vel += curl * u.curlStrength * u.dt;
        } else {
            let curl2D = curlNoise2D(pos.xy, curlTime, u.curlScale, u.curlOctaves);
            vel.x += curl2D.x * u.curlStrength * u.dt;
            vel.y += curl2D.y * u.curlStrength * u.dt;
        }
    }

    // Gravity
    vel += vec3f(u.gravityX, u.gravityY, u.gravityZ) * u.dt;

    // Drag
    if (u.drag > 0.001) {
        vel *= 1.0 - u.drag * u.dt;
    }

    // Turbulence (random jitter)
    if (u.turbulence > 0.001) {
        let seed = vec3f(p.seed * 1000.0, u.time * 100.0, f32(idx));
        let jitter = vec3f(
            hash31(seed) * 2.0 - 1.0,
            hash31(seed + vec3f(100.0, 0.0, 0.0)) * 2.0 - 1.0,
            hash31(seed + vec3f(0.0, 100.0, 0.0)) * 2.0 - 1.0
        );
        if (is3D) {
            vel += jitter * u.turbulence * u.dt;
        } else {
            vel.x += jitter.x * u.turbulence * u.dt;
            vel.y += jitter.y * u.turbulence * u.dt;
        }
    }

    // Attractor
    if (abs(u.attractorStrength) > 0.001) {
        let attPos = vec3f(u.attractorX, u.attractorY, u.attractorZ);
        let toAtt = attPos - pos;
        let dist = length(toAtt);
        if (dist > 0.01) {
            vel += normalize(toAtt) * u.attractorStrength * u.dt / dist;
        }
    }

    // === Integrate Position ===
    pos += vel * u.dt;

    // Update rotation
    p.rotation += 0.5 * u.dt;  // Simple spin

    // Update life
    p.life -= u.dt;

    // Write back
    p.posX = pos.x;
    p.posY = pos.y;
    p.posZ = pos.z;
    p.velX = vel.x;
    p.velY = vel.y;
    p.velZ = vel.z;

    particlesOut[idx] = p;
}
)";

// GPU particle struct (must match shader, 64 bytes)
struct GPUParticleData {
    float posX, posY, posZ;        // 12 bytes
    float velX, velY, velZ;        // 12 bytes (total: 24)
    float life, maxLife;           // 8 bytes (total: 32)
    float size, rotation;          // 8 bytes (total: 40)
    float colorR, colorG, colorB, colorA;  // 16 bytes (total: 56)
    float seed, _pad;              // 8 bytes (total: 64)
};
static_assert(sizeof(GPUParticleData) == 64, "GPUParticleData must be 64 bytes");

// GPU simulation uniforms (must match shader)
struct GPUSimulateUniforms {
    float dt;
    float time;
    uint32_t particleCount;
    uint32_t is3D;

    float curlStrength;
    float curlScale;
    float curlSpeed;
    int32_t curlOctaves;

    float gravityX;
    float gravityY;
    float gravityZ;
    float drag;

    float turbulence;
    float turbulenceSeed;
    float _pad0;
    float _pad1;

    float attractorX;
    float attractorY;
    float attractorZ;
    float attractorStrength;
};

// =============================================================================
// GPU Circle Rendering Shader - Reads directly from particle storage buffer
// =============================================================================

static const char* GPU_CIRCLE_RENDER_SHADER = R"(
struct Particle {
    posX: f32, posY: f32, posZ: f32,
    velX: f32, velY: f32, velZ: f32,
    life: f32, maxLife: f32,
    size: f32, rotation: f32,
    colorR: f32, colorG: f32, colorB: f32, colorA: f32,
    seed: f32, _pad: f32,
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
)";

// GPU circle render uniforms
struct GPUCircleRenderUniforms {
    float aspectRatio;
    float sizeStart;
    float sizeEnd;
    float fadeOut;
    float colorStartR, colorStartG, colorStartB, colorStartA;
    float colorEndR, colorEndG, colorEndB, colorEndA;
};

// =============================================================================
// GPU Billboard Rendering Shader - Reads directly from particle storage buffer
// =============================================================================

static const char* GPU_BILLBOARD_RENDER_SHADER = R"(
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
    let alpha = 1.0 - smoothstep(0.9, 1.0, dist);
    if (alpha < 0.01) { discard; }
    return vec4f(input.color.rgb, input.color.a * alpha);
}
)";

// GPU billboard render uniforms
struct GPUBillboardRenderUniforms {
    float viewProj[16];
    float cameraRight[3];
    float _pad1;
    float cameraUp[3];
    float _pad2;
    float sizeStart;
    float sizeEnd;
    float fadeOut;
    float _pad3;
    float colorStartR, colorStartG, colorStartB, colorStartA;
    float colorEndR, colorEndG, colorEndB, colorEndA;
};

// =============================================================================
// GPU Mesh Rendering Shader - Reads directly from particle storage buffer
// =============================================================================

static const char* GPU_MESH_RENDER_SHADER = R"(
struct Particle {
    posX: f32, posY: f32, posZ: f32,
    velX: f32, velY: f32, velZ: f32,
    life: f32, maxLife: f32,
    size: f32, rotation: f32,
    colorR: f32, colorG: f32, colorB: f32, colorA: f32,
    seed: f32, _pad: f32,
}

struct MeshUniforms {
    viewProj: mat4x4f,
    sizeStart: f32,
    sizeEnd: f32,
    fadeOut: f32,
    alignToVelocity: f32,
    colorStartR: f32, colorStartG: f32, colorStartB: f32, colorStartA: f32,
    colorEndR: f32, colorEndG: f32, colorEndB: f32, colorEndA: f32,
    meshScaleX: f32,
    meshScaleY: f32,
    meshScaleZ: f32,
    _pad: f32,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) color: vec4f,
    @location(1) worldNormal: vec3f,
}

@group(0) @binding(0) var<uniform> u: MeshUniforms;
@group(0) @binding(1) var<storage, read> particles: array<Particle>;

@vertex
fn vs_main(
    @location(0) localPos: vec3f,
    @builtin(instance_index) instanceIdx: u32
) -> VertexOutput {
    let p = particles[instanceIdx];

    // Skip dead particles
    if (p.life <= 0.0) {
        var output: VertexOutput;
        output.position = vec4f(-100.0, -100.0, -100.0, 1.0);
        output.color = vec4f(0.0);
        output.worldNormal = vec3f(0.0, 0.0, 1.0);
        return output;
    }

    // Age ratio
    let age = 1.0 - (p.life / p.maxLife);

    // Interpolate size
    let sizeScale = mix(u.sizeStart, u.sizeEnd, age);

    // Interpolate color
    let colorStart = vec4f(u.colorStartR, u.colorStartG, u.colorStartB, u.colorStartA);
    let colorEnd = vec4f(u.colorEndR, u.colorEndG, u.colorEndB, u.colorEndA);
    var color = mix(colorStart, colorEnd, age);
    color *= vec4f(p.colorR, p.colorG, p.colorB, p.colorA);

    // Fade out
    if (u.fadeOut > 0.5) {
        let fadeStart = 0.7;
        if (age > fadeStart) {
            color.a *= 1.0 - (age - fadeStart) / (1.0 - fadeStart);
        }
    }

    // Build transform matrix
    var scaledPos = localPos * vec3f(u.meshScaleX, u.meshScaleY, u.meshScaleZ) * sizeScale;
    var worldPos: vec3f;
    var normal = vec3f(0.0, 0.0, 1.0);

    if (u.alignToVelocity > 0.5) {
        // Velocity alignment - orient mesh along velocity vector
        let vel = vec3f(p.velX, p.velY, p.velZ);
        let speed = length(vel);

        if (speed > 0.001) {
            let forward = normalize(vel);

            // Find a suitable up vector (avoid parallel)
            var up = vec3f(0.0, 1.0, 0.0);
            if (abs(dot(forward, up)) > 0.99) {
                up = vec3f(0.0, 0.0, 1.0);
            }

            let right = normalize(cross(up, forward));
            up = cross(forward, right);

            // Transform local position (local Z = forward direction)
            worldPos = vec3f(p.posX, p.posY, p.posZ)
                     + right * scaledPos.x
                     + up * scaledPos.y
                     + forward * scaledPos.z;

            normal = forward;
        } else {
            worldPos = vec3f(p.posX, p.posY, p.posZ) + scaledPos;
        }
    } else {
        // Simple translation, no rotation
        worldPos = vec3f(p.posX, p.posY, p.posZ) + scaledPos;
    }

    var output: VertexOutput;
    output.position = u.viewProj * vec4f(worldPos, 1.0);
    output.color = color;
    output.worldNormal = normal;
    return output;
}

@fragment
fn fs_lit(input: VertexOutput) -> @location(0) vec4f {
    let lightDir = normalize(vec3f(1.0, 2.0, 1.5));
    let ambient = 0.3;
    let diffuse = max(dot(input.worldNormal, lightDir), 0.0);
    let lighting = ambient + diffuse * 0.7;
    return vec4f(input.color.rgb * lighting, input.color.a);
}

@fragment
fn fs_unlit(input: VertexOutput) -> @location(0) vec4f {
    return input.color;
}
)";

// GPU mesh render uniforms
struct GPUMeshRenderUniforms {
    float viewProj[16];
    float sizeStart;
    float sizeEnd;
    float fadeOut;
    float alignToVelocity;
    float colorStartR, colorStartG, colorStartB, colorStartA;
    float colorEndR, colorEndG, colorEndB, colorEndA;
    float meshScaleX;
    float meshScaleY;
    float meshScaleZ;
    float _pad;
};

// =============================================================================
// Simplex Noise (CPU version for curl noise)
// =============================================================================

namespace {

// Permutation table
static const int perm[512] = {
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
    8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
    35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
    134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
    55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,
    18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,
    250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,
    189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,
    172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
    228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,
    107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,
    138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180,
    // Repeat
    151,160,137,91,90,15,131,13,201,95,96,53,194,233,7,225,140,36,103,30,69,142,
    8,99,37,240,21,10,23,190,6,148,247,120,234,75,0,26,197,62,94,252,219,203,117,
    35,11,32,57,177,33,88,237,149,56,87,174,20,125,136,171,168,68,175,74,165,71,
    134,139,48,27,166,77,146,158,231,83,111,229,122,60,211,133,230,220,105,92,41,
    55,46,245,40,244,102,143,54,65,25,63,161,1,216,80,73,209,76,132,187,208,89,
    18,169,200,196,135,130,116,188,159,86,164,100,109,198,173,186,3,64,52,217,226,
    250,124,123,5,202,38,147,118,126,255,82,85,212,207,206,59,227,47,16,58,17,182,
    189,28,42,223,183,170,213,119,248,152,2,44,154,163,70,221,153,101,155,167,43,
    172,9,129,22,39,253,19,98,108,110,79,113,224,232,178,185,112,104,218,246,97,
    228,251,34,242,193,238,210,144,12,191,179,162,241,81,51,145,235,249,14,239,
    107,49,192,214,31,181,199,106,157,184,84,204,176,115,121,50,45,127,4,150,254,
    138,236,205,93,222,114,67,29,24,72,243,141,128,195,78,66,215,61,156,180
};

inline float grad3(int hash, float x, float y, float z) {
    int h = hash & 15;
    float u = h < 8 ? x : y;
    float v = h < 4 ? y : (h == 12 || h == 14 ? x : z);
    return ((h & 1) ? -u : u) + ((h & 2) ? -v : v);
}

inline float fade(float t) {
    return t * t * t * (t * (t * 6.0f - 15.0f) + 10.0f);
}

inline float lerp(float a, float b, float t) {
    return a + t * (b - a);
}

float snoise3(float x, float y, float z) {
    // Floor
    int X = static_cast<int>(std::floor(x)) & 255;
    int Y = static_cast<int>(std::floor(y)) & 255;
    int Z = static_cast<int>(std::floor(z)) & 255;

    x -= std::floor(x);
    y -= std::floor(y);
    z -= std::floor(z);

    float u = fade(x);
    float v = fade(y);
    float w = fade(z);

    int A = perm[X] + Y;
    int AA = perm[A] + Z;
    int AB = perm[A + 1] + Z;
    int B = perm[X + 1] + Y;
    int BA = perm[B] + Z;
    int BB = perm[B + 1] + Z;

    return lerp(lerp(lerp(grad3(perm[AA], x, y, z),
                          grad3(perm[BA], x - 1, y, z), u),
                     lerp(grad3(perm[AB], x, y - 1, z),
                          grad3(perm[BB], x - 1, y - 1, z), u), v),
                lerp(lerp(grad3(perm[AA + 1], x, y, z - 1),
                          grad3(perm[BA + 1], x - 1, y, z - 1), u),
                     lerp(grad3(perm[AB + 1], x, y - 1, z - 1),
                          grad3(perm[BB + 1], x - 1, y - 1, z - 1), u), v), w);
}

// FBM (Fractal Brownian Motion) version
float fbm3(float x, float y, float z, int octaves) {
    float result = 0.0f;
    float amplitude = 1.0f;
    float frequency = 1.0f;
    float maxAmp = 0.0f;

    for (int i = 0; i < octaves; i++) {
        result += amplitude * snoise3(x * frequency, y * frequency, z * frequency);
        maxAmp += amplitude;
        amplitude *= 0.5f;
        frequency *= 2.0f;
    }

    return result / maxAmp;
}

} // anonymous namespace

// =============================================================================
// Constructor / Destructor
// =============================================================================

ParticleSystem::ParticleSystem() {
    m_rng.seed(m_seed);
}

ParticleSystem::~ParticleSystem() {
    cleanup();
}

// =============================================================================
// Initialization
// =============================================================================

void ParticleSystem::init(Context& ctx) {
    if (!beginInit()) return;

    createOutput(ctx);
    initRenderer(ctx);

    // Load texture if specified
    if (!m_texturePath.empty()) {
        loadTexture(ctx);
    }

    // Reserve particle storage
    m_particles.reserve(static_cast<size_t>(static_cast<int>(maxParticles)));

    // Initialize GPU resources if GPU simulation mode
    if (m_simulationMode == SimulationMode::GPU) {
        initGPUBuffers(ctx.device());
        createComputePipeline(ctx.device());

        // Create GPU-direct rendering pipelines based on render mode
        if (m_renderMode == RenderMode::Circle) {
            createGPUCircleMesh(ctx.device());
            createGPUCirclePipeline(ctx.device());
        } else if (m_renderMode == RenderMode::Billboard) {
            createGPUBillboardPipeline(ctx.device());
        } else if (m_renderMode == RenderMode::Mesh) {
            createGPUMeshPipeline(ctx.device());
            // Mesh needs the builtin cube mesh
            if (m_useBuiltinMesh && !m_builtinMeshCreated) {
                createBuiltinCubeMesh(ctx.device());
            }
        }
    }
}

void ParticleSystem::initRenderer(Context& ctx) {
    // Create 2D particle renderer for Circle/Sprite modes
    if (m_renderMode == RenderMode::Circle || m_renderMode == RenderMode::Sprite) {
        if (!m_renderer2D) {
            m_renderer2D = new ParticleRenderer();
        }
        m_renderer2D->init(ctx.device(), ctx.queue());
    }

    // Create billboard pipeline for Billboard mode
    if (m_renderMode == RenderMode::Billboard) {
        createBillboardPipeline(ctx.device());
    }

    // Create mesh instanced pipeline for Mesh mode
    if (m_renderMode == RenderMode::Mesh) {
        createMeshPipeline(ctx.device());
    }
}

void ParticleSystem::loadTexture(Context& ctx) {
    // TODO: Implement texture loading
    // For now, texture modes will work without texture (solid color)
}

// =============================================================================
// Processing
// =============================================================================

void ParticleSystem::process(Context& ctx) {
    if (!isInitialized()) init(ctx);

    float dt = static_cast<float>(ctx.dt());
    m_time += dt;

    if (m_simulationMode == SimulationMode::CPU) {
        // === CPU Mode ===
        // Handle burst emission
        if (m_burstPending > 0) {
            int toEmit = std::min(m_burstPending,
                                  static_cast<int>(maxParticles) - static_cast<int>(m_particles.size()));
            for (int i = 0; i < toEmit; i++) {
                emitParticle();
            }
            m_burstPending = 0;
        }

        // Continuous emission
        m_emitAccumulator += static_cast<float>(emitRate) * dt;
        while (m_emitAccumulator >= 1.0f &&
               static_cast<int>(m_particles.size()) < static_cast<int>(maxParticles)) {
            emitParticle();
            m_emitAccumulator -= 1.0f;
        }

        // CPU simulation
        updateParticlesCPU(dt);

        // Remove dead particles
        m_particles.erase(
            std::remove_if(m_particles.begin(), m_particles.end(),
                [](const Particle& p) { return p.life <= 0.0f; }),
            m_particles.end()
        );
    } else {
        // === GPU Mode ===
        auto device = ctx.device();
        auto queue = ctx.queue();

        // Emit particles to GPU buffer
        emitParticlesGPU(device, queue, dt);

        // Run GPU compute simulation
        dispatchComputeSimulation(ctx, dt);
    }

    // Render based on mode
    if (m_simulationMode == SimulationMode::GPU) {
        // GPU-direct rendering paths (optimal - no CPU sync)
        switch (m_renderMode) {
            case RenderMode::Circle:
                renderCirclesGPU(ctx);
                break;
            case RenderMode::Billboard:
                renderBillboardsGPU(ctx);
                break;
            case RenderMode::Mesh:
                renderMeshesGPU(ctx);
                break;
            case RenderMode::Sprite:
                // Sprite mode falls back to CPU for now (texture handling)
                renderSprites(ctx);
                break;
        }
    } else {
        // CPU-based rendering
        switch (m_renderMode) {
            case RenderMode::Circle:
                renderCircles(ctx);
                break;
            case RenderMode::Sprite:
                renderSprites(ctx);
                break;
            case RenderMode::Billboard:
                renderBillboards(ctx);
                break;
            case RenderMode::Mesh:
                renderMeshes(ctx);
                break;
        }
    }

    didCook();
}

// =============================================================================
// Particle Emission
// =============================================================================

void ParticleSystem::emitParticle() {
    Particle p;
    p.index = m_particleIndex++;
    p.seed = std::uniform_real_distribution<float>(0.0f, 1.0f)(m_rng);

    // Position based on emitter shape
    p.position = getEmitterPosition();

    // Velocity
    p.velocity = getInitialVelocity(p.position);

    // Lifetime with variation
    float lifeRange = static_cast<float>(lifeMax) - static_cast<float>(lifeMin);
    p.maxLife = static_cast<float>(lifeMin) + std::uniform_real_distribution<float>(0.0f, 1.0f)(m_rng) * lifeRange;
    p.life = p.maxLife;

    // Size with variation
    float sizeVar = static_cast<float>(sizeVariation);
    p.size = static_cast<float>(sizeStart) * (1.0f + sizeVar * std::uniform_real_distribution<float>(-1.0f, 1.0f)(m_rng));

    // Rotation
    p.rotation = std::uniform_real_distribution<float>(0.0f, 2.0f * static_cast<float>(M_PI))(m_rng);
    p.angularVelocity = m_spinSpeed * std::uniform_real_distribution<float>(0.5f, 1.5f)(m_rng);

    // Color
    p.color = getSpawnColor();

    // Spritesheet frame offset
    if (m_useSpriteSheet) {
        p.frameOffset = std::uniform_int_distribution<int>(0, m_spriteFrameCount - 1)(m_rng);
    }

    m_particles.push_back(p);
}

glm::vec3 ParticleSystem::getEmitterPosition() {
    glm::vec3 center(emitterPosition.x(), emitterPosition.y(), emitterPosition.z());
    float size = static_cast<float>(emitterSize);

    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    bool is3D = (m_particleSpace == ParticleSpace::World3D);

    switch (m_emitterShape) {
        case PsEmitterShape::Point:
            return center;

        case PsEmitterShape::Line: {
            float offset = dist(m_rng) * size * 0.5f;
            glm::vec3 dir = glm::normalize(glm::vec3(emitterDirection.x(), emitterDirection.y(), emitterDirection.z()));
            return center + dir * offset;
        }

        case PsEmitterShape::Ring: {
            float angle = dist01(m_rng) * 2.0f * static_cast<float>(M_PI);
            if (is3D) {
                // Horizontal ring in XZ plane
                return center + size * glm::vec3(std::cos(angle), 0.0f, std::sin(angle));
            } else {
                return center + size * glm::vec3(std::cos(angle), std::sin(angle), 0.0f);
            }
        }

        case PsEmitterShape::Disc: {
            float angle = dist01(m_rng) * 2.0f * static_cast<float>(M_PI);
            float radius = std::sqrt(dist01(m_rng)) * size;
            if (is3D) {
                return center + radius * glm::vec3(std::cos(angle), 0.0f, std::sin(angle));
            } else {
                return center + radius * glm::vec3(std::cos(angle), std::sin(angle), 0.0f);
            }
        }

        case PsEmitterShape::Rectangle:
            return center + glm::vec3(dist(m_rng), dist(m_rng), 0.0f) * size * 0.5f;

        case PsEmitterShape::Sphere: {
            // Uniform distribution on sphere surface
            float theta = dist01(m_rng) * 2.0f * static_cast<float>(M_PI);
            float phi = std::acos(2.0f * dist01(m_rng) - 1.0f);
            float r = std::cbrt(dist01(m_rng)) * size;
            return center + glm::vec3(
                r * std::sin(phi) * std::cos(theta),
                r * std::sin(phi) * std::sin(theta),
                r * std::cos(phi)
            );
        }

        case PsEmitterShape::Box:
            return center + glm::vec3(dist(m_rng), dist(m_rng), dist(m_rng)) * size * 0.5f;

        case PsEmitterShape::Cone: {
            glm::vec3 dir = glm::normalize(glm::vec3(emitterDirection.x(), emitterDirection.y(), emitterDirection.z()));
            float angle = static_cast<float>(coneAngle) * static_cast<float>(M_PI) / 180.0f;
            float height = dist01(m_rng) * size;
            float coneRadius = height * std::tan(angle * 0.5f);
            float a = dist01(m_rng) * 2.0f * static_cast<float>(M_PI);
            float r = std::sqrt(dist01(m_rng)) * coneRadius;

            // Create basis vectors perpendicular to direction
            glm::vec3 up = std::abs(dir.y) < 0.999f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            glm::vec3 right = glm::normalize(glm::cross(up, dir));
            glm::vec3 forward = glm::cross(dir, right);

            return center + dir * height + right * r * std::cos(a) + forward * r * std::sin(a);
        }

        default:
            return center;
    }
}

glm::vec3 ParticleSystem::getInitialVelocity(const glm::vec3& spawnPos) {
    glm::vec3 vel(initialVelocity.x(), initialVelocity.y(), initialVelocity.z());
    glm::vec3 emitCenter(emitterPosition.x(), emitterPosition.y(), emitterPosition.z());
    bool is3D = (m_particleSpace == ParticleSpace::World3D);

    // Radial velocity (away from center)
    float radVel = static_cast<float>(radialVelocity);
    if (std::abs(radVel) > 0.0001f) {
        glm::vec3 dir = spawnPos - emitCenter;
        float len = glm::length(dir);
        if (len > 0.001f) {
            vel += glm::normalize(dir) * radVel;
        } else {
            // Random direction if at center
            std::uniform_real_distribution<float> dist(0.0f, 2.0f * static_cast<float>(M_PI));
            float theta = dist(m_rng);
            if (is3D) {
                float phi = std::acos(2.0f * std::uniform_real_distribution<float>(0.0f, 1.0f)(m_rng) - 1.0f);
                vel += radVel * glm::vec3(
                    std::sin(phi) * std::cos(theta),
                    std::sin(phi) * std::sin(theta),
                    std::cos(phi)
                );
            } else {
                vel += radVel * glm::vec3(std::cos(theta), std::sin(theta), 0.0f);
            }
        }
    }

    // Apply spread
    float spreadAngle = static_cast<float>(spread) * static_cast<float>(M_PI) / 180.0f;
    if (spreadAngle > 0.0f) {
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        float angle1 = dist(m_rng) * spreadAngle * 0.5f;
        float angle2 = is3D ? dist(m_rng) * spreadAngle * 0.5f : 0.0f;

        // Rotate velocity by spread angles
        float len = glm::length(vel);
        if (len > 0.001f) {
            glm::vec3 dir = vel / len;

            // Simple 2D rotation in XY plane for 2D mode
            if (!is3D) {
                float ca = std::cos(angle1);
                float sa = std::sin(angle1);
                dir = glm::vec3(dir.x * ca - dir.y * sa, dir.x * sa + dir.y * ca, 0.0f);
            } else {
                // For 3D, apply two rotations
                // Rotation around Y axis
                float ca = std::cos(angle1);
                float sa = std::sin(angle1);
                dir = glm::vec3(dir.x * ca + dir.z * sa, dir.y, -dir.x * sa + dir.z * ca);
                // Rotation around X axis
                ca = std::cos(angle2);
                sa = std::sin(angle2);
                dir = glm::vec3(dir.x, dir.y * ca - dir.z * sa, dir.y * sa + dir.z * ca);
            }

            vel = dir * len;
        }
    }

    // Velocity variation
    float velVar = static_cast<float>(velocityVariation);
    if (velVar > 0.0f) {
        float mult = 1.0f + velVar * std::uniform_real_distribution<float>(-1.0f, 1.0f)(m_rng);
        vel *= mult;
    }

    return vel;
}

glm::vec4 ParticleSystem::getSpawnColor() {
    switch (m_colorMode) {
        case PsColorMode::Solid:
        case PsColorMode::Gradient:
            return glm::vec4(colorStart.r(), colorStart.g(), colorStart.b(), colorStart.a());

        case PsColorMode::Rainbow: {
            float hue = std::fmod(m_particleIndex * 0.05f, 1.0f);
            return hsvToRgb(hue, 0.8f, 1.0f);
        }

        case PsColorMode::Random: {
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);
            return glm::vec4(dist(m_rng), dist(m_rng), dist(m_rng), 1.0f);
        }

        default:
            return glm::vec4(colorStart.r(), colorStart.g(), colorStart.b(), colorStart.a());
    }
}

// =============================================================================
// CPU Simulation
// =============================================================================

void ParticleSystem::updateParticlesCPU(float dt) {
    glm::vec3 grav(gravity.x(), gravity.y(), gravity.z());
    float dragVal = static_cast<float>(drag);
    float turbVal = static_cast<float>(turbulence);
    glm::vec3 attPos(attractorPosition.x(), attractorPosition.y(), attractorPosition.z());
    float attStr = static_cast<float>(attractorStrength);
    float curlStr = static_cast<float>(curlStrength);

    bool is3D = (m_particleSpace == ParticleSpace::World3D);

    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    for (auto& p : m_particles) {
        // Apply gravity
        if (is3D) {
            p.velocity += grav * dt;
        } else {
            // 2D: gravity is just Y component
            p.velocity.y += grav.y * dt;
        }

        // Apply drag
        if (dragVal > 0.0f) {
            p.velocity *= (1.0f - dragVal * dt);
        }

        // Apply turbulence
        if (turbVal > 0.0f) {
            if (is3D) {
                p.velocity += glm::vec3(dist(m_rng), dist(m_rng), dist(m_rng)) * turbVal * dt;
            } else {
                p.velocity += glm::vec3(dist(m_rng), dist(m_rng), 0.0f) * turbVal * dt;
            }
        }

        // Apply attractor
        if (std::abs(attStr) > 0.0001f) {
            glm::vec3 toAtt = attPos - p.position;
            float distance = glm::length(toAtt);
            if (distance > 0.01f) {
                p.velocity += glm::normalize(toAtt) * attStr * dt / distance;
            }
        }

        // Apply curl noise
        if (curlStr > 0.0001f) {
            glm::vec3 curl = computeCurlNoise(p.position, m_time + p.seed * 10.0f);
            p.velocity += curl * curlStr * dt;
        }

        // Apply custom force callback
        if (m_forceCallback) {
            glm::vec3 customForce = m_forceCallback(p, m_time);
            p.velocity += customForce * dt;
        }

        // Update position
        p.position += p.velocity * dt;

        // Update rotation
        p.rotation += p.angularVelocity * dt;

        // Update life
        p.life -= dt;
    }
}

glm::vec3 ParticleSystem::computeCurlNoise(const glm::vec3& pos, float time) {
    float scale = static_cast<float>(curlScale);
    float speed = static_cast<float>(curlSpeed);
    int octaves = static_cast<int>(curlOctaves);

    const float e = 0.001f;
    glm::vec3 p = pos * scale;
    float t = time * speed;

    bool is3D = (m_particleSpace == ParticleSpace::World3D);

    // Potential function using FBM noise
    auto potential = [&](glm::vec3 q) -> glm::vec3 {
        return glm::vec3(
            fbm3(q.x + t, q.y + 100.0f, q.z, octaves),
            fbm3(q.x + 200.0f, q.y + t, q.z + 100.0f, octaves),
            is3D ? fbm3(q.x + 100.0f, q.y + 300.0f, q.z + t, octaves) : 0.0f
        );
    };

    // Finite differences to compute curl
    glm::vec3 dx = potential(p + glm::vec3(e, 0, 0)) - potential(p - glm::vec3(e, 0, 0));
    glm::vec3 dy = potential(p + glm::vec3(0, e, 0)) - potential(p - glm::vec3(0, e, 0));

    if (is3D) {
        glm::vec3 dz = potential(p + glm::vec3(0, 0, e)) - potential(p - glm::vec3(0, 0, e));
        float d = 2.0f * e;
        return glm::vec3(
            dy.z / d - dz.y / d,
            dz.x / d - dx.z / d,
            dx.y / d - dy.x / d
        );
    } else {
        // 2D curl: perpendicular to gradient
        float d = 2.0f * e;
        return glm::vec3(dy.x / d, -dx.x / d, 0.0f);
    }
}

// =============================================================================
// Color & Size Computation
// =============================================================================

glm::vec4 ParticleSystem::computeParticleColor(const Particle& p, float age) {
    glm::vec4 color;
    glm::vec4 startColor(colorStart.r(), colorStart.g(), colorStart.b(), colorStart.a());
    glm::vec4 endColor(colorEnd.r(), colorEnd.g(), colorEnd.b(), colorEnd.a());

    switch (m_colorMode) {
        case PsColorMode::Solid:
            color = startColor;
            break;

        case PsColorMode::Gradient:
            color = glm::mix(startColor, endColor, age);
            break;

        case PsColorMode::Rainbow: {
            float hue = std::fmod(p.index * 0.05f, 1.0f);
            color = hsvToRgb(hue, 0.8f, 1.0f);
            break;
        }

        case PsColorMode::Random:
            color = p.color;  // Color assigned at spawn
            break;

        default:
            color = startColor;
    }

    // Apply fade in
    float alpha = color.a;
    float fadeInT = static_cast<float>(fadeInTime);
    if (fadeInT > 0.0f && age < fadeInT) {
        alpha *= age / fadeInT;
    }

    // Apply fade out
    if (static_cast<bool>(fadeOut)) {
        float lifeRatio = p.life / p.maxLife;
        alpha *= lifeRatio;
    }

    color.a = alpha;
    return color;
}

float ParticleSystem::computeParticleSize(const Particle& p, float age) {
    float start = static_cast<float>(sizeStart);
    float end = static_cast<float>(sizeEnd);
    float size = glm::mix(start, end, age);

    // Apply per-particle size variation
    float sizeRatio = p.size / start;
    return size * sizeRatio;
}

glm::vec4 ParticleSystem::hsvToRgb(float h, float s, float v) {
    float c = v * s;
    float x = c * (1.0f - std::abs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    float m = v - c;

    glm::vec3 rgb;
    if (h < 1.0f/6.0f)      rgb = {c, x, 0};
    else if (h < 2.0f/6.0f) rgb = {x, c, 0};
    else if (h < 3.0f/6.0f) rgb = {0, c, x};
    else if (h < 4.0f/6.0f) rgb = {0, x, c};
    else if (h < 5.0f/6.0f) rgb = {x, 0, c};
    else                    rgb = {c, 0, x};

    return glm::vec4(rgb + glm::vec3(m), 1.0f);
}

// =============================================================================
// Rendering - Circles (2D)
// =============================================================================

void ParticleSystem::renderCircles(Context& ctx) {
    if (!m_renderer2D || !m_renderer2D->isInitialized()) return;

    std::vector<Circle2D> circles;
    circles.reserve(m_particles.size());

    for (const auto& p : m_particles) {
        float age = 1.0f - (p.life / p.maxLife);
        float size = computeParticleSize(p, age);
        glm::vec4 color = computeParticleColor(p, age);

        circles.emplace_back(glm::vec2(p.position.x, p.position.y), size, color);
    }

    glm::vec4 bgColor(clearColor.r(), clearColor.g(), clearColor.b(), clearColor.a());
    m_renderer2D->renderCircles(ctx, circles, m_outputView, m_width, m_height, bgColor);
}

// =============================================================================
// Rendering - Sprites (2D)
// =============================================================================

void ParticleSystem::renderSprites(Context& ctx) {
    if (!m_renderer2D || !m_renderer2D->isInitialized()) return;

    // Fall back to circles if no texture loaded
    if (!m_spriteTextureView) {
        renderCircles(ctx);
        return;
    }

    std::vector<Sprite2D> sprites;
    sprites.reserve(m_particles.size());

    for (const auto& p : m_particles) {
        float age = 1.0f - (p.life / p.maxLife);
        float size = computeParticleSize(p, age);
        glm::vec4 color = computeParticleColor(p, age);

        Sprite2D sprite;
        sprite.position = glm::vec2(p.position.x, p.position.y);
        sprite.size = size;
        sprite.rotation = p.rotation;
        sprite.color = color;

        // Spritesheet UV calculation
        if (m_useSpriteSheet) {
            int frame;
            if (m_spriteAnimateByLife) {
                frame = static_cast<int>(age * m_spriteFrameCount) % m_spriteFrameCount;
            } else {
                frame = (p.frameOffset + static_cast<int>(m_time * m_spriteFPS)) % m_spriteFrameCount;
            }

            int col = frame % m_spriteSheetCols;
            int row = frame / m_spriteSheetCols;

            sprite.uvScale = glm::vec2(1.0f / m_spriteSheetCols, 1.0f / m_spriteSheetRows);
            sprite.uvOffset = glm::vec2(col * sprite.uvScale.x, row * sprite.uvScale.y);
        } else {
            sprite.uvOffset = glm::vec2(0.0f);
            sprite.uvScale = glm::vec2(1.0f);
        }

        sprites.push_back(sprite);
    }

    glm::vec4 bgColor(clearColor.r(), clearColor.g(), clearColor.b(), clearColor.a());
    m_renderer2D->renderSprites(ctx, sprites, m_spriteTextureView, m_outputView,
                                m_width, m_height, bgColor);
}

// =============================================================================
// Rendering - Billboards (3D camera-facing)
// =============================================================================

void ParticleSystem::renderBillboards(Context& ctx) {
    // Billboard rendering requires camera data
    if (!m_hasCamera) {
        // Fall back to circles if no camera set
        renderCircles(ctx);
        return;
    }

    if (!m_billboardPipeline) {
        createBillboardPipeline(ctx.device());
    }

    if (m_particles.empty()) return;

    // Get camera data from stored matrices/vectors
    glm::mat4 viewProj = m_hasViewProj ? m_viewProjMatrix : (m_projMatrix * m_viewMatrix);

    // Optional: depth sort (back-to-front for correct transparency)
    if (m_depthSort) {
        sortByDepth(m_viewMatrix);
    }

    // Build instance data
    size_t particleCount = m_particles.size();
    ensureInstanceCapacity(ctx.device(), particleCount);

    std::vector<BillboardInstance> instances(particleCount);
    for (size_t i = 0; i < particleCount; i++) {
        size_t idx = m_depthSort ? m_sortedIndices[i] : i;
        const auto& p = m_particles[idx];

        float age = 1.0f - (p.life / p.maxLife);
        float size = computeParticleSize(p, age);
        glm::vec4 color = computeParticleColor(p, age);

        BillboardInstance& inst = instances[i];
        inst.position[0] = p.position.x;
        inst.position[1] = p.position.y;
        inst.position[2] = p.position.z;
        inst.size = size;
        inst.color[0] = color.r;
        inst.color[1] = color.g;
        inst.color[2] = color.b;
        inst.color[3] = color.a;
        inst.rotation = p.rotation;

        // Spritesheet frame
        if (m_useSpriteSheet) {
            int frame;
            if (m_spriteAnimateByLife) {
                frame = static_cast<int>(age * m_spriteFrameCount) % m_spriteFrameCount;
            } else {
                frame = (p.frameOffset + static_cast<int>(m_time * m_spriteFPS)) % m_spriteFrameCount;
            }
            inst.frameIndex = static_cast<float>(frame);
        } else {
            inst.frameIndex = 0.0f;
        }

        inst._pad[0] = 0.0f;
        inst._pad[1] = 0.0f;
    }

    // Upload instance data
    wgpuQueueWriteBuffer(ctx.queue(), m_billboardInstanceBuffer, 0,
                         instances.data(), particleCount * sizeof(BillboardInstance));

    // Build and upload uniforms
    BillboardUniforms uniforms = {};
    std::memcpy(uniforms.viewProj, &viewProj[0][0], 16 * sizeof(float));
    uniforms.cameraRight[0] = m_cameraRight.x;
    uniforms.cameraRight[1] = m_cameraRight.y;
    uniforms.cameraRight[2] = m_cameraRight.z;
    uniforms._pad1 = 0.0f;
    uniforms.cameraUp[0] = m_cameraUp.x;
    uniforms.cameraUp[1] = m_cameraUp.y;
    uniforms.cameraUp[2] = m_cameraUp.z;
    uniforms._pad2 = 0.0f;
    uniforms.spriteSheetCols = static_cast<float>(m_spriteSheetCols);
    uniforms.spriteSheetRows = static_cast<float>(m_spriteSheetRows);
    uniforms.spriteFrameCount = static_cast<float>(m_spriteFrameCount);
    uniforms._pad3 = 0.0f;

    wgpuQueueWriteBuffer(ctx.queue(), m_billboardUniformBuffer, 0,
                         &uniforms, sizeof(BillboardUniforms));

    // Create bind group with current texture
    WGPUTextureView texView = m_spriteTextureView ? m_spriteTextureView : m_spriteTextureView;
    if (!texView) return;

    WGPUBindGroupEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].buffer = m_billboardUniformBuffer;
    entries[0].size = sizeof(BillboardUniforms);
    entries[1].binding = 1;
    entries[1].sampler = m_sampler;
    entries[2].binding = 2;
    entries[2].textureView = texView;

    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.layout = m_billboardBindGroupLayout;
    bindGroupDesc.entryCount = 3;
    bindGroupDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindGroupDesc);

    // Use shared command encoder for batched submission
    WGPUCommandEncoder encoder = ctx.gpuEncoder();

    // Begin render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = m_outputView;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {
        clearColor.r(), clearColor.g(), clearColor.b(), clearColor.a()
    };

    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    // Draw instanced billboards
    wgpuRenderPassEncoderSetPipeline(pass, m_billboardPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_billboardInstanceBuffer, 0,
                                          particleCount * sizeof(BillboardInstance));

    // 6 vertices per quad (2 triangles), particleCount instances
    wgpuRenderPassEncoderDraw(pass, 6, static_cast<uint32_t>(particleCount), 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(bindGroup);
}

// =============================================================================
// Rendering - Meshes (3D instanced)
// =============================================================================

void ParticleSystem::renderMeshes(Context& ctx) {
    // Mesh rendering requires camera
    if (!m_hasCamera) {
        // Fall back to circles
        renderCircles(ctx);
        return;
    }

    // Create builtin cube mesh if using default mesh
    if (m_useBuiltinMesh && !m_builtinMeshCreated) {
        createBuiltinCubeMesh(ctx.device());
    }

    // Upload deferred builtin mesh data
    if (m_builtinMeshCreated && !m_builtinMeshUploaded && !m_builtinVertexData.empty()) {
        wgpuQueueWriteBuffer(ctx.queue(), m_meshVertexBuffer, 0,
                             m_builtinVertexData.data(), m_builtinVertexData.size());
        wgpuQueueWriteBuffer(ctx.queue(), m_meshIndexBuffer, 0,
                             m_builtinIndexData.data(), m_builtinIndexData.size());
        m_builtinMeshUploaded = true;
        // Clear the CPU-side data
        m_builtinVertexData.clear();
        m_builtinIndexData.clear();
    }

    // Need mesh buffers at this point
    if (!m_meshVertexBuffer) {
        renderCircles(ctx);
        return;
    }

    if (!m_meshPipeline) {
        createMeshPipeline(ctx.device());
    }

    if (m_particles.empty()) return;

    // Get view-projection matrix
    glm::mat4 viewProj = m_hasViewProj ? m_viewProjMatrix : (m_projMatrix * m_viewMatrix);

    // Optional: depth sort
    if (m_depthSort) {
        sortByDepth(m_viewMatrix);
    }

    // Build instance data
    size_t particleCount = m_particles.size();
    ensureMeshInstanceCapacity(ctx.device(), particleCount);

    std::vector<MeshInstance> instances(particleCount);
    for (size_t i = 0; i < particleCount; i++) {
        size_t idx = m_depthSort ? m_sortedIndices[i] : i;
        const auto& p = m_particles[idx];

        float age = 1.0f - (p.life / p.maxLife);
        float size = computeParticleSize(p, age);
        glm::vec4 color = computeParticleColor(p, age);

        // Build transform matrix
        glm::mat4 transform = glm::mat4(1.0f);

        // Translation
        transform = glm::translate(transform, p.position);

        // Rotation: align to velocity if enabled
        if (m_alignToVelocity && glm::length(p.velocity) > 0.0001f) {
            glm::vec3 forward = glm::normalize(p.velocity);
            glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
            if (std::abs(glm::dot(forward, up)) > 0.99f) {
                up = glm::vec3(1.0f, 0.0f, 0.0f);
            }
            glm::vec3 right = glm::normalize(glm::cross(up, forward));
            up = glm::cross(forward, right);

            // Rotation matrix (right, up, forward as columns)
            glm::mat3 rot(right, up, forward);
            transform = transform * glm::mat4(rot);
        }

        // Scale (optionally stretch along velocity)
        float speed = glm::length(p.velocity);
        float lengthScale = m_alignToVelocity ? (1.0f + speed * 0.5f) : 1.0f;
        transform = glm::scale(transform, glm::vec3(size, size, size * lengthScale));

        // Copy to instance data (column-major)
        MeshInstance& inst = instances[i];
        std::memcpy(inst.transform, &transform[0][0], 16 * sizeof(float));
        inst.color[0] = color.r;
        inst.color[1] = color.g;
        inst.color[2] = color.b;
        inst.color[3] = color.a;
    }

    // Upload instance data
    wgpuQueueWriteBuffer(ctx.queue(), m_meshInstanceBuffer, 0,
                         instances.data(), particleCount * sizeof(MeshInstance));

    // Build and upload uniforms
    MeshUniforms uniforms = {};
    std::memcpy(uniforms.viewProj, &viewProj[0][0], 16 * sizeof(float));
    wgpuQueueWriteBuffer(ctx.queue(), m_meshUniformBuffer, 0,
                         &uniforms, sizeof(MeshUniforms));

    // Create bind group
    WGPUBindGroupEntry entry = {};
    entry.binding = 0;
    entry.buffer = m_meshUniformBuffer;
    entry.size = sizeof(MeshUniforms);

    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.layout = m_meshBindGroupLayout;
    bindGroupDesc.entryCount = 1;
    bindGroupDesc.entries = &entry;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindGroupDesc);

    // Use shared command encoder
    WGPUCommandEncoder encoder = ctx.gpuEncoder();

    // Begin render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = m_outputView;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {
        clearColor.r(), clearColor.g(), clearColor.b(), clearColor.a()
    };

    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &renderPassDesc);

    // Set pipeline and bind group
    wgpuRenderPassEncoderSetPipeline(pass, m_meshPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);

    // Set vertex buffers: slot 0 = mesh vertices, slot 1 = instance data
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_meshVertexBuffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, m_meshInstanceBuffer, 0,
                                          particleCount * sizeof(MeshInstance));

    // Draw indexed or non-indexed
    if (m_meshIndexBuffer && m_meshIndexCount > 0) {
        wgpuRenderPassEncoderSetIndexBuffer(pass, m_meshIndexBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);
        wgpuRenderPassEncoderDrawIndexed(pass, m_meshIndexCount, static_cast<uint32_t>(particleCount), 0, 0, 0);
    } else {
        // Assume 36 vertices for a cube if no index buffer
        wgpuRenderPassEncoderDraw(pass, 36, static_cast<uint32_t>(particleCount), 0, 0);
    }

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(bindGroup);
}

// =============================================================================
// GPU Simulation Stubs
// =============================================================================

void ParticleSystem::initGPUBuffers(WGPUDevice device) {
    int count = static_cast<int>(maxParticles);
    m_allocatedParticlesGPU = count;

    // Create ping-pong particle buffers
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = count * sizeof(GPUParticleData);
    bufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst | WGPUBufferUsage_CopySrc;
    bufferDesc.mappedAtCreation = false;

    m_particleBufferGPU[0] = wgpuDeviceCreateBuffer(device, &bufferDesc);
    m_particleBufferGPU[1] = wgpuDeviceCreateBuffer(device, &bufferDesc);

    // Initialize with zeros (all dead particles)
    std::vector<GPUParticleData> zeros(count);
    memset(zeros.data(), 0, zeros.size() * sizeof(GPUParticleData));
    WGPUQueue queue = wgpuDeviceGetQueue(device);
    wgpuQueueWriteBuffer(queue, m_particleBufferGPU[0], 0, zeros.data(), zeros.size() * sizeof(GPUParticleData));
    wgpuQueueWriteBuffer(queue, m_particleBufferGPU[1], 0, zeros.data(), zeros.size() * sizeof(GPUParticleData));

    // Create uniform buffer for simulation
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.size = sizeof(GPUSimulateUniforms);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_computeUniformBuffer = wgpuDeviceCreateBuffer(device, &uniformDesc);

    m_readBufferIndex = 0;
    m_aliveCountGPU = 0;
}

void ParticleSystem::createComputePipeline(WGPUDevice device) {
    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(GPU_SIMULATE_SHADER);

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

    // Bind group layout: uniform, storage read, storage read_write
    WGPUBindGroupLayoutEntry entries[3] = {};

    // Uniforms
    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Compute;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;

    // Particles in (read-only)
    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Compute;
    entries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

    // Particles out (read-write)
    entries[2].binding = 2;
    entries[2].visibility = WGPUShaderStage_Compute;
    entries[2].buffer.type = WGPUBufferBindingType_Storage;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 3;
    layoutDesc.entries = entries;
    m_computeBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

    // Create bind groups for both buffer configurations (ping-pong)
    for (int i = 0; i < 2; i++) {
        int readIdx = i;
        int writeIdx = 1 - i;

        WGPUBindGroupEntry bindEntries[3] = {};
        bindEntries[0].binding = 0;
        bindEntries[0].buffer = m_computeUniformBuffer;
        bindEntries[0].size = sizeof(GPUSimulateUniforms);

        bindEntries[1].binding = 1;
        bindEntries[1].buffer = m_particleBufferGPU[readIdx];
        bindEntries[1].size = m_allocatedParticlesGPU * sizeof(GPUParticleData);

        bindEntries[2].binding = 2;
        bindEntries[2].buffer = m_particleBufferGPU[writeIdx];
        bindEntries[2].size = m_allocatedParticlesGPU * sizeof(GPUParticleData);

        WGPUBindGroupDescriptor bindDesc = {};
        bindDesc.layout = m_computeBindGroupLayout;
        bindDesc.entryCount = 3;
        bindDesc.entries = bindEntries;
        m_computeBindGroup[i] = wgpuDeviceCreateBindGroup(device, &bindDesc);
    }

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    WGPUBindGroupLayout layouts[] = { m_computeBindGroupLayout };
    pipelineLayoutDesc.bindGroupLayouts = layouts;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Compute pipeline
    WGPUComputePipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.compute.module = module;
    pipelineDesc.compute.entryPoint = toStringView("main");
    m_computePipeline = wgpuDeviceCreateComputePipeline(device, &pipelineDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(module);
}

void ParticleSystem::emitParticlesGPU(WGPUDevice device, WGPUQueue queue, float dt) {
    // Calculate how many particles to emit this frame
    m_emitAccumulator += static_cast<float>(emitRate) * dt;
    int toEmit = static_cast<int>(m_emitAccumulator);
    m_emitAccumulator -= toEmit;

    // Add pending burst
    toEmit += m_burstPending;
    m_burstPending = 0;

    // Limit to available slots
    int maxCount = m_allocatedParticlesGPU;
    toEmit = std::min(toEmit, maxCount);

    if (toEmit <= 0) return;

    // Generate particles on CPU
    std::vector<GPUParticleData> emissionStaging(toEmit);

    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    float lifeMinVal = static_cast<float>(lifeMin);
    float lifeMaxVal = static_cast<float>(lifeMax);

    for (int i = 0; i < toEmit; i++) {
        GPUParticleData& p = emissionStaging[i];

        // Position from emitter
        glm::vec3 pos = getEmitterPosition();
        p.posX = pos.x;
        p.posY = pos.y;
        p.posZ = pos.z;

        // Velocity
        glm::vec3 vel = getInitialVelocity(pos);
        p.velX = vel.x;
        p.velY = vel.y;
        p.velZ = vel.z;

        // Life
        p.life = lifeMinVal + dist01(m_rng) * (lifeMaxVal - lifeMinVal);
        p.maxLife = p.life;

        // Size
        float sizeVar = static_cast<float>(sizeVariation);
        p.size = static_cast<float>(sizeStart) * (1.0f + sizeVar * std::uniform_real_distribution<float>(-1.0f, 1.0f)(m_rng));

        // Rotation
        p.rotation = dist01(m_rng) * 2.0f * static_cast<float>(M_PI);

        // Color
        glm::vec4 col = getSpawnColor();
        p.colorR = col.r;
        p.colorG = col.g;
        p.colorB = col.b;
        p.colorA = col.a;

        // Random seed
        p.seed = dist01(m_rng);
        p._pad = 0.0f;

        m_particleIndex++;
    }

    // Upload to GPU at next available slot (circular)
    static int totalEmitted = 0;
    int writeOffset = totalEmitted % maxCount;
    int writeCount = std::min(toEmit, maxCount - writeOffset);

    // Write to the buffer that will be READ next frame
    WGPUBuffer targetBuffer = m_particleBufferGPU[m_readBufferIndex];

    wgpuQueueWriteBuffer(queue, targetBuffer,
                         writeOffset * sizeof(GPUParticleData),
                         emissionStaging.data(),
                         writeCount * sizeof(GPUParticleData));

    // Handle wrap-around
    if (writeCount < toEmit) {
        int remaining = toEmit - writeCount;
        wgpuQueueWriteBuffer(queue, targetBuffer, 0,
                             emissionStaging.data() + writeCount,
                             remaining * sizeof(GPUParticleData));
    }

    totalEmitted += toEmit;
    m_aliveCountGPU = std::min(totalEmitted, maxCount);
}

void ParticleSystem::dispatchComputeSimulation(Context& ctx, float dt) {
    if (m_aliveCountGPU == 0) return;

    auto device = ctx.device();
    auto queue = ctx.queue();

    // Update uniforms
    GPUSimulateUniforms uniforms = {};
    uniforms.dt = dt;
    uniforms.time = m_time;
    uniforms.particleCount = static_cast<uint32_t>(m_aliveCountGPU);
    uniforms.is3D = (m_particleSpace == ParticleSpace::World3D) ? 1u : 0u;

    uniforms.curlStrength = static_cast<float>(curlStrength);
    uniforms.curlScale = static_cast<float>(curlScale);
    uniforms.curlSpeed = static_cast<float>(curlSpeed);
    uniforms.curlOctaves = static_cast<int>(curlOctaves);

    uniforms.gravityX = gravity.x();
    uniforms.gravityY = gravity.y();
    uniforms.gravityZ = gravity.z();
    uniforms.drag = static_cast<float>(drag);

    uniforms.turbulence = static_cast<float>(turbulence);
    uniforms.turbulenceSeed = m_time;

    uniforms.attractorX = attractorPosition.x();
    uniforms.attractorY = attractorPosition.y();
    uniforms.attractorZ = attractorPosition.z();
    uniforms.attractorStrength = static_cast<float>(attractorStrength);

    wgpuQueueWriteBuffer(queue, m_computeUniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create a separate encoder for compute and submit immediately
    WGPUCommandEncoderDescriptor encDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);

    WGPUComputePassDescriptor passDesc = {};
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

    wgpuComputePassEncoderSetPipeline(pass, m_computePipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, m_computeBindGroup[m_readBufferIndex], 0, nullptr);

    // Dispatch workgroups (256 threads each)
    uint32_t workgroups = (m_aliveCountGPU + 255) / 256;
    wgpuComputePassEncoderDispatchWorkgroups(pass, workgroups, 1, 1);

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);

    // Finish and submit compute commands
    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    // Swap buffers - render will read from what compute wrote to
    m_readBufferIndex = 1 - m_readBufferIndex;
}

void ParticleSystem::readbackParticleCount(WGPUDevice device, WGPUQueue queue) {
    // For now, we track count on CPU side via emission
    // A more accurate approach would be to use atomic counters in the shader
}

void ParticleSystem::syncGPUToCPU(WGPUDevice device, WGPUQueue queue) {
    // GPU mode uses direct rendering from GPU buffer, no sync needed
    // Just clear the CPU particles since we won't use them
    m_particles.clear();
}

void ParticleSystem::createGPUCircleMesh(WGPUDevice device) {
    // Generate circle with 32 segments (triangle fan)
    const int segments = 32;
    std::vector<float> vertices;
    std::vector<uint16_t> indices;

    // Center vertex
    vertices.push_back(0.0f);
    vertices.push_back(0.0f);

    // Edge vertices
    for (int i = 0; i <= segments; i++) {
        float angle = (float)i / segments * 2.0f * static_cast<float>(M_PI);
        vertices.push_back(std::cos(angle));
        vertices.push_back(std::sin(angle));
    }

    // Triangle fan indices
    for (int i = 0; i < segments; i++) {
        indices.push_back(0);
        indices.push_back(i + 1);
        indices.push_back(i + 2);
    }

    m_gpuCircleIndexCount = static_cast<uint32_t>(indices.size());

    // Create vertex buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = vertices.size() * sizeof(float);
    bufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    m_gpuCircleVertexBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);
    WGPUQueue queue = wgpuDeviceGetQueue(device);
    wgpuQueueWriteBuffer(queue, m_gpuCircleVertexBuffer, 0,
                         vertices.data(), vertices.size() * sizeof(float));

    // Create index buffer
    bufferDesc.size = indices.size() * sizeof(uint16_t);
    bufferDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
    m_gpuCircleIndexBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);
    wgpuQueueWriteBuffer(queue, m_gpuCircleIndexBuffer, 0,
                         indices.data(), indices.size() * sizeof(uint16_t));
}

void ParticleSystem::createGPUCirclePipeline(WGPUDevice device) {
    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(GPU_CIRCLE_RENDER_SHADER);

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

    // Bind group layout: uniform, particles storage
    WGPUBindGroupLayoutEntry entries[2] = {};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Vertex;
    entries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 2;
    layoutDesc.entries = entries;
    m_gpuCircleBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    WGPUBindGroupLayout layouts[] = { m_gpuCircleBindGroupLayout };
    pipelineLayoutDesc.bindGroupLayouts = layouts;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Vertex attributes (local position for circle mesh)
    WGPUVertexAttribute vertexAttrib = {};
    vertexAttrib.format = WGPUVertexFormat_Float32x2;
    vertexAttrib.offset = 0;
    vertexAttrib.shaderLocation = 0;

    WGPUVertexBufferLayout vertexLayout = {};
    vertexLayout.arrayStride = sizeof(float) * 2;
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.attributeCount = 1;
    vertexLayout.attributes = &vertexAttrib;

    // Blend state
    WGPUBlendState blendState = {};
    if (m_additiveBlend) {
        blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        blendState.color.dstFactor = WGPUBlendFactor_One;
        blendState.color.operation = WGPUBlendOperation_Add;
    } else {
        blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        blendState.color.operation = WGPUBlendOperation_Add;
    }
    blendState.alpha.srcFactor = WGPUBlendFactor_One;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = EFFECTS_FORMAT;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = module;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = module;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexLayout;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    m_gpuCirclePipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    // Create uniform buffer
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.size = sizeof(GPUCircleRenderUniforms);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_gpuCircleUniformBuffer = wgpuDeviceCreateBuffer(device, &uniformDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(module);
}

void ParticleSystem::renderCirclesGPU(Context& ctx) {
    if (m_aliveCountGPU == 0) {
        // Just clear the output
        WGPURenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = m_outputView;
        colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = {clearColor.r(), clearColor.g(), clearColor.b(), clearColor.a()};

        WGPURenderPassDescriptor passDesc = {};
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;

        auto encoder = ctx.gpuEncoder();
        auto pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        return;
    }

    auto device = ctx.device();
    auto queue = ctx.queue();

    // Update render uniforms
    GPUCircleRenderUniforms uniforms = {};
    uniforms.aspectRatio = (float)outputWidth() / outputHeight();
    uniforms.sizeStart = static_cast<float>(sizeStart);
    uniforms.sizeEnd = static_cast<float>(sizeEnd);
    uniforms.fadeOut = fadeOut ? 1.0f : 0.0f;
    uniforms.colorStartR = colorStart.r();
    uniforms.colorStartG = colorStart.g();
    uniforms.colorStartB = colorStart.b();
    uniforms.colorStartA = colorStart.a();
    uniforms.colorEndR = colorEnd.r();
    uniforms.colorEndG = colorEnd.g();
    uniforms.colorEndB = colorEnd.b();
    uniforms.colorEndA = colorEnd.a();

    wgpuQueueWriteBuffer(queue, m_gpuCircleUniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create bind group for current particle buffer
    WGPUBuffer readBuffer = m_particleBufferGPU[m_readBufferIndex];

    WGPUBindGroupEntry bindEntries[2] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_gpuCircleUniformBuffer;
    bindEntries[0].size = sizeof(GPUCircleRenderUniforms);

    bindEntries[1].binding = 1;
    bindEntries[1].buffer = readBuffer;
    bindEntries[1].size = m_allocatedParticlesGPU * sizeof(GPUParticleData);

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_gpuCircleBindGroupLayout;
    bindDesc.entryCount = 2;
    bindDesc.entries = bindEntries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

    // Render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = m_outputView;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {clearColor.r(), clearColor.g(), clearColor.b(), clearColor.a()};

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    auto encoder = ctx.gpuEncoder();
    auto pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    wgpuRenderPassEncoderSetPipeline(pass, m_gpuCirclePipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_gpuCircleVertexBuffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, m_gpuCircleIndexBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);

    // Draw instanced circles
    wgpuRenderPassEncoderDrawIndexed(pass, m_gpuCircleIndexCount, m_aliveCountGPU, 0, 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(bindGroup);
}

// =============================================================================
// GPU Billboard Rendering
// =============================================================================

void ParticleSystem::createGPUBillboardPipeline(WGPUDevice device) {
    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(GPU_BILLBOARD_RENDER_SHADER);

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

    // Bind group layout: uniform, particles storage
    WGPUBindGroupLayoutEntry entries[2] = {};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Vertex;
    entries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 2;
    layoutDesc.entries = entries;
    m_gpuBillboardBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    WGPUBindGroupLayout layouts[] = { m_gpuBillboardBindGroupLayout };
    pipelineLayoutDesc.bindGroupLayouts = layouts;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Blend state
    WGPUBlendState blendState = {};
    if (m_additiveBlend) {
        blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        blendState.color.dstFactor = WGPUBlendFactor_One;
        blendState.color.operation = WGPUBlendOperation_Add;
    } else {
        blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        blendState.color.operation = WGPUBlendOperation_Add;
    }
    blendState.alpha.srcFactor = WGPUBlendFactor_One;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = EFFECTS_FORMAT;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = module;
    fragmentState.entryPoint = toStringView("fs_circle");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = module;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.vertex.bufferCount = 0;  // No vertex buffer - all from storage
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    m_gpuBillboardPipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    // Create uniform buffer
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.size = sizeof(GPUBillboardRenderUniforms);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_gpuBillboardUniformBuffer = wgpuDeviceCreateBuffer(device, &uniformDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(module);
}

void ParticleSystem::renderBillboardsGPU(Context& ctx) {
    if (m_aliveCountGPU == 0 || !m_hasCamera) {
        // Just clear the output
        WGPURenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = m_outputView;
        colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = {clearColor.r(), clearColor.g(), clearColor.b(), clearColor.a()};

        WGPURenderPassDescriptor passDesc = {};
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;

        auto encoder = ctx.gpuEncoder();
        auto pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        return;
    }

    auto device = ctx.device();
    auto queue = ctx.queue();

    // Update render uniforms
    GPUBillboardRenderUniforms uniforms = {};

    // Compute viewProj
    glm::mat4 viewProj = m_hasViewProj ? m_viewProjMatrix : (m_projMatrix * m_viewMatrix);
    memcpy(uniforms.viewProj, &viewProj[0][0], sizeof(float) * 16);

    uniforms.cameraRight[0] = m_cameraRight.x;
    uniforms.cameraRight[1] = m_cameraRight.y;
    uniforms.cameraRight[2] = m_cameraRight.z;
    uniforms.cameraUp[0] = m_cameraUp.x;
    uniforms.cameraUp[1] = m_cameraUp.y;
    uniforms.cameraUp[2] = m_cameraUp.z;
    uniforms.sizeStart = static_cast<float>(sizeStart);
    uniforms.sizeEnd = static_cast<float>(sizeEnd);
    uniforms.fadeOut = fadeOut ? 1.0f : 0.0f;
    uniforms.colorStartR = colorStart.r();
    uniforms.colorStartG = colorStart.g();
    uniforms.colorStartB = colorStart.b();
    uniforms.colorStartA = colorStart.a();
    uniforms.colorEndR = colorEnd.r();
    uniforms.colorEndG = colorEnd.g();
    uniforms.colorEndB = colorEnd.b();
    uniforms.colorEndA = colorEnd.a();

    wgpuQueueWriteBuffer(queue, m_gpuBillboardUniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create bind group for current particle buffer
    WGPUBuffer readBuffer = m_particleBufferGPU[m_readBufferIndex];

    WGPUBindGroupEntry bindEntries[2] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_gpuBillboardUniformBuffer;
    bindEntries[0].size = sizeof(GPUBillboardRenderUniforms);

    bindEntries[1].binding = 1;
    bindEntries[1].buffer = readBuffer;
    bindEntries[1].size = m_allocatedParticlesGPU * sizeof(GPUParticleData);

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_gpuBillboardBindGroupLayout;
    bindDesc.entryCount = 2;
    bindDesc.entries = bindEntries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

    // Render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = m_outputView;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {clearColor.r(), clearColor.g(), clearColor.b(), clearColor.a()};

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    auto encoder = ctx.gpuEncoder();
    auto pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    wgpuRenderPassEncoderSetPipeline(pass, m_gpuBillboardPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);

    // Draw instanced quads (6 vertices per quad, no vertex buffer)
    wgpuRenderPassEncoderDraw(pass, 6, m_aliveCountGPU, 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(bindGroup);
}

// =============================================================================
// GPU Mesh Rendering
// =============================================================================

void ParticleSystem::createGPUMeshPipeline(WGPUDevice device) {
    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(GPU_MESH_RENDER_SHADER);

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule module = wgpuDeviceCreateShaderModule(device, &moduleDesc);

    // Bind group layout: uniform, particles storage
    WGPUBindGroupLayoutEntry entries[2] = {};

    entries[0].binding = 0;
    entries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    entries[0].buffer.type = WGPUBufferBindingType_Uniform;

    entries[1].binding = 1;
    entries[1].visibility = WGPUShaderStage_Vertex;
    entries[1].buffer.type = WGPUBufferBindingType_ReadOnlyStorage;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 2;
    layoutDesc.entries = entries;
    m_gpuMeshBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    WGPUBindGroupLayout layouts[] = { m_gpuMeshBindGroupLayout };
    pipelineLayoutDesc.bindGroupLayouts = layouts;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Vertex attributes (local position from mesh)
    WGPUVertexAttribute vertexAttrib = {};
    vertexAttrib.format = WGPUVertexFormat_Float32x3;
    vertexAttrib.offset = 0;
    vertexAttrib.shaderLocation = 0;

    WGPUVertexBufferLayout vertexLayout = {};
    vertexLayout.arrayStride = sizeof(float) * 3;
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.attributeCount = 1;
    vertexLayout.attributes = &vertexAttrib;

    // Blend state
    WGPUBlendState blendState = {};
    if (m_additiveBlend) {
        blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        blendState.color.dstFactor = WGPUBlendFactor_One;
        blendState.color.operation = WGPUBlendOperation_Add;
    } else {
        blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        blendState.color.operation = WGPUBlendOperation_Add;
    }
    blendState.alpha.srcFactor = WGPUBlendFactor_One;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = EFFECTS_FORMAT;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = module;
    fragmentState.entryPoint = toStringView("fs_lit");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = module;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexLayout;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    m_gpuMeshPipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    // Create uniform buffer
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.size = sizeof(GPUMeshRenderUniforms);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_gpuMeshUniformBuffer = wgpuDeviceCreateBuffer(device, &uniformDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(module);
}

void ParticleSystem::renderMeshesGPU(Context& ctx) {
    if (m_aliveCountGPU == 0 || !m_hasCamera) {
        // Just clear the output
        WGPURenderPassColorAttachment colorAttachment = {};
        colorAttachment.view = m_outputView;
        colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        colorAttachment.loadOp = WGPULoadOp_Clear;
        colorAttachment.storeOp = WGPUStoreOp_Store;
        colorAttachment.clearValue = {clearColor.r(), clearColor.g(), clearColor.b(), clearColor.a()};

        WGPURenderPassDescriptor passDesc = {};
        passDesc.colorAttachmentCount = 1;
        passDesc.colorAttachments = &colorAttachment;

        auto encoder = ctx.gpuEncoder();
        auto pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
        wgpuRenderPassEncoderEnd(pass);
        wgpuRenderPassEncoderRelease(pass);
        return;
    }

    // Ensure builtin mesh is created if needed
    if (m_useBuiltinMesh && !m_builtinMeshUploaded) {
        createBuiltinCubeMesh(ctx.device());
    }

    auto device = ctx.device();
    auto queue = ctx.queue();

    // Update render uniforms
    GPUMeshRenderUniforms uniforms = {};

    // Compute viewProj
    glm::mat4 viewProj = m_hasViewProj ? m_viewProjMatrix : (m_projMatrix * m_viewMatrix);
    memcpy(uniforms.viewProj, &viewProj[0][0], sizeof(float) * 16);

    uniforms.sizeStart = static_cast<float>(sizeStart);
    uniforms.sizeEnd = static_cast<float>(sizeEnd);
    uniforms.fadeOut = fadeOut ? 1.0f : 0.0f;
    uniforms.alignToVelocity = m_alignToVelocity ? 1.0f : 0.0f;
    uniforms.colorStartR = colorStart.r();
    uniforms.colorStartG = colorStart.g();
    uniforms.colorStartB = colorStart.b();
    uniforms.colorStartA = colorStart.a();
    uniforms.colorEndR = colorEnd.r();
    uniforms.colorEndG = colorEnd.g();
    uniforms.colorEndB = colorEnd.b();
    uniforms.colorEndA = colorEnd.a();
    uniforms.meshScaleX = m_builtinCubeWidth;
    uniforms.meshScaleY = m_builtinCubeWidth;
    uniforms.meshScaleZ = m_builtinCubeLength;

    wgpuQueueWriteBuffer(queue, m_gpuMeshUniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create bind group for current particle buffer
    WGPUBuffer readBuffer = m_particleBufferGPU[m_readBufferIndex];

    WGPUBindGroupEntry bindEntries[2] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_gpuMeshUniformBuffer;
    bindEntries[0].size = sizeof(GPUMeshRenderUniforms);

    bindEntries[1].binding = 1;
    bindEntries[1].buffer = readBuffer;
    bindEntries[1].size = m_allocatedParticlesGPU * sizeof(GPUParticleData);

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_gpuMeshBindGroupLayout;
    bindDesc.entryCount = 2;
    bindDesc.entries = bindEntries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

    // Render pass
    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = m_outputView;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {clearColor.r(), clearColor.g(), clearColor.b(), clearColor.a()};

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    auto encoder = ctx.gpuEncoder();
    auto pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);

    wgpuRenderPassEncoderSetPipeline(pass, m_gpuMeshPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_meshVertexBuffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, m_meshIndexBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);

    // Draw instanced meshes
    wgpuRenderPassEncoderDrawIndexed(pass, m_meshIndexCount, m_aliveCountGPU, 0, 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(bindGroup);
}

void ParticleSystem::createBillboardPipeline(WGPUDevice device) {
    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(BILLBOARD_SHADER);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Bind group layout
    WGPUBindGroupLayoutEntry layoutEntries[3] = {};

    // Uniforms
    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Vertex;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntries[0].buffer.minBindingSize = sizeof(BillboardUniforms);

    // Sampler
    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = WGPUShaderStage_Fragment;
    layoutEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    // Texture
    layoutEntries[2].binding = 2;
    layoutEntries[2].visibility = WGPUShaderStage_Fragment;
    layoutEntries[2].texture.sampleType = WGPUTextureSampleType_Float;
    layoutEntries[2].texture.viewDimension = WGPUTextureViewDimension_2D;

    WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = {};
    bindGroupLayoutDesc.entryCount = 3;
    bindGroupLayoutDesc.entries = layoutEntries;
    m_billboardBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDesc);

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_billboardBindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Vertex buffer layout for instancing
    WGPUVertexAttribute instanceAttrs[6] = {};
    instanceAttrs[0].format = WGPUVertexFormat_Float32x3;  // position
    instanceAttrs[0].offset = 0;
    instanceAttrs[0].shaderLocation = 0;
    instanceAttrs[1].format = WGPUVertexFormat_Float32;    // size
    instanceAttrs[1].offset = 12;
    instanceAttrs[1].shaderLocation = 1;
    instanceAttrs[2].format = WGPUVertexFormat_Float32x4;  // color
    instanceAttrs[2].offset = 16;
    instanceAttrs[2].shaderLocation = 2;
    instanceAttrs[3].format = WGPUVertexFormat_Float32;    // rotation
    instanceAttrs[3].offset = 32;
    instanceAttrs[3].shaderLocation = 3;
    instanceAttrs[4].format = WGPUVertexFormat_Float32;    // frameIndex
    instanceAttrs[4].offset = 36;
    instanceAttrs[4].shaderLocation = 4;
    instanceAttrs[5].format = WGPUVertexFormat_Float32x2;  // padding
    instanceAttrs[5].offset = 40;
    instanceAttrs[5].shaderLocation = 5;

    WGPUVertexBufferLayout instanceLayout = {};
    instanceLayout.arrayStride = sizeof(BillboardInstance);
    instanceLayout.stepMode = WGPUVertexStepMode_Instance;
    instanceLayout.attributeCount = 6;
    instanceLayout.attributes = instanceAttrs;

    // Color target with blending
    WGPUBlendState blendState = {};
    if (m_additiveBlend) {
        blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        blendState.color.dstFactor = WGPUBlendFactor_One;
        blendState.color.operation = WGPUBlendOperation_Add;
    } else {
        blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        blendState.color.operation = WGPUBlendOperation_Add;
    }
    blendState.alpha.srcFactor = WGPUBlendFactor_One;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = EFFECTS_FORMAT;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView(m_useTexture ? "fs_textured" : "fs_circle");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    // Render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &instanceLayout;
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    m_billboardPipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);

    // Create uniform buffer
    WGPUBufferDescriptor uniformBufferDesc = {};
    uniformBufferDesc.size = sizeof(BillboardUniforms);
    uniformBufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_billboardUniformBuffer = wgpuDeviceCreateBuffer(device, &uniformBufferDesc);

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;
    m_sampler = wgpuDeviceCreateSampler(device, &samplerDesc);

    // Create 1x1 white texture as default
    WGPUTextureDescriptor texDesc = {};
    texDesc.size = {1, 1, 1};
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    m_spriteTexture = wgpuDeviceCreateTexture(device, &texDesc);

    uint8_t whitePixel[4] = {255, 255, 255, 255};
    WGPUTexelCopyTextureInfo dest = {};
    dest.texture = m_spriteTexture;
    WGPUTexelCopyBufferLayout layout = {};
    layout.bytesPerRow = 4;
    layout.rowsPerImage = 1;
    WGPUExtent3D size = {1, 1, 1};
    // Note: Need queue to write texture, will do in renderBillboards
    m_spriteTextureView = wgpuTextureCreateView(m_spriteTexture, nullptr);
}

void ParticleSystem::createBuiltinCubeMesh(WGPUDevice device) {
    if (m_builtinMeshCreated) return;

    // Elongated cube: width x width x length (Z is long axis)
    float w = m_builtinCubeWidth * 0.5f;
    float l = m_builtinCubeLength * 0.5f;

    // 8 vertices of the cube
    float vertices[] = {
        // Front face (Z+)
        -w, -w,  l,
         w, -w,  l,
         w,  w,  l,
        -w,  w,  l,
        // Back face (Z-)
        -w, -w, -l,
        -w,  w, -l,
         w,  w, -l,
         w, -w, -l,
        // Top face (Y+)
        -w,  w, -l,
        -w,  w,  l,
         w,  w,  l,
         w,  w, -l,
        // Bottom face (Y-)
        -w, -w, -l,
         w, -w, -l,
         w, -w,  l,
        -w, -w,  l,
        // Right face (X+)
         w, -w, -l,
         w,  w, -l,
         w,  w,  l,
         w, -w,  l,
        // Left face (X-)
        -w, -w, -l,
        -w, -w,  l,
        -w,  w,  l,
        -w,  w, -l,
    };

    // 6 faces * 2 triangles * 3 indices = 36 indices
    uint16_t indices[] = {
        0, 1, 2, 0, 2, 3,       // Front
        4, 5, 6, 4, 6, 7,       // Back
        8, 9, 10, 8, 10, 11,    // Top
        12, 13, 14, 12, 14, 15, // Bottom
        16, 17, 18, 16, 18, 19, // Right
        20, 21, 22, 20, 22, 23  // Left
    };

    // Create vertex buffer
    WGPUBufferDescriptor vertexBufferDesc = {};
    vertexBufferDesc.size = sizeof(vertices);
    vertexBufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    m_meshVertexBuffer = wgpuDeviceCreateBuffer(device, &vertexBufferDesc);

    // Create index buffer
    WGPUBufferDescriptor indexBufferDesc = {};
    indexBufferDesc.size = sizeof(indices);
    indexBufferDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
    m_meshIndexBuffer = wgpuDeviceCreateBuffer(device, &indexBufferDesc);

    // We need a queue to write data - defer to first render
    m_meshIndexCount = 36;
    m_meshVertexStride = 12;  // 3 floats * 4 bytes
    m_builtinMeshCreated = true;

    // Store vertex/index data for deferred upload
    m_builtinVertexData.assign(reinterpret_cast<uint8_t*>(vertices),
                                reinterpret_cast<uint8_t*>(vertices) + sizeof(vertices));
    m_builtinIndexData.assign(reinterpret_cast<uint8_t*>(indices),
                               reinterpret_cast<uint8_t*>(indices) + sizeof(indices));
}

void ParticleSystem::createMeshPipeline(WGPUDevice device) {
    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(MESH_SHADER);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Bind group layout (just uniforms)
    WGPUBindGroupLayoutEntry layoutEntry = {};
    layoutEntry.binding = 0;
    layoutEntry.visibility = WGPUShaderStage_Vertex;
    layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntry.buffer.minBindingSize = sizeof(MeshUniforms);

    WGPUBindGroupLayoutDescriptor bindGroupLayoutDesc = {};
    bindGroupLayoutDesc.entryCount = 1;
    bindGroupLayoutDesc.entries = &layoutEntry;
    m_meshBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &bindGroupLayoutDesc);

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_meshBindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Vertex buffer layout for mesh vertices (position only)
    WGPUVertexAttribute meshAttrs[1] = {};
    meshAttrs[0].format = WGPUVertexFormat_Float32x3;  // position
    meshAttrs[0].offset = 0;
    meshAttrs[0].shaderLocation = 0;

    WGPUVertexBufferLayout meshLayout = {};
    meshLayout.arrayStride = m_meshVertexStride > 0 ? m_meshVertexStride : 12;  // Default to vec3
    meshLayout.stepMode = WGPUVertexStepMode_Vertex;
    meshLayout.attributeCount = 1;
    meshLayout.attributes = meshAttrs;

    // Instance buffer layout (transform matrix as 4 vec4s + color)
    WGPUVertexAttribute instanceAttrs[5] = {};
    instanceAttrs[0].format = WGPUVertexFormat_Float32x4;  // transform column 0
    instanceAttrs[0].offset = 0;
    instanceAttrs[0].shaderLocation = 4;
    instanceAttrs[1].format = WGPUVertexFormat_Float32x4;  // transform column 1
    instanceAttrs[1].offset = 16;
    instanceAttrs[1].shaderLocation = 5;
    instanceAttrs[2].format = WGPUVertexFormat_Float32x4;  // transform column 2
    instanceAttrs[2].offset = 32;
    instanceAttrs[2].shaderLocation = 6;
    instanceAttrs[3].format = WGPUVertexFormat_Float32x4;  // transform column 3
    instanceAttrs[3].offset = 48;
    instanceAttrs[3].shaderLocation = 7;
    instanceAttrs[4].format = WGPUVertexFormat_Float32x4;  // color
    instanceAttrs[4].offset = 64;
    instanceAttrs[4].shaderLocation = 8;

    WGPUVertexBufferLayout instanceLayout = {};
    instanceLayout.arrayStride = sizeof(MeshInstance);
    instanceLayout.stepMode = WGPUVertexStepMode_Instance;
    instanceLayout.attributeCount = 5;
    instanceLayout.attributes = instanceAttrs;

    WGPUVertexBufferLayout bufferLayouts[2] = {meshLayout, instanceLayout};

    // Color target with blending
    WGPUBlendState blendState = {};
    if (m_additiveBlend) {
        blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        blendState.color.dstFactor = WGPUBlendFactor_One;
        blendState.color.operation = WGPUBlendOperation_Add;
    } else {
        blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
        blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
        blendState.color.operation = WGPUBlendOperation_Add;
    }
    blendState.alpha.srcFactor = WGPUBlendFactor_One;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = EFFECTS_FORMAT;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_lit");  // Use lit shader for meshes
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    // Render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.vertex.bufferCount = 2;
    pipelineDesc.vertex.buffers = bufferLayouts;
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.cullMode = WGPUCullMode_Back;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    m_meshPipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);

    // Create uniform buffer
    WGPUBufferDescriptor uniformBufferDesc = {};
    uniformBufferDesc.size = sizeof(MeshUniforms);
    uniformBufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_meshUniformBuffer = wgpuDeviceCreateBuffer(device, &uniformBufferDesc);
}

// =============================================================================
// Utility Methods
// =============================================================================

void ParticleSystem::ensureInstanceCapacity(WGPUDevice device, size_t count) {
    // Reallocate billboard instance buffer if needed
    if (count > m_billboardInstanceCapacity) {
        if (m_billboardInstanceBuffer) {
            wgpuBufferRelease(m_billboardInstanceBuffer);
        }

        // Round up to next power of 2 for less frequent reallocations
        size_t newCapacity = 1;
        while (newCapacity < count) newCapacity *= 2;
        m_billboardInstanceCapacity = newCapacity;

        WGPUBufferDescriptor bufDesc = {};
        bufDesc.size = newCapacity * sizeof(BillboardInstance);
        bufDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        m_billboardInstanceBuffer = wgpuDeviceCreateBuffer(device, &bufDesc);
    }
}

void ParticleSystem::ensureMeshInstanceCapacity(WGPUDevice device, size_t count) {
    // Reallocate mesh instance buffer if needed
    if (count > m_meshInstanceCapacity) {
        if (m_meshInstanceBuffer) {
            wgpuBufferRelease(m_meshInstanceBuffer);
        }

        // Round up to next power of 2 for less frequent reallocations
        size_t newCapacity = 1;
        while (newCapacity < count) newCapacity *= 2;
        m_meshInstanceCapacity = newCapacity;

        WGPUBufferDescriptor bufDesc = {};
        bufDesc.size = newCapacity * sizeof(MeshInstance);
        bufDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        m_meshInstanceBuffer = wgpuDeviceCreateBuffer(device, &bufDesc);
    }
}

void ParticleSystem::spriteSheet(int cols, int rows) {
    m_spriteSheetCols = cols;
    m_spriteSheetRows = rows;
    m_spriteFrameCount = cols * rows;
    m_useSpriteSheet = true;
}

std::vector<glm::vec3> ParticleSystem::getPositions() const {
    std::vector<glm::vec3> positions;
    positions.reserve(m_particles.size());
    for (const auto& p : m_particles) {
        positions.push_back(p.position);
    }
    return positions;
}

void ParticleSystem::sortByDepth(const glm::mat4& viewMatrix) {
    m_sortedIndices.resize(m_particles.size());
    for (size_t i = 0; i < m_particles.size(); i++) {
        m_sortedIndices[i] = i;
    }

    std::sort(m_sortedIndices.begin(), m_sortedIndices.end(),
        [this, &viewMatrix](size_t a, size_t b) {
            glm::vec4 posA = viewMatrix * glm::vec4(m_particles[a].position, 1.0f);
            glm::vec4 posB = viewMatrix * glm::vec4(m_particles[b].position, 1.0f);
            return posA.z > posB.z;  // Back-to-front
        });
}

// =============================================================================
// Parameter System
// =============================================================================

std::vector<ParamDecl> ParticleSystem::params() {
    return {
        emitRate.decl(),
        maxParticles.decl(),
        emitterPosition.decl(),
        emitterSize.decl(),
        emitterDirection.decl(),
        coneAngle.decl(),
        lifeMin.decl(),
        lifeMax.decl(),
        sizeStart.decl(),
        sizeEnd.decl(),
        sizeVariation.decl(),
        initialVelocity.decl(),
        radialVelocity.decl(),
        spread.decl(),
        velocityVariation.decl(),
        gravity.decl(),
        drag.decl(),
        turbulence.decl(),
        attractorPosition.decl(),
        attractorStrength.decl(),
        curlStrength.decl(),
        curlScale.decl(),
        curlSpeed.decl(),
        curlOctaves.decl(),
        colorStart.decl(),
        colorEnd.decl(),
        fadeInTime.decl(),
        fadeOut.decl(),
        clearColor.decl()
    };
}

bool ParticleSystem::getParam(const std::string& name, float out[4]) {
    // Scalar params
    if (name == "emitRate") { out[0] = emitRate; return true; }
    if (name == "maxParticles") { out[0] = static_cast<float>(static_cast<int>(maxParticles)); return true; }
    if (name == "emitterSize") { out[0] = emitterSize; return true; }
    if (name == "coneAngle") { out[0] = coneAngle; return true; }
    if (name == "lifeMin") { out[0] = lifeMin; return true; }
    if (name == "lifeMax") { out[0] = lifeMax; return true; }
    if (name == "sizeStart") { out[0] = sizeStart; return true; }
    if (name == "sizeEnd") { out[0] = sizeEnd; return true; }
    if (name == "sizeVariation") { out[0] = sizeVariation; return true; }
    if (name == "radialVelocity") { out[0] = radialVelocity; return true; }
    if (name == "spread") { out[0] = spread; return true; }
    if (name == "velocityVariation") { out[0] = velocityVariation; return true; }
    if (name == "drag") { out[0] = drag; return true; }
    if (name == "turbulence") { out[0] = turbulence; return true; }
    if (name == "attractorStrength") { out[0] = attractorStrength; return true; }
    if (name == "curlStrength") { out[0] = curlStrength; return true; }
    if (name == "curlScale") { out[0] = curlScale; return true; }
    if (name == "curlSpeed") { out[0] = curlSpeed; return true; }
    if (name == "curlOctaves") { out[0] = static_cast<float>(static_cast<int>(curlOctaves)); return true; }
    if (name == "fadeInTime") { out[0] = fadeInTime; return true; }
    if (name == "fadeOut") { out[0] = static_cast<bool>(fadeOut) ? 1.0f : 0.0f; return true; }

    // Vec3 params
    if (name == "emitterPosition") { out[0] = emitterPosition.x(); out[1] = emitterPosition.y(); out[2] = emitterPosition.z(); return true; }
    if (name == "emitterDirection") { out[0] = emitterDirection.x(); out[1] = emitterDirection.y(); out[2] = emitterDirection.z(); return true; }
    if (name == "initialVelocity") { out[0] = initialVelocity.x(); out[1] = initialVelocity.y(); out[2] = initialVelocity.z(); return true; }
    if (name == "gravity") { out[0] = gravity.x(); out[1] = gravity.y(); out[2] = gravity.z(); return true; }
    if (name == "attractorPosition") { out[0] = attractorPosition.x(); out[1] = attractorPosition.y(); out[2] = attractorPosition.z(); return true; }

    // Color params
    if (name == "colorStart") { colorStart.getData(out); return true; }
    if (name == "colorEnd") { colorEnd.getData(out); return true; }
    if (name == "clearColor") { clearColor.getData(out); return true; }

    return false;
}

bool ParticleSystem::setParam(const std::string& name, const float value[4]) {
    // Scalar params
    if (name == "emitRate") { emitRate = value[0]; return true; }
    if (name == "maxParticles") { maxParticles = static_cast<int>(value[0]); return true; }
    if (name == "emitterSize") { emitterSize = value[0]; return true; }
    if (name == "coneAngle") { coneAngle = value[0]; return true; }
    if (name == "lifeMin") { lifeMin = value[0]; return true; }
    if (name == "lifeMax") { lifeMax = value[0]; return true; }
    if (name == "sizeStart") { sizeStart = value[0]; return true; }
    if (name == "sizeEnd") { sizeEnd = value[0]; return true; }
    if (name == "sizeVariation") { sizeVariation = value[0]; return true; }
    if (name == "radialVelocity") { radialVelocity = value[0]; return true; }
    if (name == "spread") { spread = value[0]; return true; }
    if (name == "velocityVariation") { velocityVariation = value[0]; return true; }
    if (name == "drag") { drag = value[0]; return true; }
    if (name == "turbulence") { turbulence = value[0]; return true; }
    if (name == "attractorStrength") { attractorStrength = value[0]; return true; }
    if (name == "curlStrength") { curlStrength = value[0]; return true; }
    if (name == "curlScale") { curlScale = value[0]; return true; }
    if (name == "curlSpeed") { curlSpeed = value[0]; return true; }
    if (name == "curlOctaves") { curlOctaves = static_cast<int>(value[0]); return true; }
    if (name == "fadeInTime") { fadeInTime = value[0]; return true; }
    if (name == "fadeOut") { fadeOut = value[0] > 0.5f; return true; }

    // Vec3 params
    if (name == "emitterPosition") { emitterPosition.set(value[0], value[1], value[2]); return true; }
    if (name == "emitterDirection") { emitterDirection.set(value[0], value[1], value[2]); return true; }
    if (name == "initialVelocity") { initialVelocity.set(value[0], value[1], value[2]); return true; }
    if (name == "gravity") { gravity.set(value[0], value[1], value[2]); return true; }
    if (name == "attractorPosition") { attractorPosition.set(value[0], value[1], value[2]); return true; }

    // Color params
    if (name == "colorStart") { colorStart.set(value[0], value[1], value[2], value[3]); return true; }
    if (name == "colorEnd") { colorEnd.set(value[0], value[1], value[2], value[3]); return true; }
    if (name == "clearColor") { clearColor.set(value[0], value[1], value[2], value[3]); return true; }

    return false;
}

// =============================================================================
// Cleanup
// =============================================================================

void ParticleSystem::cleanup() {
    if (m_renderer2D) {
        m_renderer2D->cleanup();
        delete m_renderer2D;
        m_renderer2D = nullptr;
    }

    if (m_spriteTexture) {
        wgpuTextureRelease(m_spriteTexture);
        m_spriteTexture = nullptr;
    }
    if (m_spriteTextureView) {
        wgpuTextureViewRelease(m_spriteTextureView);
        m_spriteTextureView = nullptr;
    }

    // Release billboard resources
    if (m_billboardPipeline) {
        wgpuRenderPipelineRelease(m_billboardPipeline);
        m_billboardPipeline = nullptr;
    }
    if (m_billboardBindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_billboardBindGroupLayout);
        m_billboardBindGroupLayout = nullptr;
    }
    if (m_billboardUniformBuffer) {
        wgpuBufferRelease(m_billboardUniformBuffer);
        m_billboardUniformBuffer = nullptr;
    }
    if (m_billboardInstanceBuffer) {
        wgpuBufferRelease(m_billboardInstanceBuffer);
        m_billboardInstanceBuffer = nullptr;
    }
    if (m_sampler) {
        wgpuSamplerRelease(m_sampler);
        m_sampler = nullptr;
    }

    // Release mesh resources
    if (m_meshPipeline) {
        wgpuRenderPipelineRelease(m_meshPipeline);
        m_meshPipeline = nullptr;
    }
    if (m_meshBindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_meshBindGroupLayout);
        m_meshBindGroupLayout = nullptr;
    }
    if (m_meshUniformBuffer) {
        wgpuBufferRelease(m_meshUniformBuffer);
        m_meshUniformBuffer = nullptr;
    }
    if (m_meshInstanceBuffer) {
        wgpuBufferRelease(m_meshInstanceBuffer);
        m_meshInstanceBuffer = nullptr;
    }

    // Release GPU compute resources
    for (int i = 0; i < 2; i++) {
        if (m_particleBufferGPU[i]) {
            wgpuBufferRelease(m_particleBufferGPU[i]);
            m_particleBufferGPU[i] = nullptr;
        }
        if (m_computeBindGroup[i]) {
            wgpuBindGroupRelease(m_computeBindGroup[i]);
            m_computeBindGroup[i] = nullptr;
        }
    }
    if (m_computePipeline) {
        wgpuComputePipelineRelease(m_computePipeline);
        m_computePipeline = nullptr;
    }
    if (m_computeBindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_computeBindGroupLayout);
        m_computeBindGroupLayout = nullptr;
    }
    if (m_computeUniformBuffer) {
        wgpuBufferRelease(m_computeUniformBuffer);
        m_computeUniformBuffer = nullptr;
    }

    releaseOutput();
    m_initialized = false;
    m_particles.clear();
}

} // namespace vivid::effects
