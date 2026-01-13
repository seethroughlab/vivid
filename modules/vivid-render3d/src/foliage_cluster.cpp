// FoliageCluster - GPU-instanced procedural fronds with wind animation
// Generates plant geometry with curved stems and tapered leaflets

#include <vivid/render3d/foliage_cluster.h>
#include <vivid/operator_registry.h>
#include <vivid/context.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <random>
#include <cmath>

namespace vivid::render3d {

REGISTER_OPERATOR(FoliageCluster, "3D Vegetation", "Procedural fronds with wind animation", false);

using namespace vivid::effects;

namespace {

constexpr WGPUTextureFormat DEPTH_FORMAT = WGPUTextureFormat_Depth24Plus;
constexpr uint32_t MAX_LIGHTS = 4;

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

// Uniform buffer for frond rendering
// WGSL alignment: vec3f needs 16-byte alignment, array<Light> needs 16-byte alignment
struct FrondUniforms {
    float viewProj[16];         // 64 bytes, offset 0-63
    float cameraPos[3];         // 12 bytes, offset 64-75
    float time;                 // 4 bytes, offset 76-79
    float baseColor[3];         // 12 bytes, offset 80-91
    float windStrength;         // 4 bytes, offset 92-95
    float tipColor[3];          // 12 bytes, offset 96-107
    float windSpeed;            // 4 bytes, offset 108-111
    float windDir[2];           // 8 bytes, offset 112-119
    float stemLength;           // 4 bytes, offset 120-123
    float stemCurve;            // 4 bytes, offset 124-127
    uint32_t lightCount;        // 4 bytes, offset 128-131
    float _pad0[3];             // 12 bytes, offset 132-143 (padding to align vec3f to 16)
    float _pad1[3];             // 12 bytes, offset 144-155 (vec3f _pad0 in shader)
    float _pad2;                // 4 bytes, offset 156-159 (padding to align vec4f to 16)
    float _pad3[4];             // 16 bytes, offset 160-175 (vec4f _pad1 in shader)
    GPULight lights[MAX_LIGHTS]; // 256 bytes, offset 176-431
};

static_assert(sizeof(FrondUniforms) == 432, "FrondUniforms must be 432 bytes");

// GPU instance data (80 bytes)
struct GPUFrondInstance {
    float model[16];      // 64 bytes - transform matrix
    float variation[4];   // 16 bytes - scale.xyz + phase in w
};

static_assert(sizeof(GPUFrondInstance) == 80, "GPUFrondInstance must be 80 bytes");

// Frond shader with wind animation
const char* FROND_SHADER = R"(
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
    stemLength: f32,
    stemCurve: f32,
    lightCount: u32,
    _pad0: vec3f,
    _pad1: vec4f,
    lights: array<Light, 4>,
}

struct InstanceData {
    @location(4) model0: vec4f,
    @location(5) model1: vec4f,
    @location(6) model2: vec4f,
    @location(7) model3: vec4f,
    @location(8) variation: vec4f,  // scale.xyz + phase in w
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
    @location(3) heightParam: f32,
}

@vertex
fn vs_main(vert: VertexInput, inst: InstanceData) -> VertexOutput {
    var out: VertexOutput;

    // Reconstruct model matrix and apply instance scale
    let model = mat4x4f(inst.model0, inst.model1, inst.model2, inst.model3);
    let scale = inst.variation.xyz;

    // Scale the local position
    var localPos = vert.position * scale;

    // Transform to world space
    var worldPos = (model * vec4f(localPos, 1.0)).xyz;

    // Height parameter from UV.y (0 at base, 1 at tip)
    let heightParam = vert.uv.y;
    out.heightParam = heightParam;

    // Wind animation
    let phaseOffset = inst.variation.w * PI * 2.0;

    // Primary sway - affects whole frond
    let swayPhase = worldPos.x * 0.3 + worldPos.z * 0.2 + uniforms.time * uniforms.windSpeed + phaseOffset;
    let sway = sin(swayPhase) * uniforms.windStrength;

    // Secondary flutter - higher frequency for leaflet tips
    let flutterPhase = worldPos.x * 1.5 + worldPos.z * 1.2 + uniforms.time * uniforms.windSpeed * 2.5 + phaseOffset * 3.1;
    let flutter = sin(flutterPhase) * uniforms.windStrength * 0.15;

    // Displacement increases with height (tips move more)
    let windDisplacement = (sway + flutter) * heightParam * heightParam;

    // Apply wind
    worldPos.x += uniforms.windDir.x * windDisplacement;
    worldPos.z += uniforms.windDir.y * windDisplacement;

    // Droop from wind
    worldPos.y -= abs(windDisplacement) * 0.3;

    out.worldPos = worldPos;
    out.clipPos = uniforms.viewProj * vec4f(worldPos, 1.0);

    // Transform normal
    let normalMat = mat3x3f(model[0].xyz, model[1].xyz, model[2].xyz);
    out.worldNormal = normalize(normalMat * vert.normal);

    // Color gradient from base to tip
    out.color = mix(uniforms.baseColor, uniforms.tipColor, heightParam);

    return out;
}

fn calculateLighting(worldPos: vec3f, normal: vec3f) -> f32 {
    var totalLight = 0.35;  // Ambient

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
        } else {
            let toLight = light.position - worldPos;
            let dist = length(toLight);
            lightDir = toLight / dist;
            attenuation = max(0.0, 1.0 - dist / light.range);
            let spotCos = dot(-lightDir, normalize(light.direction));
            let spotFade = clamp((spotCos - light.spotAngle) / (light.spotBlend - light.spotAngle), 0.0, 1.0);
            attenuation *= spotFade * spotFade;
        }

        // Two-sided lighting for foliage
        let NdotL = abs(dot(normal, lightDir));
        totalLight += NdotL * light.intensity * attenuation;
    }

    return min(totalLight, 2.0);
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let lighting = calculateLighting(input.worldPos, input.worldNormal);
    let finalColor = input.color * lighting;
    return vec4f(finalColor, 1.0);
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

// Evaluate quadratic bezier curve for stem shape
glm::vec3 evalStemCurve(float t, float length, float curve) {
    // Start at origin, curve outward and down
    // Control points: P0 = origin, P1 = (0, length*0.5, 0), P2 = (0, length*(1-curve), length*curve)
    float y = length * t * (1.0f - curve * t);  // Parabolic droop
    float z = length * t;  // Forward extension (will be rotated per-instance)
    return glm::vec3(0.0f, y, z);
}

// Get tangent direction along stem
glm::vec3 evalStemTangent(float t, float length, float curve) {
    float dy = length * (1.0f - 2.0f * curve * t);
    float dz = length;
    return glm::normalize(glm::vec3(0.0f, dy, dz));
}

} // namespace

FoliageCluster::FoliageCluster() {
    registerParam(fieldWidth);
    registerParam(fieldDepth);
    registerParam(frondCount);
    registerParam(seed);
    registerParam(baseHeight);
    registerParam(stemLength);
    registerParam(stemCurve);
    registerParam(leafletPairs);
    registerParam(leafletWidth);
    registerParam(leafletLength);
    registerParam(leafletAngle);
    registerParam(sizeVariation);
    registerParam(windStrength);
    registerParam(windSpeed);
    registerParam(windDirX);
    registerParam(windDirZ);
}

FoliageCluster::~FoliageCluster() {
    cleanup();
}

void FoliageCluster::setPlantType(PlantType type) {
    if (m_plantType != type) {
        m_plantType = type;
        applyPreset(type);
        m_meshDirty = true;
        m_instancesDirty = true;
        markDirty();
    }
}

void FoliageCluster::applyPreset(PlantType type) {
    switch (type) {
        case PlantType::Fern:
            stemLength = 0.8f;
            stemCurve = 0.35f;
            leafletPairs = 10;
            leafletWidth = 0.08f;
            leafletLength = 0.2f;
            leafletAngle = 50.0f;
            windStrength = 0.35f;
            windSpeed = 1.0f;
            baseColor[0] = 0.06f; baseColor[1] = 0.15f; baseColor[2] = 0.03f;
            tipColor[0] = 0.12f; tipColor[1] = 0.3f; tipColor[2] = 0.06f;
            break;

        case PlantType::PalmFrond:
            stemLength = 1.5f;
            stemCurve = 0.5f;
            leafletPairs = 12;
            leafletWidth = 0.06f;
            leafletLength = 0.4f;
            leafletAngle = 35.0f;
            windStrength = 0.25f;
            windSpeed = 0.6f;
            baseColor[0] = 0.04f; baseColor[1] = 0.12f; baseColor[2] = 0.02f;
            tipColor[0] = 0.08f; tipColor[1] = 0.22f; tipColor[2] = 0.04f;
            break;

        case PlantType::Grass:
            stemLength = 0.5f;
            stemCurve = 0.2f;
            leafletPairs = 0;  // No leaflets, just stem
            leafletWidth = 0.03f;
            leafletLength = 0.0f;
            leafletAngle = 0.0f;
            windStrength = 0.5f;
            windSpeed = 1.2f;
            baseColor[0] = 0.1f; baseColor[1] = 0.25f; baseColor[2] = 0.05f;
            tipColor[0] = 0.2f; tipColor[1] = 0.4f; tipColor[2] = 0.1f;
            break;

        case PlantType::Custom:
            break;
    }
}

void FoliageCluster::setCameraInput(CameraOperator* cam) {
    if (m_cameraOp != cam) {
        m_cameraOp = cam;
        markDirty();
    }
}

void FoliageCluster::setLightInput(LightOperator* light) {
    m_lightOps.clear();
    if (light) {
        m_lightOps.push_back(light);
    }
    markDirty();
}

void FoliageCluster::addLight(LightOperator* light) {
    if (light && m_lightOps.size() < MAX_LIGHTS) {
        m_lightOps.push_back(light);
        markDirty();
    }
}

void FoliageCluster::setClearColor(float r, float g, float b, float a) {
    glm::vec4 newColor(r, g, b, a);
    if (m_clearColor != newColor) {
        m_clearColor = newColor;
        markDirty();
    }
}

void FoliageCluster::setResolution(int width, int height) {
    if (m_width != width || m_height != height) {
        TextureOperator::setResolution(width, height);
        markDirty();
    }
}

void FoliageCluster::generateFrondMesh() {
    m_frondMesh.vertices.clear();
    m_frondMesh.indices.clear();

    float length = static_cast<float>(stemLength);
    float curve = static_cast<float>(stemCurve);
    int pairs = static_cast<int>(leafletPairs);
    float lWidth = static_cast<float>(leafletWidth);
    float lLength = static_cast<float>(leafletLength);
    float lAngle = glm::radians(static_cast<float>(leafletAngle));

    // Stem segments (for smooth curve)
    const int stemSegments = std::max(pairs * 2, 8);
    const float stemWidth = 0.015f;  // Thin stem

    // Generate stem vertices
    std::vector<glm::vec3> stemPositions;
    std::vector<glm::vec3> stemTangents;

    for (int i = 0; i <= stemSegments; i++) {
        float t = static_cast<float>(i) / stemSegments;
        stemPositions.push_back(evalStemCurve(t, length, curve));
        stemTangents.push_back(evalStemTangent(t, length, curve));
    }

    // Create stem geometry (thin quad strip)
    for (int i = 0; i <= stemSegments; i++) {
        float t = static_cast<float>(i) / stemSegments;
        glm::vec3 pos = stemPositions[i];
        glm::vec3 tangent = stemTangents[i];

        // Perpendicular direction for stem width
        glm::vec3 right = glm::normalize(glm::cross(tangent, glm::vec3(1, 0, 0)));
        if (glm::length(right) < 0.01f) {
            right = glm::normalize(glm::cross(tangent, glm::vec3(0, 0, 1)));
        }

        float width = stemWidth * (1.0f - t * 0.5f);  // Taper stem

        // Left and right vertices
        Vertex3D left, rightV;
        left.position = pos - right * width;
        left.normal = glm::vec3(0, 0, 1);  // Face camera roughly
        left.uv = glm::vec2(0, t);
        left.color = glm::vec4(1);

        rightV.position = pos + right * width;
        rightV.normal = glm::vec3(0, 0, 1);
        rightV.uv = glm::vec2(1, t);
        rightV.color = glm::vec4(1);

        m_frondMesh.vertices.push_back(left);
        m_frondMesh.vertices.push_back(rightV);
    }

    // Create stem triangles
    for (int i = 0; i < stemSegments; i++) {
        uint32_t base = i * 2;
        m_frondMesh.indices.push_back(base);
        m_frondMesh.indices.push_back(base + 1);
        m_frondMesh.indices.push_back(base + 2);
        m_frondMesh.indices.push_back(base + 1);
        m_frondMesh.indices.push_back(base + 3);
        m_frondMesh.indices.push_back(base + 2);
    }

    // Generate leaflets (pinnae) along the stem
    if (pairs > 0) {
        for (int p = 0; p < pairs; p++) {
            // Position along stem (skip the very base and tip)
            float t = 0.1f + 0.8f * static_cast<float>(p + 1) / (pairs + 1);

            // Size falloff toward tip
            float sizeFactor = 1.0f - 0.6f * t;
            float currentLength = lLength * sizeFactor;
            float currentWidth = lWidth * sizeFactor;

            glm::vec3 stemPos = evalStemCurve(t, length, curve);
            glm::vec3 stemTangent = evalStemTangent(t, length, curve);

            // Create left and right leaflets
            for (int side = 0; side < 2; side++) {
                float sideSign = (side == 0) ? -1.0f : 1.0f;

                // Leaflet base direction (perpendicular to stem, angled down)
                glm::vec3 perpDir = glm::normalize(glm::cross(stemTangent, glm::vec3(0, 1, 0)));
                if (glm::length(perpDir) < 0.01f) {
                    perpDir = glm::vec3(1, 0, 0);
                }
                perpDir *= sideSign;

                // Angle the leaflet downward
                glm::vec3 leafletDir = glm::normalize(
                    perpDir * std::cos(lAngle) +
                    glm::vec3(0, -1, 0) * std::sin(lAngle) * 0.5f +
                    stemTangent * 0.3f  // Slight forward angle
                );

                // Leaflet tip position
                glm::vec3 tipPos = stemPos + leafletDir * currentLength;

                // Create tapered leaflet (triangle)
                // Base left, base right, tip
                glm::vec3 leafletPerp = glm::normalize(glm::cross(leafletDir, stemTangent));

                uint32_t baseIdx = static_cast<uint32_t>(m_frondMesh.vertices.size());

                // Leaflet normal (facing outward)
                glm::vec3 leafletNormal = glm::normalize(glm::cross(leafletDir, leafletPerp));
                if (leafletNormal.y < 0) leafletNormal = -leafletNormal;  // Face up-ish

                // Base left vertex
                Vertex3D v0;
                v0.position = stemPos - leafletPerp * currentWidth * 0.5f;
                v0.normal = leafletNormal;
                v0.uv = glm::vec2(0, t);  // UV.y = position along stem
                v0.color = glm::vec4(1);

                // Base right vertex
                Vertex3D v1;
                v1.position = stemPos + leafletPerp * currentWidth * 0.5f;
                v1.normal = leafletNormal;
                v1.uv = glm::vec2(1, t);
                v1.color = glm::vec4(1);

                // Tip vertex
                Vertex3D v2;
                v2.position = tipPos;
                v2.normal = leafletNormal;
                v2.uv = glm::vec2(0.5f, t + 0.15f);  // Slightly higher UV for tip color
                v2.color = glm::vec4(1);

                m_frondMesh.vertices.push_back(v0);
                m_frondMesh.vertices.push_back(v1);
                m_frondMesh.vertices.push_back(v2);

                // Triangle indices (both sides for double-sided)
                m_frondMesh.indices.push_back(baseIdx);
                m_frondMesh.indices.push_back(baseIdx + 1);
                m_frondMesh.indices.push_back(baseIdx + 2);

                // Back face
                m_frondMesh.indices.push_back(baseIdx + 2);
                m_frondMesh.indices.push_back(baseIdx + 1);
                m_frondMesh.indices.push_back(baseIdx);
            }
        }
    }

}

void FoliageCluster::generateInstances() {
    int count = static_cast<int>(frondCount);
    int currentSeed = static_cast<int>(seed);
    float currentBaseHeight = static_cast<float>(baseHeight);
    float currentFieldWidth = static_cast<float>(fieldWidth);
    float currentFieldDepth = static_cast<float>(fieldDepth);
    float currentSizeVariation = static_cast<float>(sizeVariation);

    bool instanceParamsChanged = (count != m_lastFrondCount ||
                                   currentSeed != m_lastSeed ||
                                   currentBaseHeight != m_lastBaseHeight ||
                                   currentFieldWidth != m_lastFieldWidth ||
                                   currentFieldDepth != m_lastFieldDepth ||
                                   currentSizeVariation != m_lastSizeVariation);

    if (!instanceParamsChanged && !m_instances.empty()) {
        return;
    }

    m_lastFrondCount = count;
    m_lastSeed = currentSeed;
    m_lastBaseHeight = currentBaseHeight;
    m_lastFieldWidth = currentFieldWidth;
    m_lastFieldDepth = currentFieldDepth;
    m_lastSizeVariation = currentSizeVariation;

    m_instances.clear();
    m_instances.reserve(count);

    std::mt19937 rng(currentSeed);
    std::uniform_real_distribution<float> distX(-static_cast<float>(fieldWidth) * 0.5f,
                                                 static_cast<float>(fieldWidth) * 0.5f);
    std::uniform_real_distribution<float> distZ(-static_cast<float>(fieldDepth) * 0.5f,
                                                 static_cast<float>(fieldDepth) * 0.5f);
    std::uniform_real_distribution<float> distRot(0.0f, glm::two_pi<float>());
    std::uniform_real_distribution<float> distTilt(-0.2f, 0.3f);  // Slight tilt variation
    std::uniform_real_distribution<float> distScale(1.0f - static_cast<float>(sizeVariation),
                                                    1.0f + static_cast<float>(sizeVariation));
    std::uniform_real_distribution<float> distPhase(0.0f, 1.0f);

    float yOffset = static_cast<float>(baseHeight);

    for (int i = 0; i < count; i++) {
        FrondInstance inst;

        float x = distX(rng);
        float z = distZ(rng);
        float rot = distRot(rng);
        float tilt = distTilt(rng);
        float scale = distScale(rng);
        float phase = distPhase(rng);

        // Build transform: translate, rotate around Y, slight tilt
        glm::mat4 transform = glm::translate(glm::mat4(1.0f), glm::vec3(x, yOffset, z));
        transform = glm::rotate(transform, rot, glm::vec3(0.0f, 1.0f, 0.0f));
        transform = glm::rotate(transform, tilt, glm::vec3(1.0f, 0.0f, 0.0f));

        inst.transform = transform;
        inst.variation = glm::vec4(scale, scale, scale, phase);

        m_instances.push_back(inst);
    }

    m_instancesDirty = true;
}

void FoliageCluster::init(Context& ctx) {
    if (m_initialized) return;

    if (m_width == 0 || m_height == 0) {
        m_width = 1280;
        m_height = 720;
    }

    createOutput(ctx);
    generateFrondMesh();
    generateInstances();
    createPipeline(ctx);

    m_initialized = true;
}

void FoliageCluster::createDepthBuffer(Context& ctx) {
    if (m_depthTexture && m_depthWidth == m_width && m_depthHeight == m_height) {
        return;
    }

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

void FoliageCluster::createPipeline(Context& ctx) {
    if (m_pipelineCreated) return;

    WGPUDevice device = ctx.device();

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(FROND_SHADER);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Create uniform buffer
    WGPUBufferDescriptor uniformDesc = {};
    uniformDesc.size = sizeof(FrondUniforms);
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

    // Vertex attributes
    WGPUVertexAttribute vertAttrs[4] = {};
    vertAttrs[0].format = WGPUVertexFormat_Float32x3;
    vertAttrs[0].offset = offsetof(Vertex3D, position);
    vertAttrs[0].shaderLocation = 0;
    vertAttrs[1].format = WGPUVertexFormat_Float32x3;
    vertAttrs[1].offset = offsetof(Vertex3D, normal);
    vertAttrs[1].shaderLocation = 1;
    vertAttrs[2].format = WGPUVertexFormat_Float32x2;
    vertAttrs[2].offset = offsetof(Vertex3D, uv);
    vertAttrs[2].shaderLocation = 2;
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
    instAttrs[4].format = WGPUVertexFormat_Float32x4;
    instAttrs[4].offset = 64;
    instAttrs[4].shaderLocation = 8;

    WGPUVertexBufferLayout instanceLayout = {};
    instanceLayout.arrayStride = sizeof(GPUFrondInstance);
    instanceLayout.stepMode = WGPUVertexStepMode_Instance;
    instanceLayout.attributeCount = 5;
    instanceLayout.attributes = instAttrs;

    WGPUVertexBufferLayout bufferLayouts[2] = {vertexLayout, instanceLayout};

    // Color target (no blending needed for solid geometry)
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_RGBA16Float;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    // Depth stencil
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
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;  // Double-sided foliage
    pipelineDesc.depthStencil = &depthState;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    m_pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);

    m_pipelineCreated = true;
}

void FoliageCluster::uploadMesh(Context& ctx) {
    if (m_frondMesh.vertices.empty()) return;

    size_t vertSize = m_frondMesh.vertices.size() * sizeof(Vertex3D);
    size_t idxSize = m_frondMesh.indices.size() * sizeof(uint32_t);

    // Recreate vertex buffer if needed
    if (m_vertexCapacity < m_frondMesh.vertices.size()) {
        if (m_vertexBuffer) wgpuBufferRelease(m_vertexBuffer);

        WGPUBufferDescriptor desc = {};
        desc.size = vertSize;
        desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        m_vertexBuffer = wgpuDeviceCreateBuffer(ctx.device(), &desc);
        m_vertexCapacity = m_frondMesh.vertices.size();
    }
    wgpuQueueWriteBuffer(ctx.queue(), m_vertexBuffer, 0,
                         m_frondMesh.vertices.data(), vertSize);

    // Recreate index buffer if needed
    if (m_indexCapacity < m_frondMesh.indices.size()) {
        if (m_indexBuffer) wgpuBufferRelease(m_indexBuffer);

        WGPUBufferDescriptor desc = {};
        desc.size = idxSize;
        desc.usage = WGPUBufferUsage_Index | WGPUBufferUsage_CopyDst;
        m_indexBuffer = wgpuDeviceCreateBuffer(ctx.device(), &desc);
        m_indexCapacity = m_frondMesh.indices.size();
    }
    wgpuQueueWriteBuffer(ctx.queue(), m_indexBuffer, 0,
                         m_frondMesh.indices.data(), idxSize);
}

void FoliageCluster::uploadInstances(Context& ctx) {
    if (m_instances.empty()) return;

    size_t requiredSize = m_instances.size() * sizeof(GPUFrondInstance);

    if (m_instanceCapacity < m_instances.size()) {
        if (m_instanceBuffer) wgpuBufferRelease(m_instanceBuffer);

        WGPUBufferDescriptor desc = {};
        desc.size = requiredSize;
        desc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        m_instanceBuffer = wgpuDeviceCreateBuffer(ctx.device(), &desc);
        m_instanceCapacity = m_instances.size();
    }

    std::vector<GPUFrondInstance> gpuInstances;
    gpuInstances.reserve(m_instances.size());

    for (const auto& inst : m_instances) {
        GPUFrondInstance gpu = {};
        memcpy(gpu.model, glm::value_ptr(inst.transform), sizeof(gpu.model));
        gpu.variation[0] = inst.variation.x;
        gpu.variation[1] = inst.variation.y;
        gpu.variation[2] = inst.variation.z;
        gpu.variation[3] = inst.variation.w;
        gpuInstances.push_back(gpu);
    }

    wgpuQueueWriteBuffer(ctx.queue(), m_instanceBuffer, 0,
                         gpuInstances.data(), requiredSize);

    m_instancesDirty = false;
}

void FoliageCluster::process(Context& ctx) {
    if (!m_initialized) init(ctx);

    createDepthBuffer(ctx);

    // Check if geometry params changed (direct assignment via ImGui)
    float currentStemLength = static_cast<float>(stemLength);
    float currentStemCurve = static_cast<float>(stemCurve);
    int currentPairs = static_cast<int>(leafletPairs);
    float currentLeafletWidth = static_cast<float>(leafletWidth);
    float currentLeafletLength = static_cast<float>(leafletLength);
    float currentLeafletAngle = static_cast<float>(leafletAngle);

    bool geomChanged = (currentStemLength != m_lastStemLength ||
                        currentStemCurve != m_lastStemCurve ||
                        currentPairs != m_lastLeafletPairs ||
                        currentLeafletWidth != m_lastLeafletWidth ||
                        currentLeafletLength != m_lastLeafletLength ||
                        currentLeafletAngle != m_lastLeafletAngle);

    if (geomChanged) {
        m_meshDirty = true;
        m_lastStemLength = currentStemLength;
        m_lastStemCurve = currentStemCurve;
        m_lastLeafletPairs = currentPairs;
        m_lastLeafletWidth = currentLeafletWidth;
        m_lastLeafletLength = currentLeafletLength;
        m_lastLeafletAngle = currentLeafletAngle;
    }

    // Regenerate and upload mesh if dirty or not yet created
    if (m_meshDirty || !m_vertexBuffer) {
        generateFrondMesh();
        uploadMesh(ctx);
        m_meshDirty = false;
    }

    // Regenerate instances if needed
    generateInstances();
    if (m_instancesDirty || !m_instanceBuffer) {
        uploadInstances(ctx);
    }

    if (!m_cameraOp || m_instances.empty() || m_frondMesh.vertices.empty()) {
        return;
    }

    WGPUDevice device = ctx.device();

    // Get camera data
    const Camera3D& cam = m_cameraOp->outputCamera();
    glm::mat4 viewProj = cam.viewProjectionMatrix();
    glm::vec3 camPos = cam.getPosition();

    // Update uniforms
    FrondUniforms uniforms = {};
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

    glm::vec2 windDir(static_cast<float>(windDirX), static_cast<float>(windDirZ));
    if (glm::length(windDir) > 0.001f) {
        windDir = glm::normalize(windDir);
    }
    uniforms.windDir[0] = windDir.x;
    uniforms.windDir[1] = windDir.y;
    uniforms.stemLength = static_cast<float>(stemLength);
    uniforms.stemCurve = static_cast<float>(stemCurve);

    uniforms.lightCount = static_cast<uint32_t>(m_lightOps.size());
    for (size_t i = 0; i < m_lightOps.size() && i < MAX_LIGHTS; i++) {
        uniforms.lights[i] = toGPULight(m_lightOps[i]->outputLight());
    }

    wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));

    // Create bind group
    WGPUBindGroupEntry bindEntry = {};
    bindEntry.binding = 0;
    bindEntry.buffer = m_uniformBuffer;
    bindEntry.size = sizeof(FrondUniforms);

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
                                          m_frondMesh.vertices.size() * sizeof(Vertex3D));
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, m_instanceBuffer, 0,
                                          m_instances.size() * sizeof(GPUFrondInstance));
    wgpuRenderPassEncoderSetIndexBuffer(pass, m_indexBuffer, WGPUIndexFormat_Uint32, 0,
                                         m_frondMesh.indices.size() * sizeof(uint32_t));
    wgpuRenderPassEncoderDrawIndexed(pass,
                                      static_cast<uint32_t>(m_frondMesh.indices.size()),
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

void FoliageCluster::cleanup() {
    if (m_pipeline) { wgpuRenderPipelineRelease(m_pipeline); m_pipeline = nullptr; }
    if (m_bindGroupLayout) { wgpuBindGroupLayoutRelease(m_bindGroupLayout); m_bindGroupLayout = nullptr; }
    if (m_uniformBuffer) { wgpuBufferRelease(m_uniformBuffer); m_uniformBuffer = nullptr; }
    if (m_vertexBuffer) { wgpuBufferRelease(m_vertexBuffer); m_vertexBuffer = nullptr; }
    if (m_indexBuffer) { wgpuBufferRelease(m_indexBuffer); m_indexBuffer = nullptr; }
    if (m_instanceBuffer) { wgpuBufferRelease(m_instanceBuffer); m_instanceBuffer = nullptr; }
    if (m_depthView) { wgpuTextureViewRelease(m_depthView); m_depthView = nullptr; }
    if (m_depthTexture) { wgpuTextureRelease(m_depthTexture); m_depthTexture = nullptr; }

    releaseOutput();
    m_pipelineCreated = false;
    m_initialized = false;
}

std::vector<ParamDecl> FoliageCluster::params() {
    return {
        fieldWidth.decl(), fieldDepth.decl(), frondCount.decl(), seed.decl(),
        baseHeight.decl(), stemLength.decl(), stemCurve.decl(), leafletPairs.decl(),
        leafletWidth.decl(), leafletLength.decl(), leafletAngle.decl(),
        sizeVariation.decl(), windStrength.decl(), windSpeed.decl(),
        windDirX.decl(), windDirZ.decl()
    };
}

bool FoliageCluster::getParam(const std::string& name, float out[4]) {
    if (name == "fieldWidth") { out[0] = static_cast<float>(fieldWidth); return true; }
    if (name == "fieldDepth") { out[0] = static_cast<float>(fieldDepth); return true; }
    if (name == "frondCount") { out[0] = static_cast<float>(static_cast<int>(frondCount)); return true; }
    if (name == "seed") { out[0] = static_cast<float>(static_cast<int>(seed)); return true; }
    if (name == "baseHeight") { out[0] = static_cast<float>(baseHeight); return true; }
    if (name == "stemLength") { out[0] = static_cast<float>(stemLength); return true; }
    if (name == "stemCurve") { out[0] = static_cast<float>(stemCurve); return true; }
    if (name == "leafletPairs") { out[0] = static_cast<float>(static_cast<int>(leafletPairs)); return true; }
    if (name == "leafletWidth") { out[0] = static_cast<float>(leafletWidth); return true; }
    if (name == "leafletLength") { out[0] = static_cast<float>(leafletLength); return true; }
    if (name == "leafletAngle") { out[0] = static_cast<float>(leafletAngle); return true; }
    if (name == "sizeVariation") { out[0] = static_cast<float>(sizeVariation); return true; }
    if (name == "windStrength") { out[0] = static_cast<float>(windStrength); return true; }
    if (name == "windSpeed") { out[0] = static_cast<float>(windSpeed); return true; }
    if (name == "windDirX") { out[0] = static_cast<float>(windDirX); return true; }
    if (name == "windDirZ") { out[0] = static_cast<float>(windDirZ); return true; }
    return false;
}

bool FoliageCluster::setParam(const std::string& name, const float value[4]) {
    if (name == "fieldWidth") { fieldWidth = value[0]; m_instancesDirty = true; markDirty(); return true; }
    if (name == "fieldDepth") { fieldDepth = value[0]; m_instancesDirty = true; markDirty(); return true; }
    if (name == "frondCount") { frondCount = static_cast<int>(value[0]); m_instancesDirty = true; markDirty(); return true; }
    if (name == "seed") { seed = static_cast<int>(value[0]); m_instancesDirty = true; markDirty(); return true; }
    if (name == "baseHeight") { baseHeight = value[0]; m_instancesDirty = true; markDirty(); return true; }
    if (name == "stemLength") { stemLength = value[0]; m_meshDirty = true; markDirty(); return true; }
    if (name == "stemCurve") { stemCurve = value[0]; m_meshDirty = true; markDirty(); return true; }
    if (name == "leafletPairs") { leafletPairs = static_cast<int>(value[0]); m_meshDirty = true; markDirty(); return true; }
    if (name == "leafletWidth") { leafletWidth = value[0]; m_meshDirty = true; markDirty(); return true; }
    if (name == "leafletLength") { leafletLength = value[0]; m_meshDirty = true; markDirty(); return true; }
    if (name == "leafletAngle") { leafletAngle = value[0]; m_meshDirty = true; markDirty(); return true; }
    if (name == "sizeVariation") { sizeVariation = value[0]; m_instancesDirty = true; markDirty(); return true; }
    if (name == "windStrength") { windStrength = value[0]; markDirty(); return true; }
    if (name == "windSpeed") { windSpeed = value[0]; markDirty(); return true; }
    if (name == "windDirX") { windDirX = value[0]; markDirty(); return true; }
    if (name == "windDirZ") { windDirZ = value[0]; markDirty(); return true; }
    return false;
}

} // namespace vivid::render3d
