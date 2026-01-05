// Vivid Effects 2D - GPU Particles Implementation
// Compute shader-based particle system with curl noise

#include <vivid/effects/gpu_particles.h>
#include <vivid/effects/gpu_common.h>
#include <vivid/context.h>
#include <cmath>
#include <algorithm>
#include <iostream>

namespace vivid::effects {

// =============================================================================
// Compute Shader - Particle Simulation
// =============================================================================

static constexpr const char* SIMULATE_SHADER = R"(
struct Particle {
    posX: f32, posY: f32,
    velX: f32, velY: f32,
    life: f32, maxLife: f32,
    size: f32, rotation: f32,
    colorR: f32, colorG: f32, colorB: f32, colorA: f32,
    seed: f32,
    _pad0: f32, _pad1: f32, _pad2: f32,
}

struct SimulateUniforms {
    dt: f32,
    time: f32,
    particleCount: u32,
    _pad0: f32,

    // Curl noise
    curlStrength: f32,
    curlScale: f32,
    curlSpeed: f32,
    curlOctaves: i32,

    // Vortex
    vortexStrength: f32,
    vortexCenterX: f32,
    vortexCenterY: f32,
    vortexFalloff: f32,

    // Gravity and drag
    gravityX: f32,
    gravityY: f32,
    drag: f32,
    _pad1: f32,
}

@group(0) @binding(0) var<uniform> u: SimulateUniforms;
@group(0) @binding(1) var<storage, read> particlesIn: array<Particle>;
@group(0) @binding(2) var<storage, read_write> particlesOut: array<Particle>;

// =============================================================================
// Simplex Noise (3D) for Curl
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

// Curl noise: perpendicular to gradient of 3D noise field
fn curlNoise(p: vec2f, z: f32, scale: f32) -> vec2f {
    let eps = 0.001;
    let sp = p * scale;

    // Compute partial derivatives via finite differences
    let n = snoise(vec3f(sp, z));
    let nx = snoise(vec3f(sp.x + eps, sp.y, z));
    let ny = snoise(vec3f(sp.x, sp.y + eps, z));

    let dnx = (nx - n) / eps;
    let dny = (ny - n) / eps;

    // Curl in 2D: perpendicular to gradient (divergence-free)
    return vec2f(dny, -dnx);
}

fn curlNoiseFBM(p: vec2f, z: f32, scale: f32, octaves: i32) -> vec2f {
    var result = vec2f(0.0);
    var amplitude = 1.0;
    var frequency = 1.0;
    var maxAmp = 0.0;

    for (var i = 0; i < octaves; i++) {
        result += amplitude * curlNoise(p, z, scale * frequency);
        maxAmp += amplitude;
        amplitude *= 0.5;
        frequency *= 2.0;
    }

    return result / maxAmp;
}

// Vortex force: tangential rotation around a center point
fn vortexForce(pos: vec2f, center: vec2f, strength: f32, falloff: f32) -> vec2f {
    let toCenter = center - pos;
    let dist = length(toCenter);
    if (dist < 0.0001) { return vec2f(0.0); }

    let tangent = vec2f(-toCenter.y, toCenter.x) / dist;
    let attenuation = exp(-dist / falloff);

    return tangent * strength * attenuation;
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

    // Current state
    let pos = vec2f(p.posX, p.posY);
    var vel = vec2f(p.velX, p.velY);

    // === Apply Forces ===

    // Curl noise (primary force field for organic motion)
    if (u.curlStrength > 0.001) {
        let curlZ = u.time * u.curlSpeed + p.seed * 10.0;
        let curl = curlNoiseFBM(pos, curlZ, u.curlScale, u.curlOctaves);
        vel += curl * u.curlStrength * u.dt;
    }

    // Vortex rotation
    if (abs(u.vortexStrength) > 0.001) {
        let vortex = vortexForce(pos, vec2f(u.vortexCenterX, u.vortexCenterY),
                                  u.vortexStrength, u.vortexFalloff);
        vel += vortex * u.dt;
    }

    // Gravity
    vel += vec2f(u.gravityX, u.gravityY) * u.dt;

    // Drag
    if (u.drag > 0.001) {
        vel *= 1.0 - u.drag * u.dt;
    }

    // === Integrate Position ===
    let newPos = pos + vel * u.dt;

    p.posX = newPos.x;
    p.posY = newPos.y;
    p.velX = vel.x;
    p.velY = vel.y;

    // Update life
    p.life -= u.dt;

    particlesOut[idx] = p;
}
)";

// =============================================================================
// Render Shader - Draw circles from particle buffer
// =============================================================================

static constexpr const char* RENDER_SHADER = R"(
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
)";

// =============================================================================
// Uniform Structures (must match shader)
// =============================================================================

struct SimulateUniforms {
    float dt;
    float time;
    uint32_t particleCount;
    float _pad0;

    float curlStrength;
    float curlScale;
    float curlSpeed;
    int32_t curlOctaves;

    float vortexStrength;
    float vortexCenterX;
    float vortexCenterY;
    float vortexFalloff;

    float gravityX;
    float gravityY;
    float drag;
    float _pad1;
};

struct RenderUniforms {
    float aspectRatio;
    float sizeStart;
    float sizeEnd;
    float fadeOut;
    float colorStartR, colorStartG, colorStartB, colorStartA;
    float colorEndR, colorEndG, colorEndB, colorEndA;
};

// =============================================================================
// GPUParticles Implementation
// =============================================================================

GPUParticles::GPUParticles() {
    m_rng.seed(m_seed);
}

GPUParticles::~GPUParticles() {
    cleanup();
}

void GPUParticles::init(Context& ctx) {
    if (!beginInit()) return;

    createOutput(ctx);

    auto device = ctx.device();
    auto queue = ctx.queue();

    createBuffers(device, queue);
    createComputePipeline(device);
    createRenderPipeline(device);
    createCircleMesh(device);
}

void GPUParticles::createBuffers(WGPUDevice device, WGPUQueue queue) {
    int count = static_cast<int>(maxParticles);
    m_allocatedParticles = count;

    // Create ping-pong particle buffers
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = count * sizeof(GPUParticle);
    bufferDesc.usage = WGPUBufferUsage_Storage | WGPUBufferUsage_CopyDst;
    bufferDesc.mappedAtCreation = false;

    m_particleBuffer[0].reset(wgpuDeviceCreateBuffer(device, &bufferDesc));
    m_particleBuffer[1].reset(wgpuDeviceCreateBuffer(device, &bufferDesc));

    // Initialize with zeros (all dead particles)
    std::vector<GPUParticle> zeros(count);
    memset(zeros.data(), 0, zeros.size() * sizeof(GPUParticle));
    wgpuQueueWriteBuffer(queue, m_particleBuffer[0], 0, zeros.data(), zeros.size() * sizeof(GPUParticle));
    wgpuQueueWriteBuffer(queue, m_particleBuffer[1], 0, zeros.data(), zeros.size() * sizeof(GPUParticle));

    // Create uniform buffer for simulation
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.size = sizeof(SimulateUniforms);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_simulateUniformBuffer.reset(wgpuDeviceCreateBuffer(device, &uniformDesc));

    // Create uniform buffer for rendering
    uniformDesc.size = sizeof(RenderUniforms);
    m_renderUniformBuffer.reset(wgpuDeviceCreateBuffer(device, &uniformDesc));
}

void GPUParticles::createComputePipeline(WGPUDevice device) {
    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = gpu::toStringView(SIMULATE_SHADER);

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
    m_simulateBindGroupLayout.reset(wgpuDeviceCreateBindGroupLayout(device, &layoutDesc));

    // Create bind groups for both buffer configurations (ping-pong)
    for (int i = 0; i < 2; i++) {
        int readIdx = i;
        int writeIdx = 1 - i;

        WGPUBindGroupEntry bindEntries[3] = {};
        bindEntries[0].binding = 0;
        bindEntries[0].buffer = m_simulateUniformBuffer;
        bindEntries[0].size = sizeof(SimulateUniforms);

        bindEntries[1].binding = 1;
        bindEntries[1].buffer = m_particleBuffer[readIdx];
        bindEntries[1].size = m_allocatedParticles * sizeof(GPUParticle);

        bindEntries[2].binding = 2;
        bindEntries[2].buffer = m_particleBuffer[writeIdx];
        bindEntries[2].size = m_allocatedParticles * sizeof(GPUParticle);

        WGPUBindGroupDescriptor bindDesc = {};
        bindDesc.layout = m_simulateBindGroupLayout;
        bindDesc.entryCount = 3;
        bindDesc.entries = bindEntries;
        m_simulateBindGroup[i].reset(wgpuDeviceCreateBindGroup(device, &bindDesc));
    }

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    WGPUBindGroupLayout layouts[] = { m_simulateBindGroupLayout };
    pipelineLayoutDesc.bindGroupLayouts = layouts;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Compute pipeline
    WGPUComputePipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.compute.module = module;
    pipelineDesc.compute.entryPoint = gpu::toStringView("main");
    m_simulatePipeline.reset(wgpuDeviceCreateComputePipeline(device, &pipelineDesc));

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(module);
}

void GPUParticles::createRenderPipeline(WGPUDevice device) {
    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = gpu::toStringView(RENDER_SHADER);

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
    m_renderBindGroupLayout.reset(wgpuDeviceCreateBindGroupLayout(device, &layoutDesc));

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    WGPUBindGroupLayout layouts[] = { m_renderBindGroupLayout };
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

    // Blend state for additive-like particles
    WGPUBlendState blendState = {};
    blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.color.operation = WGPUBlendOperation_Add;
    blendState.alpha.srcFactor = WGPUBlendFactor_One;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = EFFECTS_FORMAT;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = module;
    fragmentState.entryPoint = gpu::toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = module;
    pipelineDesc.vertex.entryPoint = gpu::toStringView("vs_main");
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexLayout;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    m_renderPipeline.reset(wgpuDeviceCreateRenderPipeline(device, &pipelineDesc));

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(module);
}

void GPUParticles::createCircleMesh(WGPUDevice device) {
    // Generate circle with 32 segments (triangle fan)
    const int segments = 32;
    std::vector<float> vertices;
    std::vector<uint16_t> indices;

    // Center vertex
    vertices.push_back(0.0f);
    vertices.push_back(0.0f);

    // Edge vertices
    for (int i = 0; i <= segments; i++) {
        float angle = (float)i / segments * 2.0f * 3.14159265f;
        vertices.push_back(std::cos(angle));
        vertices.push_back(std::sin(angle));
    }

    // Triangle fan indices
    for (int i = 0; i < segments; i++) {
        indices.push_back(0);
        indices.push_back(i + 1);
        indices.push_back(i + 2);
    }

    m_circleIndexCount = static_cast<uint32_t>(indices.size());

    // Create vertex buffer
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.size = vertices.size() * sizeof(float);
    bufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    m_circleVertexBuffer.reset(wgpuDeviceCreateBuffer(device, &bufferDesc));
    wgpuQueueWriteBuffer(wgpuDeviceGetQueue(device), m_circleVertexBuffer, 0,
                         vertices.data(), vertices.size() * sizeof(float));

    // Create index buffer
    bufferDesc.size = indices.size() * sizeof(uint16_t);
    bufferDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
    m_circleIndexBuffer.reset(wgpuDeviceCreateBuffer(device, &bufferDesc));
    wgpuQueueWriteBuffer(wgpuDeviceGetQueue(device), m_circleIndexBuffer, 0,
                         indices.data(), indices.size() * sizeof(uint16_t));
}

void GPUParticles::process(Context& ctx) {
    if (!isInitialized()) init(ctx);

    float dt = static_cast<float>(ctx.dt());
    m_time += dt;

    auto device = ctx.device();
    auto queue = ctx.queue();

    // Emit new particles BEFORE compute (to read buffer)
    emitParticles(device, queue, dt);

    // Simulate all particles (reads from read buffer, writes to write buffer)
    dispatchSimulation(ctx, dt);

    // Render particles from read buffer (which is now the compute output after swap)
    renderParticles(ctx);

    // Mark output as updated so downstream operators (like Bloom) know to re-process
    didCook();
}

void GPUParticles::emitParticles(WGPUDevice device, WGPUQueue queue, float dt) {
    // Calculate how many particles to emit this frame
    m_emitAccumulator += static_cast<float>(emitRate) * dt;
    int toEmit = static_cast<int>(m_emitAccumulator);
    m_emitAccumulator -= toEmit;

    // Add pending burst
    toEmit += m_burstPending;
    m_burstPending = 0;

    // Limit to available slots
    int maxCount = static_cast<int>(maxParticles);
    toEmit = std::min(toEmit, maxCount);

    if (toEmit <= 0) return;

    // Generate particles on CPU
    m_emissionStaging.resize(toEmit);

    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    std::uniform_real_distribution<float> distAngle(-3.14159f, 3.14159f);

    float lifeMinVal = static_cast<float>(lifeMin);
    float lifeMaxVal = static_cast<float>(lifeMax);

    for (int i = 0; i < toEmit; i++) {
        GPUParticle& p = m_emissionStaging[i];

        // Position from emitter
        glm::vec2 pos = getEmitterPosition();
        p.posX = pos.x;
        p.posY = pos.y;

        // Velocity with spread
        glm::vec2 vel = getInitialVelocity();
        p.velX = vel.x;
        p.velY = vel.y;

        // Life
        p.life = lifeMinVal + dist01(m_rng) * (lifeMaxVal - lifeMinVal);
        p.maxLife = p.life;

        // Size (will be interpolated in shader)
        p.size = static_cast<float>(sizeStart);

        // Rotation
        p.rotation = 0.0f;

        // Color (will be modulated in shader)
        glm::vec4 col = getParticleColor();
        p.colorR = col.r;
        p.colorG = col.g;
        p.colorB = col.b;
        p.colorA = col.a;

        // Random seed for curl noise variation
        p.seed = dist01(m_rng);

        m_particleIndex++;
    }

    // Upload to GPU at next available slot (circular)
    // For simplicity, we write at the start of the buffer (old particles die naturally)
    int writeOffset = m_totalEmitted % maxCount;
    int writeCount = std::min(toEmit, maxCount - writeOffset);

    // Write to the buffer that will be READ next frame (so compute can update them)
    WGPUBuffer targetBuffer = m_particleBuffer[m_readBufferIndex];

    wgpuQueueWriteBuffer(queue, targetBuffer,
                         writeOffset * sizeof(GPUParticle),
                         m_emissionStaging.data(),
                         writeCount * sizeof(GPUParticle));

    // Handle wrap-around
    if (writeCount < toEmit) {
        int remaining = toEmit - writeCount;
        wgpuQueueWriteBuffer(queue, targetBuffer, 0,
                             m_emissionStaging.data() + writeCount,
                             remaining * sizeof(GPUParticle));
    }

    m_totalEmitted += toEmit;
    m_aliveCount = std::min(m_totalEmitted, maxCount);
}

void GPUParticles::dispatchSimulation(Context& ctx, float dt) {
    if (m_aliveCount == 0) return;

    auto device = ctx.device();
    auto queue = ctx.queue();

    // Update uniforms
    SimulateUniforms uniforms = {};
    uniforms.dt = dt;
    uniforms.time = m_time;
    uniforms.particleCount = static_cast<uint32_t>(m_aliveCount);

    uniforms.curlStrength = static_cast<float>(curlStrength);
    uniforms.curlScale = static_cast<float>(curlScale);
    uniforms.curlSpeed = static_cast<float>(curlSpeed);
    uniforms.curlOctaves = static_cast<int>(curlOctaves);

    uniforms.vortexStrength = static_cast<float>(vortexStrength);
    uniforms.vortexCenterX = vortexCenter.x();
    uniforms.vortexCenterY = vortexCenter.y();
    uniforms.vortexFalloff = static_cast<float>(vortexFalloff);

    uniforms.gravityX = gravity.x();
    uniforms.gravityY = gravity.y();
    uniforms.drag = static_cast<float>(drag);

    wgpuQueueWriteBuffer(queue, m_simulateUniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create a separate encoder for compute and submit immediately
    // This ensures compute results are available before render
    WGPUCommandEncoderDescriptor encDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encDesc);

    WGPUComputePassDescriptor passDesc = {};
    WGPUComputePassEncoder pass = wgpuCommandEncoderBeginComputePass(encoder, &passDesc);

    wgpuComputePassEncoderSetPipeline(pass, m_simulatePipeline);
    wgpuComputePassEncoderSetBindGroup(pass, 0, m_simulateBindGroup[m_readBufferIndex], 0, nullptr);

    // Dispatch workgroups (256 threads each)
    uint32_t workgroups = (m_aliveCount + 255) / 256;
    wgpuComputePassEncoderDispatchWorkgroups(pass, workgroups, 1, 1);

    wgpuComputePassEncoderEnd(pass);
    wgpuComputePassEncoderRelease(pass);

    // Finish and submit compute commands immediately
    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(queue, 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    // Swap buffers - render will read from what compute wrote to
    m_readBufferIndex = 1 - m_readBufferIndex;
}

void GPUParticles::renderParticles(Context& ctx) {
    if (m_aliveCount == 0) {
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
    RenderUniforms uniforms = {};
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

    wgpuQueueWriteBuffer(queue, m_renderUniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create bind group for current particle buffer
    // We read from the buffer that was WRITTEN by the last compute pass
    WGPUBuffer readBuffer = m_particleBuffer[m_readBufferIndex];

    WGPUBindGroupEntry bindEntries[2] = {};
    bindEntries[0].binding = 0;
    bindEntries[0].buffer = m_renderUniformBuffer;
    bindEntries[0].size = sizeof(RenderUniforms);

    bindEntries[1].binding = 1;
    bindEntries[1].buffer = readBuffer;
    bindEntries[1].size = m_allocatedParticles * sizeof(GPUParticle);

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_renderBindGroupLayout;
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

    wgpuRenderPassEncoderSetPipeline(pass, m_renderPipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_circleVertexBuffer, 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetIndexBuffer(pass, m_circleIndexBuffer, WGPUIndexFormat_Uint16, 0, WGPU_WHOLE_SIZE);

    // Draw instanced circles
    wgpuRenderPassEncoderDrawIndexed(pass, m_circleIndexCount, m_aliveCount, 0, 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(bindGroup);
}

void GPUParticles::cleanup() {
    m_particleBuffer[0].reset();
    m_particleBuffer[1].reset();
    m_simulatePipeline.reset();
    m_simulateBindGroupLayout.reset();
    m_simulateBindGroup[0].reset();
    m_simulateBindGroup[1].reset();
    m_simulateUniformBuffer.reset();
    m_renderPipeline.reset();
    m_renderBindGroupLayout.reset();
    m_renderBindGroup.reset();
    m_renderUniformBuffer.reset();
    m_circleVertexBuffer.reset();
    m_circleIndexBuffer.reset();
}

// =============================================================================
// Helper Methods
// =============================================================================

glm::vec2 GPUParticles::getEmitterPosition() {
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    glm::vec2 center(emitterPosition.x(), emitterPosition.y());
    float size = static_cast<float>(emitterSize);

    switch (m_emitterShape) {
        case GPUEmitterShape::Point:
            return center;

        case GPUEmitterShape::Line: {
            float t = dist01(m_rng);
            return center + glm::vec2(t - 0.5f, 0.0f) * size;
        }

        case GPUEmitterShape::Ring: {
            float angle = dist01(m_rng) * 6.28318f;
            return center + glm::vec2(std::cos(angle), std::sin(angle)) * size * 0.5f;
        }

        case GPUEmitterShape::Disc: {
            float angle = dist01(m_rng) * 6.28318f;
            float r = std::sqrt(dist01(m_rng)) * size * 0.5f;
            return center + glm::vec2(std::cos(angle), std::sin(angle)) * r;
        }

        case GPUEmitterShape::Rectangle:
            return center + glm::vec2(dist(m_rng), dist(m_rng)) * size * 0.5f;
    }

    return center;
}

glm::vec2 GPUParticles::getInitialVelocity() {
    glm::vec2 base(initialVelocity.x(), initialVelocity.y());

    float spreadVal = static_cast<float>(velocitySpread);
    if (spreadVal > 0.001f) {
        std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
        base.x += dist(m_rng) * spreadVal;
        base.y += dist(m_rng) * spreadVal;
    }

    return base;
}

glm::vec4 GPUParticles::getParticleColor() {
    switch (m_colorMode) {
        case GPUColorMode::Solid:
        case GPUColorMode::Gradient:
            return glm::vec4(1.0f); // Shader handles gradient

        case GPUColorMode::Rainbow: {
            float hue = (m_particleIndex % 360) / 360.0f;
            // Simple HSV to RGB
            float h = hue * 6.0f;
            float c = 1.0f;
            float x = c * (1.0f - std::abs(std::fmod(h, 2.0f) - 1.0f));
            glm::vec3 rgb;
            if (h < 1) rgb = {c, x, 0};
            else if (h < 2) rgb = {x, c, 0};
            else if (h < 3) rgb = {0, c, x};
            else if (h < 4) rgb = {0, x, c};
            else if (h < 5) rgb = {x, 0, c};
            else rgb = {c, 0, x};
            return glm::vec4(rgb, 1.0f);
        }

        case GPUColorMode::Random: {
            std::uniform_real_distribution<float> dist(0.0f, 1.0f);
            return glm::vec4(dist(m_rng), dist(m_rng), dist(m_rng), 1.0f);
        }
    }

    return glm::vec4(1.0f);
}

// =============================================================================
// Parameter Interface
// =============================================================================

std::vector<ParamDecl> GPUParticles::params() {
    return {
        emitRate.decl(),
        maxParticles.decl(),
        lifeMin.decl(),
        lifeMax.decl(),
        sizeStart.decl(),
        sizeEnd.decl(),
        curlStrength.decl(),
        curlScale.decl(),
        curlSpeed.decl(),
        curlOctaves.decl(),
        vortexStrength.decl(),
        vortexFalloff.decl(),
        drag.decl(),
        fadeOut.decl(),
    };
}

bool GPUParticles::getParam(const std::string& name, float out[4]) {
    if (name == "emitRate") { out[0] = emitRate; return true; }
    if (name == "maxParticles") { out[0] = static_cast<float>(static_cast<int>(maxParticles)); return true; }
    if (name == "lifeMin") { out[0] = lifeMin; return true; }
    if (name == "lifeMax") { out[0] = lifeMax; return true; }
    if (name == "sizeStart") { out[0] = sizeStart; return true; }
    if (name == "sizeEnd") { out[0] = sizeEnd; return true; }
    if (name == "curlStrength") { out[0] = curlStrength; return true; }
    if (name == "curlScale") { out[0] = curlScale; return true; }
    if (name == "curlSpeed") { out[0] = curlSpeed; return true; }
    if (name == "curlOctaves") { out[0] = static_cast<float>(static_cast<int>(curlOctaves)); return true; }
    if (name == "vortexStrength") { out[0] = vortexStrength; return true; }
    if (name == "vortexFalloff") { out[0] = vortexFalloff; return true; }
    if (name == "drag") { out[0] = drag; return true; }
    if (name == "fadeOut") { out[0] = fadeOut ? 1.0f : 0.0f; return true; }
    if (name == "emitterPosition") { out[0] = emitterPosition.x(); out[1] = emitterPosition.y(); return true; }
    if (name == "vortexCenter") { out[0] = vortexCenter.x(); out[1] = vortexCenter.y(); return true; }
    if (name == "gravity") { out[0] = gravity.x(); out[1] = gravity.y(); return true; }
    if (name == "initialVelocity") { out[0] = initialVelocity.x(); out[1] = initialVelocity.y(); return true; }
    if (name == "colorStart") { colorStart.getData(out); return true; }
    if (name == "colorEnd") { colorEnd.getData(out); return true; }
    if (name == "clearColor") { clearColor.getData(out); return true; }
    return false;
}

bool GPUParticles::setParam(const std::string& name, const float value[4]) {
    if (name == "emitRate") { emitRate = value[0]; return true; }
    if (name == "lifeMin") { lifeMin = value[0]; return true; }
    if (name == "lifeMax") { lifeMax = value[0]; return true; }
    if (name == "sizeStart") { sizeStart = value[0]; return true; }
    if (name == "sizeEnd") { sizeEnd = value[0]; return true; }
    if (name == "curlStrength") { curlStrength = value[0]; return true; }
    if (name == "curlScale") { curlScale = value[0]; return true; }
    if (name == "curlSpeed") { curlSpeed = value[0]; return true; }
    if (name == "curlOctaves") { curlOctaves = static_cast<int>(value[0]); return true; }
    if (name == "vortexStrength") { vortexStrength = value[0]; return true; }
    if (name == "vortexFalloff") { vortexFalloff = value[0]; return true; }
    if (name == "drag") { drag = value[0]; return true; }
    if (name == "fadeOut") { fadeOut = value[0] > 0.5f; return true; }
    if (name == "emitterPosition") { emitterPosition.set(value[0], value[1]); return true; }
    if (name == "vortexCenter") { vortexCenter.set(value[0], value[1]); return true; }
    if (name == "gravity") { gravity.set(value[0], value[1]); return true; }
    if (name == "initialVelocity") { initialVelocity.set(value[0], value[1]); return true; }
    if (name == "colorStart") { colorStart.set(value[0], value[1], value[2], value[3]); return true; }
    if (name == "colorEnd") { colorEnd.set(value[0], value[1], value[2], value[3]); return true; }
    if (name == "clearColor") { clearColor.set(value[0], value[1], value[2], value[3]); return true; }
    return false;
}

} // namespace vivid::effects
