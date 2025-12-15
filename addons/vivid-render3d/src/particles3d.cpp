// Vivid Render3D - Particles3D Implementation

#include <vivid/render3d/particles3d.h>
#include <vivid/render3d/camera_operator.h>
#include <vivid/context.h>
#include <vivid/io/image_loader.h>
#include <vivid/effects/gpu_common.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <iostream>

namespace vivid::render3d {

// Billboard particle shader with spritesheet animation support
static const char* PARTICLE3D_SHADER = R"(
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
)";

struct ParticleUniforms {
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

struct ParticleInstanceData {
    float position[3];
    float size;
    float color[4];
    float rotation;
    float frameIndex;
    float _pad[2];
};

Particles3D::Particles3D() {
    m_rng.seed(m_seed);
}

Particles3D::~Particles3D() {
    cleanup();
}

void Particles3D::init(Context& ctx) {
    if (m_initialized) return;

    createPipeline(ctx);

    if (m_useSprites && !m_texturePath.empty()) {
        loadTexture(ctx);
    }

    m_initialized = true;
}

void Particles3D::createPipeline(Context& ctx) {
    using namespace vivid::effects;

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(PARTICLE3D_SHADER);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(ctx.device(), &shaderDesc);

    // Bind group layout
    WGPUBindGroupLayoutEntry layoutEntries[3] = {};

    // Uniforms
    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Vertex;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntries[0].buffer.minBindingSize = sizeof(ParticleUniforms);

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
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(ctx.device(), &bindGroupLayoutDesc);

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(ctx.device(), &pipelineLayoutDesc);

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
    instanceLayout.arrayStride = sizeof(ParticleInstanceData);
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
    fragmentState.entryPoint = toStringView(m_useSprites ? "fs_main" : "fs_circle");
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

    m_pipeline = wgpuDeviceCreateRenderPipeline(ctx.device(), &pipelineDesc);

    wgpuShaderModuleRelease(shaderModule);
    wgpuPipelineLayoutRelease(pipelineLayout);

    // Create uniform buffer
    WGPUBufferDescriptor uniformBufferDesc = {};
    uniformBufferDesc.size = sizeof(ParticleUniforms);
    uniformBufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(ctx.device(), &uniformBufferDesc);

    // Create instance buffer (will resize as needed)
    WGPUBufferDescriptor instanceBufferDesc = {};
    instanceBufferDesc.size = sizeof(ParticleInstanceData) * m_maxParticles;
    instanceBufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    m_instanceBuffer = wgpuDeviceCreateBuffer(ctx.device(), &instanceBufferDesc);

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    samplerDesc.maxAnisotropy = 1;
    m_sampler = wgpuDeviceCreateSampler(ctx.device(), &samplerDesc);

    // Create 1x1 white texture as default
    if (!m_spriteTextureView) {
        WGPUTextureDescriptor texDesc = {};
        texDesc.size = {1, 1, 1};
        texDesc.format = WGPUTextureFormat_RGBA8Unorm;
        texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        texDesc.mipLevelCount = 1;
        texDesc.sampleCount = 1;
        texDesc.dimension = WGPUTextureDimension_2D;
        m_spriteTexture = wgpuDeviceCreateTexture(ctx.device(), &texDesc);

        uint8_t whitePixel[4] = {255, 255, 255, 255};
        WGPUTexelCopyTextureInfo dest = {};
        dest.texture = m_spriteTexture;
        WGPUTexelCopyBufferLayout layout = {};
        layout.bytesPerRow = 4;
        layout.rowsPerImage = 1;
        WGPUExtent3D size = {1, 1, 1};
        wgpuQueueWriteTexture(ctx.queue(), &dest, whitePixel, 4, &layout, &size);

        m_spriteTextureView = wgpuTextureCreateView(m_spriteTexture, nullptr);
    }

    // Create output texture
    m_width = 1280;
    m_height = 720;
    createOutput(ctx);
}

void Particles3D::loadTexture(Context& ctx) {
    auto imageData = vivid::io::loadImage(m_texturePath);
    if (!imageData.valid()) {
        std::cerr << "Particles3D: Failed to load texture: " << m_texturePath << std::endl;
        return;
    }

    // Release old texture
    if (m_spriteTexture) {
        wgpuTextureRelease(m_spriteTexture);
    }
    if (m_spriteTextureView) {
        wgpuTextureViewRelease(m_spriteTextureView);
    }

    // Create texture
    WGPUTextureDescriptor texDesc = {};
    texDesc.size = {static_cast<uint32_t>(imageData.width), static_cast<uint32_t>(imageData.height), 1};
    texDesc.format = WGPUTextureFormat_RGBA8Unorm;
    texDesc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
    texDesc.mipLevelCount = 1;
    texDesc.sampleCount = 1;
    texDesc.dimension = WGPUTextureDimension_2D;
    m_spriteTexture = wgpuDeviceCreateTexture(ctx.device(), &texDesc);

    // Upload
    WGPUTexelCopyTextureInfo dest = {};
    dest.texture = m_spriteTexture;
    WGPUTexelCopyBufferLayout layout = {};
    layout.bytesPerRow = imageData.width * 4;
    layout.rowsPerImage = imageData.height;
    WGPUExtent3D size = {static_cast<uint32_t>(imageData.width), static_cast<uint32_t>(imageData.height), 1};
    wgpuQueueWriteTexture(ctx.queue(), &dest, imageData.pixels.data(), imageData.pixels.size(), &layout, &size);

    m_spriteTextureView = wgpuTextureCreateView(m_spriteTexture, nullptr);
}

glm::vec3 Particles3D::getEmitterPosition() {
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    switch (m_emitterShape) {
        case Emitter3DShape::Point:
            return m_emitterPos;

        case Emitter3DShape::Sphere: {
            // Random point on/in sphere
            float theta = dist01(m_rng) * 6.28318f;
            float phi = std::acos(dist(m_rng));
            float r = std::cbrt(dist01(m_rng)) * m_emitterSize;  // Cube root for uniform volume
            return m_emitterPos + glm::vec3(
                r * std::sin(phi) * std::cos(theta),
                r * std::sin(phi) * std::sin(theta),
                r * std::cos(phi)
            );
        }

        case Emitter3DShape::Box:
            return m_emitterPos + glm::vec3(
                dist(m_rng) * m_emitterSizeVec.x,
                dist(m_rng) * m_emitterSizeVec.y,
                dist(m_rng) * m_emitterSizeVec.z
            );

        case Emitter3DShape::Cone: {
            // Random point in cone volume
            float angle = dist01(m_rng) * 6.28318f;
            float height = dist01(m_rng);
            float radius = height * std::tan(m_coneAngle) * m_emitterSize * dist01(m_rng);

            // Build basis from emitter direction
            glm::vec3 up = std::abs(m_emitterDir.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
            glm::vec3 right = glm::normalize(glm::cross(m_emitterDir, up));
            up = glm::cross(right, m_emitterDir);

            return m_emitterPos + m_emitterDir * height * m_emitterSize
                   + right * std::cos(angle) * radius
                   + up * std::sin(angle) * radius;
        }

        case Emitter3DShape::Disc: {
            float angle = dist01(m_rng) * 6.28318f;
            float radius = std::sqrt(dist01(m_rng)) * m_emitterSize;
            return m_emitterPos + glm::vec3(
                std::cos(angle) * radius,
                0.0f,
                std::sin(angle) * radius
            );
        }
    }
    return m_emitterPos;
}

glm::vec3 Particles3D::getInitialVelocity(const glm::vec3& pos) {
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);

    glm::vec3 velocity = m_baseVelocity;

    // Add radial velocity (outward from emitter center)
    if (m_radialVelocity != 0.0f) {
        glm::vec3 radialDir = pos - m_emitterPos;
        float len = glm::length(radialDir);
        if (len > 0.001f) {
            velocity += glm::normalize(radialDir) * m_radialVelocity;
        } else {
            // Random direction for point emitter
            velocity += glm::normalize(glm::vec3(dist(m_rng), dist(m_rng), dist(m_rng))) * m_radialVelocity;
        }
    }

    // Apply spread
    if (m_spread > 0.0f) {
        float len = glm::length(velocity);
        if (len > 0.001f) {
            glm::vec3 dir = velocity / len;

            // Random rotation within spread cone
            float spreadAngle = dist01(m_rng) * m_spread;
            float rotAngle = dist01(m_rng) * 6.28318f;

            glm::vec3 perp = std::abs(dir.y) < 0.99f
                ? glm::normalize(glm::cross(dir, glm::vec3(0, 1, 0)))
                : glm::normalize(glm::cross(dir, glm::vec3(1, 0, 0)));
            glm::vec3 perp2 = glm::cross(dir, perp);

            glm::vec3 offset = (perp * std::cos(rotAngle) + perp2 * std::sin(rotAngle)) * std::sin(spreadAngle);
            velocity = glm::normalize(dir * std::cos(spreadAngle) + offset) * len;
        }
    }

    // Apply variation
    if (m_velocityVariation > 0.0f) {
        float factor = 1.0f + dist(m_rng) * m_velocityVariation;
        velocity *= factor;
    }

    return velocity;
}

void Particles3D::emitParticle() {
    if (static_cast<int>(m_particles.size()) >= m_maxParticles) return;

    std::uniform_real_distribution<float> dist01(0.0f, 1.0f);
    std::uniform_real_distribution<float> distPM(-1.0f, 1.0f);

    Particle3D p;
    p.position = getEmitterPosition();
    p.velocity = getInitialVelocity(p.position);
    p.maxLife = m_baseLife * (1.0f + distPM(m_rng) * m_lifeVariation);
    p.life = p.maxLife;
    p.size = m_sizeStart * (1.0f + distPM(m_rng) * m_sizeVariation);
    p.rotation = 0.0f;
    p.angularVel = m_spinSpeed * (0.5f + dist01(m_rng));
    p.index = m_particleIndex++;

    // Spritesheet random start frame
    if (m_spriteRandomStart && m_spriteFrameCount > 1) {
        std::uniform_int_distribution<int> frameDist(0, m_spriteFrameCount - 1);
        p.frameOffset = frameDist(m_rng);
    } else {
        p.frameOffset = 0;
    }

    // Initial color based on mode
    switch (m_colorMode) {
        case Color3DMode::Solid:
        case Color3DMode::Gradient:
            p.color = m_colorStart;
            break;
        case Color3DMode::Rainbow:
            p.color = hsvToRgb(std::fmod(p.index * 0.1f, 1.0f), 1.0f, 1.0f);
            break;
        case Color3DMode::Random:
            p.color = hsvToRgb(dist01(m_rng), 0.8f + dist01(m_rng) * 0.2f, 0.8f + dist01(m_rng) * 0.2f);
            break;
    }

    m_particles.push_back(p);
}

void Particles3D::updateParticles(float dt) {
    std::uniform_real_distribution<float> dist(-1.0f, 1.0f);

    for (auto it = m_particles.begin(); it != m_particles.end();) {
        Particle3D& p = *it;

        // Update life
        p.life -= dt;
        if (p.life <= 0.0f) {
            it = m_particles.erase(it);
            continue;
        }

        // Physics
        p.velocity += m_gravity * dt;

        // Drag
        if (m_drag > 0.0f) {
            p.velocity *= (1.0f - m_drag * dt);
        }

        // Turbulence
        if (m_turbulence > 0.0f) {
            p.velocity += glm::vec3(dist(m_rng), dist(m_rng), dist(m_rng)) * m_turbulence * dt;
        }

        // Attractor
        if (m_attractorStrength != 0.0f) {
            glm::vec3 toAttractor = m_attractorPos - p.position;
            float distSq = glm::dot(toAttractor, toAttractor);
            if (distSq > 0.01f) {
                p.velocity += glm::normalize(toAttractor) * (m_attractorStrength / distSq) * dt;
            }
        }

        // Update position
        p.position += p.velocity * dt;

        // Update rotation
        p.rotation += p.angularVel * dt;

        // Update size over lifetime
        float age = 1.0f - p.life / p.maxLife;
        float sizeT = age;
        p.size = glm::mix(m_sizeStart, m_sizeEnd, sizeT) * (1.0f + (p.index % 100) * 0.001f * m_sizeVariation);

        // Update color
        p.color = getParticleColor(p, age);

        ++it;
    }
}

glm::vec4 Particles3D::getParticleColor(const Particle3D& p, float age) {
    glm::vec4 color;

    switch (m_colorMode) {
        case Color3DMode::Solid:
            color = m_colorStart;
            break;
        case Color3DMode::Gradient:
            color = glm::mix(m_colorStart, m_colorEnd, age);
            break;
        case Color3DMode::Rainbow:
        case Color3DMode::Random:
            color = p.color;  // Set at emit time
            break;
    }

    // Fade in
    if (m_fadeInTime > 0.0f && age < m_fadeInTime / p.maxLife) {
        float fadeIn = age / (m_fadeInTime / p.maxLife);
        color.a *= fadeIn;
    }

    // Fade out
    if (m_fadeOut) {
        float fadeStart = 0.7f;
        if (age > fadeStart) {
            color.a *= 1.0f - (age - fadeStart) / (1.0f - fadeStart);
        }
    }

    return color;
}

glm::vec4 Particles3D::hsvToRgb(float h, float s, float v) {
    float c = v * s;
    float x = c * (1.0f - std::abs(std::fmod(h * 6.0f, 2.0f) - 1.0f));
    float m = v - c;

    glm::vec3 rgb;
    if (h < 1.0f/6.0f) rgb = {c, x, 0};
    else if (h < 2.0f/6.0f) rgb = {x, c, 0};
    else if (h < 3.0f/6.0f) rgb = {0, c, x};
    else if (h < 4.0f/6.0f) rgb = {0, x, c};
    else if (h < 5.0f/6.0f) rgb = {x, 0, c};
    else rgb = {c, 0, x};

    return glm::vec4(rgb + m, 1.0f);
}

void Particles3D::sortParticlesByDepth(const glm::mat4& viewMatrix) {
    m_sortedIndices.resize(m_particles.size());
    for (size_t i = 0; i < m_particles.size(); ++i) {
        m_sortedIndices[i] = i;
    }

    // Sort by distance from camera (back to front)
    std::sort(m_sortedIndices.begin(), m_sortedIndices.end(),
        [this, &viewMatrix](size_t a, size_t b) {
            glm::vec4 posA = viewMatrix * glm::vec4(m_particles[a].position, 1.0f);
            glm::vec4 posB = viewMatrix * glm::vec4(m_particles[b].position, 1.0f);
            return posA.z < posB.z;  // More negative Z = further away
        });
}

void Particles3D::process(Context& ctx) {
    if (!m_initialized) {
        init(ctx);
    }

    if (!m_cameraOp) {
        std::cerr << "Particles3D: No camera input set!" << std::endl;
        return;
    }

    float dt = static_cast<float>(ctx.dt());

    // Emit particles
    m_emitAccumulator += m_emitRate * dt;
    while (m_emitAccumulator >= 1.0f) {
        emitParticle();
        m_emitAccumulator -= 1.0f;
    }

    // Handle burst
    if (m_needsBurst) {
        for (int i = 0; i < m_burstCount; ++i) {
            emitParticle();
        }
        m_needsBurst = false;
    }

    // Update physics
    updateParticles(dt);

    if (m_particles.empty()) return;

    // Get camera matrices
    Camera3D cam = m_cameraOp->outputCamera();
    cam.aspect(static_cast<float>(m_width) / m_height);
    glm::mat4 viewMatrix = cam.viewMatrix();
    glm::mat4 viewProj = cam.viewProjectionMatrix();

    // Extract camera right and up vectors for billboarding
    glm::vec3 cameraRight = glm::vec3(viewMatrix[0][0], viewMatrix[1][0], viewMatrix[2][0]);
    glm::vec3 cameraUp = glm::vec3(viewMatrix[0][1], viewMatrix[1][1], viewMatrix[2][1]);

    // Update uniforms
    ParticleUniforms uniforms;
    std::memcpy(uniforms.viewProj, &viewProj[0][0], sizeof(uniforms.viewProj));
    std::memcpy(uniforms.cameraRight, &cameraRight[0], sizeof(uniforms.cameraRight));
    std::memcpy(uniforms.cameraUp, &cameraUp[0], sizeof(uniforms.cameraUp));
    uniforms.spriteSheetCols = static_cast<float>(m_spriteSheetCols);
    uniforms.spriteSheetRows = static_cast<float>(m_spriteSheetRows);
    uniforms.spriteFrameCount = static_cast<float>(m_spriteFrameCount);
    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Sort particles if needed
    if (m_depthSort) {
        sortParticlesByDepth(viewMatrix);
    } else {
        m_sortedIndices.resize(m_particles.size());
        for (size_t i = 0; i < m_particles.size(); ++i) {
            m_sortedIndices[i] = i;
        }
    }

    // Upload instance data
    float currentTime = static_cast<float>(ctx.time());
    std::vector<ParticleInstanceData> instanceData(m_particles.size());
    for (size_t i = 0; i < m_sortedIndices.size(); ++i) {
        const Particle3D& p = m_particles[m_sortedIndices[i]];
        instanceData[i].position[0] = p.position.x;
        instanceData[i].position[1] = p.position.y;
        instanceData[i].position[2] = p.position.z;
        instanceData[i].size = p.size;
        instanceData[i].color[0] = p.color.r;
        instanceData[i].color[1] = p.color.g;
        instanceData[i].color[2] = p.color.b;
        instanceData[i].color[3] = p.color.a;
        instanceData[i].rotation = p.rotation;

        // Compute sprite frame index
        if (m_useSpriteSheet && m_spriteFrameCount > 1) {
            float frame;
            if (m_spriteAnimateByLife) {
                // Frame based on particle age (0 to 1 maps to frame 0 to N-1)
                float age = 1.0f - (p.life / p.maxLife);  // 0 at birth, 1 at death
                frame = age * (m_spriteFrameCount - 1);
            } else {
                // Frame based on time and FPS
                float particleAge = p.maxLife - p.life;
                frame = particleAge * m_spriteFPS;
            }
            instanceData[i].frameIndex = std::fmod(frame + p.frameOffset, static_cast<float>(m_spriteFrameCount));
        } else {
            instanceData[i].frameIndex = 0.0f;
        }
    }
    wgpuQueueWriteBuffer(ctx.queue(), m_instanceBuffer, 0,
                         instanceData.data(), instanceData.size() * sizeof(ParticleInstanceData));

    // Create bind group
    WGPUBindGroupEntry entries[3] = {};
    entries[0].binding = 0;
    entries[0].buffer = m_uniformBuffer;
    entries[0].size = sizeof(ParticleUniforms);
    entries[1].binding = 1;
    entries[1].sampler = m_sampler;
    entries[2].binding = 2;
    entries[2].textureView = m_spriteTextureView;

    WGPUBindGroupDescriptor bindGroupDesc = {};
    bindGroupDesc.layout = m_bindGroupLayout;
    bindGroupDesc.entryCount = 3;
    bindGroupDesc.entries = entries;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(ctx.device(), &bindGroupDesc);

    // Render
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(ctx.device(), &encoderDesc);

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = m_outputView;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a};

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_instanceBuffer, 0,
                                          m_particles.size() * sizeof(ParticleInstanceData));
    wgpuRenderPassEncoderDraw(pass, 6, static_cast<uint32_t>(m_particles.size()), 0, 0);
    wgpuRenderPassEncoderEnd(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(ctx.queue(), 1, &cmdBuffer);

    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);
    wgpuRenderPassEncoderRelease(pass);
    wgpuBindGroupRelease(bindGroup);
}

void Particles3D::cleanup() {
    using namespace vivid::effects;

    gpu::release(m_pipeline);
    gpu::release(m_bindGroupLayout);
    gpu::release(m_uniformBuffer);
    gpu::release(m_instanceBuffer);
    gpu::release(m_sampler);

    if (m_spriteTexture) {
        wgpuTextureRelease(m_spriteTexture);
        m_spriteTexture = nullptr;
    }
    if (m_spriteTextureView) {
        wgpuTextureViewRelease(m_spriteTextureView);
        m_spriteTextureView = nullptr;
    }

    releaseOutput();
    m_particles.clear();
    m_initialized = false;
}

} // namespace vivid::render3d
