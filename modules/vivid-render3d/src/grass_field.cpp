// GrassField - GPU-instanced grass with wind animation
// Renders thousands of animated grass blades efficiently

#include <vivid/render3d/grass_field.h>
#include <vivid/operator_registry.h>
#include <vivid/context.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <random>
#include <cmath>

namespace vivid::render3d {

REGISTER_OPERATOR(GrassField, "3D Vegetation", "GPU-instanced grass field with wind animation", false);

using namespace vivid::effects;

namespace {

constexpr WGPUTextureFormat DEPTH_FORMAT = WGPUTextureFormat_Depth24Plus;
constexpr uint32_t MAX_LIGHTS = 4;
constexpr int BLADE_SEGMENTS = 4;  // Segments per blade for smooth bending

// GPU Light structure (64 bytes)
struct GPULight {
    float position[3];
    float range;
    float direction[3];
    float spotAngle;
    float color[3];
    float intensity;
    uint32_t type;
    float spotBlend;
    float _pad[2];
};

// Uniform buffer for grass rendering
struct GrassUniforms {
    float viewProj[16];         // View-projection matrix
    float cameraPos[3];
    float time;
    float baseColor[3];
    float windStrength;
    float tipColor[3];
    float windSpeed;
    float windDir[2];
    uint32_t lightCount;
    float _pad0;
    GPULight lights[MAX_LIGHTS];
};

// GPU instance data (80 bytes)
struct GPUGrassInstance {
    float model[16];      // 64 bytes - transform matrix
    float color[4];       // 16 bytes - RGB color + phase offset in alpha
};

static_assert(sizeof(GPUGrassInstance) == 80, "GPUGrassInstance must be 80 bytes");

// Grass shader with wind animation
const char* GRASS_SHADER = R"(
const PI: f32 = 3.14159265359;
const MAX_LIGHTS: u32 = 4u;

const LIGHT_DIRECTIONAL: u32 = 0u;
const LIGHT_POINT: u32 = 1u;
const LIGHT_SPOT: u32 = 2u;

struct Light {
    position: vec3f,
    range: f32,
    direction: vec3f,
    spotAngle: f32,
    color: vec3f,
    intensity: f32,
    lightType: u32,
    spotBlend: f32,
    _pad: vec2f,
}

struct Uniforms {
    viewProj: mat4x4f,
    cameraPos: vec3f,
    time: f32,
    baseColor: vec3f,
    windStrength: f32,
    tipColor: vec3f,
    windSpeed: f32,
    windDir: vec2f,
    lightCount: u32,
    _pad0: f32,
    lights: array<Light, 4>,
}

struct InstanceData {
    @location(4) model0: vec4f,
    @location(5) model1: vec4f,
    @location(6) model2: vec4f,
    @location(7) model3: vec4f,
    @location(8) colorPhase: vec4f,  // RGB + phase offset in alpha
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) color: vec4f,
}

struct VertexOutput {
    @builtin(position) clipPos: vec4f,
    @location(0) worldPos: vec3f,
    @location(1) worldNormal: vec3f,
    @location(2) color: vec3f,
    @location(3) height: f32,
}

@vertex
fn vs_main(vert: VertexInput, inst: InstanceData) -> VertexOutput {
    var out: VertexOutput;

    // Reconstruct model matrix from instance data
    let model = mat4x4f(inst.model0, inst.model1, inst.model2, inst.model3);

    // Get world position before wind
    var worldPos = (model * vec4f(vert.position, 1.0)).xyz;

    // Height factor from UV.y (0 at base, 1 at tip)
    let heightFactor = vert.uv.y;
    out.height = heightFactor;

    // Wind animation
    // Per-blade phase offset from instance color alpha
    let phaseOffset = inst.colorPhase.a * PI * 2.0;

    // Multi-frequency wind waves for natural motion
    let windPhase1 = worldPos.x * 0.5 + worldPos.z * 0.3 + uniforms.time * uniforms.windSpeed + phaseOffset;
    let windPhase2 = worldPos.x * 0.8 + worldPos.z * 0.6 + uniforms.time * uniforms.windSpeed * 1.3 + phaseOffset * 1.7;

    let windWave = sin(windPhase1) * 0.7 + sin(windPhase2) * 0.3;

    // Displacement increases quadratically with height (tips bend more)
    let bendAmount = windWave * uniforms.windStrength * heightFactor * heightFactor;

    // Apply wind displacement
    worldPos.x += uniforms.windDir.x * bendAmount;
    worldPos.z += uniforms.windDir.y * bendAmount;

    // Slight vertical droop when bent
    worldPos.y -= abs(bendAmount) * 0.2;

    out.worldPos = worldPos;
    out.clipPos = uniforms.viewProj * vec4f(worldPos, 1.0);

    // Transform normal (simplified - assumes uniform scale)
    let normalMat = mat3x3f(model[0].xyz, model[1].xyz, model[2].xyz);
    out.worldNormal = normalize(normalMat * vert.normal);

    // Interpolate color from base to tip
    out.color = mix(uniforms.baseColor, uniforms.tipColor, heightFactor) * inst.colorPhase.rgb;

    return out;
}

// Simple diffuse + ambient lighting
fn calculateLighting(worldPos: vec3f, normal: vec3f) -> f32 {
    var totalLight = 0.3;  // Ambient

    for (var i = 0u; i < uniforms.lightCount; i++) {
        let light = uniforms.lights[i];

        var lightDir: vec3f;
        var attenuation = 1.0;

        if (light.lightType == LIGHT_DIRECTIONAL) {
            lightDir = -normalize(light.direction);
        } else if (light.lightType == LIGHT_POINT) {
            let toLight = light.position - worldPos;
            let dist = length(toLight);
            lightDir = toLight / dist;
            attenuation = max(0.0, 1.0 - dist / light.range);
            attenuation *= attenuation;
        } else {  // Spot
            let toLight = light.position - worldPos;
            let dist = length(toLight);
            lightDir = toLight / dist;
            attenuation = max(0.0, 1.0 - dist / light.range);

            let spotCos = dot(-lightDir, normalize(light.direction));
            let spotFade = clamp((spotCos - light.spotAngle) / (light.spotBlend - light.spotAngle), 0.0, 1.0);
            attenuation *= spotFade * spotFade;
        }

        let NdotL = max(dot(normal, lightDir), 0.0);
        totalLight += NdotL * light.intensity * attenuation;
    }

    return min(totalLight, 2.0);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let lighting = calculateLighting(input.worldPos, input.worldNormal);
    let finalColor = input.color * lighting;

    // Slight alpha fade at very tips for softer look
    let alpha = 1.0 - smoothstep(0.9, 1.0, input.height) * 0.3;

    return vec4f(finalColor, alpha);
}
)";

WGPUStringView toStringView(const char* str) {
    return WGPUStringView{str, strlen(str)};
}

GPULight toGPULight(const LightData& light) {
    GPULight gpu = {};
    gpu.position[0] = light.position.x;
    gpu.position[1] = light.position.y;
    gpu.position[2] = light.position.z;
    gpu.range = light.range;
    gpu.direction[0] = light.direction.x;
    gpu.direction[1] = light.direction.y;
    gpu.direction[2] = light.direction.z;
    float outerRad = glm::radians(light.spotAngle);
    float innerRad = outerRad * (1.0f - light.spotBlend);
    gpu.spotAngle = std::cos(outerRad);
    gpu.spotBlend = std::cos(innerRad);
    gpu.color[0] = light.color.r;
    gpu.color[1] = light.color.g;
    gpu.color[2] = light.color.b;
    gpu.intensity = light.intensity;
    switch (light.type) {
        case LightType::Directional: gpu.type = 0; break;
        case LightType::Point:       gpu.type = 1; break;
        case LightType::Spot:        gpu.type = 2; break;
    }
    return gpu;
}

} // namespace

GrassField::GrassField() {
    registerParam(fieldWidth);
    registerParam(fieldDepth);
    registerParam(bladeCount);
    registerParam(seed);
    registerParam(bladeHeight);
    registerParam(bladeWidth);
    registerParam(heightVariation);
    registerParam(windStrength);
    registerParam(windSpeed);
    registerParam(windDirX);
    registerParam(windDirZ);
}

GrassField::~GrassField() {
    cleanup();
}

void GrassField::setCameraInput(CameraOperator* cam) {
    if (m_cameraOp != cam) {
        m_cameraOp = cam;
        markDirty();
    }
}

void GrassField::setLightInput(LightOperator* light) {
    m_lightOps.clear();
    if (light) {
        m_lightOps.push_back(light);
    }
    markDirty();
}

void GrassField::addLight(LightOperator* light) {
    if (light && m_lightOps.size() < MAX_LIGHTS) {
        m_lightOps.push_back(light);
        markDirty();
    }
}

void GrassField::setClearColor(float r, float g, float b, float a) {
    glm::vec4 newColor(r, g, b, a);
    if (m_clearColor != newColor) {
        m_clearColor = newColor;
        markDirty();
    }
}

void GrassField::setResolution(int width, int height) {
    if (m_width != width || m_height != height) {
        TextureOperator::setResolution(width, height);
        markDirty();
    }
}

void GrassField::createBladeMesh() {
    m_bladeMesh.vertices.clear();
    m_bladeMesh.indices.clear();

    float height = 1.0f;  // Normalized, actual height via instance scale
    float width = static_cast<float>(bladeWidth);

    // Create tapered blade with BLADE_SEGMENTS
    for (int i = 0; i <= BLADE_SEGMENTS; i++) {
        float t = static_cast<float>(i) / BLADE_SEGMENTS;
        float y = t * height;
        float w = width * (1.0f - t * 0.8f);  // Taper toward tip

        // Normal points outward (toward camera typically)
        glm::vec3 normal(0.0f, 0.0f, 1.0f);

        // Left vertex
        Vertex3D left;
        left.position = glm::vec3(-w * 0.5f, y, 0.0f);
        left.normal = normal;
        left.uv = glm::vec2(0.0f, t);
        left.color = glm::vec4(1.0f);
        m_bladeMesh.vertices.push_back(left);

        // Right vertex
        Vertex3D right;
        right.position = glm::vec3(w * 0.5f, y, 0.0f);
        right.normal = normal;
        right.uv = glm::vec2(1.0f, t);
        right.color = glm::vec4(1.0f);
        m_bladeMesh.vertices.push_back(right);
    }

    // Create triangles
    for (int i = 0; i < BLADE_SEGMENTS; i++) {
        uint32_t base = i * 2;
        // First triangle
        m_bladeMesh.indices.push_back(base);
        m_bladeMesh.indices.push_back(base + 1);
        m_bladeMesh.indices.push_back(base + 2);
        // Second triangle
        m_bladeMesh.indices.push_back(base + 1);
        m_bladeMesh.indices.push_back(base + 3);
        m_bladeMesh.indices.push_back(base + 2);
    }

    m_meshCreated = true;
}

void GrassField::generateInstances() {
    int count = static_cast<int>(bladeCount);
    int currentSeed = static_cast<int>(seed);

    // Only regenerate if parameters changed
    if (count == m_lastBladeCount && currentSeed == m_lastSeed && !m_instances.empty()) {
        return;
    }

    m_instances.clear();
    m_instances.reserve(count);

    std::mt19937 rng(currentSeed);
    std::uniform_real_distribution<float> distX(-static_cast<float>(fieldWidth) * 0.5f,
                                                 static_cast<float>(fieldWidth) * 0.5f);
    std::uniform_real_distribution<float> distZ(-static_cast<float>(fieldDepth) * 0.5f,
                                                 static_cast<float>(fieldDepth) * 0.5f);
    std::uniform_real_distribution<float> distRot(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> distHeight(1.0f - static_cast<float>(heightVariation),
                                                     1.0f + static_cast<float>(heightVariation));
    std::uniform_real_distribution<float> distPhase(0.0f, 1.0f);
    std::uniform_real_distribution<float> distColorVar(0.85f, 1.15f);

    float baseH = static_cast<float>(bladeHeight);

    for (int i = 0; i < count; i++) {
        GrassInstance inst;

        // Random position
        float x = distX(rng);
        float z = distZ(rng);

        // Random rotation around Y axis
        float rot = distRot(rng);

        // Random height scale
        float heightScale = distHeight(rng) * baseH;

        // Build transform: translate, rotate, scale
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, 0.0f, z));
        transform = glm::rotate(transform, rot, glm::vec3(0.0f, 1.0f, 0.0f));
        transform = glm::scale(transform, glm::vec3(1.0f, heightScale, 1.0f));
        inst.transform = transform;

        // Color variation + phase offset in alpha
        float colorVar = distColorVar(rng);
        float phase = distPhase(rng);
        inst.color = glm::vec4(colorVar, colorVar, colorVar, phase);

        m_instances.push_back(inst);
    }

    m_lastBladeCount = count;
    m_lastSeed = currentSeed;
    m_instancesDirty = true;
}

void GrassField::init(Context& ctx) {
    if (m_initialized) return;

    // Default resolution if not set
    if (m_width == 0 || m_height == 0) {
        m_width = 1280;
        m_height = 720;
    }

    createOutput(ctx);

    if (!m_meshCreated) {
        createBladeMesh();
    }

    generateInstances();
    createPipeline(ctx);

    m_initialized = true;
}

void GrassField::createDepthBuffer(Context& ctx) {
    if (m_depthTexture && m_depthWidth == m_width && m_depthHeight == m_height) {
        return;
    }

    // Clean up old depth buffer
    if (m_depthView) {
        wgpuTextureViewRelease(m_depthView);
        m_depthView = nullptr;
    }
    if (m_depthTexture) {
        wgpuTextureRelease(m_depthTexture);
        m_depthTexture = nullptr;
    }

    WGPUTextureDescriptor depthDesc = {};
    depthDesc.size.width = m_width;
    depthDesc.size.height = m_height;
    depthDesc.size.depthOrArrayLayers = 1;
    depthDesc.mipLevelCount = 1;
    depthDesc.sampleCount = 1;
    depthDesc.dimension = WGPUTextureDimension_2D;
    depthDesc.format = DEPTH_FORMAT;
    depthDesc.usage = WGPUTextureUsage_RenderAttachment;

    m_depthTexture = wgpuDeviceCreateTexture(ctx.device(), &depthDesc);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = DEPTH_FORMAT;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.baseMipLevel = 0;
    viewDesc.mipLevelCount = 1;
    viewDesc.baseArrayLayer = 0;
    viewDesc.arrayLayerCount = 1;
    m_depthView = wgpuTextureCreateView(m_depthTexture, &viewDesc);

    m_depthWidth = m_width;
    m_depthHeight = m_height;
}

void GrassField::createPipeline(Context& ctx) {
    if (m_pipelineCreated) return;

    WGPUDevice device = ctx.device();

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(GRASS_SHADER);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Create uniform buffer
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.size = sizeof(GrassUniforms);
    uniformDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(device, &uniformDesc);

    // Create bind group layout
    WGPUBindGroupLayoutEntry layoutEntry = {};
    layoutEntry.binding = 0;
    layoutEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &layoutEntry;
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Vertex attributes for blade mesh
    WGPUVertexAttribute vertAttrs[4] = {};
    // Position
    vertAttrs[0].format = WGPUVertexFormat_Float32x3;
    vertAttrs[0].offset = offsetof(Vertex3D, position);
    vertAttrs[0].shaderLocation = 0;
    // Normal
    vertAttrs[1].format = WGPUVertexFormat_Float32x3;
    vertAttrs[1].offset = offsetof(Vertex3D, normal);
    vertAttrs[1].shaderLocation = 1;
    // UV
    vertAttrs[2].format = WGPUVertexFormat_Float32x2;
    vertAttrs[2].offset = offsetof(Vertex3D, uv);
    vertAttrs[2].shaderLocation = 2;
    // Color
    vertAttrs[3].format = WGPUVertexFormat_Float32x4;
    vertAttrs[3].offset = offsetof(Vertex3D, color);
    vertAttrs[3].shaderLocation = 3;

    WGPUVertexBufferLayout vertexLayout = {};
    vertexLayout.arrayStride = sizeof(Vertex3D);
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.attributeCount = 4;
    vertexLayout.attributes = vertAttrs;

    // Instance attributes
    WGPUVertexAttribute instAttrs[5] = {};
    // Model matrix (4 vec4s)
    instAttrs[0].format = WGPUVertexFormat_Float32x4;
    instAttrs[0].offset = 0;
    instAttrs[0].shaderLocation = 4;
    instAttrs[1].format = WGPUVertexFormat_Float32x4;
    instAttrs[1].offset = 16;
    instAttrs[1].shaderLocation = 5;
    instAttrs[2].format = WGPUVertexFormat_Float32x4;
    instAttrs[2].offset = 32;
    instAttrs[2].shaderLocation = 6;
    instAttrs[3].format = WGPUVertexFormat_Float32x4;
    instAttrs[3].offset = 48;
    instAttrs[3].shaderLocation = 7;
    // Color + phase
    instAttrs[4].format = WGPUVertexFormat_Float32x4;
    instAttrs[4].offset = 64;
    instAttrs[4].shaderLocation = 8;

    WGPUVertexBufferLayout instanceLayout = {};
    instanceLayout.arrayStride = sizeof(GPUGrassInstance);
    instanceLayout.stepMode = WGPUVertexStepMode_Instance;
    instanceLayout.attributeCount = 5;
    instanceLayout.attributes = instAttrs;

    WGPUVertexBufferLayout bufferLayouts[2] = {vertexLayout, instanceLayout};

    // Color target with alpha blending for soft tips
    WGPUBlendState blendState = {};
    blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.color.operation = WGPUBlendOperation_Add;
    blendState.alpha.srcFactor = WGPUBlendFactor_One;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;

    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_RGBA16Float;
    colorTarget.blend = &blendState;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    // Depth stencil state
    WGPUDepthStencilState depthState = {};
    depthState.format = DEPTH_FORMAT;
    depthState.depthWriteEnabled = WGPUOptionalBool_True;
    depthState.depthCompare = WGPUCompareFunction_Less;

    // Render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.vertex.bufferCount = 2;
    pipelineDesc.vertex.buffers = bufferLayouts;
    pipelineDesc.fragment = &fragmentState;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;  // Grass is double-sided
    pipelineDesc.depthStencil = &depthState;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    m_pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);

    // Create vertex buffer
    WGPUBufferDescriptor vbDesc = {};
    vbDesc.size = m_bladeMesh.vertices.size() * sizeof(Vertex3D);
    vbDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
    m_vertexBuffer = wgpuDeviceCreateBuffer(device, &vbDesc);
    wgpuQueueWriteBuffer(ctx.queue(), m_vertexBuffer, 0,
                         m_bladeMesh.vertices.data(), vbDesc.size);

    // Create index buffer
    WGPUBufferDescriptor ibDesc = {};
    ibDesc.size = m_bladeMesh.indices.size() * sizeof(uint32_t);
    ibDesc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
    m_indexBuffer = wgpuDeviceCreateBuffer(device, &ibDesc);
    wgpuQueueWriteBuffer(ctx.queue(), m_indexBuffer, 0,
                         m_bladeMesh.indices.data(), ibDesc.size);

    m_pipelineCreated = true;
}

void GrassField::uploadInstances(Context& ctx) {
    if (m_instances.empty()) return;

    size_t requiredSize = m_instances.size() * sizeof(GPUGrassInstance);

    // Resize buffer if needed
    if (m_instanceCapacity < m_instances.size()) {
        if (m_instanceBuffer) {
            wgpuBufferRelease(m_instanceBuffer);
        }

        WGPUBufferDescriptor desc = {};
        desc.size = requiredSize;
        desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        m_instanceBuffer = wgpuDeviceCreateBuffer(ctx.device(), &desc);
        m_instanceCapacity = m_instances.size();
    }

    // Convert and upload instance data
    std::vector<GPUGrassInstance> gpuInstances;
    gpuInstances.reserve(m_instances.size());

    for (const auto& inst : m_instances) {
        GPUGrassInstance gpu = {};
        memcpy(gpu.model, glm::value_ptr(inst.transform), sizeof(gpu.model));
        gpu.color[0] = inst.color.r;
        gpu.color[1] = inst.color.g;
        gpu.color[2] = inst.color.b;
        gpu.color[3] = inst.color.a;
        gpuInstances.push_back(gpu);
    }

    wgpuQueueWriteBuffer(ctx.queue(), m_instanceBuffer, 0,
                         gpuInstances.data(), requiredSize);

    m_instancesDirty = false;
}

void GrassField::process(Context& ctx) {
    if (!m_initialized) init(ctx);

    createDepthBuffer(ctx);

    // Regenerate instances if parameters changed
    generateInstances();

    if (m_instancesDirty) {
        uploadInstances(ctx);
    }

    if (!m_cameraOp || m_instances.empty()) {
        return;
    }

    WGPUDevice device = ctx.device();

    // Get camera data
    const Camera3D& cam = m_cameraOp->outputCamera();
    glm::mat4 viewProj = cam.viewProjectionMatrix();
    glm::vec3 camPos = cam.getPosition();

    // Update uniforms
    GrassUniforms uniforms = {};
    memcpy(uniforms.viewProj, glm::value_ptr(viewProj), sizeof(uniforms.viewProj));
    uniforms.cameraPos[0] = camPos.x;
    uniforms.cameraPos[1] = camPos.y;
    uniforms.cameraPos[2] = camPos.z;
    uniforms.time = static_cast<float>(ctx.time());

    uniforms.baseColor[0] = baseColor[0];
    uniforms.baseColor[1] = baseColor[1];
    uniforms.baseColor[2] = baseColor[2];
    uniforms.windStrength = static_cast<float>(windStrength);

    uniforms.tipColor[0] = tipColor[0];
    uniforms.tipColor[1] = tipColor[1];
    uniforms.tipColor[2] = tipColor[2];
    uniforms.windSpeed = static_cast<float>(windSpeed);

    // Normalize wind direction
    glm::vec2 windDir(static_cast<float>(windDirX), static_cast<float>(windDirZ));
    if (glm::length(windDir) > 0.001f) {
        windDir = glm::normalize(windDir);
    }
    uniforms.windDir[0] = windDir.x;
    uniforms.windDir[1] = windDir.y;

    // Lights
    uniforms.lightCount = static_cast<uint32_t>(m_lightOps.size());
    for (size_t i = 0; i < m_lightOps.size() && i < MAX_LIGHTS; i++) {
        uniforms.lights[i] = toGPULight(m_lightOps[i]->outputLight());
    }

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create bind group
    WGPUBindGroupEntry bindEntry = {};
    bindEntry.binding = 0;
    bindEntry.buffer = m_uniformBuffer;
    bindEntry.size = sizeof(GrassUniforms);

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 1;
    bindDesc.entries = &bindEntry;
    WGPUBindGroup bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

    // Render
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = m_outputView;
    colorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
    colorAttachment.loadOp = WGPULoadOp_Clear;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {m_clearColor.r, m_clearColor.g, m_clearColor.b, m_clearColor.a};

    WGPURenderPassDepthStencilAttachment depthAttachment = {};
    depthAttachment.view = m_depthView;
    depthAttachment.depthLoadOp = WGPULoadOp_Clear;
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    depthAttachment.depthClearValue = 1.0f;

    WGPURenderPassDescriptor passDesc = {};
    passDesc.colorAttachmentCount = 1;
    passDesc.colorAttachments = &colorAttachment;
    passDesc.depthStencilAttachment = &depthAttachment;

    WGPURenderPassEncoder pass = wgpuCommandEncoderBeginRenderPass(encoder, &passDesc);
    wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, bindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, m_vertexBuffer, 0,
                                          m_bladeMesh.vertices.size() * sizeof(Vertex3D));
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, m_instanceBuffer, 0,
                                          m_instances.size() * sizeof(GPUGrassInstance));
    wgpuRenderPassEncoderSetIndexBuffer(pass, m_indexBuffer, WGPUIndexFormat_Uint32, 0,
                                         m_bladeMesh.indices.size() * sizeof(uint32_t));
    wgpuRenderPassEncoderDrawIndexed(pass,
                                      static_cast<uint32_t>(m_bladeMesh.indices.size()),
                                      static_cast<uint32_t>(m_instances.size()),
                                      0, 0, 0);
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(ctx.queue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    wgpuBindGroupRelease(bindGroup);

    didCook();
}

void GrassField::cleanup() {
    if (m_pipeline) {
        wgpuRenderPipelineRelease(m_pipeline);
        m_pipeline = nullptr;
    }
    if (m_bindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_bindGroupLayout);
        m_bindGroupLayout = nullptr;
    }
    if (m_uniformBuffer) {
        wgpuBufferRelease(m_uniformBuffer);
        m_uniformBuffer = nullptr;
    }
    if (m_vertexBuffer) {
        wgpuBufferRelease(m_vertexBuffer);
        m_vertexBuffer = nullptr;
    }
    if (m_indexBuffer) {
        wgpuBufferRelease(m_indexBuffer);
        m_indexBuffer = nullptr;
    }
    if (m_instanceBuffer) {
        wgpuBufferRelease(m_instanceBuffer);
        m_instanceBuffer = nullptr;
    }
    if (m_depthView) {
        wgpuTextureViewRelease(m_depthView);
        m_depthView = nullptr;
    }
    if (m_depthTexture) {
        wgpuTextureRelease(m_depthTexture);
        m_depthTexture = nullptr;
    }

    releaseOutput();
    m_pipelineCreated = false;
    m_initialized = false;
}

std::vector<ParamDecl> GrassField::params() {
    return {
        fieldWidth.decl(),
        fieldDepth.decl(),
        bladeCount.decl(),
        seed.decl(),
        bladeHeight.decl(),
        bladeWidth.decl(),
        heightVariation.decl(),
        windStrength.decl(),
        windSpeed.decl(),
        windDirX.decl(),
        windDirZ.decl()
    };
}

bool GrassField::getParam(const std::string& name, float out[4]) {
    if (name == "fieldWidth") { out[0] = static_cast<float>(fieldWidth); return true; }
    if (name == "fieldDepth") { out[0] = static_cast<float>(fieldDepth); return true; }
    if (name == "bladeCount") { out[0] = static_cast<float>(static_cast<int>(bladeCount)); return true; }
    if (name == "seed") { out[0] = static_cast<float>(static_cast<int>(seed)); return true; }
    if (name == "bladeHeight") { out[0] = static_cast<float>(bladeHeight); return true; }
    if (name == "bladeWidth") { out[0] = static_cast<float>(bladeWidth); return true; }
    if (name == "heightVariation") { out[0] = static_cast<float>(heightVariation); return true; }
    if (name == "windStrength") { out[0] = static_cast<float>(windStrength); return true; }
    if (name == "windSpeed") { out[0] = static_cast<float>(windSpeed); return true; }
    if (name == "windDirX") { out[0] = static_cast<float>(windDirX); return true; }
    if (name == "windDirZ") { out[0] = static_cast<float>(windDirZ); return true; }
    if (name == "baseColor") { out[0] = baseColor[0]; out[1] = baseColor[1]; out[2] = baseColor[2]; return true; }
    if (name == "tipColor") { out[0] = tipColor[0]; out[1] = tipColor[1]; out[2] = tipColor[2]; return true; }
    return false;
}

bool GrassField::setParam(const std::string& name, const float value[4]) {
    if (name == "fieldWidth") { fieldWidth = value[0]; m_instancesDirty = true; markDirty(); return true; }
    if (name == "fieldDepth") { fieldDepth = value[0]; m_instancesDirty = true; markDirty(); return true; }
    if (name == "bladeCount") { bladeCount = static_cast<int>(value[0]); m_instancesDirty = true; markDirty(); return true; }
    if (name == "seed") { seed = static_cast<int>(value[0]); m_instancesDirty = true; markDirty(); return true; }
    if (name == "bladeHeight") { bladeHeight = value[0]; m_instancesDirty = true; markDirty(); return true; }
    if (name == "bladeWidth") { bladeWidth = value[0]; markDirty(); return true; }
    if (name == "heightVariation") { heightVariation = value[0]; m_instancesDirty = true; markDirty(); return true; }
    if (name == "windStrength") { windStrength = value[0]; markDirty(); return true; }
    if (name == "windSpeed") { windSpeed = value[0]; markDirty(); return true; }
    if (name == "windDirX") { windDirX = value[0]; markDirty(); return true; }
    if (name == "windDirZ") { windDirZ = value[0]; markDirty(); return true; }
    if (name == "baseColor") { baseColor[0] = value[0]; baseColor[1] = value[1]; baseColor[2] = value[2]; markDirty(); return true; }
    if (name == "tipColor") { tipColor[0] = value[0]; tipColor[1] = value[1]; tipColor[2] = value[2]; markDirty(); return true; }
    return false;
}

} // namespace vivid::render3d
