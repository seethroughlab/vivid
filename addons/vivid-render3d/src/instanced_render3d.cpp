// InstancedRender3D - GPU-instanced 3D mesh rendering
// Renders thousands of identical meshes in a single draw call

#include <vivid/render3d/instanced_render3d.h>
#include <vivid/context.h>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/matrix_inverse.hpp>
#include <iostream>
#include <cmath>

namespace vivid::render3d {

using namespace vivid::effects;

namespace {

constexpr WGPUTextureFormat DEPTH_FORMAT = WGPUTextureFormat_Depth24Plus;
constexpr uint32_t MAX_LIGHTS = 4;
constexpr uint32_t LIGHT_TYPE_DIRECTIONAL = 0;
constexpr uint32_t LIGHT_TYPE_POINT = 1;
constexpr uint32_t LIGHT_TYPE_SPOT = 2;

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

// Uniform buffer for instanced PBR rendering
struct InstancedUniforms {
    float viewProj[16];         // View-projection matrix
    float cameraPos[3];
    float ambientIntensity;
    float baseColor[4];
    float metallic;
    float roughness;
    uint32_t lightCount;
    float _pad0;
    GPULight lights[MAX_LIGHTS];
};

static_assert(sizeof(GPULight) == 64, "GPULight must be 64 bytes");
static_assert(sizeof(InstancedUniforms) == 368, "InstancedUniforms size check");

// GPU instance data (must match shader)
struct GPUInstance {
    float model[16];      // 64 bytes - transform matrix
    float color[4];       // 16 bytes - RGBA color
    float metallic;       // 4 bytes
    float roughness;      // 4 bytes
    float _pad[2];        // 8 bytes padding
};

static_assert(sizeof(GPUInstance) == 96, "GPUInstance must be 96 bytes");

// Convert Instance3D to GPU format
GPUInstance toGPUInstance(const Instance3D& inst) {
    GPUInstance gpu = {};
    memcpy(gpu.model, glm::value_ptr(inst.transform), sizeof(gpu.model));
    gpu.color[0] = inst.color.r;
    gpu.color[1] = inst.color.g;
    gpu.color[2] = inst.color.b;
    gpu.color[3] = inst.color.a;
    gpu.metallic = inst.metallic;
    gpu.roughness = inst.roughness;
    return gpu;
}

// Convert LightData to GPULight
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
        case LightType::Directional: gpu.type = LIGHT_TYPE_DIRECTIONAL; break;
        case LightType::Point:       gpu.type = LIGHT_TYPE_POINT; break;
        case LightType::Spot:        gpu.type = LIGHT_TYPE_SPOT; break;
    }
    return gpu;
}

// Instanced PBR shader with multi-light support
const char* INSTANCED_SHADER = R"(
const PI: f32 = 3.14159265359;
const EPSILON: f32 = 0.0001;
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
    ambientIntensity: f32,
    baseColor: vec4f,
    metallic: f32,
    roughness: f32,
    lightCount: u32,
    _pad0: f32,
    lights: array<Light, 4>,
}

struct InstanceData {
    @location(5) model0: vec4f,
    @location(6) model1: vec4f,
    @location(7) model2: vec4f,
    @location(8) model3: vec4f,
    @location(9) color: vec4f,
    @location(10) metallicRoughness: vec2f,
    @location(11) _pad: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec4f,
    @location(3) uv: vec2f,
    @location(4) color: vec4f,
}

struct VertexOutput {
    @builtin(position) clipPos: vec4f,
    @location(0) worldPos: vec3f,
    @location(1) worldNormal: vec3f,
    @location(2) color: vec4f,
    @location(3) metallic: f32,
    @location(4) roughness: f32,
}

@vertex
fn vs_main(vert: VertexInput, inst: InstanceData) -> VertexOutput {
    var out: VertexOutput;

    // Reconstruct model matrix from instance data
    let model = mat4x4f(inst.model0, inst.model1, inst.model2, inst.model3);

    let worldPos = model * vec4f(vert.position, 1.0);
    out.worldPos = worldPos.xyz;
    out.clipPos = uniforms.viewProj * worldPos;

    // Transform normal (using upper-left 3x3, assumes uniform scale or orthonormal)
    let normalMat = mat3x3f(model[0].xyz, model[1].xyz, model[2].xyz);
    out.worldNormal = normalize(normalMat * vert.normal);

    // Combine instance color with vertex color and base color
    out.color = inst.color * vert.color * uniforms.baseColor;

    // Per-instance material properties (0 = use uniform default)
    out.metallic = select(uniforms.metallic, inst.metallicRoughness.x, inst.metallicRoughness.x > 0.0);
    out.roughness = select(uniforms.roughness, inst.metallicRoughness.y, inst.metallicRoughness.y > 0.0);

    return out;
}

fn D_GGX(NdotH: f32, roughness: f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let NdotH2 = NdotH * NdotH;
    let denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom + EPSILON);
}

fn G_SchlickGGX(NdotV: f32, roughness: f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k + EPSILON);
}

fn G_Smith(NdotV: f32, NdotL: f32, roughness: f32) -> f32 {
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

fn F_Schlick(cosTheta: f32, F0: vec3f) -> vec3f {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

fn getAttenuation(distance: f32, range: f32) -> f32 {
    if (range <= 0.0) { return 1.0; }
    let d = max(distance, EPSILON);
    let att = 1.0 / (d * d);
    let falloff = saturate(1.0 - pow(d / range, 4.0));
    return att * falloff * falloff;
}

fn getSpotFactor(lightDir: vec3f, spotDir: vec3f, innerAngle: f32, outerAngle: f32) -> f32 {
    let cosAngle = dot(lightDir, spotDir);
    return saturate((cosAngle - outerAngle) / max(innerAngle - outerAngle, EPSILON));
}

fn calculateLightContribution(
    light: Light,
    worldPos: vec3f,
    N: vec3f,
    V: vec3f,
    albedo: vec3f,
    metallic: f32,
    roughness: f32,
    F0: vec3f
) -> vec3f {
    var L: vec3f;
    var radiance: vec3f;

    if (light.lightType == LIGHT_DIRECTIONAL) {
        L = normalize(light.direction);
        radiance = light.color * light.intensity;
    } else if (light.lightType == LIGHT_POINT) {
        let lightVec = light.position - worldPos;
        let dist = length(lightVec);
        L = lightVec / max(dist, EPSILON);
        let att = getAttenuation(dist, light.range);
        radiance = light.color * light.intensity * att;
    } else {
        let lightVec = light.position - worldPos;
        let dist = length(lightVec);
        L = lightVec / max(dist, EPSILON);
        let att = getAttenuation(dist, light.range);
        let spot = getSpotFactor(-L, normalize(light.direction), light.spotBlend, light.spotAngle);
        radiance = light.color * light.intensity * att * spot;
    }

    let H = normalize(V + L);
    let NdotL = max(dot(N, L), 0.0);
    let NdotV = max(dot(N, V), EPSILON);
    let NdotH = max(dot(N, H), 0.0);
    let HdotV = max(dot(H, V), 0.0);

    if (NdotL <= 0.0) { return vec3f(0.0); }

    let D = D_GGX(NdotH, roughness);
    let G = G_Smith(NdotV, NdotL, roughness);
    let F = F_Schlick(HdotV, F0);

    let numerator = D * G * F;
    let denominator = 4.0 * NdotV * NdotL + EPSILON;
    let specular = numerator / denominator;

    let kS = F;
    var kD = vec3f(1.0) - kS;
    kD *= 1.0 - metallic;

    let diffuse = kD * albedo / PI;
    return (diffuse + specular) * radiance * NdotL;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let N = normalize(in.worldNormal);
    let V = normalize(uniforms.cameraPos - in.worldPos);

    let albedo = in.color.rgb;
    let metallic = in.metallic;
    let roughness = max(in.roughness, 0.04);
    let F0 = mix(vec3f(0.04), albedo, metallic);

    var Lo = vec3f(0.0);
    let lightCount = min(uniforms.lightCount, MAX_LIGHTS);
    for (var i = 0u; i < lightCount; i++) {
        Lo += calculateLightContribution(
            uniforms.lights[i], in.worldPos, N, V, albedo, metallic, roughness, F0
        );
    }

    let ambient = vec3f(0.03) * albedo * uniforms.ambientIntensity;

    var color = ambient + Lo;
    color = color / (color + vec3f(1.0));  // Reinhard
    color = pow(color, vec3f(1.0 / 2.2));  // Gamma

    return vec4f(color, in.color.a);
}
)";

// Textured instanced PBR shader with multi-light support
const char* INSTANCED_TEXTURED_SHADER = R"(
const PI: f32 = 3.14159265359;
const EPSILON: f32 = 0.0001;
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
    ambientIntensity: f32,
    baseColorFactor: vec4f,
    metallicFactor: f32,
    roughnessFactor: f32,
    normalScale: f32,
    aoStrength: f32,
    lightCount: u32,
    _pad0: u32,
    _pad1: u32,
    _pad2: u32,
    @align(16) lights: array<Light, 4>,
}

struct InstanceData {
    @location(5) model0: vec4f,
    @location(6) model1: vec4f,
    @location(7) model2: vec4f,
    @location(8) model3: vec4f,
    @location(9) color: vec4f,
    @location(10) metallicRoughness: vec2f,
    @location(11) _pad: vec2f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var materialSampler: sampler;
@group(0) @binding(2) var baseColorMap: texture_2d<f32>;
@group(0) @binding(3) var normalMap: texture_2d<f32>;
@group(0) @binding(4) var metallicMap: texture_2d<f32>;
@group(0) @binding(5) var roughnessMap: texture_2d<f32>;
@group(0) @binding(6) var aoMap: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec4f,
    @location(3) uv: vec2f,
    @location(4) color: vec4f,
}

struct VertexOutput {
    @builtin(position) clipPos: vec4f,
    @location(0) worldPos: vec3f,
    @location(1) worldNormal: vec3f,
    @location(2) worldTangent: vec3f,
    @location(3) worldBitangent: vec3f,
    @location(4) uv: vec2f,
    @location(5) color: vec4f,
}

@vertex
fn vs_main(vert: VertexInput, inst: InstanceData) -> VertexOutput {
    var out: VertexOutput;

    let model = mat4x4f(inst.model0, inst.model1, inst.model2, inst.model3);
    let worldPos = model * vec4f(vert.position, 1.0);
    out.worldPos = worldPos.xyz;
    out.clipPos = uniforms.viewProj * worldPos;

    let normalMat = mat3x3f(model[0].xyz, model[1].xyz, model[2].xyz);
    let N = normalize(normalMat * vert.normal);
    let T = normalize(normalMat * vert.tangent.xyz);
    let B = cross(N, T) * vert.tangent.w;

    out.worldNormal = N;
    out.worldTangent = T;
    out.worldBitangent = B;
    out.uv = vert.uv;
    out.color = inst.color * vert.color;

    return out;
}

fn D_GGX(NdotH: f32, roughness: f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let NdotH2 = NdotH * NdotH;
    let denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom + EPSILON);
}

fn G_SchlickGGX(NdotV: f32, roughness: f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k + EPSILON);
}

fn G_Smith(NdotV: f32, NdotL: f32, roughness: f32) -> f32 {
    return G_SchlickGGX(NdotV, roughness) * G_SchlickGGX(NdotL, roughness);
}

fn F_Schlick(cosTheta: f32, F0: vec3f) -> vec3f {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

fn getAttenuation(distance: f32, range: f32) -> f32 {
    if (range <= 0.0) { return 1.0; }
    let d = max(distance, EPSILON);
    let att = 1.0 / (d * d);
    let falloff = saturate(1.0 - pow(d / range, 4.0));
    return att * falloff * falloff;
}

fn getSpotFactor(lightDir: vec3f, spotDir: vec3f, innerAngle: f32, outerAngle: f32) -> f32 {
    let cosAngle = dot(lightDir, spotDir);
    return saturate((cosAngle - outerAngle) / max(innerAngle - outerAngle, EPSILON));
}

fn calculateLightContribution(
    light: Light,
    worldPos: vec3f,
    N: vec3f,
    V: vec3f,
    albedo: vec3f,
    metallic: f32,
    roughness: f32,
    F0: vec3f
) -> vec3f {
    var L: vec3f;
    var radiance: vec3f;

    if (light.lightType == LIGHT_DIRECTIONAL) {
        L = normalize(light.direction);
        radiance = light.color * light.intensity;
    } else if (light.lightType == LIGHT_POINT) {
        let lightVec = light.position - worldPos;
        let dist = length(lightVec);
        L = lightVec / max(dist, EPSILON);
        let att = getAttenuation(dist, light.range);
        radiance = light.color * light.intensity * att;
    } else {
        let lightVec = light.position - worldPos;
        let dist = length(lightVec);
        L = lightVec / max(dist, EPSILON);
        let att = getAttenuation(dist, light.range);
        let spot = getSpotFactor(-L, normalize(light.direction), light.spotBlend, light.spotAngle);
        radiance = light.color * light.intensity * att * spot;
    }

    let H = normalize(V + L);
    let NdotL = max(dot(N, L), 0.0);
    let NdotV = max(dot(N, V), EPSILON);
    let NdotH = max(dot(N, H), 0.0);
    let HdotV = max(dot(H, V), 0.0);

    if (NdotL <= 0.0) { return vec3f(0.0); }

    let D = D_GGX(NdotH, roughness);
    let G = G_Smith(NdotV, NdotL, roughness);
    let F = F_Schlick(HdotV, F0);

    let numerator = D * G * F;
    let denominator = 4.0 * NdotV * NdotL + EPSILON;
    let specular = numerator / denominator;

    let kS = F;
    var kD = vec3f(1.0) - kS;
    kD *= 1.0 - metallic;

    let diffuse = kD * albedo / PI;
    return (diffuse + specular) * radiance * NdotL;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Sample textures
    let baseColorSample = textureSample(baseColorMap, materialSampler, in.uv);
    let normalSample = textureSample(normalMap, materialSampler, in.uv);
    let metallicSample = textureSample(metallicMap, materialSampler, in.uv).r;
    let roughnessSample = textureSample(roughnessMap, materialSampler, in.uv).r;
    let aoSample = textureSample(aoMap, materialSampler, in.uv).r;

    let albedo = baseColorSample.rgb * uniforms.baseColorFactor.rgb * in.color.rgb;
    let metallic = metallicSample * uniforms.metallicFactor;
    let roughness = max(roughnessSample * uniforms.roughnessFactor, 0.04);
    let ao = mix(1.0, aoSample, uniforms.aoStrength);

    // Normal mapping
    var tangentNormal = normalSample.xyz * 2.0 - 1.0;
    tangentNormal.x = tangentNormal.x * uniforms.normalScale;
    tangentNormal.y = tangentNormal.y * uniforms.normalScale;
    tangentNormal = normalize(tangentNormal);

    let TBN = mat3x3f(
        normalize(in.worldTangent),
        normalize(in.worldBitangent),
        normalize(in.worldNormal)
    );
    let N = normalize(TBN * tangentNormal);
    let V = normalize(uniforms.cameraPos - in.worldPos);

    let F0 = mix(vec3f(0.04), albedo, metallic);

    var Lo = vec3f(0.0);
    let lightCount = min(uniforms.lightCount, MAX_LIGHTS);
    for (var i = 0u; i < lightCount; i++) {
        Lo += calculateLightContribution(
            uniforms.lights[i], in.worldPos, N, V, albedo, metallic, roughness, F0
        );
    }

    let ambient = vec3f(0.03) * albedo * uniforms.ambientIntensity * ao;

    var color = ambient + Lo;
    color = color / (color + vec3f(1.0));  // Reinhard
    color = pow(color, vec3f(1.0 / 2.2));  // Gamma

    // Use instance alpha only - PBR materials are typically opaque
    return vec4f(color, in.color.a);
}
)";

// Textured uniforms structure (matches shader)
// WGSL: lightCount ends at 116, padding to align lights at 128
struct TexturedInstancedUniforms {
    float viewProj[16];         // 64 bytes, offset 0
    float cameraPos[3];         // 12 bytes, offset 64
    float ambientIntensity;     // 4 bytes, offset 76
    float baseColorFactor[4];   // 16 bytes, offset 80
    float metallicFactor;       // 4 bytes, offset 96
    float roughnessFactor;      // 4 bytes, offset 100
    float normalScale;          // 4 bytes, offset 104
    float aoStrength;           // 4 bytes, offset 108
    uint32_t lightCount;        // 4 bytes, offset 112
    uint32_t _pad0;             // 4 bytes, offset 116
    uint32_t _pad1;             // 4 bytes, offset 120
    uint32_t _pad2;             // 4 bytes, offset 124
    GPULight lights[MAX_LIGHTS]; // 256 bytes, offset 128
};

static_assert(sizeof(TexturedInstancedUniforms) == 384, "TexturedInstancedUniforms size check");

} // anonymous namespace

InstancedRender3D::InstancedRender3D() {
    m_camera.lookAt(glm::vec3(5, 3, 5), glm::vec3(0, 0, 0));
    registerParam(metallic);
    registerParam(roughness);
    registerParam(ambient);
}

InstancedRender3D::~InstancedRender3D() {
    cleanup();
}

void InstancedRender3D::setMesh(MeshOperator* geom) {
    if (m_meshOp != geom) {
        m_meshOp = geom;
        m_mesh = nullptr;
        if (geom) {
            Operator::setInput(0, geom);
        }
        markDirty();
    }
}

void InstancedRender3D::setMesh(Mesh* m) {
    if (m_mesh != m) {
        m_mesh = m;
        m_meshOp = nullptr;
        markDirty();
    }
}

void InstancedRender3D::setInstances(const std::vector<Instance3D>& instances) {
    m_instances = instances;
    m_instancesDirty = true;
    markDirty();
}

void InstancedRender3D::addInstance(const Instance3D& instance) {
    m_instances.push_back(instance);
    m_instancesDirty = true;
    markDirty();
}

void InstancedRender3D::addInstance(const glm::mat4& transform, const glm::vec4& color) {
    Instance3D inst;
    inst.transform = transform;
    inst.color = color;
    addInstance(inst);
}

void InstancedRender3D::addInstance(const glm::vec3& position, float scale, const glm::vec4& color) {
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), position);
    transform = glm::scale(transform, glm::vec3(scale));
    addInstance(transform, color);
}

void InstancedRender3D::clearInstances() {
    if (!m_instances.empty()) {
        m_instances.clear();
        m_instancesDirty = true;
        markDirty();
    }
}

void InstancedRender3D::reserve(size_t count) {
    m_instances.reserve(count);
}

void InstancedRender3D::setCameraInput(CameraOperator* cam) {
    if (m_cameraOp != cam) {
        m_cameraOp = cam;
        if (cam) {
            Operator::setInput(1, cam);
        }
        markDirty();
    }
}

void InstancedRender3D::setCamera(const Camera3D& cam) {
    m_camera = cam;
    m_cameraOp = nullptr;
    markDirty();
}

void InstancedRender3D::setLightInput(LightOperator* light) {
    if (light) {
        bool changed = false;
        if (m_lightOps.empty()) {
            m_lightOps.push_back(light);
            changed = true;
        } else if (m_lightOps[0] != light) {
            m_lightOps[0] = light;
            changed = true;
        }
        if (changed) {
            Operator::setInput(2, light);
            markDirty();
        }
    }
}

void InstancedRender3D::addLight(LightOperator* light) {
    if (light && m_lightOps.size() < MAX_LIGHTS) {
        m_lightOps.push_back(light);
        Operator::setInput(2 + static_cast<int>(m_lightOps.size()), light);
        markDirty();
    }
}

void InstancedRender3D::setMaterial(TexturedMaterial* mat) {
    if (m_material != mat) {
        m_material = mat;
        if (mat) {
            Operator::setInput(7, mat);  // After lights (2-6)
        }
        markDirty();
    }
}

void InstancedRender3D::init(Context& ctx) {
    TextureOperator::init(ctx);
    createOutput(ctx);
}

void InstancedRender3D::createPipeline(Context& ctx) {
    if (m_pipelineCreated) return;

    WGPUDevice device = ctx.device();

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(INSTANCED_SHADER);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Bind group layout (uniforms only)
    WGPUBindGroupLayoutEntry layoutEntry = {};
    layoutEntry.binding = 0;
    layoutEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntry.buffer.minBindingSize = sizeof(InstancedUniforms);

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &layoutEntry;
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

    // Create uniform buffer
    WGPUBufferDescriptor uniformBufferDesc = {};
    uniformBufferDesc.size = sizeof(InstancedUniforms);
    uniformBufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(device, &uniformBufferDesc);

    // Create bind group
    WGPUBindGroupEntry bindEntry = {};
    bindEntry.binding = 0;
    bindEntry.buffer = m_uniformBuffer;
    bindEntry.size = sizeof(InstancedUniforms);

    WGPUBindGroupDescriptor bindDesc = {};
    bindDesc.layout = m_bindGroupLayout;
    bindDesc.entryCount = 1;
    bindDesc.entries = &bindEntry;
    m_bindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Vertex attributes (per-vertex data from mesh)
    WGPUVertexAttribute vertexAttribs[5] = {};
    vertexAttribs[0] = {WGPUVertexFormat_Float32x3, 0, 0};   // position
    vertexAttribs[1] = {WGPUVertexFormat_Float32x3, 12, 1};  // normal
    vertexAttribs[2] = {WGPUVertexFormat_Float32x4, 24, 2};  // tangent
    vertexAttribs[3] = {WGPUVertexFormat_Float32x2, 40, 3};  // uv
    vertexAttribs[4] = {WGPUVertexFormat_Float32x4, 48, 4};  // color

    WGPUVertexBufferLayout vertexLayout = {};
    vertexLayout.arrayStride = sizeof(Vertex3D);
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.attributeCount = 5;
    vertexLayout.attributes = vertexAttribs;

    // Instance attributes (per-instance data)
    WGPUVertexAttribute instanceAttribs[7] = {};
    instanceAttribs[0] = {WGPUVertexFormat_Float32x4, 0, 5};   // model column 0
    instanceAttribs[1] = {WGPUVertexFormat_Float32x4, 16, 6};  // model column 1
    instanceAttribs[2] = {WGPUVertexFormat_Float32x4, 32, 7};  // model column 2
    instanceAttribs[3] = {WGPUVertexFormat_Float32x4, 48, 8};  // model column 3
    instanceAttribs[4] = {WGPUVertexFormat_Float32x4, 64, 9};  // color
    instanceAttribs[5] = {WGPUVertexFormat_Float32x2, 80, 10}; // metallic/roughness
    instanceAttribs[6] = {WGPUVertexFormat_Float32x2, 88, 11}; // padding

    WGPUVertexBufferLayout instanceLayout = {};
    instanceLayout.arrayStride = sizeof(GPUInstance);
    instanceLayout.stepMode = WGPUVertexStepMode_Instance;
    instanceLayout.attributeCount = 7;
    instanceLayout.attributes = instanceAttribs;

    WGPUVertexBufferLayout bufferLayouts[2] = {vertexLayout, instanceLayout};

    // Depth stencil state
    WGPUDepthStencilState depthState = {};
    depthState.format = DEPTH_FORMAT;
    depthState.depthWriteEnabled = m_depthTest ? WGPUOptionalBool_True : WGPUOptionalBool_False;
    depthState.depthCompare = m_depthTest ? WGPUCompareFunction_Less : WGPUCompareFunction_Always;

    // Fragment state
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = EFFECTS_FORMAT;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    // Pipeline descriptor
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.vertex.bufferCount = 2;
    pipelineDesc.vertex.buffers = bufferLayouts;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.cullMode = m_cullBack ? WGPUCullMode_Back : WGPUCullMode_None;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.depthStencil = &depthState;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    pipelineDesc.fragment = &fragmentState;

    m_pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);

    m_pipelineCreated = true;
}

void InstancedRender3D::createTexturedPipeline(Context& ctx) {
    if (m_texturedPipelineCreated) return;

    WGPUDevice device = ctx.device();

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(INSTANCED_TEXTURED_SHADER);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Create sampler
    WGPUSamplerDescriptor samplerDesc = {};
    samplerDesc.addressModeU = WGPUAddressMode_Repeat;
    samplerDesc.addressModeV = WGPUAddressMode_Repeat;
    samplerDesc.addressModeW = WGPUAddressMode_Repeat;
    samplerDesc.magFilter = WGPUFilterMode_Linear;
    samplerDesc.minFilter = WGPUFilterMode_Linear;
    samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    samplerDesc.maxAnisotropy = 8;
    m_sampler = wgpuDeviceCreateSampler(device, &samplerDesc);

    // Bind group layout entries: uniform + sampler + 5 textures
    WGPUBindGroupLayoutEntry layoutEntries[7] = {};

    // Uniform buffer
    layoutEntries[0].binding = 0;
    layoutEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    layoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntries[0].buffer.minBindingSize = sizeof(TexturedInstancedUniforms);

    // Sampler
    layoutEntries[1].binding = 1;
    layoutEntries[1].visibility = WGPUShaderStage_Fragment;
    layoutEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    // Textures (base color, normal, metallic, roughness, ao)
    for (int i = 0; i < 5; i++) {
        layoutEntries[2 + i].binding = 2 + i;
        layoutEntries[2 + i].visibility = WGPUShaderStage_Fragment;
        layoutEntries[2 + i].texture.sampleType = WGPUTextureSampleType_Float;
        layoutEntries[2 + i].texture.viewDimension = WGPUTextureViewDimension_2D;
    }

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.entryCount = 7;
    layoutDesc.entries = layoutEntries;
    m_texturedBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

    // Pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_texturedBindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Vertex attributes (same as non-textured)
    WGPUVertexAttribute vertexAttribs[5] = {};
    vertexAttribs[0] = {WGPUVertexFormat_Float32x3, 0, 0};   // position
    vertexAttribs[1] = {WGPUVertexFormat_Float32x3, 12, 1};  // normal
    vertexAttribs[2] = {WGPUVertexFormat_Float32x4, 24, 2};  // tangent
    vertexAttribs[3] = {WGPUVertexFormat_Float32x2, 40, 3};  // uv
    vertexAttribs[4] = {WGPUVertexFormat_Float32x4, 48, 4};  // color

    WGPUVertexBufferLayout vertexLayout = {};
    vertexLayout.arrayStride = sizeof(Vertex3D);
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.attributeCount = 5;
    vertexLayout.attributes = vertexAttribs;

    // Instance attributes
    WGPUVertexAttribute instanceAttribs[7] = {};
    instanceAttribs[0] = {WGPUVertexFormat_Float32x4, 0, 5};   // model column 0
    instanceAttribs[1] = {WGPUVertexFormat_Float32x4, 16, 6};  // model column 1
    instanceAttribs[2] = {WGPUVertexFormat_Float32x4, 32, 7};  // model column 2
    instanceAttribs[3] = {WGPUVertexFormat_Float32x4, 48, 8};  // model column 3
    instanceAttribs[4] = {WGPUVertexFormat_Float32x4, 64, 9};  // color
    instanceAttribs[5] = {WGPUVertexFormat_Float32x2, 80, 10}; // metallic/roughness
    instanceAttribs[6] = {WGPUVertexFormat_Float32x2, 88, 11}; // padding

    WGPUVertexBufferLayout instanceLayout = {};
    instanceLayout.arrayStride = sizeof(GPUInstance);
    instanceLayout.stepMode = WGPUVertexStepMode_Instance;
    instanceLayout.attributeCount = 7;
    instanceLayout.attributes = instanceAttribs;

    WGPUVertexBufferLayout bufferLayouts[2] = {vertexLayout, instanceLayout};

    // Depth stencil state
    WGPUDepthStencilState depthState = {};
    depthState.format = DEPTH_FORMAT;
    depthState.depthWriteEnabled = m_depthTest ? WGPUOptionalBool_True : WGPUOptionalBool_False;
    depthState.depthCompare = m_depthTest ? WGPUCompareFunction_Less : WGPUCompareFunction_Always;

    // Fragment state
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = EFFECTS_FORMAT;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    // Pipeline descriptor
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.vertex.bufferCount = 2;
    pipelineDesc.vertex.buffers = bufferLayouts;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.cullMode = m_cullBack ? WGPUCullMode_Back : WGPUCullMode_None;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.depthStencil = &depthState;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    pipelineDesc.fragment = &fragmentState;

    m_texturedPipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);

    m_texturedPipelineCreated = true;
}

void InstancedRender3D::createDepthBuffer(Context& ctx) {
    if (m_depthTexture && m_depthWidth == m_width && m_depthHeight == m_height) {
        return;
    }

    // Cleanup old depth buffer
    if (m_depthView) {
        wgpuTextureViewRelease(m_depthView);
        m_depthView = nullptr;
    }
    if (m_depthTexture) {
        wgpuTextureDestroy(m_depthTexture);
        wgpuTextureRelease(m_depthTexture);
        m_depthTexture = nullptr;
    }

    // Create new depth buffer
    WGPUTextureDescriptor depthDesc = {};
    depthDesc.size = {static_cast<uint32_t>(m_width), static_cast<uint32_t>(m_height), 1};
    depthDesc.format = DEPTH_FORMAT;
    depthDesc.usage = WGPUTextureUsage_RenderAttachment;
    depthDesc.mipLevelCount = 1;
    depthDesc.sampleCount = 1;
    depthDesc.dimension = WGPUTextureDimension_2D;

    m_depthTexture = wgpuDeviceCreateTexture(ctx.device(), &depthDesc);
    m_depthView = wgpuTextureCreateView(m_depthTexture, nullptr);
    m_depthWidth = m_width;
    m_depthHeight = m_height;
}

void InstancedRender3D::ensureInstanceCapacity(size_t count) {
    if (count <= m_instanceCapacity && m_instanceBuffer) return;

    if (m_instanceBuffer) {
        wgpuBufferRelease(m_instanceBuffer);
        m_instanceBuffer = nullptr;
    }

    m_instanceCapacity = count + count / 4;  // 25% headroom
    if (m_instanceCapacity < 64) m_instanceCapacity = 64;

    // Note: device is needed, but we defer creation to process()
}

void InstancedRender3D::uploadInstances() {
    // Handled in process() where we have access to the device
}

void InstancedRender3D::process(Context& ctx) {
    // Instanced renderer uses declared resolution() - no auto-resize

    if (!needsCook()) return;

    bool useTextured = m_material != nullptr && m_material->baseColorView() != nullptr;

    // Ensure appropriate pipeline and depth buffer are created
    if (useTextured) {
        createTexturedPipeline(ctx);
    } else {
        createPipeline(ctx);
    }
    createDepthBuffer(ctx);

    // Get mesh
    Mesh* meshToRender = m_mesh;
    if (m_meshOp) {
        meshToRender = m_meshOp->outputMesh();
    }

    if (!meshToRender || !meshToRender->valid() || m_instances.empty()) {
        return;
    }

    WGPUDevice device = ctx.device();

    // Ensure instance buffer capacity
    if (m_instances.size() > m_instanceCapacity || !m_instanceBuffer) {
        if (m_instanceBuffer) {
            wgpuBufferRelease(m_instanceBuffer);
        }
        m_instanceCapacity = m_instances.size() + m_instances.size() / 4;
        if (m_instanceCapacity < 64) m_instanceCapacity = 64;

        WGPUBufferDescriptor bufferDesc = {};
        bufferDesc.size = m_instanceCapacity * sizeof(GPUInstance);
        bufferDesc.usage = WGPUBufferUsage_Vertex | WGPUBufferUsage_CopyDst;
        m_instanceBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);
        m_instancesDirty = true;
    }

    // Upload instance data if dirty
    if (m_instancesDirty && !m_instances.empty()) {
        std::vector<GPUInstance> gpuInstances(m_instances.size());
        for (size_t i = 0; i < m_instances.size(); i++) {
            gpuInstances[i] = toGPUInstance(m_instances[i]);
        }
        wgpuQueueWriteBuffer(ctx.queue(), m_instanceBuffer, 0,
                             gpuInstances.data(), gpuInstances.size() * sizeof(GPUInstance));
        m_instancesDirty = false;
    }

    // Get camera
    Camera3D activeCamera = m_camera;
    if (m_cameraOp) {
        activeCamera = m_cameraOp->outputCamera();
    }
    activeCamera.aspect(static_cast<float>(m_width) / m_height);
    glm::mat4 viewProj = activeCamera.viewProjectionMatrix();
    glm::vec3 cameraPos = activeCamera.getPosition();

    // Collect lights
    GPULight gpuLights[MAX_LIGHTS] = {};
    uint32_t lightCount = 0;

    for (size_t i = 0; i < m_lightOps.size() && lightCount < MAX_LIGHTS; i++) {
        if (m_lightOps[i]) {
            gpuLights[lightCount++] = toGPULight(m_lightOps[i]->outputLight());
        }
    }

    // Default light if none connected
    if (lightCount == 0) {
        LightData defaultLight;
        defaultLight.type = LightType::Directional;
        defaultLight.direction = glm::normalize(glm::vec3(1, 2, 1));
        defaultLight.color = glm::vec3(1, 1, 1);
        defaultLight.intensity = 1.0f;
        gpuLights[lightCount++] = toGPULight(defaultLight);
    }

    WGPUBindGroup activeBindGroup;

    if (useTextured) {
        // Create textured uniform buffer if needed
        static WGPUBuffer texturedUniformBuffer = nullptr;
        if (!texturedUniformBuffer) {
            WGPUBufferDescriptor bufDesc = {};
            bufDesc.size = sizeof(TexturedInstancedUniforms);
            bufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
            texturedUniformBuffer = wgpuDeviceCreateBuffer(device, &bufDesc);
        }

        // Update textured uniforms
        TexturedInstancedUniforms uniforms = {};
        memcpy(uniforms.viewProj, glm::value_ptr(viewProj), sizeof(uniforms.viewProj));
        uniforms.cameraPos[0] = cameraPos.x;
        uniforms.cameraPos[1] = cameraPos.y;
        uniforms.cameraPos[2] = cameraPos.z;
        uniforms.ambientIntensity = static_cast<float>(ambient);

        const glm::vec4& matBaseColor = m_material->getBaseColorFactor();
        uniforms.baseColorFactor[0] = matBaseColor.r * m_baseColor.r;
        uniforms.baseColorFactor[1] = matBaseColor.g * m_baseColor.g;
        uniforms.baseColorFactor[2] = matBaseColor.b * m_baseColor.b;
        uniforms.baseColorFactor[3] = matBaseColor.a * m_baseColor.a;
        uniforms.metallicFactor = m_material->getMetallicFactor();
        uniforms.roughnessFactor = m_material->getRoughnessFactor();
        uniforms.normalScale = m_material->getNormalScale();
        uniforms.aoStrength = m_material->getAoStrength();
        uniforms.lightCount = lightCount;
        memcpy(uniforms.lights, gpuLights, sizeof(gpuLights));

        wgpuQueueWriteBuffer(ctx.queue(), texturedUniformBuffer, 0, &uniforms, sizeof(uniforms));

        // Create bind group with textures
        WGPUBindGroupEntry bindEntries[7] = {};
        bindEntries[0].binding = 0;
        bindEntries[0].buffer = texturedUniformBuffer;
        bindEntries[0].size = sizeof(TexturedInstancedUniforms);

        bindEntries[1].binding = 1;
        bindEntries[1].sampler = m_sampler;

        bindEntries[2].binding = 2;
        bindEntries[2].textureView = m_material->baseColorView();

        bindEntries[3].binding = 3;
        bindEntries[3].textureView = m_material->normalView();

        bindEntries[4].binding = 4;
        bindEntries[4].textureView = m_material->metallicView();

        bindEntries[5].binding = 5;
        bindEntries[5].textureView = m_material->roughnessView();

        bindEntries[6].binding = 6;
        bindEntries[6].textureView = m_material->aoView();

        WGPUBindGroupDescriptor bindDesc = {};
        bindDesc.layout = m_texturedBindGroupLayout;
        bindDesc.entryCount = 7;
        bindDesc.entries = bindEntries;

        // Release old bind group if exists
        if (m_texturedBindGroup) {
            wgpuBindGroupRelease(m_texturedBindGroup);
        }
        m_texturedBindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);
        activeBindGroup = m_texturedBindGroup;
    } else {
        // Update non-textured uniforms
        InstancedUniforms uniforms = {};
        memcpy(uniforms.viewProj, glm::value_ptr(viewProj), sizeof(uniforms.viewProj));
        uniforms.cameraPos[0] = cameraPos.x;
        uniforms.cameraPos[1] = cameraPos.y;
        uniforms.cameraPos[2] = cameraPos.z;
        uniforms.ambientIntensity = static_cast<float>(ambient);
        uniforms.baseColor[0] = m_baseColor.r;
        uniforms.baseColor[1] = m_baseColor.g;
        uniforms.baseColor[2] = m_baseColor.b;
        uniforms.baseColor[3] = m_baseColor.a;
        uniforms.metallic = static_cast<float>(metallic);
        uniforms.roughness = static_cast<float>(roughness);
        uniforms.lightCount = lightCount;
        memcpy(uniforms.lights, gpuLights, sizeof(gpuLights));

        wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, 0, &uniforms, sizeof(uniforms));
        activeBindGroup = m_bindGroup;
    }

    // Create command encoder
    WGPUCommandEncoderDescriptor encoderDesc = {};
    WGPUCommandEncoder encoder = wgpuDeviceCreateCommandEncoder(device, &encoderDesc);

    // Begin render pass
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

    // Draw instanced
    wgpuRenderPassEncoderSetPipeline(pass, useTextured ? m_texturedPipeline : m_pipeline);
    wgpuRenderPassEncoderSetBindGroup(pass, 0, activeBindGroup, 0, nullptr);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 0, meshToRender->vertexBuffer(), 0, WGPU_WHOLE_SIZE);
    wgpuRenderPassEncoderSetVertexBuffer(pass, 1, m_instanceBuffer, 0,
                                          m_instances.size() * sizeof(GPUInstance));
    wgpuRenderPassEncoderSetIndexBuffer(pass, meshToRender->indexBuffer(),
                                         WGPUIndexFormat_Uint32, 0, WGPU_WHOLE_SIZE);

    // Single draw call for all instances!
    wgpuRenderPassEncoderDrawIndexed(pass, meshToRender->indexCount(),
                                      static_cast<uint32_t>(m_instances.size()), 0, 0, 0);

    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    // Submit
    WGPUCommandBufferDescriptor cmdBufferDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdBufferDesc);
    wgpuQueueSubmit(ctx.queue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    didCook();
}

void InstancedRender3D::cleanup() {
    // Non-textured pipeline resources
    if (m_pipeline) { wgpuRenderPipelineRelease(m_pipeline); m_pipeline = nullptr; }
    if (m_bindGroupLayout) { wgpuBindGroupLayoutRelease(m_bindGroupLayout); m_bindGroupLayout = nullptr; }
    if (m_bindGroup) { wgpuBindGroupRelease(m_bindGroup); m_bindGroup = nullptr; }
    if (m_uniformBuffer) { wgpuBufferRelease(m_uniformBuffer); m_uniformBuffer = nullptr; }
    if (m_instanceBuffer) { wgpuBufferRelease(m_instanceBuffer); m_instanceBuffer = nullptr; }

    // Textured pipeline resources
    if (m_texturedPipeline) { wgpuRenderPipelineRelease(m_texturedPipeline); m_texturedPipeline = nullptr; }
    if (m_texturedBindGroupLayout) { wgpuBindGroupLayoutRelease(m_texturedBindGroupLayout); m_texturedBindGroupLayout = nullptr; }
    if (m_texturedBindGroup) { wgpuBindGroupRelease(m_texturedBindGroup); m_texturedBindGroup = nullptr; }
    if (m_sampler) { wgpuSamplerRelease(m_sampler); m_sampler = nullptr; }

    // Depth buffer
    if (m_depthView) { wgpuTextureViewRelease(m_depthView); m_depthView = nullptr; }
    if (m_depthTexture) { wgpuTextureDestroy(m_depthTexture); wgpuTextureRelease(m_depthTexture); m_depthTexture = nullptr; }

    m_pipelineCreated = false;
    m_texturedPipelineCreated = false;
    m_instanceCapacity = 0;
    m_depthWidth = 0;
    m_depthHeight = 0;

    TextureOperator::cleanup();
}

} // namespace vivid::render3d
