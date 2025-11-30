#include "pipeline3d_lit.h"
#include "cubemap.h"
#include <iostream>
#include <cstring>

namespace vivid {

// ============================================================================
// Phong Shader
// ============================================================================

namespace shaders3d {

const char* PHONG_LIT = R"(
// ============================================================================
// Blinn-Phong Lighting Shader
// ============================================================================

const MAX_LIGHTS: u32 = 8u;

// Camera uniform - group 0
struct CameraUniform {
    view: mat4x4f,
    projection: mat4x4f,
    viewProjection: mat4x4f,
    cameraPosition: vec3f,
    _pad: f32,
}

// Transform uniform - group 1
struct TransformUniform {
    model: mat4x4f,
    normalMatrix: mat4x4f,
}

// Light data - group 2
struct LightData {
    lightType: i32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
    position: vec3f,
    _pad4: f32,
    direction: vec3f,
    _pad5: f32,
    color: vec3f,
    intensity: f32,
    radius: f32,
    innerAngle: f32,
    outerAngle: f32,
    _pad6: f32,
}

struct LightsUniform {
    lights: array<LightData, MAX_LIGHTS>,
    lightCount: i32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
    ambientColor: vec3f,
    ambientIntensity: f32,
}

// Phong material - group 3
struct PhongMaterial {
    ambient: vec3f,
    _pad1: f32,
    diffuse: vec3f,
    _pad2: f32,
    specular: vec3f,
    shininess: f32,
    emissive: vec3f,
    _pad3: f32,
}

@group(0) @binding(0) var<uniform> camera: CameraUniform;
@group(1) @binding(0) var<uniform> transform: TransformUniform;
@group(2) @binding(0) var<uniform> lights: LightsUniform;
@group(3) @binding(0) var<uniform> material: PhongMaterial;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) tangent: vec4f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) worldPos: vec3f,
    @location(1) worldNormal: vec3f,
    @location(2) uv: vec2f,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    let worldPos = transform.model * vec4f(in.position, 1.0);
    out.worldPos = worldPos.xyz;
    out.position = camera.viewProjection * worldPos;
    out.worldNormal = normalize((transform.normalMatrix * vec4f(in.normal, 0.0)).xyz);
    out.uv = in.uv;

    return out;
}

// Attenuation for point/spot lights
fn getAttenuation(distance: f32, radius: f32) -> f32 {
    let d = distance / radius;
    let d2 = d * d;
    let falloff = saturate(1.0 - d2 * d2);
    return falloff * falloff / (distance * distance + 1.0);
}

// Spot light intensity based on cone angle
fn getSpotIntensity(lightDir: vec3f, spotDir: vec3f, innerAngle: f32, outerAngle: f32) -> f32 {
    let theta = dot(lightDir, normalize(-spotDir));
    let epsilon = cos(innerAngle) - cos(outerAngle);
    return saturate((theta - cos(outerAngle)) / epsilon);
}

// Calculate contribution from a single light
fn calculateLight(light: LightData, worldPos: vec3f, normal: vec3f, viewDir: vec3f) -> vec3f {
    var lightDir: vec3f;
    var attenuation: f32 = 1.0;

    // Directional light
    if (light.lightType == 0) {
        lightDir = normalize(-light.direction);
    }
    // Point light
    else if (light.lightType == 1) {
        let toLight = light.position - worldPos;
        let distance = length(toLight);
        lightDir = toLight / distance;
        attenuation = getAttenuation(distance, light.radius);
    }
    // Spot light
    else {
        let toLight = light.position - worldPos;
        let distance = length(toLight);
        lightDir = toLight / distance;
        attenuation = getAttenuation(distance, light.radius);
        attenuation *= getSpotIntensity(lightDir, light.direction, light.innerAngle, light.outerAngle);
    }

    // Skip if light doesn't reach this point
    if (attenuation < 0.001) {
        return vec3f(0.0);
    }

    let radiance = light.color * light.intensity * attenuation;

    // Diffuse (Lambertian)
    let NdotL = max(dot(normal, lightDir), 0.0);
    let diffuse = material.diffuse * NdotL;

    // Specular (Blinn-Phong)
    let halfDir = normalize(lightDir + viewDir);
    let NdotH = max(dot(normal, halfDir), 0.0);
    let spec = pow(NdotH, material.shininess);
    let specular = material.specular * spec;

    return (diffuse + specular) * radiance;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let normal = normalize(in.worldNormal);
    let viewDir = normalize(camera.cameraPosition - in.worldPos);

    // Start with ambient
    var color = material.ambient * lights.ambientColor * lights.ambientIntensity;

    // Add emissive
    color += material.emissive;

    // Accumulate light contributions
    for (var i = 0; i < lights.lightCount; i++) {
        color += calculateLight(lights.lights[i], in.worldPos, normal, viewDir);
    }

    // Clamp and output
    return vec4f(clamp(color, vec3f(0.0), vec3f(1.0)), 1.0);
}
)";

// ============================================================================
// PBR Shader (Cook-Torrance BRDF)
// ============================================================================

const char* PBR_LIT = R"(
// ============================================================================
// Physically Based Rendering Shader (Metallic-Roughness Workflow)
// ============================================================================

const MAX_LIGHTS: u32 = 8u;
const PI: f32 = 3.14159265359;

// Camera uniform - group 0
struct CameraUniform {
    view: mat4x4f,
    projection: mat4x4f,
    viewProjection: mat4x4f,
    cameraPosition: vec3f,
    _pad: f32,
}

// Transform uniform - group 1
struct TransformUniform {
    model: mat4x4f,
    normalMatrix: mat4x4f,
}

// Light data - group 2
struct LightData {
    lightType: i32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
    position: vec3f,
    _pad4: f32,
    direction: vec3f,
    _pad5: f32,
    color: vec3f,
    intensity: f32,
    radius: f32,
    innerAngle: f32,
    outerAngle: f32,
    _pad6: f32,
}

struct LightsUniform {
    lights: array<LightData, MAX_LIGHTS>,
    lightCount: i32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
    ambientColor: vec3f,
    ambientIntensity: f32,
}

// PBR material - group 3 (64 bytes to match Phong)
struct PBRMaterial {
    albedo: vec3f,
    _pad0: f32,
    metallic: f32,
    roughness: f32,
    ao: f32,
    _pad1: f32,
    emissive: vec3f,
    _pad2: f32,
    _pad3: vec4f,
}

@group(0) @binding(0) var<uniform> camera: CameraUniform;
@group(1) @binding(0) var<uniform> transform: TransformUniform;
@group(2) @binding(0) var<uniform> lights: LightsUniform;
@group(3) @binding(0) var<uniform> material: PBRMaterial;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) tangent: vec4f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) worldPos: vec3f,
    @location(1) worldNormal: vec3f,
    @location(2) uv: vec2f,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    let worldPos = transform.model * vec4f(in.position, 1.0);
    out.worldPos = worldPos.xyz;
    out.position = camera.viewProjection * worldPos;
    out.worldNormal = normalize((transform.normalMatrix * vec4f(in.normal, 0.0)).xyz);
    out.uv = in.uv;

    return out;
}

// Normal Distribution Function (GGX/Trowbridge-Reitz)
fn distributionGGX(N: vec3f, H: vec3f, roughness: f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let NdotH = max(dot(N, H), 0.0);
    let NdotH2 = NdotH * NdotH;

    let denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Geometry Function (Schlick-GGX)
fn geometrySchlickGGX(NdotV: f32, roughness: f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith's method for geometry
fn geometrySmith(N: vec3f, V: vec3f, L: vec3f, roughness: f32) -> f32 {
    let NdotV = max(dot(N, V), 0.0);
    let NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

// Fresnel (Schlick approximation)
fn fresnelSchlick(cosTheta: f32, F0: vec3f) -> vec3f {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Attenuation for point/spot lights
fn getAttenuation(distance: f32, radius: f32) -> f32 {
    let d = distance / radius;
    let d2 = d * d;
    let falloff = saturate(1.0 - d2 * d2);
    return falloff * falloff / (distance * distance + 1.0);
}

// Spot light intensity
fn getSpotIntensity(lightDir: vec3f, spotDir: vec3f, innerAngle: f32, outerAngle: f32) -> f32 {
    let theta = dot(lightDir, normalize(-spotDir));
    let epsilon = cos(innerAngle) - cos(outerAngle);
    return saturate((theta - cos(outerAngle)) / epsilon);
}

// Calculate PBR contribution from a single light
fn calculatePBRLight(light: LightData, worldPos: vec3f, N: vec3f, V: vec3f, F0: vec3f) -> vec3f {
    var L: vec3f;
    var attenuation: f32 = 1.0;

    // Directional light
    if (light.lightType == 0) {
        L = normalize(-light.direction);
    }
    // Point light
    else if (light.lightType == 1) {
        let toLight = light.position - worldPos;
        let distance = length(toLight);
        L = toLight / distance;
        attenuation = getAttenuation(distance, light.radius);
    }
    // Spot light
    else {
        let toLight = light.position - worldPos;
        let distance = length(toLight);
        L = toLight / distance;
        attenuation = getAttenuation(distance, light.radius);
        attenuation *= getSpotIntensity(L, light.direction, light.innerAngle, light.outerAngle);
    }

    if (attenuation < 0.001) {
        return vec3f(0.0);
    }

    let radiance = light.color * light.intensity * attenuation;
    let H = normalize(V + L);

    // Cook-Torrance BRDF
    let NDF = distributionGGX(N, H, material.roughness);
    let G = geometrySmith(N, V, L, material.roughness);
    let F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    // Specular contribution
    let numerator = NDF * G * F;
    let denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    let specular = numerator / denominator;

    // Energy conservation
    let kS = F;
    let kD = (vec3f(1.0) - kS) * (1.0 - material.metallic);

    let NdotL = max(dot(N, L), 0.0);
    return (kD * material.albedo / PI + specular) * radiance * NdotL;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let N = normalize(in.worldNormal);
    let V = normalize(camera.cameraPosition - in.worldPos);

    // F0 (reflectance at normal incidence)
    // Dielectrics use 0.04, metals use albedo
    let F0 = mix(vec3f(0.04), material.albedo, material.metallic);

    // Ambient (simplified - ideally use IBL)
    let ambient = lights.ambientColor * lights.ambientIntensity * material.albedo * material.ao;

    // Emissive
    var color = ambient + material.emissive;

    // Accumulate light contributions
    for (var i = 0; i < lights.lightCount; i++) {
        color += calculatePBRLight(lights.lights[i], in.worldPos, N, V, F0);
    }

    // HDR tone mapping (Reinhard)
    color = color / (color + vec3f(1.0));

    // Gamma correction
    color = pow(color, vec3f(1.0 / 2.2));

    return vec4f(color, 1.0);
}
)";

// ============================================================================
// PBR + IBL Shader (Cook-Torrance BRDF with Image-Based Lighting)
// ============================================================================

const char* PBR_IBL = R"(
// ============================================================================
// Physically Based Rendering with Image-Based Lighting
// ============================================================================

const MAX_LIGHTS: u32 = 8u;
const PI: f32 = 3.14159265359;
const MAX_REFLECTION_LOD: f32 = 4.0;

// Camera uniform - group 0
struct CameraUniform {
    view: mat4x4f,
    projection: mat4x4f,
    viewProjection: mat4x4f,
    cameraPosition: vec3f,
    _pad: f32,
}

// Transform uniform - group 1
struct TransformUniform {
    model: mat4x4f,
    normalMatrix: mat4x4f,
}

// Light data - group 2
struct LightData {
    lightType: i32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
    position: vec3f,
    _pad4: f32,
    direction: vec3f,
    _pad5: f32,
    color: vec3f,
    intensity: f32,
    radius: f32,
    innerAngle: f32,
    outerAngle: f32,
    _pad6: f32,
}

struct LightsUniform {
    lights: array<LightData, MAX_LIGHTS>,
    lightCount: i32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
    ambientColor: vec3f,
    ambientIntensity: f32,
}

// PBR material - group 3
struct PBRMaterial {
    albedo: vec3f,
    _pad0: f32,
    metallic: f32,
    roughness: f32,
    ao: f32,
    _pad1: f32,
    emissive: vec3f,
    _pad2: f32,
    _pad3: vec4f,
}

@group(0) @binding(0) var<uniform> camera: CameraUniform;
@group(1) @binding(0) var<uniform> transform: TransformUniform;
@group(2) @binding(0) var<uniform> lights: LightsUniform;

// Group 3: Material + IBL textures (combined to stay within 4 bind group limit)
@group(3) @binding(0) var<uniform> material: PBRMaterial;
@group(3) @binding(1) var irradianceMap: texture_cube<f32>;
@group(3) @binding(2) var radianceMap: texture_cube<f32>;
@group(3) @binding(3) var brdfLUT: texture_2d<f32>;
@group(3) @binding(4) var iblSampler: sampler;
@group(3) @binding(5) var brdfSampler: sampler;  // Non-filtering sampler for BRDF LUT

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) tangent: vec4f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) worldPos: vec3f,
    @location(1) worldNormal: vec3f,
    @location(2) uv: vec2f,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    let worldPos = transform.model * vec4f(in.position, 1.0);
    out.worldPos = worldPos.xyz;
    out.position = camera.viewProjection * worldPos;
    out.worldNormal = normalize((transform.normalMatrix * vec4f(in.normal, 0.0)).xyz);
    out.uv = in.uv;

    return out;
}

// Normal Distribution Function (GGX/Trowbridge-Reitz)
fn distributionGGX(N: vec3f, H: vec3f, roughness: f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let NdotH = max(dot(N, H), 0.0);
    let NdotH2 = NdotH * NdotH;

    let denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Geometry Function (Schlick-GGX)
fn geometrySchlickGGX(NdotV: f32, roughness: f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith's method for geometry
fn geometrySmith(N: vec3f, V: vec3f, L: vec3f, roughness: f32) -> f32 {
    let NdotV = max(dot(N, V), 0.0);
    let NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

// Fresnel (Schlick approximation)
fn fresnelSchlick(cosTheta: f32, F0: vec3f) -> vec3f {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Fresnel with roughness (for IBL)
fn fresnelSchlickRoughness(cosTheta: f32, F0: vec3f, roughness: f32) -> vec3f {
    return F0 + (max(vec3f(1.0 - roughness), F0) - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Attenuation for point/spot lights
fn getAttenuation(distance: f32, radius: f32) -> f32 {
    let d = distance / radius;
    let d2 = d * d;
    let falloff = saturate(1.0 - d2 * d2);
    return falloff * falloff / (distance * distance + 1.0);
}

// Spot light intensity
fn getSpotIntensity(lightDir: vec3f, spotDir: vec3f, innerAngle: f32, outerAngle: f32) -> f32 {
    let theta = dot(lightDir, normalize(-spotDir));
    let epsilon = cos(innerAngle) - cos(outerAngle);
    return saturate((theta - cos(outerAngle)) / epsilon);
}

// Calculate PBR contribution from a single light
fn calculatePBRLight(light: LightData, worldPos: vec3f, N: vec3f, V: vec3f, F0: vec3f) -> vec3f {
    var L: vec3f;
    var attenuation: f32 = 1.0;

    // Directional light
    if (light.lightType == 0) {
        L = normalize(-light.direction);
    }
    // Point light
    else if (light.lightType == 1) {
        let toLight = light.position - worldPos;
        let distance = length(toLight);
        L = toLight / distance;
        attenuation = getAttenuation(distance, light.radius);
    }
    // Spot light
    else {
        let toLight = light.position - worldPos;
        let distance = length(toLight);
        L = toLight / distance;
        attenuation = getAttenuation(distance, light.radius);
        attenuation *= getSpotIntensity(L, light.direction, light.innerAngle, light.outerAngle);
    }

    if (attenuation < 0.001) {
        return vec3f(0.0);
    }

    let radiance = light.color * light.intensity * attenuation;
    let H = normalize(V + L);

    // Cook-Torrance BRDF
    let NDF = distributionGGX(N, H, material.roughness);
    let G = geometrySmith(N, V, L, material.roughness);
    let F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    // Specular contribution
    let numerator = NDF * G * F;
    let denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    let specular = numerator / denominator;

    // Energy conservation
    let kS = F;
    let kD = (vec3f(1.0) - kS) * (1.0 - material.metallic);

    let NdotL = max(dot(N, L), 0.0);
    return (kD * material.albedo / PI + specular) * radiance * NdotL;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let N = normalize(in.worldNormal);
    let V = normalize(camera.cameraPosition - in.worldPos);
    let R = reflect(-V, N);

    // F0 (reflectance at normal incidence)
    let F0 = mix(vec3f(0.04), material.albedo, material.metallic);
    let NdotV = max(dot(N, V), 0.0);

    // -----------------------------------------------------------------
    // Image-Based Lighting
    // -----------------------------------------------------------------

    // Fresnel term for IBL (accounts for roughness)
    let F = fresnelSchlickRoughness(NdotV, F0, material.roughness);

    // Energy conservation
    let kS = F;
    let kD = (1.0 - kS) * (1.0 - material.metallic);

    // Diffuse IBL - sample irradiance map
    let irradiance = textureSample(irradianceMap, iblSampler, N).rgb;
    let diffuse = irradiance * material.albedo;

    // Specular IBL - sample pre-filtered radiance map at roughness mip level
    let prefilteredColor = textureSampleLevel(radianceMap, iblSampler, R, material.roughness * MAX_REFLECTION_LOD).rgb;

    // BRDF lookup (use non-filtering sampler for RG32Float texture)
    let envBRDF = textureSample(brdfLUT, brdfSampler, vec2f(NdotV, material.roughness)).rg;
    let specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);

    // Combined ambient from IBL
    let ambient = (kD * diffuse + specular) * material.ao;

    // -----------------------------------------------------------------
    // Direct Lighting
    // -----------------------------------------------------------------

    var directLighting = vec3f(0.0);
    for (var i = 0; i < lights.lightCount; i++) {
        directLighting += calculatePBRLight(lights.lights[i], in.worldPos, N, V, F0);
    }

    // -----------------------------------------------------------------
    // Final Color
    // -----------------------------------------------------------------

    var color = ambient + directLighting + material.emissive;

    // HDR tone mapping (Reinhard)
    color = color / (color + vec3f(1.0));

    // Gamma correction
    color = pow(color, vec3f(1.0 / 2.2));

    return vec4f(color, 1.0);
}
)";

// ============================================================================
// PBR + IBL + Texture Maps Shader
// ============================================================================

const char* PBR_IBL_TEXTURED = R"(
// ============================================================================
// Physically Based Rendering with IBL and Texture Maps
// Supports: albedo, normal, metallic-roughness, AO, emissive maps
// ============================================================================

const MAX_LIGHTS: u32 = 8u;
const PI: f32 = 3.14159265359;
const MAX_REFLECTION_LOD: f32 = 4.0;

// Texture flags (bit masks)
const HAS_ALBEDO_MAP: u32 = 1u;
const HAS_NORMAL_MAP: u32 = 2u;
const HAS_METALLIC_ROUGHNESS_MAP: u32 = 4u;
const HAS_AO_MAP: u32 = 8u;
const HAS_EMISSIVE_MAP: u32 = 16u;
const HAS_ROUGHNESS_MAP: u32 = 32u;      // Separate roughness map (R channel)
const HAS_METALLIC_MAP: u32 = 64u;       // Separate metallic map (R channel)

// Camera uniform - group 0
struct CameraUniform {
    view: mat4x4f,
    projection: mat4x4f,
    viewProjection: mat4x4f,
    cameraPosition: vec3f,
    _pad: f32,
}

// Transform uniform - group 1
struct TransformUniform {
    model: mat4x4f,
    normalMatrix: mat4x4f,
}

// Light data - group 2
struct LightData {
    lightType: i32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
    position: vec3f,
    _pad4: f32,
    direction: vec3f,
    _pad5: f32,
    color: vec3f,
    intensity: f32,
    radius: f32,
    innerAngle: f32,
    outerAngle: f32,
    _pad6: f32,
}

struct LightsUniform {
    lights: array<LightData, MAX_LIGHTS>,
    lightCount: i32,
    _pad1: f32,
    _pad2: f32,
    _pad3: f32,
    ambientColor: vec3f,
    ambientIntensity: f32,
}

// Textured PBR material - group 3
struct TexturedPBRMaterial {
    albedo: vec3f,
    metallic: f32,
    roughness: f32,
    ao: f32,
    normalStrength: f32,
    emissiveStrength: f32,
    emissive: vec3f,
    textureFlags: u32,
}

@group(0) @binding(0) var<uniform> camera: CameraUniform;
@group(1) @binding(0) var<uniform> transform: TransformUniform;
@group(2) @binding(0) var<uniform> lights: LightsUniform;

// Group 3: Material + IBL + Material Textures
@group(3) @binding(0) var<uniform> material: TexturedPBRMaterial;
@group(3) @binding(1) var irradianceMap: texture_cube<f32>;
@group(3) @binding(2) var radianceMap: texture_cube<f32>;
@group(3) @binding(3) var brdfLUT: texture_2d<f32>;
@group(3) @binding(4) var iblSampler: sampler;
@group(3) @binding(5) var brdfSampler: sampler;
// Material textures
@group(3) @binding(6) var albedoMap: texture_2d<f32>;
@group(3) @binding(7) var normalMap: texture_2d<f32>;
@group(3) @binding(8) var metallicRoughnessMap: texture_2d<f32>;
@group(3) @binding(9) var aoMap: texture_2d<f32>;
@group(3) @binding(10) var emissiveMap: texture_2d<f32>;
@group(3) @binding(11) var textureSampler: sampler;
@group(3) @binding(12) var roughnessMap: texture_2d<f32>;
@group(3) @binding(13) var metallicMapTex: texture_2d<f32>;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) uv: vec2f,
    @location(3) tangent: vec4f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) worldPos: vec3f,
    @location(1) worldNormal: vec3f,
    @location(2) uv: vec2f,
    @location(3) worldTangent: vec3f,
    @location(4) worldBitangent: vec3f,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;

    let worldPos = transform.model * vec4f(in.position, 1.0);
    out.worldPos = worldPos.xyz;
    out.position = camera.viewProjection * worldPos;
    out.worldNormal = normalize((transform.normalMatrix * vec4f(in.normal, 0.0)).xyz);
    out.uv = in.uv;

    // Calculate TBN matrix for normal mapping
    let T = normalize((transform.normalMatrix * vec4f(in.tangent.xyz, 0.0)).xyz);
    let B = cross(out.worldNormal, T) * in.tangent.w;
    out.worldTangent = T;
    out.worldBitangent = B;

    return out;
}

// Normal Distribution Function (GGX/Trowbridge-Reitz)
fn distributionGGX(N: vec3f, H: vec3f, roughness: f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let NdotH = max(dot(N, H), 0.0);
    let NdotH2 = NdotH * NdotH;

    let denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Geometry Function (Schlick-GGX)
fn geometrySchlickGGX(NdotV: f32, roughness: f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

// Smith's method for geometry
fn geometrySmith(N: vec3f, V: vec3f, L: vec3f, roughness: f32) -> f32 {
    let NdotV = max(dot(N, V), 0.0);
    let NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

// Fresnel (Schlick approximation)
fn fresnelSchlick(cosTheta: f32, F0: vec3f) -> vec3f {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Fresnel with roughness (for IBL)
fn fresnelSchlickRoughness(cosTheta: f32, F0: vec3f, roughness: f32) -> vec3f {
    return F0 + (max(vec3f(1.0 - roughness), F0) - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Attenuation for point/spot lights
fn getAttenuation(distance: f32, radius: f32) -> f32 {
    let d = distance / radius;
    let d2 = d * d;
    let falloff = saturate(1.0 - d2 * d2);
    return falloff * falloff / (distance * distance + 1.0);
}

// Spot light intensity
fn getSpotIntensity(lightDir: vec3f, spotDir: vec3f, innerAngle: f32, outerAngle: f32) -> f32 {
    let theta = dot(lightDir, normalize(-spotDir));
    let epsilon = cos(innerAngle) - cos(outerAngle);
    return saturate((theta - cos(outerAngle)) / epsilon);
}

// Calculate PBR contribution from a single light
fn calculatePBRLight(light: LightData, worldPos: vec3f, N: vec3f, V: vec3f, F0: vec3f,
                     albedo: vec3f, metallic: f32, roughness: f32) -> vec3f {
    var L: vec3f;
    var attenuation: f32 = 1.0;

    // Directional light
    if (light.lightType == 0) {
        L = normalize(-light.direction);
    }
    // Point light
    else if (light.lightType == 1) {
        let toLight = light.position - worldPos;
        let distance = length(toLight);
        L = toLight / distance;
        attenuation = getAttenuation(distance, light.radius);
    }
    // Spot light
    else {
        let toLight = light.position - worldPos;
        let distance = length(toLight);
        L = toLight / distance;
        attenuation = getAttenuation(distance, light.radius);
        attenuation *= getSpotIntensity(L, light.direction, light.innerAngle, light.outerAngle);
    }

    if (attenuation < 0.001) {
        return vec3f(0.0);
    }

    let radiance = light.color * light.intensity * attenuation;
    let H = normalize(V + L);

    // Cook-Torrance BRDF
    let NDF = distributionGGX(N, H, roughness);
    let G = geometrySmith(N, V, L, roughness);
    let F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    // Specular contribution
    let numerator = NDF * G * F;
    let denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    let specular = numerator / denominator;

    // Energy conservation
    let kS = F;
    let kD = (vec3f(1.0) - kS) * (1.0 - metallic);

    let NdotL = max(dot(N, L), 0.0);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // Sample textures based on flags
    var albedo = material.albedo;
    var metallic = material.metallic;
    var roughness = material.roughness;
    var ao = material.ao;
    var emissive = material.emissive * material.emissiveStrength;

    // Albedo map (convert sRGB to linear space for correct PBR)
    if ((material.textureFlags & HAS_ALBEDO_MAP) != 0u) {
        let albedoSample = textureSample(albedoMap, textureSampler, in.uv).rgb;
        // sRGB to linear conversion (textures are stored in sRGB)
        albedo *= pow(albedoSample, vec3f(2.2));
    }

    // DEBUG: Set to true to visualize albedo directly
    let debugAlbedo = false;
    if (debugAlbedo) {
        return vec4f(albedo, 1.0);
    }

    // Metallic-Roughness map (glTF convention: G=roughness, B=metallic)
    if ((material.textureFlags & HAS_METALLIC_ROUGHNESS_MAP) != 0u) {
        let mrSample = textureSample(metallicRoughnessMap, textureSampler, in.uv);
        roughness *= mrSample.g;
        metallic *= mrSample.b;
    }

    // Separate roughness map (R channel) - overrides combined map
    if ((material.textureFlags & HAS_ROUGHNESS_MAP) != 0u) {
        let roughSample = textureSample(roughnessMap, textureSampler, in.uv);
        roughness = material.roughness * roughSample.r;
    }

    // Separate metallic map (R channel) - overrides combined map
    if ((material.textureFlags & HAS_METALLIC_MAP) != 0u) {
        let metalSample = textureSample(metallicMapTex, textureSampler, in.uv);
        metallic = material.metallic * metalSample.r;
    }

    // AO map
    if ((material.textureFlags & HAS_AO_MAP) != 0u) {
        let aoSample = textureSample(aoMap, textureSampler, in.uv);
        ao *= aoSample.r;
    }

    // Emissive map (convert sRGB to linear)
    if ((material.textureFlags & HAS_EMISSIVE_MAP) != 0u) {
        let emissiveSample = textureSample(emissiveMap, textureSampler, in.uv).rgb;
        emissive *= pow(emissiveSample, vec3f(2.2));
    }

    // Normal mapping
    var N = normalize(in.worldNormal);
    if ((material.textureFlags & HAS_NORMAL_MAP) != 0u) {
        // Sample and decode normal from [0,1] to [-1,1]
        let normalSample = textureSample(normalMap, textureSampler, in.uv).rgb;
        var tangentNormal = normalSample * 2.0 - 1.0;

        // Apply normal strength
        tangentNormal.x *= material.normalStrength;
        tangentNormal.y *= material.normalStrength;

        // Build TBN matrix and transform to world space
        let T = normalize(in.worldTangent);
        let B = normalize(in.worldBitangent);
        let TBN = mat3x3f(T, B, N);
        N = normalize(TBN * tangentNormal);
    }

    let V = normalize(camera.cameraPosition - in.worldPos);
    let R = reflect(-V, N);

    // F0 (reflectance at normal incidence)
    let F0 = mix(vec3f(0.04), albedo, metallic);
    let NdotV = max(dot(N, V), 0.0);

    // -----------------------------------------------------------------
    // Image-Based Lighting
    // -----------------------------------------------------------------

    // Fresnel term for IBL (accounts for roughness)
    let F = fresnelSchlickRoughness(NdotV, F0, roughness);

    // Energy conservation
    let kS = F;
    let kD = (1.0 - kS) * (1.0 - metallic);

    // Diffuse IBL - sample irradiance map
    let irradiance = textureSample(irradianceMap, iblSampler, N).rgb;
    let diffuse = irradiance * albedo;

    // Specular IBL - sample pre-filtered radiance map at roughness mip level
    let prefilteredColor = textureSampleLevel(radianceMap, iblSampler, R, roughness * MAX_REFLECTION_LOD).rgb;

    // BRDF lookup (use non-filtering sampler for RG32Float texture)
    let envBRDF = textureSample(brdfLUT, brdfSampler, vec2f(NdotV, roughness)).rg;
    let specular = prefilteredColor * (F * envBRDF.x + envBRDF.y);

    // IBL intensity (tune this if environment is too bright)
    let iblIntensity = 0.3;  // Scale down HDR environment

    // Combined ambient from IBL
    let ambient = (kD * diffuse + specular) * ao * iblIntensity;

    // DEBUG: Visualize components
    let debugMode = 2; // 0=normal, 1=diffuse only, 2=specular only, 3=kD value
    if (debugMode == 1) {
        return vec4f(pow(kD * diffuse * ao, vec3f(1.0/2.2)), 1.0);
    } else if (debugMode == 2) {
        return vec4f(pow(specular * ao, vec3f(1.0/2.2)), 1.0);
    } else if (debugMode == 3) {
        return vec4f(vec3f(kD), 1.0);
    }

    // -----------------------------------------------------------------
    // Direct Lighting
    // -----------------------------------------------------------------

    var directLighting = vec3f(0.0);
    for (var i = 0; i < lights.lightCount; i++) {
        directLighting += calculatePBRLight(lights.lights[i], in.worldPos, N, V, F0, albedo, metallic, roughness);
    }

    // -----------------------------------------------------------------
    // Final Color
    // -----------------------------------------------------------------

    var color = ambient + directLighting + emissive;

    // HDR tone mapping (Reinhard)
    color = color / (color + vec3f(1.0));

    // Gamma correction
    color = pow(color, vec3f(1.0 / 2.2));

    return vec4f(color, 1.0);
}
)";

} // namespace shaders3d

// ============================================================================
// Pipeline3DLit Implementation
// ============================================================================

Pipeline3DLit::~Pipeline3DLit() {
    destroy();
}

bool Pipeline3DLit::init(Renderer& renderer, ShadingModel model) {
    destroy();
    renderer_ = &renderer;
    model_ = model;

    const char* shaderSource;
    switch (model) {
        case ShadingModel::PBR_IBL_Textured:
            shaderSource = shaders3d::PBR_IBL_TEXTURED;
            break;
        case ShadingModel::PBR_IBL:
            shaderSource = shaders3d::PBR_IBL;
            break;
        case ShadingModel::PBR:
            shaderSource = shaders3d::PBR_LIT;
            break;
        default:
            shaderSource = shaders3d::PHONG_LIT;
            break;
    }
    return createPipeline(shaderSource);
}

bool Pipeline3DLit::createPipeline(const std::string& shaderSource) {
    WGPUDevice device = renderer_->device();

    // Create shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = WGPUStringView{.data = shaderSource.c_str(), .length = shaderSource.size()};

    WGPUShaderModuleDescriptor moduleDesc = {};
    moduleDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&wgslDesc);

    shaderModule_ = wgpuDeviceCreateShaderModule(device, &moduleDesc);
    if (!shaderModule_) {
        std::cerr << "[Pipeline3DLit] Failed to create shader module\n";
        return false;
    }

    // Create bind group layouts

    // Group 0: Camera
    WGPUBindGroupLayoutEntry cameraEntry = {};
    cameraEntry.binding = 0;
    cameraEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    cameraEntry.buffer.type = WGPUBufferBindingType_Uniform;
    cameraEntry.buffer.minBindingSize = sizeof(CameraUniform);

    WGPUBindGroupLayoutDescriptor cameraLayoutDesc = {};
    cameraLayoutDesc.entryCount = 1;
    cameraLayoutDesc.entries = &cameraEntry;
    cameraLayout_ = wgpuDeviceCreateBindGroupLayout(device, &cameraLayoutDesc);

    // Group 1: Transform
    WGPUBindGroupLayoutEntry transformEntry = {};
    transformEntry.binding = 0;
    transformEntry.visibility = WGPUShaderStage_Vertex;
    transformEntry.buffer.type = WGPUBufferBindingType_Uniform;
    transformEntry.buffer.minBindingSize = sizeof(TransformUniform);

    WGPUBindGroupLayoutDescriptor transformLayoutDesc = {};
    transformLayoutDesc.entryCount = 1;
    transformLayoutDesc.entries = &transformEntry;
    transformLayout_ = wgpuDeviceCreateBindGroupLayout(device, &transformLayoutDesc);

    // Group 2: Lights
    WGPUBindGroupLayoutEntry lightsEntry = {};
    lightsEntry.binding = 0;
    lightsEntry.visibility = WGPUShaderStage_Fragment;
    lightsEntry.buffer.type = WGPUBufferBindingType_Uniform;
    lightsEntry.buffer.minBindingSize = sizeof(LightsUniform);

    WGPUBindGroupLayoutDescriptor lightsLayoutDesc = {};
    lightsLayoutDesc.entryCount = 1;
    lightsLayoutDesc.entries = &lightsEntry;
    lightsLayout_ = wgpuDeviceCreateBindGroupLayout(device, &lightsLayoutDesc);

    // Group 3: Material (for non-IBL) or Material + IBL textures (for PBR_IBL)
    if (model_ == ShadingModel::PBR_IBL_Textured) {
        // Combined layout: material uniform + IBL textures + material textures + samplers
        WGPUBindGroupLayoutEntry entries[14] = {};

        // @binding(0) = material uniform
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Fragment;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        entries[0].buffer.minBindingSize = sizeof(TexturedPBRMaterialUniform);

        // @binding(1) = irradianceMap (texture_cube)
        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].texture.sampleType = WGPUTextureSampleType_Float;
        entries[1].texture.viewDimension = WGPUTextureViewDimension_Cube;

        // @binding(2) = radianceMap (texture_cube)
        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Fragment;
        entries[2].texture.sampleType = WGPUTextureSampleType_Float;
        entries[2].texture.viewDimension = WGPUTextureViewDimension_Cube;

        // @binding(3) = brdfLUT (texture_2d) - unfilterable
        entries[3].binding = 3;
        entries[3].visibility = WGPUShaderStage_Fragment;
        entries[3].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        entries[3].texture.viewDimension = WGPUTextureViewDimension_2D;

        // @binding(4) = iblSampler (filtering sampler for cubemaps)
        entries[4].binding = 4;
        entries[4].visibility = WGPUShaderStage_Fragment;
        entries[4].sampler.type = WGPUSamplerBindingType_Filtering;

        // @binding(5) = brdfSampler (non-filtering for BRDF LUT)
        entries[5].binding = 5;
        entries[5].visibility = WGPUShaderStage_Fragment;
        entries[5].sampler.type = WGPUSamplerBindingType_NonFiltering;

        // @binding(6) = albedoMap (texture_2d)
        entries[6].binding = 6;
        entries[6].visibility = WGPUShaderStage_Fragment;
        entries[6].texture.sampleType = WGPUTextureSampleType_Float;
        entries[6].texture.viewDimension = WGPUTextureViewDimension_2D;

        // @binding(7) = normalMap (texture_2d)
        entries[7].binding = 7;
        entries[7].visibility = WGPUShaderStage_Fragment;
        entries[7].texture.sampleType = WGPUTextureSampleType_Float;
        entries[7].texture.viewDimension = WGPUTextureViewDimension_2D;

        // @binding(8) = metallicRoughnessMap (texture_2d)
        entries[8].binding = 8;
        entries[8].visibility = WGPUShaderStage_Fragment;
        entries[8].texture.sampleType = WGPUTextureSampleType_Float;
        entries[8].texture.viewDimension = WGPUTextureViewDimension_2D;

        // @binding(9) = aoMap (texture_2d)
        entries[9].binding = 9;
        entries[9].visibility = WGPUShaderStage_Fragment;
        entries[9].texture.sampleType = WGPUTextureSampleType_Float;
        entries[9].texture.viewDimension = WGPUTextureViewDimension_2D;

        // @binding(10) = emissiveMap (texture_2d)
        entries[10].binding = 10;
        entries[10].visibility = WGPUShaderStage_Fragment;
        entries[10].texture.sampleType = WGPUTextureSampleType_Float;
        entries[10].texture.viewDimension = WGPUTextureViewDimension_2D;

        // @binding(11) = textureSampler (filtering sampler for material textures)
        entries[11].binding = 11;
        entries[11].visibility = WGPUShaderStage_Fragment;
        entries[11].sampler.type = WGPUSamplerBindingType_Filtering;

        // @binding(12) = roughnessMap (texture_2d) - separate roughness
        entries[12].binding = 12;
        entries[12].visibility = WGPUShaderStage_Fragment;
        entries[12].texture.sampleType = WGPUTextureSampleType_Float;
        entries[12].texture.viewDimension = WGPUTextureViewDimension_2D;

        // @binding(13) = metallicMapTex (texture_2d) - separate metallic
        entries[13].binding = 13;
        entries[13].visibility = WGPUShaderStage_Fragment;
        entries[13].texture.sampleType = WGPUTextureSampleType_Float;
        entries[13].texture.viewDimension = WGPUTextureViewDimension_2D;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 14;
        layoutDesc.entries = entries;
        materialLayout_ = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

        // Create IBL sampler (linear filtering for cubemaps)
        WGPUSamplerDescriptor samplerDesc = {};
        samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
        samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
        samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
        samplerDesc.magFilter = WGPUFilterMode_Linear;
        samplerDesc.minFilter = WGPUFilterMode_Linear;
        samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
        samplerDesc.maxAnisotropy = 1;
        iblSampler_ = wgpuDeviceCreateSampler(device, &samplerDesc);

        // Create BRDF sampler (non-filtering for RG32Float texture)
        WGPUSamplerDescriptor brdfSamplerDesc = {};
        brdfSamplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
        brdfSamplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
        brdfSamplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
        brdfSamplerDesc.magFilter = WGPUFilterMode_Nearest;
        brdfSamplerDesc.minFilter = WGPUFilterMode_Nearest;
        brdfSamplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        brdfSamplerDesc.maxAnisotropy = 1;
        brdfSampler_ = wgpuDeviceCreateSampler(device, &brdfSamplerDesc);

        // Create texture sampler (linear filtering with wrapping for material textures)
        WGPUSamplerDescriptor texSamplerDesc = {};
        texSamplerDesc.addressModeU = WGPUAddressMode_Repeat;
        texSamplerDesc.addressModeV = WGPUAddressMode_Repeat;
        texSamplerDesc.addressModeW = WGPUAddressMode_Repeat;
        texSamplerDesc.magFilter = WGPUFilterMode_Linear;
        texSamplerDesc.minFilter = WGPUFilterMode_Linear;
        texSamplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
        texSamplerDesc.maxAnisotropy = 1;
        textureSampler_ = wgpuDeviceCreateSampler(device, &texSamplerDesc);
    } else if (model_ == ShadingModel::PBR_IBL) {
        // Combined layout: material uniform + IBL textures + 2 samplers
        WGPUBindGroupLayoutEntry entries[6] = {};

        // @binding(0) = material uniform
        entries[0].binding = 0;
        entries[0].visibility = WGPUShaderStage_Fragment;
        entries[0].buffer.type = WGPUBufferBindingType_Uniform;
        entries[0].buffer.minBindingSize = sizeof(PBRMaterialUniform);

        // @binding(1) = irradianceMap (texture_cube)
        entries[1].binding = 1;
        entries[1].visibility = WGPUShaderStage_Fragment;
        entries[1].texture.sampleType = WGPUTextureSampleType_Float;
        entries[1].texture.viewDimension = WGPUTextureViewDimension_Cube;

        // @binding(2) = radianceMap (texture_cube)
        entries[2].binding = 2;
        entries[2].visibility = WGPUShaderStage_Fragment;
        entries[2].texture.sampleType = WGPUTextureSampleType_Float;
        entries[2].texture.viewDimension = WGPUTextureViewDimension_Cube;

        // @binding(3) = brdfLUT (texture_2d) - use unfilterable since RG32Float doesn't support filtering
        entries[3].binding = 3;
        entries[3].visibility = WGPUShaderStage_Fragment;
        entries[3].texture.sampleType = WGPUTextureSampleType_UnfilterableFloat;
        entries[3].texture.viewDimension = WGPUTextureViewDimension_2D;

        // @binding(4) = iblSampler (filtering sampler for cubemaps)
        entries[4].binding = 4;
        entries[4].visibility = WGPUShaderStage_Fragment;
        entries[4].sampler.type = WGPUSamplerBindingType_Filtering;

        // @binding(5) = brdfSampler (non-filtering sampler for BRDF LUT)
        entries[5].binding = 5;
        entries[5].visibility = WGPUShaderStage_Fragment;
        entries[5].sampler.type = WGPUSamplerBindingType_NonFiltering;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 6;
        layoutDesc.entries = entries;
        materialLayout_ = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

        // Create IBL sampler (linear filtering for cubemaps)
        WGPUSamplerDescriptor samplerDesc = {};
        samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
        samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
        samplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
        samplerDesc.magFilter = WGPUFilterMode_Linear;
        samplerDesc.minFilter = WGPUFilterMode_Linear;
        samplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
        samplerDesc.maxAnisotropy = 1;
        iblSampler_ = wgpuDeviceCreateSampler(device, &samplerDesc);

        // Create BRDF sampler (non-filtering for RG32Float texture)
        WGPUSamplerDescriptor brdfSamplerDesc = {};
        brdfSamplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
        brdfSamplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
        brdfSamplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
        brdfSamplerDesc.magFilter = WGPUFilterMode_Nearest;
        brdfSamplerDesc.minFilter = WGPUFilterMode_Nearest;
        brdfSamplerDesc.mipmapFilter = WGPUMipmapFilterMode_Nearest;
        brdfSamplerDesc.maxAnisotropy = 1;
        brdfSampler_ = wgpuDeviceCreateSampler(device, &brdfSamplerDesc);
    } else {
        // Non-IBL: just material uniform
        WGPUBindGroupLayoutEntry materialEntry = {};
        materialEntry.binding = 0;
        materialEntry.visibility = WGPUShaderStage_Fragment;
        materialEntry.buffer.type = WGPUBufferBindingType_Uniform;
        size_t materialSize = std::max(sizeof(PhongMaterialUniform), sizeof(PBRMaterialUniform));
        materialEntry.buffer.minBindingSize = materialSize;

        WGPUBindGroupLayoutDescriptor materialLayoutDesc = {};
        materialLayoutDesc.entryCount = 1;
        materialLayoutDesc.entries = &materialEntry;
        materialLayout_ = wgpuDeviceCreateBindGroupLayout(device, &materialLayoutDesc);
    }

    // Create pipeline layout (always 4 groups now)
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    WGPUBindGroupLayout layouts[] = {cameraLayout_, transformLayout_, lightsLayout_, materialLayout_};
    pipelineLayoutDesc.bindGroupLayoutCount = 4;
    pipelineLayoutDesc.bindGroupLayouts = layouts;

    pipelineLayout_ = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Get vertex layout
    WGPUVertexBufferLayout vertexLayout = Mesh::getVertexLayout();

    // Create render pipeline
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.layout = pipelineLayout_;

    pipelineDesc.vertex.module = shaderModule_;
    pipelineDesc.vertex.entryPoint = WGPUStringView{.data = "vs_main", .length = 7};
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexLayout;

    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_Back;

    // Depth stencil
    WGPUDepthStencilState depthStencilState = {};
    depthStencilState.format = DEPTH_FORMAT;
    depthStencilState.depthWriteEnabled = WGPUOptionalBool_True;
    depthStencilState.depthCompare = WGPUCompareFunction_Less;
    depthStencilState.stencilFront.compare = WGPUCompareFunction_Always;
    depthStencilState.stencilFront.failOp = WGPUStencilOperation_Keep;
    depthStencilState.stencilFront.depthFailOp = WGPUStencilOperation_Keep;
    depthStencilState.stencilFront.passOp = WGPUStencilOperation_Keep;
    depthStencilState.stencilBack = depthStencilState.stencilFront;
    depthStencilState.stencilReadMask = 0xFFFFFFFF;
    depthStencilState.stencilWriteMask = 0xFFFFFFFF;
    pipelineDesc.depthStencil = &depthStencilState;

    // Fragment state
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = WGPUTextureFormat_RGBA8Unorm;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule_;
    fragmentState.entryPoint = WGPUStringView{.data = "fs_main", .length = 7};
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;
    pipelineDesc.fragment = &fragmentState;

    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;

    pipeline_ = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);
    if (!pipeline_) {
        std::cerr << "[Pipeline3DLit] Failed to create render pipeline\n";
        destroy();
        return false;
    }

    // Create uniform buffers
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;

    bufferDesc.size = sizeof(CameraUniform);
    cameraBuffer_ = wgpuDeviceCreateBuffer(device, &bufferDesc);

    bufferDesc.size = sizeof(TransformUniform);
    transformBuffer_ = wgpuDeviceCreateBuffer(device, &bufferDesc);

    bufferDesc.size = sizeof(LightsUniform);
    lightsBuffer_ = wgpuDeviceCreateBuffer(device, &bufferDesc);

    bufferDesc.size = std::max(sizeof(PhongMaterialUniform), sizeof(PBRMaterialUniform));
    materialBuffer_ = wgpuDeviceCreateBuffer(device, &bufferDesc);

    const char* modelName = "Phong";
    if (model_ == ShadingModel::PBR) modelName = "PBR";
    else if (model_ == ShadingModel::PBR_IBL) modelName = "PBR+IBL";
    else if (model_ == ShadingModel::PBR_IBL_Textured) modelName = "PBR+IBL+Textured";
    std::cout << "[Pipeline3DLit] Created " << modelName << " pipeline\n";
    return true;
}

void Pipeline3DLit::destroy() {
    if (pipeline_) wgpuRenderPipelineRelease(pipeline_);
    if (pipelineLayout_) wgpuPipelineLayoutRelease(pipelineLayout_);
    if (cameraLayout_) wgpuBindGroupLayoutRelease(cameraLayout_);
    if (transformLayout_) wgpuBindGroupLayoutRelease(transformLayout_);
    if (lightsLayout_) wgpuBindGroupLayoutRelease(lightsLayout_);
    if (materialLayout_) wgpuBindGroupLayoutRelease(materialLayout_);
    if (shaderModule_) wgpuShaderModuleRelease(shaderModule_);
    if (iblSampler_) wgpuSamplerRelease(iblSampler_);
    if (brdfSampler_) wgpuSamplerRelease(brdfSampler_);
    if (textureSampler_) wgpuSamplerRelease(textureSampler_);

    if (cameraBuffer_) wgpuBufferRelease(cameraBuffer_);
    if (transformBuffer_) wgpuBufferRelease(transformBuffer_);
    if (lightsBuffer_) wgpuBufferRelease(lightsBuffer_);
    if (materialBuffer_) wgpuBufferRelease(materialBuffer_);

    destroyDepthBuffer();

    pipeline_ = nullptr;
    pipelineLayout_ = nullptr;
    cameraLayout_ = nullptr;
    transformLayout_ = nullptr;
    lightsLayout_ = nullptr;
    materialLayout_ = nullptr;
    shaderModule_ = nullptr;
    iblSampler_ = nullptr;
    brdfSampler_ = nullptr;
    textureSampler_ = nullptr;
    cameraBuffer_ = nullptr;
    transformBuffer_ = nullptr;
    lightsBuffer_ = nullptr;
    materialBuffer_ = nullptr;
    renderer_ = nullptr;
}

void Pipeline3DLit::ensureDepthBuffer(int width, int height) {
    if (depthTexture_ && depthWidth_ == width && depthHeight_ == height) {
        return;
    }

    destroyDepthBuffer();

    WGPUTextureDescriptor depthDesc = {};
    depthDesc.size = {static_cast<uint32_t>(width), static_cast<uint32_t>(height), 1};
    depthDesc.format = DEPTH_FORMAT;
    depthDesc.usage = WGPUTextureUsage_RenderAttachment;
    depthDesc.mipLevelCount = 1;
    depthDesc.sampleCount = 1;
    depthDesc.dimension = WGPUTextureDimension_2D;

    depthTexture_ = wgpuDeviceCreateTexture(renderer_->device(), &depthDesc);

    WGPUTextureViewDescriptor viewDesc = {};
    viewDesc.format = DEPTH_FORMAT;
    viewDesc.dimension = WGPUTextureViewDimension_2D;
    viewDesc.mipLevelCount = 1;
    viewDesc.arrayLayerCount = 1;
    viewDesc.aspect = WGPUTextureAspect_DepthOnly;

    depthView_ = wgpuTextureCreateView(depthTexture_, &viewDesc);

    depthWidth_ = width;
    depthHeight_ = height;
}

void Pipeline3DLit::destroyDepthBuffer() {
    if (depthView_) wgpuTextureViewRelease(depthView_);
    if (depthTexture_) wgpuTextureRelease(depthTexture_);
    depthView_ = nullptr;
    depthTexture_ = nullptr;
    depthWidth_ = 0;
    depthHeight_ = 0;
}

void Pipeline3DLit::beginRenderPass(Texture& output, const glm::vec4& clearColor) {
    ensureDepthBuffer(output.width, output.height);

    auto* outputData = getTextureData(output);
    if (!outputData) return;

    WGPUCommandEncoderDescriptor encoderDesc = {};
    encoder_ = wgpuDeviceCreateCommandEncoder(renderer_->device(), &encoderDesc);

    // Convention: negative alpha means "don't clear, keep existing content"
    bool shouldClear = clearColor.a >= 0.0f;

    WGPURenderPassColorAttachment colorAttachment = {};
    colorAttachment.view = outputData->view;
    colorAttachment.loadOp = shouldClear ? WGPULoadOp_Clear : WGPULoadOp_Load;
    colorAttachment.storeOp = WGPUStoreOp_Store;
    colorAttachment.clearValue = {clearColor.r, clearColor.g, clearColor.b, glm::max(0.0f, clearColor.a)};

    WGPURenderPassDepthStencilAttachment depthAttachment = {};
    depthAttachment.view = depthView_;
    depthAttachment.depthLoadOp = shouldClear ? WGPULoadOp_Clear : WGPULoadOp_Load;
    depthAttachment.depthStoreOp = WGPUStoreOp_Store;
    depthAttachment.depthClearValue = 1.0f;
    depthAttachment.depthReadOnly = false;
    depthAttachment.stencilLoadOp = WGPULoadOp_Undefined;
    depthAttachment.stencilStoreOp = WGPUStoreOp_Undefined;
    depthAttachment.stencilReadOnly = true;

    WGPURenderPassDescriptor renderPassDesc = {};
    renderPassDesc.colorAttachmentCount = 1;
    renderPassDesc.colorAttachments = &colorAttachment;
    renderPassDesc.depthStencilAttachment = &depthAttachment;

    renderPass_ = wgpuCommandEncoderBeginRenderPass(encoder_, &renderPassDesc);
}

void Pipeline3DLit::endRenderPass() {
    if (renderPass_) {
        wgpuRenderPassEncoderEnd(renderPass_);
        wgpuRenderPassEncoderRelease(renderPass_);
        renderPass_ = nullptr;
    }

    if (encoder_) {
        WGPUCommandBufferDescriptor cmdBufferDesc = {};
        WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder_, &cmdBufferDesc);
        wgpuQueueSubmit(renderer_->queue(), 1, &cmdBuffer);
        wgpuCommandBufferRelease(cmdBuffer);
        wgpuCommandEncoderRelease(encoder_);
        encoder_ = nullptr;
    }
}

void Pipeline3DLit::renderPhong(const Mesh3D& mesh, const Camera3D& camera, const glm::mat4& transform,
                                 const PhongMaterial& material, const SceneLighting& lighting,
                                 Texture& output, const glm::vec4& clearColor) {
    if (!valid() || !mesh.valid() || !hasValidGPU(output)) return;
    if (model_ != ShadingModel::Phong) {
        std::cerr << "[Pipeline3DLit] renderPhong called on PBR pipeline\n";
        return;
    }

    WGPUDevice device = renderer_->device();
    WGPUQueue queue = renderer_->queue();
    float aspectRatio = static_cast<float>(output.width) / output.height;

    // Upload uniforms
    CameraUniform cameraUniform = makeCameraUniform(camera, aspectRatio);
    wgpuQueueWriteBuffer(queue, cameraBuffer_, 0, &cameraUniform, sizeof(CameraUniform));

    TransformUniform transformUniform;
    transformUniform.model = transform;
    transformUniform.normalMatrix = glm::transpose(glm::inverse(transform));
    wgpuQueueWriteBuffer(queue, transformBuffer_, 0, &transformUniform, sizeof(TransformUniform));

    LightsUniform lightsUniform = makeLightsUniform(lighting);
    wgpuQueueWriteBuffer(queue, lightsBuffer_, 0, &lightsUniform, sizeof(LightsUniform));

    PhongMaterialUniform materialUniform = makePhongMaterialUniform(material);
    wgpuQueueWriteBuffer(queue, materialBuffer_, 0, &materialUniform, sizeof(PhongMaterialUniform));

    // Create bind groups
    WGPUBindGroupEntry cameraEntry = {};
    cameraEntry.binding = 0;
    cameraEntry.buffer = cameraBuffer_;
    cameraEntry.size = sizeof(CameraUniform);

    WGPUBindGroupDescriptor cameraBindGroupDesc = {};
    cameraBindGroupDesc.layout = cameraLayout_;
    cameraBindGroupDesc.entryCount = 1;
    cameraBindGroupDesc.entries = &cameraEntry;
    WGPUBindGroup cameraBindGroup = wgpuDeviceCreateBindGroup(device, &cameraBindGroupDesc);

    WGPUBindGroupEntry transformEntry = {};
    transformEntry.binding = 0;
    transformEntry.buffer = transformBuffer_;
    transformEntry.size = sizeof(TransformUniform);

    WGPUBindGroupDescriptor transformBindGroupDesc = {};
    transformBindGroupDesc.layout = transformLayout_;
    transformBindGroupDesc.entryCount = 1;
    transformBindGroupDesc.entries = &transformEntry;
    WGPUBindGroup transformBindGroup = wgpuDeviceCreateBindGroup(device, &transformBindGroupDesc);

    WGPUBindGroupEntry lightsEntry = {};
    lightsEntry.binding = 0;
    lightsEntry.buffer = lightsBuffer_;
    lightsEntry.size = sizeof(LightsUniform);

    WGPUBindGroupDescriptor lightsBindGroupDesc = {};
    lightsBindGroupDesc.layout = lightsLayout_;
    lightsBindGroupDesc.entryCount = 1;
    lightsBindGroupDesc.entries = &lightsEntry;
    WGPUBindGroup lightsBindGroup = wgpuDeviceCreateBindGroup(device, &lightsBindGroupDesc);

    WGPUBindGroupEntry materialEntry = {};
    materialEntry.binding = 0;
    materialEntry.buffer = materialBuffer_;
    materialEntry.size = sizeof(PhongMaterialUniform);

    WGPUBindGroupDescriptor materialBindGroupDesc = {};
    materialBindGroupDesc.layout = materialLayout_;
    materialBindGroupDesc.entryCount = 1;
    materialBindGroupDesc.entries = &materialEntry;
    WGPUBindGroup materialBindGroup = wgpuDeviceCreateBindGroup(device, &materialBindGroupDesc);

    // Render
    beginRenderPass(output, clearColor);
    if (renderPass_) {
        wgpuRenderPassEncoderSetPipeline(renderPass_, pipeline_);
        wgpuRenderPassEncoderSetBindGroup(renderPass_, 0, cameraBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(renderPass_, 1, transformBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(renderPass_, 2, lightsBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(renderPass_, 3, materialBindGroup, 0, nullptr);

        Mesh* meshData = static_cast<Mesh*>(mesh.handle);
        meshData->draw(renderPass_);
    }
    endRenderPass();

    // Cleanup bind groups
    wgpuBindGroupRelease(cameraBindGroup);
    wgpuBindGroupRelease(transformBindGroup);
    wgpuBindGroupRelease(lightsBindGroup);
    wgpuBindGroupRelease(materialBindGroup);
}

void Pipeline3DLit::renderPBR(const Mesh3D& mesh, const Camera3D& camera, const glm::mat4& transform,
                               const PBRMaterial& material, const SceneLighting& lighting,
                               Texture& output, const glm::vec4& clearColor) {
    if (!valid() || !mesh.valid() || !hasValidGPU(output)) return;
    if (model_ != ShadingModel::PBR) {
        std::cerr << "[Pipeline3DLit] renderPBR called on Phong pipeline\n";
        return;
    }

    WGPUDevice device = renderer_->device();
    WGPUQueue queue = renderer_->queue();
    float aspectRatio = static_cast<float>(output.width) / output.height;

    // Upload uniforms
    CameraUniform cameraUniform = makeCameraUniform(camera, aspectRatio);
    wgpuQueueWriteBuffer(queue, cameraBuffer_, 0, &cameraUniform, sizeof(CameraUniform));

    TransformUniform transformUniform;
    transformUniform.model = transform;
    transformUniform.normalMatrix = glm::transpose(glm::inverse(transform));
    wgpuQueueWriteBuffer(queue, transformBuffer_, 0, &transformUniform, sizeof(TransformUniform));

    LightsUniform lightsUniform = makeLightsUniform(lighting);
    wgpuQueueWriteBuffer(queue, lightsBuffer_, 0, &lightsUniform, sizeof(LightsUniform));

    PBRMaterialUniform materialUniform = makePBRMaterialUniform(material);
    wgpuQueueWriteBuffer(queue, materialBuffer_, 0, &materialUniform, sizeof(PBRMaterialUniform));

    // Create bind groups
    WGPUBindGroupEntry cameraEntry = {};
    cameraEntry.binding = 0;
    cameraEntry.buffer = cameraBuffer_;
    cameraEntry.size = sizeof(CameraUniform);

    WGPUBindGroupDescriptor cameraBindGroupDesc = {};
    cameraBindGroupDesc.layout = cameraLayout_;
    cameraBindGroupDesc.entryCount = 1;
    cameraBindGroupDesc.entries = &cameraEntry;
    WGPUBindGroup cameraBindGroup = wgpuDeviceCreateBindGroup(device, &cameraBindGroupDesc);

    WGPUBindGroupEntry transformEntry = {};
    transformEntry.binding = 0;
    transformEntry.buffer = transformBuffer_;
    transformEntry.size = sizeof(TransformUniform);

    WGPUBindGroupDescriptor transformBindGroupDesc = {};
    transformBindGroupDesc.layout = transformLayout_;
    transformBindGroupDesc.entryCount = 1;
    transformBindGroupDesc.entries = &transformEntry;
    WGPUBindGroup transformBindGroup = wgpuDeviceCreateBindGroup(device, &transformBindGroupDesc);

    WGPUBindGroupEntry lightsEntry = {};
    lightsEntry.binding = 0;
    lightsEntry.buffer = lightsBuffer_;
    lightsEntry.size = sizeof(LightsUniform);

    WGPUBindGroupDescriptor lightsBindGroupDesc = {};
    lightsBindGroupDesc.layout = lightsLayout_;
    lightsBindGroupDesc.entryCount = 1;
    lightsBindGroupDesc.entries = &lightsEntry;
    WGPUBindGroup lightsBindGroup = wgpuDeviceCreateBindGroup(device, &lightsBindGroupDesc);

    WGPUBindGroupEntry materialEntry = {};
    materialEntry.binding = 0;
    materialEntry.buffer = materialBuffer_;
    materialEntry.size = sizeof(PBRMaterialUniform);

    WGPUBindGroupDescriptor materialBindGroupDesc = {};
    materialBindGroupDesc.layout = materialLayout_;
    materialBindGroupDesc.entryCount = 1;
    materialBindGroupDesc.entries = &materialEntry;
    WGPUBindGroup materialBindGroup = wgpuDeviceCreateBindGroup(device, &materialBindGroupDesc);

    // Render
    beginRenderPass(output, clearColor);
    if (renderPass_) {
        wgpuRenderPassEncoderSetPipeline(renderPass_, pipeline_);
        wgpuRenderPassEncoderSetBindGroup(renderPass_, 0, cameraBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(renderPass_, 1, transformBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(renderPass_, 2, lightsBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(renderPass_, 3, materialBindGroup, 0, nullptr);

        Mesh* meshData = static_cast<Mesh*>(mesh.handle);
        meshData->draw(renderPass_);
    }
    endRenderPass();

    // Cleanup bind groups
    wgpuBindGroupRelease(cameraBindGroup);
    wgpuBindGroupRelease(transformBindGroup);
    wgpuBindGroupRelease(lightsBindGroup);
    wgpuBindGroupRelease(materialBindGroup);
}

void Pipeline3DLit::renderPBRWithIBL(const Mesh3D& mesh, const Camera3D& camera, const glm::mat4& transform,
                                      const PBRMaterial& material, const SceneLighting& lighting,
                                      const Environment& env,
                                      Texture& output, const glm::vec4& clearColor) {
    if (!valid() || !mesh.valid() || !hasValidGPU(output)) return;
    if (model_ != ShadingModel::PBR_IBL) {
        std::cerr << "[Pipeline3DLit] renderPBRWithIBL called on non-IBL pipeline\n";
        return;
    }
    if (!env.valid()) {
        std::cerr << "[Pipeline3DLit] renderPBRWithIBL called with invalid environment\n";
        return;
    }

    WGPUDevice device = renderer_->device();
    WGPUQueue queue = renderer_->queue();
    float aspectRatio = static_cast<float>(output.width) / output.height;

    // Upload uniforms
    CameraUniform cameraUniform = makeCameraUniform(camera, aspectRatio);
    wgpuQueueWriteBuffer(queue, cameraBuffer_, 0, &cameraUniform, sizeof(CameraUniform));

    TransformUniform transformUniform;
    transformUniform.model = transform;
    transformUniform.normalMatrix = glm::transpose(glm::inverse(transform));
    wgpuQueueWriteBuffer(queue, transformBuffer_, 0, &transformUniform, sizeof(TransformUniform));

    LightsUniform lightsUniform = makeLightsUniform(lighting);
    wgpuQueueWriteBuffer(queue, lightsBuffer_, 0, &lightsUniform, sizeof(LightsUniform));

    PBRMaterialUniform materialUniform = makePBRMaterialUniform(material);
    wgpuQueueWriteBuffer(queue, materialBuffer_, 0, &materialUniform, sizeof(PBRMaterialUniform));

    // Create bind groups for camera, transform, lights, material
    WGPUBindGroupEntry cameraEntry = {};
    cameraEntry.binding = 0;
    cameraEntry.buffer = cameraBuffer_;
    cameraEntry.size = sizeof(CameraUniform);

    WGPUBindGroupDescriptor cameraBindGroupDesc = {};
    cameraBindGroupDesc.layout = cameraLayout_;
    cameraBindGroupDesc.entryCount = 1;
    cameraBindGroupDesc.entries = &cameraEntry;
    WGPUBindGroup cameraBindGroup = wgpuDeviceCreateBindGroup(device, &cameraBindGroupDesc);

    WGPUBindGroupEntry transformEntry = {};
    transformEntry.binding = 0;
    transformEntry.buffer = transformBuffer_;
    transformEntry.size = sizeof(TransformUniform);

    WGPUBindGroupDescriptor transformBindGroupDesc = {};
    transformBindGroupDesc.layout = transformLayout_;
    transformBindGroupDesc.entryCount = 1;
    transformBindGroupDesc.entries = &transformEntry;
    WGPUBindGroup transformBindGroup = wgpuDeviceCreateBindGroup(device, &transformBindGroupDesc);

    WGPUBindGroupEntry lightsEntry = {};
    lightsEntry.binding = 0;
    lightsEntry.buffer = lightsBuffer_;
    lightsEntry.size = sizeof(LightsUniform);

    WGPUBindGroupDescriptor lightsBindGroupDesc = {};
    lightsBindGroupDesc.layout = lightsLayout_;
    lightsBindGroupDesc.entryCount = 1;
    lightsBindGroupDesc.entries = &lightsEntry;
    WGPUBindGroup lightsBindGroup = wgpuDeviceCreateBindGroup(device, &lightsBindGroupDesc);

    // Create combined material + IBL bind group (group 3)
    CubemapData* irradianceData = getCubemapData(env.irradianceMap);
    CubemapData* radianceData = getCubemapData(env.radianceMap);
    WGPUTextureView brdfView = static_cast<WGPUTextureView>(env.brdfLUT);

    WGPUBindGroupEntry materialIBLEntries[6] = {};
    // binding 0: material uniform
    materialIBLEntries[0].binding = 0;
    materialIBLEntries[0].buffer = materialBuffer_;
    materialIBLEntries[0].size = sizeof(PBRMaterialUniform);
    // binding 1: irradiance map
    materialIBLEntries[1].binding = 1;
    materialIBLEntries[1].textureView = irradianceData->view;
    // binding 2: radiance map
    materialIBLEntries[2].binding = 2;
    materialIBLEntries[2].textureView = radianceData->view;
    // binding 3: BRDF LUT
    materialIBLEntries[3].binding = 3;
    materialIBLEntries[3].textureView = brdfView;
    // binding 4: filtering sampler (for cubemaps)
    materialIBLEntries[4].binding = 4;
    materialIBLEntries[4].sampler = iblSampler_;
    // binding 5: non-filtering sampler (for BRDF LUT)
    materialIBLEntries[5].binding = 5;
    materialIBLEntries[5].sampler = brdfSampler_;

    WGPUBindGroupDescriptor materialIBLBindGroupDesc = {};
    materialIBLBindGroupDesc.layout = materialLayout_;
    materialIBLBindGroupDesc.entryCount = 6;
    materialIBLBindGroupDesc.entries = materialIBLEntries;
    WGPUBindGroup materialIBLBindGroup = wgpuDeviceCreateBindGroup(device, &materialIBLBindGroupDesc);

    // Render
    beginRenderPass(output, clearColor);
    if (renderPass_) {
        wgpuRenderPassEncoderSetPipeline(renderPass_, pipeline_);
        wgpuRenderPassEncoderSetBindGroup(renderPass_, 0, cameraBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(renderPass_, 1, transformBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(renderPass_, 2, lightsBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(renderPass_, 3, materialIBLBindGroup, 0, nullptr);

        Mesh* meshData = static_cast<Mesh*>(mesh.handle);
        meshData->draw(renderPass_);
    }
    endRenderPass();

    // Cleanup bind groups
    wgpuBindGroupRelease(cameraBindGroup);
    wgpuBindGroupRelease(transformBindGroup);
    wgpuBindGroupRelease(lightsBindGroup);
    wgpuBindGroupRelease(materialIBLBindGroup);
}

void Pipeline3DLit::renderPBRTexturedWithIBL(const Mesh3D& mesh, const Camera3D& camera, const glm::mat4& transform,
                                              const TexturedPBRMaterial& material, const SceneLighting& lighting,
                                              const Environment& env,
                                              Texture& output, const glm::vec4& clearColor) {
    if (!valid() || !mesh.valid() || !hasValidGPU(output)) return;
    if (model_ != ShadingModel::PBR_IBL_Textured) {
        std::cerr << "[Pipeline3DLit] renderPBRTexturedWithIBL called on non-textured pipeline\n";
        return;
    }
    if (!env.valid()) {
        std::cerr << "[Pipeline3DLit] renderPBRTexturedWithIBL called with invalid environment\n";
        return;
    }

    WGPUDevice device = renderer_->device();
    WGPUQueue queue = renderer_->queue();
    float aspectRatio = static_cast<float>(output.width) / output.height;

    // Upload uniforms
    CameraUniform cameraUniform = makeCameraUniform(camera, aspectRatio);
    wgpuQueueWriteBuffer(queue, cameraBuffer_, 0, &cameraUniform, sizeof(CameraUniform));

    TransformUniform transformUniform;
    transformUniform.model = transform;
    transformUniform.normalMatrix = glm::transpose(glm::inverse(transform));
    wgpuQueueWriteBuffer(queue, transformBuffer_, 0, &transformUniform, sizeof(TransformUniform));

    LightsUniform lightsUniform = makeLightsUniform(lighting);
    wgpuQueueWriteBuffer(queue, lightsBuffer_, 0, &lightsUniform, sizeof(LightsUniform));

    TexturedPBRMaterialUniform materialUniform = makeTexturedPBRMaterialUniform(material);

    // Debug: print texture flags once
    static bool debugPrinted = false;
    if (!debugPrinted) {
        std::cout << "[DEBUG] TexturedPBR textureFlags: " << materialUniform.textureFlags << std::endl;
        std::cout << "  - albedoMap ptr: " << material.albedoMap << std::endl;
        std::cout << "  - normalMap ptr: " << material.normalMap << std::endl;
        std::cout << "  - roughnessMap ptr: " << material.roughnessMap << std::endl;
        std::cout << "  - metallicMap ptr: " << material.metallicMap << std::endl;
        std::cout << "  - sizeof(TexturedPBRMaterialUniform): " << sizeof(TexturedPBRMaterialUniform) << std::endl;
        debugPrinted = true;
    }

    wgpuQueueWriteBuffer(queue, materialBuffer_, 0, &materialUniform, sizeof(TexturedPBRMaterialUniform));

    // Create bind groups for camera, transform, lights
    WGPUBindGroupEntry cameraEntry = {};
    cameraEntry.binding = 0;
    cameraEntry.buffer = cameraBuffer_;
    cameraEntry.size = sizeof(CameraUniform);

    WGPUBindGroupDescriptor cameraBindGroupDesc = {};
    cameraBindGroupDesc.layout = cameraLayout_;
    cameraBindGroupDesc.entryCount = 1;
    cameraBindGroupDesc.entries = &cameraEntry;
    WGPUBindGroup cameraBindGroup = wgpuDeviceCreateBindGroup(device, &cameraBindGroupDesc);

    WGPUBindGroupEntry transformEntry = {};
    transformEntry.binding = 0;
    transformEntry.buffer = transformBuffer_;
    transformEntry.size = sizeof(TransformUniform);

    WGPUBindGroupDescriptor transformBindGroupDesc = {};
    transformBindGroupDesc.layout = transformLayout_;
    transformBindGroupDesc.entryCount = 1;
    transformBindGroupDesc.entries = &transformEntry;
    WGPUBindGroup transformBindGroup = wgpuDeviceCreateBindGroup(device, &transformBindGroupDesc);

    WGPUBindGroupEntry lightsEntry = {};
    lightsEntry.binding = 0;
    lightsEntry.buffer = lightsBuffer_;
    lightsEntry.size = sizeof(LightsUniform);

    WGPUBindGroupDescriptor lightsBindGroupDesc = {};
    lightsBindGroupDesc.layout = lightsLayout_;
    lightsBindGroupDesc.entryCount = 1;
    lightsBindGroupDesc.entries = &lightsEntry;
    WGPUBindGroup lightsBindGroup = wgpuDeviceCreateBindGroup(device, &lightsBindGroupDesc);

    // Create dummy textures for null material textures
    // We need to create 1x1 textures for any missing maps
    std::vector<WGPUTexture> dummyTextures;
    std::vector<WGPUTextureView> dummyViews;

    auto createDummy1x1 = [&](uint8_t r, uint8_t g, uint8_t b, uint8_t a) -> WGPUTextureView {
        WGPUTextureDescriptor desc = {};
        desc.size = {1, 1, 1};
        desc.format = WGPUTextureFormat_RGBA8Unorm;
        desc.usage = WGPUTextureUsage_TextureBinding | WGPUTextureUsage_CopyDst;
        desc.mipLevelCount = 1;
        desc.sampleCount = 1;
        desc.dimension = WGPUTextureDimension_2D;

        WGPUTexture tex = wgpuDeviceCreateTexture(device, &desc);
        dummyTextures.push_back(tex);

        uint8_t pixels[4] = {r, g, b, a};
        WGPUTexelCopyTextureInfo destination = {};
        destination.texture = tex;
        destination.mipLevel = 0;
        destination.origin = {0, 0, 0};

        WGPUTexelCopyBufferLayout dataLayout = {};
        dataLayout.offset = 0;
        dataLayout.bytesPerRow = 4;
        dataLayout.rowsPerImage = 1;

        WGPUExtent3D size = {1, 1, 1};
        wgpuQueueWriteTexture(queue, &destination, pixels, 4, &dataLayout, &size);

        WGPUTextureViewDescriptor viewDesc = {};
        viewDesc.format = WGPUTextureFormat_RGBA8Unorm;
        viewDesc.dimension = WGPUTextureViewDimension_2D;
        viewDesc.mipLevelCount = 1;
        viewDesc.arrayLayerCount = 1;

        WGPUTextureView view = wgpuTextureCreateView(tex, &viewDesc);
        dummyViews.push_back(view);
        return view;
    };

    // Get texture views, using dummies for null textures
    WGPUTextureView albedoView = material.albedoMap && hasValidGPU(*material.albedoMap)
        ? getTextureData(*material.albedoMap)->view
        : createDummy1x1(255, 255, 255, 255);  // White default

    WGPUTextureView normalView = material.normalMap && hasValidGPU(*material.normalMap)
        ? getTextureData(*material.normalMap)->view
        : createDummy1x1(128, 128, 255, 255);  // Flat normal (0.5, 0.5, 1.0)

    WGPUTextureView mrView = material.metallicRoughnessMap && hasValidGPU(*material.metallicRoughnessMap)
        ? getTextureData(*material.metallicRoughnessMap)->view
        : createDummy1x1(255, 255, 255, 255);  // White (use base values)

    WGPUTextureView aoView = material.aoMap && hasValidGPU(*material.aoMap)
        ? getTextureData(*material.aoMap)->view
        : createDummy1x1(255, 255, 255, 255);  // White (no occlusion)

    WGPUTextureView emissiveView = material.emissiveMap && hasValidGPU(*material.emissiveMap)
        ? getTextureData(*material.emissiveMap)->view
        : createDummy1x1(255, 255, 255, 255);  // White (use base values)

    WGPUTextureView roughnessView = material.roughnessMap && hasValidGPU(*material.roughnessMap)
        ? getTextureData(*material.roughnessMap)->view
        : createDummy1x1(255, 255, 255, 255);  // White (use base values)

    WGPUTextureView metallicView = material.metallicMap && hasValidGPU(*material.metallicMap)
        ? getTextureData(*material.metallicMap)->view
        : createDummy1x1(255, 255, 255, 255);  // White (use base values)

    // Create combined material + IBL + textures bind group (group 3)
    CubemapData* irradianceData = getCubemapData(env.irradianceMap);
    CubemapData* radianceData = getCubemapData(env.radianceMap);
    WGPUTextureView brdfView = static_cast<WGPUTextureView>(env.brdfLUT);

    WGPUBindGroupEntry entries[14] = {};
    // binding 0: material uniform
    entries[0].binding = 0;
    entries[0].buffer = materialBuffer_;
    entries[0].size = sizeof(TexturedPBRMaterialUniform);
    // binding 1: irradiance map
    entries[1].binding = 1;
    entries[1].textureView = irradianceData->view;
    // binding 2: radiance map
    entries[2].binding = 2;
    entries[2].textureView = radianceData->view;
    // binding 3: BRDF LUT
    entries[3].binding = 3;
    entries[3].textureView = brdfView;
    // binding 4: IBL sampler
    entries[4].binding = 4;
    entries[4].sampler = iblSampler_;
    // binding 5: BRDF sampler
    entries[5].binding = 5;
    entries[5].sampler = brdfSampler_;
    // binding 6: albedo map
    entries[6].binding = 6;
    entries[6].textureView = albedoView;
    // binding 7: normal map
    entries[7].binding = 7;
    entries[7].textureView = normalView;
    // binding 8: metallic-roughness map
    entries[8].binding = 8;
    entries[8].textureView = mrView;
    // binding 9: AO map
    entries[9].binding = 9;
    entries[9].textureView = aoView;
    // binding 10: emissive map
    entries[10].binding = 10;
    entries[10].textureView = emissiveView;
    // binding 11: texture sampler
    entries[11].binding = 11;
    entries[11].sampler = textureSampler_;
    // binding 12: roughness map (separate)
    entries[12].binding = 12;
    entries[12].textureView = roughnessView;
    // binding 13: metallic map (separate)
    entries[13].binding = 13;
    entries[13].textureView = metallicView;

    WGPUBindGroupDescriptor materialBindGroupDesc = {};
    materialBindGroupDesc.layout = materialLayout_;
    materialBindGroupDesc.entryCount = 14;
    materialBindGroupDesc.entries = entries;
    WGPUBindGroup materialBindGroup = wgpuDeviceCreateBindGroup(device, &materialBindGroupDesc);

    // Render
    beginRenderPass(output, clearColor);
    if (renderPass_) {
        wgpuRenderPassEncoderSetPipeline(renderPass_, pipeline_);
        wgpuRenderPassEncoderSetBindGroup(renderPass_, 0, cameraBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(renderPass_, 1, transformBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(renderPass_, 2, lightsBindGroup, 0, nullptr);
        wgpuRenderPassEncoderSetBindGroup(renderPass_, 3, materialBindGroup, 0, nullptr);

        Mesh* meshData = static_cast<Mesh*>(mesh.handle);
        meshData->draw(renderPass_);
    }
    endRenderPass();

    // Cleanup bind groups and dummy textures
    wgpuBindGroupRelease(cameraBindGroup);
    wgpuBindGroupRelease(transformBindGroup);
    wgpuBindGroupRelease(lightsBindGroup);
    wgpuBindGroupRelease(materialBindGroup);

    for (auto view : dummyViews) {
        wgpuTextureViewRelease(view);
    }
    for (auto tex : dummyTextures) {
        wgpuTextureRelease(tex);
    }
}

} // namespace vivid
