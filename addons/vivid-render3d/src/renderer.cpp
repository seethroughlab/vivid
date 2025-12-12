#include <vivid/render3d/renderer.h>
#include <vivid/render3d/scene_composer.h>
#include <vivid/render3d/camera_operator.h>
#include <vivid/render3d/light_operators.h>
#include <vivid/render3d/textured_material.h>
#include <vivid/render3d/ibl_environment.h>
#include <vivid/context.h>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cmath>

namespace vivid::render3d {

using namespace vivid::effects;

namespace {
// Use Depth32Float so we can sample it for post-processing
constexpr WGPUTextureFormat DEPTH_FORMAT = WGPUTextureFormat_Depth32Float;
// toStringView is inherited from vivid::effects (texture_operator.h)

// Shader to copy depth buffer to a linear depth output texture
const char* DEPTH_COPY_SHADER_SOURCE = R"(
struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) uv: vec2f,
};

struct DepthUniforms {
    near: f32,
    far: f32,
    _pad: vec2f,
};

@group(0) @binding(0) var depthTexture: texture_depth_2d;
@group(0) @binding(1) var depthSampler: sampler;
@group(0) @binding(2) var<uniform> uniforms: DepthUniforms;

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var output: VertexOutput;
    // Fullscreen triangle
    let x = f32(i32(vertexIndex & 1u) * 4 - 1);
    let y = f32(i32(vertexIndex >> 1u) * 4 - 1);
    output.position = vec4f(x, y, 0.0, 1.0);
    output.uv = vec2f((x + 1.0) * 0.5, (1.0 - y) * 0.5);
    return output;
}

@fragment
fn fs_main(input: VertexOutput) -> @location(0) vec4f {
    let depth = textureSample(depthTexture, depthSampler, input.uv);

    // Linearize depth from [0,1] non-linear to [0,1] linear
    // depth = (far * near) / (far - z * (far - near)) -> solve for z
    // z = near * far / (far - depth * (far - near))
    let near = uniforms.near;
    let far = uniforms.far;
    let linearZ = near * far / (far - depth * (far - near));

    // Normalize to [0,1] range
    let normalizedDepth = saturate((linearZ - near) / (far - near));

    return vec4f(normalizedDepth, 0.0, 0.0, 1.0);
}
)";


// Flat/Gouraud/Unlit shader source with multi-light support
const char* FLAT_SHADER_SOURCE = R"(
const MAX_LIGHTS: u32 = 4u;
const LIGHT_DIRECTIONAL: u32 = 0u;
const LIGHT_POINT: u32 = 1u;
const LIGHT_SPOT: u32 = 2u;
const EPSILON: f32 = 0.0001;

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
    mvp: mat4x4f,
    model: mat4x4f,
    worldPos: vec3f,  // Not used in flat shading but kept for alignment
    _pad0: f32,
    baseColor: vec4f,
    ambient: f32,
    shadingMode: u32,  // 0=Unlit, 1=Flat, 2=Gouraud, 3=VertexLit, 4=Toon
    lightCount: u32,
    toonLevels: u32,   // Number of toon shading bands (2-8)
    lights: array<Light, 4>,
};

@group(0) @binding(0) var<uniform> uniforms: Uniforms;

struct VertexInput {
    @location(0) position: vec3f,
    @location(1) normal: vec3f,
    @location(2) tangent: vec4f,
    @location(3) uv: vec2f,
    @location(4) color: vec4f,
};

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) worldPos: vec3f,
    @location(1) worldNormal: vec3f,
    @location(2) uv: vec2f,
    @location(3) color: vec4f,
    @location(4) lighting: vec3f,  // For Gouraud (per-vertex) lighting
};

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

fn calculateSimpleLighting(worldPos: vec3f, N: vec3f) -> vec3f {
    var Lo = vec3f(0.0);
    let lightCount = min(uniforms.lightCount, MAX_LIGHTS);
    for (var i = 0u; i < lightCount; i++) {
        let light = uniforms.lights[i];
        var L: vec3f;
        var attenuation: f32 = 1.0;

        if (light.lightType == LIGHT_DIRECTIONAL) {
            L = normalize(light.direction);
        } else if (light.lightType == LIGHT_POINT) {
            let lightVec = light.position - worldPos;
            let dist = length(lightVec);
            L = lightVec / max(dist, EPSILON);
            attenuation = getAttenuation(dist, light.range);
        } else {
            let lightVec = light.position - worldPos;
            let dist = length(lightVec);
            L = lightVec / max(dist, EPSILON);
            attenuation = getAttenuation(dist, light.range);
            attenuation *= getSpotFactor(-L, normalize(light.direction), light.spotBlend, light.spotAngle);
        }

        let NdotL = max(dot(N, L), 0.0);
        Lo += light.color * light.intensity * NdotL * attenuation;
    }
    return Lo;
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let worldPos4 = uniforms.model * vec4f(in.position, 1.0);
    out.worldPos = worldPos4.xyz;
    out.position = uniforms.mvp * vec4f(in.position, 1.0);
    out.worldNormal = normalize((uniforms.model * vec4f(in.normal, 0.0)).xyz);
    out.uv = in.uv;
    out.color = in.color;

    // Per-vertex lighting modes
    if (uniforms.shadingMode == 2u) {
        // Gouraud: per-vertex lighting with ambient
        out.lighting = calculateSimpleLighting(out.worldPos, out.worldNormal);
    } else if (uniforms.shadingMode == 3u) {
        // VertexLit: simple per-vertex N·L diffuse (no ambient in vertex shader)
        out.lighting = calculateSimpleLighting(out.worldPos, out.worldNormal);
    } else {
        out.lighting = vec3f(1.0);
    }
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    var finalColor = uniforms.baseColor * in.color;

    if (uniforms.shadingMode == 0u) {
        // Unlit
        return finalColor;
    } else if (uniforms.shadingMode == 1u) {
        // Flat (per-fragment) lighting
        let N = normalize(in.worldNormal);
        let lighting = calculateSimpleLighting(in.worldPos, N);
        return vec4f(finalColor.rgb * (vec3f(uniforms.ambient) + lighting), finalColor.a);
    } else if (uniforms.shadingMode == 2u) {
        // Gouraud (per-vertex) lighting - use pre-computed lighting with ambient
        return vec4f(finalColor.rgb * (vec3f(uniforms.ambient) + in.lighting), finalColor.a);
    } else if (uniforms.shadingMode == 3u) {
        // VertexLit: simple per-vertex diffuse without ambient term
        return vec4f(finalColor.rgb * in.lighting, finalColor.a);
    } else if (uniforms.shadingMode == 4u) {
        // Toon: quantized cel-shading
        let N = normalize(in.worldNormal);
        let lighting = calculateSimpleLighting(in.worldPos, N);
        let luminance = dot(lighting, vec3f(0.299, 0.587, 0.114));
        let levels = f32(uniforms.toonLevels);
        let quantized = floor(luminance * levels + 0.5) / levels;
        return vec4f(finalColor.rgb * (uniforms.ambient + quantized), finalColor.a);
    } else {
        // Default fallback (shouldn't reach here for non-PBR modes)
        return vec4f(finalColor.rgb * (vec3f(uniforms.ambient) + in.lighting), finalColor.a);
    }
}
)";

// PBR shader source with multi-light support
const char* PBR_SHADER_SOURCE = R"(
const PI: f32 = 3.14159265359;
const EPSILON: f32 = 0.0001;
const MAX_LIGHTS: u32 = 4u;

// Light types
const LIGHT_DIRECTIONAL: u32 = 0u;
const LIGHT_POINT: u32 = 1u;
const LIGHT_SPOT: u32 = 2u;

struct Light {
    position: vec3f,       // World position (point/spot)
    range: f32,            // Falloff range (point/spot)
    direction: vec3f,      // Light direction (directional/spot)
    spotAngle: f32,        // Cosine of outer cone angle (spot)
    color: vec3f,          // Light color
    intensity: f32,        // Light intensity
    lightType: u32,        // 0=directional, 1=point, 2=spot
    spotBlend: f32,        // Cosine of inner cone angle (spot)
    _pad: vec2f,
}

struct Uniforms {
    mvp: mat4x4f,
    model: mat4x4f,
    normalMatrix: mat4x4f,
    cameraPos: vec3f,
    ambientIntensity: f32,
    baseColor: vec4f,
    metallic: f32,
    roughness: f32,
    lightCount: u32,
    _pad0: f32,
    lights: array<Light, 4>,
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
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let worldPos = uniforms.model * vec4f(in.position, 1.0);
    out.worldPos = worldPos.xyz;
    out.clipPos = uniforms.mvp * vec4f(in.position, 1.0);
    out.worldNormal = normalize((uniforms.normalMatrix * vec4f(in.normal, 0.0)).xyz);
    out.color = in.color;
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

// Point light attenuation (inverse square with range cutoff)
fn getAttenuation(distance: f32, range: f32) -> f32 {
    if (range <= 0.0) { return 1.0; }
    let d = max(distance, EPSILON);
    let att = 1.0 / (d * d);
    let falloff = saturate(1.0 - pow(d / range, 4.0));
    return att * falloff * falloff;
}

// Spot light cone factor
fn getSpotFactor(lightDir: vec3f, spotDir: vec3f, innerAngle: f32, outerAngle: f32) -> f32 {
    let cosAngle = dot(lightDir, spotDir);
    return saturate((cosAngle - outerAngle) / max(innerAngle - outerAngle, EPSILON));
}

// Calculate contribution from a single light
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
    } else { // LIGHT_SPOT
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

    let albedo = uniforms.baseColor.rgb * in.color.rgb;
    let metallic = uniforms.metallic;
    let roughness = max(uniforms.roughness, 0.04);
    let F0 = mix(vec3f(0.04), albedo, metallic);

    // Accumulate lighting from all lights
    var Lo = vec3f(0.0);
    let lightCount = min(uniforms.lightCount, MAX_LIGHTS);
    for (var i = 0u; i < lightCount; i++) {
        Lo += calculateLightContribution(
            uniforms.lights[i], in.worldPos, N, V, albedo, metallic, roughness, F0
        );
    }

    let ambient = vec3f(0.03) * albedo * uniforms.ambientIntensity;

    var color = ambient + Lo;
    color = color / (color + vec3f(1.0));  // Reinhard tone mapping
    color = pow(color, vec3f(1.0 / 2.2));  // Gamma correction

    return vec4f(color, uniforms.baseColor.a * in.color.a);
}
)";

// Textured PBR shader source with multi-light support
const char* PBR_TEXTURED_SHADER_SOURCE = R"(
const PI: f32 = 3.14159265359;
const EPSILON: f32 = 0.0001;
const MAX_LIGHTS: u32 = 4u;

// Light types
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
    mvp: mat4x4f,
    model: mat4x4f,
    normalMatrix: mat4x4f,
    cameraPos: vec3f,
    ambientIntensity: f32,
    baseColorFactor: vec4f,
    metallicFactor: f32,
    roughnessFactor: f32,
    normalScale: f32,
    aoStrength: f32,
    emissiveFactor: vec3f,
    emissiveStrength: f32,
    textureFlags: u32,
    lightCount: u32,
    alphaCutoff: f32,
    alphaMode: u32,
    lights: array<Light, 4>,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var materialSampler: sampler;
@group(0) @binding(2) var baseColorMap: texture_2d<f32>;
@group(0) @binding(3) var normalMap: texture_2d<f32>;
@group(0) @binding(4) var metallicMap: texture_2d<f32>;
@group(0) @binding(5) var roughnessMap: texture_2d<f32>;
@group(0) @binding(6) var aoMap: texture_2d<f32>;
@group(0) @binding(7) var emissiveMap: texture_2d<f32>;

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
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let worldPos = uniforms.model * vec4f(in.position, 1.0);
    out.worldPos = worldPos.xyz;
    out.clipPos = uniforms.mvp * vec4f(in.position, 1.0);

    let N = normalize((uniforms.normalMatrix * vec4f(in.normal, 0.0)).xyz);
    let T = normalize((uniforms.model * vec4f(in.tangent.xyz, 0.0)).xyz);
    let B = cross(N, T) * in.tangent.w;

    out.worldNormal = N;
    out.worldTangent = T;
    out.worldBitangent = B;
    out.uv = in.uv;
    out.color = in.color;
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

// Alpha mode constants
const ALPHA_OPAQUE: u32 = 0u;
const ALPHA_MASK: u32 = 1u;
const ALPHA_BLEND: u32 = 2u;

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let baseColorSample = textureSample(baseColorMap, materialSampler, in.uv);

    // Compute final alpha
    let finalAlpha = baseColorSample.a * uniforms.baseColorFactor.a * in.color.a;

    // Alpha mask mode: discard fragments below cutoff
    if (uniforms.alphaMode == ALPHA_MASK && finalAlpha < uniforms.alphaCutoff) {
        discard;
    }

    let normalSample = textureSample(normalMap, materialSampler, in.uv);
    let metallicSample = textureSample(metallicMap, materialSampler, in.uv).r;
    let roughnessSample = textureSample(roughnessMap, materialSampler, in.uv).r;
    let aoSample = textureSample(aoMap, materialSampler, in.uv).r;
    let emissiveSample = textureSample(emissiveMap, materialSampler, in.uv).rgb;

    let albedo = baseColorSample.rgb * uniforms.baseColorFactor.rgb * in.color.rgb;
    let metallic = metallicSample * uniforms.metallicFactor;
    let roughness = max(roughnessSample * uniforms.roughnessFactor, 0.04);
    let ao = mix(1.0, aoSample, uniforms.aoStrength);
    let emissive = emissiveSample * uniforms.emissiveFactor * uniforms.emissiveStrength;

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

    var color = ambient + Lo + emissive;
    color = color / (color + vec3f(1.0));
    color = pow(color, vec3f(1.0 / 2.2));

    // For opaque mode, use alpha 1.0; for mask/blend modes, use computed alpha
    var outAlpha = finalAlpha;
    if (uniforms.alphaMode == ALPHA_OPAQUE) {
        outAlpha = 1.0;
    }

    return vec4f(color, outAlpha);
}
)";

// PBR with IBL shader - extends textured PBR with multi-light support and image-based lighting
const char* PBR_IBL_SHADER_SOURCE = R"(
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
    mvp: mat4x4f,
    model: mat4x4f,
    normalMatrix: mat4x4f,
    cameraPos: vec3f,
    ambientIntensity: f32,
    baseColorFactor: vec4f,
    metallicFactor: f32,
    roughnessFactor: f32,
    normalScale: f32,
    aoStrength: f32,
    emissiveFactor: vec3f,
    emissiveStrength: f32,
    textureFlags: u32,
    lightCount: u32,
    alphaCutoff: f32,
    alphaMode: u32,
    lights: array<Light, 4>,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var materialSampler: sampler;
@group(0) @binding(2) var baseColorMap: texture_2d<f32>;
@group(0) @binding(3) var normalMap: texture_2d<f32>;
@group(0) @binding(4) var metallicMap: texture_2d<f32>;
@group(0) @binding(5) var roughnessMap: texture_2d<f32>;
@group(0) @binding(6) var aoMap: texture_2d<f32>;
@group(0) @binding(7) var emissiveMap: texture_2d<f32>;

@group(1) @binding(0) var iblSampler: sampler;
@group(1) @binding(1) var brdfLUT: texture_2d<f32>;
@group(1) @binding(2) var irradianceMap: texture_cube<f32>;
@group(1) @binding(3) var prefilteredMap: texture_cube<f32>;

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
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let worldPos = uniforms.model * vec4f(in.position, 1.0);
    out.worldPos = worldPos.xyz;
    out.clipPos = uniforms.mvp * vec4f(in.position, 1.0);

    let N = normalize((uniforms.normalMatrix * vec4f(in.normal, 0.0)).xyz);
    let T = normalize((uniforms.model * vec4f(in.tangent.xyz, 0.0)).xyz);
    let B = cross(N, T) * in.tangent.w;

    out.worldNormal = N;
    out.worldTangent = T;
    out.worldBitangent = B;
    out.uv = in.uv;
    out.color = in.color;
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

fn F_SchlickRoughness(cosTheta: f32, F0: vec3f, roughness: f32) -> vec3f {
    return F0 + (max(vec3f(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
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

// Alpha mode constants
const ALPHA_OPAQUE: u32 = 0u;
const ALPHA_MASK: u32 = 1u;
const ALPHA_BLEND: u32 = 2u;

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let baseColorSample = textureSample(baseColorMap, materialSampler, in.uv);

    // Compute final alpha
    let finalAlpha = baseColorSample.a * uniforms.baseColorFactor.a * in.color.a;

    // Alpha mask mode: discard fragments below cutoff
    if (uniforms.alphaMode == ALPHA_MASK && finalAlpha < uniforms.alphaCutoff) {
        discard;
    }

    let normalSample = textureSample(normalMap, materialSampler, in.uv);
    let metallicSample = textureSample(metallicMap, materialSampler, in.uv).r;
    let roughnessSample = textureSample(roughnessMap, materialSampler, in.uv).r;
    let aoSample = textureSample(aoMap, materialSampler, in.uv).r;
    let emissiveSample = textureSample(emissiveMap, materialSampler, in.uv).rgb;

    let albedo = baseColorSample.rgb * uniforms.baseColorFactor.rgb * in.color.rgb;
    let metallic = metallicSample * uniforms.metallicFactor;
    let roughness = max(roughnessSample * uniforms.roughnessFactor, 0.04);
    let ao = mix(1.0, aoSample, uniforms.aoStrength);
    let emissive = emissiveSample * uniforms.emissiveFactor * uniforms.emissiveStrength;

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
    let R = reflect(-V, N);
    let NdotV = max(dot(N, V), EPSILON);
    let F0 = mix(vec3f(0.04), albedo, metallic);

    // Accumulate direct lighting from all lights
    var Lo = vec3f(0.0);
    let lightCount = min(uniforms.lightCount, MAX_LIGHTS);
    for (var i = 0u; i < lightCount; i++) {
        Lo += calculateLightContribution(
            uniforms.lights[i], in.worldPos, N, V, albedo, metallic, roughness, F0
        );
    }

    // IBL ambient lighting
    let F_ibl = F_SchlickRoughness(NdotV, F0, roughness);
    let kS_ibl = F_ibl;
    var kD_ibl = vec3f(1.0) - kS_ibl;
    kD_ibl *= 1.0 - metallic;

    let irradiance = textureSample(irradianceMap, iblSampler, N).rgb;
    let diffuseIBL = irradiance * albedo;

    let MAX_REFLECTION_LOD = 4.0;
    let prefilteredColor = textureSampleLevel(prefilteredMap, iblSampler, R, roughness * MAX_REFLECTION_LOD).rgb;
    let brdf = textureSample(brdfLUT, iblSampler, vec2f(NdotV, roughness)).rg;
    let specularIBL = prefilteredColor * (F_ibl * brdf.x + brdf.y);

    let ambient = (kD_ibl * diffuseIBL + specularIBL) * uniforms.ambientIntensity * ao;

    var color = ambient + Lo + emissive;
    color = color / (color + vec3f(1.0));
    color = pow(color, vec3f(1.0 / 2.2));

    // For opaque mode, use alpha 1.0; for mask/blend modes, use computed alpha
    var outAlpha = finalAlpha;
    if (uniforms.alphaMode == ALPHA_OPAQUE) {
        outAlpha = 1.0;
    }

    return vec4f(color, outAlpha);
}
)";

const char* SKYBOX_SHADER_SOURCE = R"(
struct Uniforms {
    invViewProj: mat4x4f,
}

@group(0) @binding(0) var<uniform> uniforms: Uniforms;
@group(0) @binding(1) var envSampler: sampler;
@group(0) @binding(2) var envCubemap: texture_cube<f32>;

struct VertexOutput {
    @builtin(position) position: vec4f,
    @location(0) viewDir: vec3f,
}

@vertex
fn vs_main(@builtin(vertex_index) vertexIndex: u32) -> VertexOutput {
    var out: VertexOutput;
    let x = f32((vertexIndex << 1u) & 2u) * 2.0 - 1.0;
    let y = f32(vertexIndex & 2u) * 2.0 - 1.0;
    out.position = vec4f(x, y, 0.9999, 1.0);
    let nearPoint = uniforms.invViewProj * vec4f(x, y, -1.0, 1.0);
    let farPoint = uniforms.invViewProj * vec4f(x, y, 1.0, 1.0);
    let nearWorld = nearPoint.xyz / nearPoint.w;
    let farWorld = farPoint.xyz / farPoint.w;
    out.viewDir = normalize(farWorld - nearWorld);
    return out;
}

@fragment
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    let color = textureSample(envCubemap, envSampler, in.viewDir).rgb;
    let mapped = color / (color + vec3f(1.0));
    return vec4f(mapped, 1.0);
}
)";

// Skybox uniform buffer structure
struct SkyboxUniforms {
    float invViewProj[16];  // mat4x4f: 64 bytes
};

// Maximum lights supported per draw call
constexpr uint32_t MAX_LIGHTS = 4;

// Light types (must match WGSL constants)
constexpr uint32_t LIGHT_TYPE_DIRECTIONAL = 0;
constexpr uint32_t LIGHT_TYPE_POINT = 1;
constexpr uint32_t LIGHT_TYPE_SPOT = 2;

// GPU Light structure (48 bytes, 16-byte aligned)
struct GPULight {
    float position[3];    // 12 bytes, offset 0
    float range;          // 4 bytes, offset 12
    float direction[3];   // 12 bytes, offset 16
    float spotAngle;      // 4 bytes, offset 28 (cosine of outer angle)
    float color[3];       // 12 bytes, offset 32
    float intensity;      // 4 bytes, offset 44
    uint32_t type;        // 4 bytes, offset 48
    float spotBlend;      // 4 bytes, offset 52 (cosine of inner angle)
    float _pad[2];        // 8 bytes padding to 64 bytes
};

static_assert(sizeof(GPULight) == 64, "GPULight struct must be 64 bytes");

// Flat/Gouraud uniform buffer structure (with multi-light support)
struct Uniforms {
    float mvp[16];              // mat4x4f: 64 bytes, offset 0
    float model[16];            // mat4x4f: 64 bytes, offset 64
    float worldPos[3];          // vec3f: 12 bytes, offset 128 (for point/spot lights)
    float _pad0;                // 4 bytes, offset 140
    float baseColor[4];         // vec4f: 16 bytes, offset 144
    float ambient;              // f32: 4 bytes, offset 160
    uint32_t shadingMode;       // u32: 4 bytes, offset 164
    uint32_t lightCount;        // u32: 4 bytes, offset 168
    uint32_t toonLevels;        // u32: 4 bytes, offset 172
    GPULight lights[MAX_LIGHTS]; // 64 * 4 = 256 bytes, offset 176
};                               // Total: 432 bytes

static_assert(sizeof(Uniforms) == 432, "Uniforms struct must be 432 bytes");

// PBR uniform buffer structure (with multi-light support)
struct PBRUniforms {
    float mvp[16];              // mat4x4f: 64 bytes, offset 0
    float model[16];            // mat4x4f: 64 bytes, offset 64
    float normalMatrix[16];     // mat4x4f: 64 bytes, offset 128
    float cameraPos[3];         // vec3f: 12 bytes, offset 192
    float ambientIntensity;     // f32: 4 bytes, offset 204
    float baseColor[4];         // vec4f: 16 bytes, offset 208
    float metallic;             // f32: 4 bytes, offset 224
    float roughness;            // f32: 4 bytes, offset 228
    uint32_t lightCount;        // u32: 4 bytes, offset 232
    float _pad0;                // 4 bytes, offset 236
    GPULight lights[MAX_LIGHTS]; // 64 * 4 = 256 bytes, offset 240
};                               // Total: 496 bytes

static_assert(sizeof(PBRUniforms) == 496, "PBRUniforms struct must be 496 bytes");

// Textured PBR uniform buffer structure (with multi-light support)
struct PBRTexturedUniforms {
    float mvp[16];              // mat4x4f: 64 bytes, offset 0
    float model[16];            // mat4x4f: 64 bytes, offset 64
    float normalMatrix[16];     // mat4x4f: 64 bytes, offset 128
    float cameraPos[3];         // vec3f: 12 bytes, offset 192
    float ambientIntensity;     // f32: 4 bytes, offset 204
    float baseColorFactor[4];   // vec4f: 16 bytes, offset 208
    float metallicFactor;       // f32: 4 bytes, offset 224
    float roughnessFactor;      // f32: 4 bytes, offset 228
    float normalScale;          // f32: 4 bytes, offset 232
    float aoStrength;           // f32: 4 bytes, offset 236
    float emissiveFactor[3];    // vec3f: 12 bytes, offset 240
    float emissiveStrength;     // f32: 4 bytes, offset 252
    uint32_t textureFlags;      // u32: 4 bytes, offset 256
    uint32_t lightCount;        // u32: 4 bytes, offset 260
    float alphaCutoff;          // f32: 4 bytes, offset 264 (for alpha mask mode)
    uint32_t alphaMode;         // u32: 4 bytes, offset 268 (0=opaque, 1=mask, 2=blend)
    GPULight lights[MAX_LIGHTS]; // 64 * 4 = 256 bytes, offset 272
};                               // Total: 528 bytes

static_assert(sizeof(PBRTexturedUniforms) == 528, "PBRTexturedUniforms struct must be 528 bytes");

// Helper function to convert LightData to GPULight
GPULight lightDataToGPU(const LightData& light) {
    GPULight gpu = {};
    gpu.position[0] = light.position.x;
    gpu.position[1] = light.position.y;
    gpu.position[2] = light.position.z;
    gpu.range = light.range;
    gpu.direction[0] = light.direction.x;
    gpu.direction[1] = light.direction.y;
    gpu.direction[2] = light.direction.z;
    // Convert spot angles from degrees to cosines
    float outerAngleRad = glm::radians(light.spotAngle);
    float innerAngleRad = outerAngleRad * (1.0f - light.spotBlend);
    gpu.spotAngle = std::cos(outerAngleRad);
    gpu.spotBlend = std::cos(innerAngleRad);
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

} // anonymous namespace

Render3D::Render3D() {
    m_camera.lookAt(glm::vec3(3, 2, 3), glm::vec3(0, 0, 0));
}

Render3D::~Render3D() {
    cleanup();
}

Render3D& Render3D::scene(Scene& s) {
    if (m_scene != &s) {
        m_scene = &s;
        m_composer = nullptr;  // Clear composer when scene is set directly
        markDirty();
    }
    return *this;
}

Render3D& Render3D::input(SceneComposer* composer) {
    if (m_composer != composer) {
        m_composer = composer;
        if (composer) {
            setInput(0, composer);  // Register for dependency tracking
        }
        markDirty();
    }
    return *this;
}

Render3D& Render3D::camera(const Camera3D& cam) {
    m_camera = cam;
    m_cameraOp = nullptr;  // Clear operator when camera is set directly
    markDirty();
    return *this;
}

Render3D& Render3D::cameraInput(CameraOperator* camOp) {
    if (m_cameraOp != camOp) {
        m_cameraOp = camOp;
        if (camOp) {
            setInput(1, camOp);  // Slot 1 for camera (slot 0 is scene)
        }
        markDirty();
    }
    return *this;
}

Render3D& Render3D::lightInput(LightOperator* lightOp) {
    if (lightOp) {
        bool changed = false;
        if (m_lightOps.empty()) {
            m_lightOps.push_back(lightOp);
            changed = true;
        } else if (m_lightOps[0] != lightOp) {
            m_lightOps[0] = lightOp;
            changed = true;
        }
        if (changed) {
            setInput(2, lightOp);  // Slot 2 for primary light
            markDirty();
        }
    }
    return *this;
}

Render3D& Render3D::addLight(LightOperator* lightOp) {
    if (lightOp && m_lightOps.size() < 4) {  // Max 4 lights
        m_lightOps.push_back(lightOp);
        setInput(1 + static_cast<int>(m_lightOps.size()), lightOp);  // Slots 2, 3, 4, 5
        markDirty();
    }
    return *this;
}

Render3D& Render3D::shadingMode(ShadingMode mode) {
    if (m_shadingMode != mode) {
        m_shadingMode = mode;
        markDirty();
    }
    return *this;
}

Render3D& Render3D::toonLevels(int levels) {
    int clamped = glm::clamp(levels, 2, 8);
    if (m_toonLevels != clamped) {
        m_toonLevels = clamped;
        markDirty();
    }
    return *this;
}

Render3D& Render3D::color(float r, float g, float b, float a) {
    glm::vec4 newColor(r, g, b, a);
    if (m_defaultColor != newColor) {
        m_defaultColor = newColor;
        markDirty();
    }
    return *this;
}

Render3D& Render3D::color(const glm::vec4& c) {
    if (m_defaultColor != c) {
        m_defaultColor = c;
        markDirty();
    }
    return *this;
}

Render3D& Render3D::lightDirection(glm::vec3 dir) {
    glm::vec3 normalized = glm::normalize(dir);
    if (m_lightDirection != normalized) {
        m_lightDirection = normalized;
        markDirty();
    }
    return *this;
}

Render3D& Render3D::lightColor(glm::vec3 color) {
    if (m_lightColor != color) {
        m_lightColor = color;
        markDirty();
    }
    return *this;
}

Render3D& Render3D::ambient(float a) {
    if (m_ambient != a) {
        m_ambient = a;
        markDirty();
    }
    return *this;
}

Render3D& Render3D::metallic(float m) {
    float clamped = glm::clamp(m, 0.0f, 1.0f);
    if (m_metallic != clamped) {
        m_metallic = clamped;
        markDirty();
    }
    return *this;
}

Render3D& Render3D::roughness(float r) {
    float clamped = glm::clamp(r, 0.0f, 1.0f);
    if (m_roughness != clamped) {
        m_roughness = clamped;
        markDirty();
    }
    return *this;
}

Render3D& Render3D::material(TexturedMaterial* mat) {
    if (m_material != mat) {
        m_material = mat;
        if (mat) {
            setInput(6, mat);  // Slot 6 for material (after lights)
        }
        markDirty();
    }
    return *this;
}

Render3D& Render3D::ibl(bool enabled) {
    if (m_iblEnabled != enabled) {
        m_iblEnabled = enabled;
        markDirty();
    }
    return *this;
}

Render3D& Render3D::environment(IBLEnvironment* env) {
    if (m_iblEnvironment != env) {
        m_iblEnvironment = env;
        if (env && env->isLoaded()) {
            m_iblEnabled = true;
        }
        markDirty();
    }
    return *this;
}

Render3D& Render3D::environmentInput(IBLEnvironment* envOp) {
    if (m_iblEnvironmentOp != envOp) {
        m_iblEnvironmentOp = envOp;
        if (envOp) {
            setInput(6, envOp);  // Slot 6 for environment (after scene, camera, lights)
        }
        markDirty();
    }
    return *this;
}

Render3D& Render3D::environmentHDR(const std::string& hdrPath) {
    if (m_pendingHDRPath != hdrPath) {
        m_pendingHDRPath = hdrPath;
        m_iblEnabled = true;  // Will be enabled when loaded
        markDirty();
    }
    return *this;
}

Render3D& Render3D::showSkybox(bool enabled) {
    if (m_showSkybox != enabled) {
        m_showSkybox = enabled;
        markDirty();
    }
    return *this;
}

Render3D& Render3D::clearColor(float r, float g, float b, float a) {
    glm::vec4 newColor(r, g, b, a);
    if (m_clearColor != newColor) {
        m_clearColor = newColor;
        markDirty();
    }
    return *this;
}

Render3D& Render3D::wireframe(bool enabled) {
    if (m_wireframe != enabled) {
        m_wireframe = enabled;
        markDirty();
    }
    return *this;
}

Render3D& Render3D::depthOutput(bool enabled) {
    if (m_depthOutputEnabled != enabled) {
        m_depthOutputEnabled = enabled;
        markDirty();
    }
    return *this;
}

void Render3D::init(Context& ctx) {
    if (m_initialized) return;

    // Use inherited createOutput() from TextureOperator
    createOutput(ctx);

    createDepthBuffer(ctx);
    createPipeline(ctx);

    m_initialized = true;
}

void Render3D::createDepthBuffer(Context& ctx) {
    WGPUDevice device = ctx.device();

    // Hardware depth buffer for depth testing
    WGPUTextureDescriptor depthDesc = {};
    depthDesc.label = toStringView("Render3D Depth");
    depthDesc.size.width = m_width;
    depthDesc.size.height = m_height;
    depthDesc.size.depthOrArrayLayers = 1;
    depthDesc.mipLevelCount = 1;
    depthDesc.sampleCount = 1;
    depthDesc.dimension = WGPUTextureDimension_2D;
    depthDesc.format = DEPTH_FORMAT;
    // Add TextureBinding usage when depth output is enabled so we can sample it
    depthDesc.usage = WGPUTextureUsage_RenderAttachment |
        (m_depthOutputEnabled ? WGPUTextureUsage_TextureBinding : 0);

    m_depthTexture = wgpuDeviceCreateTexture(device, &depthDesc);

    WGPUTextureViewDescriptor depthViewDesc = {};
    depthViewDesc.format = DEPTH_FORMAT;
    depthViewDesc.dimension = WGPUTextureViewDimension_2D;
    depthViewDesc.mipLevelCount = 1;
    depthViewDesc.arrayLayerCount = 1;
    m_depthView = wgpuTextureCreateView(m_depthTexture, &depthViewDesc);

    // Linear depth output texture for post-processing (DOF, fog, etc.)
    if (m_depthOutputEnabled) {
        WGPUTextureDescriptor depthOutDesc = {};
        depthOutDesc.label = toStringView("Render3D Depth Output");
        depthOutDesc.size.width = m_width;
        depthOutDesc.size.height = m_height;
        depthOutDesc.size.depthOrArrayLayers = 1;
        depthOutDesc.mipLevelCount = 1;
        depthOutDesc.sampleCount = 1;
        depthOutDesc.dimension = WGPUTextureDimension_2D;
        depthOutDesc.format = WGPUTextureFormat_R16Float;  // Linear depth, sampleable
        depthOutDesc.usage = WGPUTextureUsage_RenderAttachment | WGPUTextureUsage_TextureBinding;

        m_depthOutputTexture = wgpuDeviceCreateTexture(device, &depthOutDesc);

        WGPUTextureViewDescriptor depthOutViewDesc = {};
        depthOutViewDesc.format = WGPUTextureFormat_R16Float;
        depthOutViewDesc.dimension = WGPUTextureViewDimension_2D;
        depthOutViewDesc.mipLevelCount = 1;
        depthOutViewDesc.arrayLayerCount = 1;
        m_depthOutputView = wgpuTextureCreateView(m_depthOutputTexture, &depthOutViewDesc);

        // Create depth copy pipeline
        WGPUShaderSourceWGSL depthCopyWgsl = {};
        depthCopyWgsl.chain.sType = WGPUSType_ShaderSourceWGSL;
        depthCopyWgsl.code = toStringView(DEPTH_COPY_SHADER_SOURCE);

        WGPUShaderModuleDescriptor depthCopyModuleDesc = {};
        depthCopyModuleDesc.nextInChain = &depthCopyWgsl.chain;
        WGPUShaderModule depthCopyModule = wgpuDeviceCreateShaderModule(device, &depthCopyModuleDesc);

        // Sampler for depth texture
        WGPUSamplerDescriptor samplerDesc = {};
        samplerDesc.magFilter = WGPUFilterMode_Nearest;
        samplerDesc.minFilter = WGPUFilterMode_Nearest;
        samplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
        samplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
        samplerDesc.maxAnisotropy = 1;
        m_depthCopySampler = wgpuDeviceCreateSampler(device, &samplerDesc);

        // Uniform buffer for near/far planes
        WGPUBufferDescriptor uniformBufferDesc = {};
        uniformBufferDesc.size = 16;  // near (4) + far (4) + padding (8)
        uniformBufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
        m_depthCopyUniformBuffer = wgpuDeviceCreateBuffer(device, &uniformBufferDesc);

        // Bind group layout
        WGPUBindGroupLayoutEntry layoutEntries[3] = {};
        layoutEntries[0].binding = 0;
        layoutEntries[0].visibility = WGPUShaderStage_Fragment;
        layoutEntries[0].texture.sampleType = WGPUTextureSampleType_Depth;
        layoutEntries[0].texture.viewDimension = WGPUTextureViewDimension_2D;

        layoutEntries[1].binding = 1;
        layoutEntries[1].visibility = WGPUShaderStage_Fragment;
        layoutEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;

        layoutEntries[2].binding = 2;
        layoutEntries[2].visibility = WGPUShaderStage_Fragment;
        layoutEntries[2].buffer.type = WGPUBufferBindingType_Uniform;

        WGPUBindGroupLayoutDescriptor layoutDesc = {};
        layoutDesc.entryCount = 3;
        layoutDesc.entries = layoutEntries;
        m_depthCopyBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

        // Bind group
        WGPUBindGroupEntry bindEntries[3] = {};
        bindEntries[0].binding = 0;
        bindEntries[0].textureView = m_depthView;

        bindEntries[1].binding = 1;
        bindEntries[1].sampler = m_depthCopySampler;

        bindEntries[2].binding = 2;
        bindEntries[2].buffer = m_depthCopyUniformBuffer;
        bindEntries[2].size = 16;

        WGPUBindGroupDescriptor bindDesc = {};
        bindDesc.layout = m_depthCopyBindGroupLayout;
        bindDesc.entryCount = 3;
        bindDesc.entries = bindEntries;
        m_depthCopyBindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);

        // Pipeline layout
        WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
        pipelineLayoutDesc.bindGroupLayoutCount = 1;
        pipelineLayoutDesc.bindGroupLayouts = &m_depthCopyBindGroupLayout;
        WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

        // Color target (R16Float)
        WGPUColorTargetState colorTarget = {};
        colorTarget.format = WGPUTextureFormat_R16Float;
        colorTarget.writeMask = WGPUColorWriteMask_All;

        WGPUFragmentState fragmentState = {};
        fragmentState.module = depthCopyModule;
        fragmentState.entryPoint = toStringView("fs_main");
        fragmentState.targetCount = 1;
        fragmentState.targets = &colorTarget;

        WGPURenderPipelineDescriptor pipelineDesc = {};
        pipelineDesc.layout = pipelineLayout;
        pipelineDesc.vertex.module = depthCopyModule;
        pipelineDesc.vertex.entryPoint = toStringView("vs_main");
        pipelineDesc.fragment = &fragmentState;
        pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
        pipelineDesc.primitive.cullMode = WGPUCullMode_None;
        pipelineDesc.multisample.count = 1;
        pipelineDesc.multisample.mask = ~0u;

        m_depthCopyPipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

        wgpuPipelineLayoutRelease(pipelineLayout);
        wgpuShaderModuleRelease(depthCopyModule);
    }
}

void Render3D::createPipeline(Context& ctx) {
    WGPUDevice device = ctx.device();

    // Create flat/Gouraud shader module
    WGPUShaderSourceWGSL wgslDesc = {};
    wgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    wgslDesc.code = toStringView(FLAT_SHADER_SOURCE);

    WGPUShaderModuleDescriptor shaderDesc = {};
    shaderDesc.nextInChain = &wgslDesc.chain;
    shaderDesc.label = toStringView("Render3D Shader");

    WGPUShaderModule shaderModule = wgpuDeviceCreateShaderModule(device, &shaderDesc);

    // Query device limits for uniform buffer alignment
    WGPULimits limits = {};
    wgpuDeviceGetLimits(device, &limits);
    m_uniformAlignment = limits.minUniformBufferOffsetAlignment;
    if (m_uniformAlignment < sizeof(Uniforms)) {
        m_uniformAlignment = ((sizeof(Uniforms) + 255) / 256) * 256;  // Round up to 256
    }

    // Create uniform buffer large enough for MAX_OBJECTS
    WGPUBufferDescriptor bufferDesc = {};
    bufferDesc.label = toStringView("Render3D Uniforms");
    bufferDesc.size = m_uniformAlignment * MAX_OBJECTS;
    bufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_uniformBuffer = wgpuDeviceCreateBuffer(device, &bufferDesc);

    // Create bind group layout with dynamic offset
    WGPUBindGroupLayoutEntry layoutEntry = {};
    layoutEntry.binding = 0;
    layoutEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    layoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    layoutEntry.buffer.hasDynamicOffset = true;
    layoutEntry.buffer.minBindingSize = sizeof(Uniforms);

    WGPUBindGroupLayoutDescriptor layoutDesc = {};
    layoutDesc.label = toStringView("Render3D Bind Group Layout");
    layoutDesc.entryCount = 1;
    layoutDesc.entries = &layoutEntry;
    m_bindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &layoutDesc);

    // Create pipeline layout
    WGPUPipelineLayoutDescriptor pipelineLayoutDesc = {};
    pipelineLayoutDesc.bindGroupLayoutCount = 1;
    pipelineLayoutDesc.bindGroupLayouts = &m_bindGroupLayout;
    WGPUPipelineLayout pipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pipelineLayoutDesc);

    // Vertex attributes for Vertex3D (with tangent for PBR)
    WGPUVertexAttribute vertexAttrs[5] = {};

    // position: vec3f at offset 0
    vertexAttrs[0].format = WGPUVertexFormat_Float32x3;
    vertexAttrs[0].offset = offsetof(Vertex3D, position);
    vertexAttrs[0].shaderLocation = 0;

    // normal: vec3f at offset 12
    vertexAttrs[1].format = WGPUVertexFormat_Float32x3;
    vertexAttrs[1].offset = offsetof(Vertex3D, normal);
    vertexAttrs[1].shaderLocation = 1;

    // tangent: vec4f at offset 24 (xyz = tangent, w = handedness)
    vertexAttrs[2].format = WGPUVertexFormat_Float32x4;
    vertexAttrs[2].offset = offsetof(Vertex3D, tangent);
    vertexAttrs[2].shaderLocation = 2;

    // uv: vec2f at offset 40
    vertexAttrs[3].format = WGPUVertexFormat_Float32x2;
    vertexAttrs[3].offset = offsetof(Vertex3D, uv);
    vertexAttrs[3].shaderLocation = 3;

    // color: vec4f at offset 48
    vertexAttrs[4].format = WGPUVertexFormat_Float32x4;
    vertexAttrs[4].offset = offsetof(Vertex3D, color);
    vertexAttrs[4].shaderLocation = 4;

    WGPUVertexBufferLayout vertexLayout = {};
    vertexLayout.arrayStride = sizeof(Vertex3D);
    vertexLayout.stepMode = WGPUVertexStepMode_Vertex;
    vertexLayout.attributeCount = 5;
    vertexLayout.attributes = vertexAttrs;

    // Color target
    WGPUColorTargetState colorTarget = {};
    colorTarget.format = EFFECTS_FORMAT;
    colorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState fragmentState = {};
    fragmentState.module = shaderModule;
    fragmentState.entryPoint = toStringView("fs_main");
    fragmentState.targetCount = 1;
    fragmentState.targets = &colorTarget;

    // Depth stencil state
    WGPUDepthStencilState depthStencil = {};
    depthStencil.format = DEPTH_FORMAT;
    depthStencil.depthWriteEnabled = WGPUOptionalBool_True;
    depthStencil.depthCompare = WGPUCompareFunction_Less;

    // Main pipeline (filled triangles)
    WGPURenderPipelineDescriptor pipelineDesc = {};
    pipelineDesc.label = toStringView("Render3D Pipeline");
    pipelineDesc.layout = pipelineLayout;
    pipelineDesc.vertex.module = shaderModule;
    pipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pipelineDesc.vertex.bufferCount = 1;
    pipelineDesc.vertex.buffers = &vertexLayout;
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pipelineDesc.primitive.cullMode = WGPUCullMode_Back;
    pipelineDesc.depthStencil = &depthStencil;
    pipelineDesc.multisample.count = 1;
    pipelineDesc.multisample.mask = ~0u;
    pipelineDesc.fragment = &fragmentState;

    m_pipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    // Wireframe pipeline
    pipelineDesc.label = toStringView("Render3D Wireframe Pipeline");
    pipelineDesc.primitive.topology = WGPUPrimitiveTopology_LineList;
    pipelineDesc.primitive.cullMode = WGPUCullMode_None;
    m_wireframePipeline = wgpuDeviceCreateRenderPipeline(device, &pipelineDesc);

    // Cleanup flat pipeline resources
    wgpuPipelineLayoutRelease(pipelineLayout);
    wgpuShaderModuleRelease(shaderModule);

    // =========================================================================
    // Create PBR pipeline
    // =========================================================================

    // Create PBR shader module
    WGPUShaderSourceWGSL pbrWgslDesc = {};
    pbrWgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    pbrWgslDesc.code = toStringView(PBR_SHADER_SOURCE);

    WGPUShaderModuleDescriptor pbrShaderDesc = {};
    pbrShaderDesc.nextInChain = &pbrWgslDesc.chain;
    pbrShaderDesc.label = toStringView("Render3D PBR Shader");

    WGPUShaderModule pbrShaderModule = wgpuDeviceCreateShaderModule(device, &pbrShaderDesc);

    // Calculate PBR uniform alignment
    m_pbrUniformAlignment = limits.minUniformBufferOffsetAlignment;
    if (m_pbrUniformAlignment < sizeof(PBRUniforms)) {
        m_pbrUniformAlignment = ((sizeof(PBRUniforms) + 255) / 256) * 256;
    }

    // Create PBR uniform buffer
    WGPUBufferDescriptor pbrBufferDesc = {};
    pbrBufferDesc.label = toStringView("Render3D PBR Uniforms");
    pbrBufferDesc.size = m_pbrUniformAlignment * MAX_OBJECTS;
    pbrBufferDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_pbrUniformBuffer = wgpuDeviceCreateBuffer(device, &pbrBufferDesc);

    // Create PBR bind group layout
    WGPUBindGroupLayoutEntry pbrLayoutEntry = {};
    pbrLayoutEntry.binding = 0;
    pbrLayoutEntry.visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    pbrLayoutEntry.buffer.type = WGPUBufferBindingType_Uniform;
    pbrLayoutEntry.buffer.hasDynamicOffset = true;
    pbrLayoutEntry.buffer.minBindingSize = sizeof(PBRUniforms);

    WGPUBindGroupLayoutDescriptor pbrLayoutDesc = {};
    pbrLayoutDesc.label = toStringView("Render3D PBR Bind Group Layout");
    pbrLayoutDesc.entryCount = 1;
    pbrLayoutDesc.entries = &pbrLayoutEntry;
    m_pbrBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &pbrLayoutDesc);

    // Create PBR pipeline layout
    WGPUPipelineLayoutDescriptor pbrPipelineLayoutDesc = {};
    pbrPipelineLayoutDesc.bindGroupLayoutCount = 1;
    pbrPipelineLayoutDesc.bindGroupLayouts = &m_pbrBindGroupLayout;
    WGPUPipelineLayout pbrPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pbrPipelineLayoutDesc);

    // PBR fragment state
    WGPUFragmentState pbrFragmentState = {};
    pbrFragmentState.module = pbrShaderModule;
    pbrFragmentState.entryPoint = toStringView("fs_main");
    pbrFragmentState.targetCount = 1;
    pbrFragmentState.targets = &colorTarget;

    // PBR pipeline descriptor
    WGPURenderPipelineDescriptor pbrPipelineDesc = {};
    pbrPipelineDesc.label = toStringView("Render3D PBR Pipeline");
    pbrPipelineDesc.layout = pbrPipelineLayout;
    pbrPipelineDesc.vertex.module = pbrShaderModule;
    pbrPipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pbrPipelineDesc.vertex.bufferCount = 1;
    pbrPipelineDesc.vertex.buffers = &vertexLayout;
    pbrPipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pbrPipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pbrPipelineDesc.primitive.cullMode = WGPUCullMode_Back;
    pbrPipelineDesc.depthStencil = &depthStencil;
    pbrPipelineDesc.multisample.count = 1;
    pbrPipelineDesc.multisample.mask = ~0u;
    pbrPipelineDesc.fragment = &pbrFragmentState;

    m_pbrPipeline = wgpuDeviceCreateRenderPipeline(device, &pbrPipelineDesc);

    // Cleanup PBR resources
    wgpuPipelineLayoutRelease(pbrPipelineLayout);
    wgpuShaderModuleRelease(pbrShaderModule);

    // =========================================================================
    // Create Textured PBR pipeline
    // =========================================================================

    // Create textured PBR shader module
    WGPUShaderSourceWGSL pbrTexWgslDesc = {};
    pbrTexWgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    pbrTexWgslDesc.code = toStringView(PBR_TEXTURED_SHADER_SOURCE);

    WGPUShaderModuleDescriptor pbrTexShaderDesc = {};
    pbrTexShaderDesc.nextInChain = &pbrTexWgslDesc.chain;
    pbrTexShaderDesc.label = toStringView("Render3D PBR Textured Shader");

    WGPUShaderModule pbrTexShaderModule = wgpuDeviceCreateShaderModule(device, &pbrTexShaderDesc);

    // Create textured PBR bind group layout with uniform + sampler + 6 textures
    WGPUBindGroupLayoutEntry pbrTexLayoutEntries[8] = {};

    // Binding 0: Uniform buffer
    pbrTexLayoutEntries[0].binding = 0;
    pbrTexLayoutEntries[0].visibility = WGPUShaderStage_Vertex | WGPUShaderStage_Fragment;
    pbrTexLayoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    pbrTexLayoutEntries[0].buffer.hasDynamicOffset = true;
    pbrTexLayoutEntries[0].buffer.minBindingSize = sizeof(PBRTexturedUniforms);

    // Binding 1: Sampler
    pbrTexLayoutEntries[1].binding = 1;
    pbrTexLayoutEntries[1].visibility = WGPUShaderStage_Fragment;
    pbrTexLayoutEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    // Binding 2-7: Texture views (baseColor, normal, metallic, roughness, ao, emissive)
    for (int i = 0; i < 6; i++) {
        pbrTexLayoutEntries[2 + i].binding = 2 + i;
        pbrTexLayoutEntries[2 + i].visibility = WGPUShaderStage_Fragment;
        pbrTexLayoutEntries[2 + i].texture.sampleType = WGPUTextureSampleType_Float;
        pbrTexLayoutEntries[2 + i].texture.viewDimension = WGPUTextureViewDimension_2D;
        pbrTexLayoutEntries[2 + i].texture.multisampled = false;
    }

    WGPUBindGroupLayoutDescriptor pbrTexLayoutDesc = {};
    pbrTexLayoutDesc.label = toStringView("Render3D PBR Textured Bind Group Layout");
    pbrTexLayoutDesc.entryCount = 8;
    pbrTexLayoutDesc.entries = pbrTexLayoutEntries;
    m_pbrTexturedBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &pbrTexLayoutDesc);

    // Create textured PBR pipeline layout
    WGPUPipelineLayoutDescriptor pbrTexPipelineLayoutDesc = {};
    pbrTexPipelineLayoutDesc.bindGroupLayoutCount = 1;
    pbrTexPipelineLayoutDesc.bindGroupLayouts = &m_pbrTexturedBindGroupLayout;
    WGPUPipelineLayout pbrTexPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &pbrTexPipelineLayoutDesc);

    // Textured PBR fragment state
    WGPUFragmentState pbrTexFragmentState = {};
    pbrTexFragmentState.module = pbrTexShaderModule;
    pbrTexFragmentState.entryPoint = toStringView("fs_main");
    pbrTexFragmentState.targetCount = 1;
    pbrTexFragmentState.targets = &colorTarget;

    // Textured PBR pipeline descriptor
    WGPURenderPipelineDescriptor pbrTexPipelineDesc = {};
    pbrTexPipelineDesc.label = toStringView("Render3D PBR Textured Pipeline");
    pbrTexPipelineDesc.layout = pbrTexPipelineLayout;
    pbrTexPipelineDesc.vertex.module = pbrTexShaderModule;
    pbrTexPipelineDesc.vertex.entryPoint = toStringView("vs_main");
    pbrTexPipelineDesc.vertex.bufferCount = 1;
    pbrTexPipelineDesc.vertex.buffers = &vertexLayout;
    pbrTexPipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    pbrTexPipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    pbrTexPipelineDesc.primitive.cullMode = WGPUCullMode_Back;
    pbrTexPipelineDesc.depthStencil = &depthStencil;
    pbrTexPipelineDesc.multisample.count = 1;
    pbrTexPipelineDesc.multisample.mask = ~0u;
    pbrTexPipelineDesc.fragment = &pbrTexFragmentState;

    m_pbrTexturedPipeline = wgpuDeviceCreateRenderPipeline(device, &pbrTexPipelineDesc);

    // Create blend state for alpha blending
    WGPUBlendState blendState = {};
    blendState.color.srcFactor = WGPUBlendFactor_SrcAlpha;
    blendState.color.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.color.operation = WGPUBlendOperation_Add;
    blendState.alpha.srcFactor = WGPUBlendFactor_One;
    blendState.alpha.dstFactor = WGPUBlendFactor_OneMinusSrcAlpha;
    blendState.alpha.operation = WGPUBlendOperation_Add;

    // Color target with blending enabled
    WGPUColorTargetState blendColorTarget = {};
    blendColorTarget.format = EFFECTS_FORMAT;
    blendColorTarget.blend = &blendState;
    blendColorTarget.writeMask = WGPUColorWriteMask_All;

    // Fragment state for blend pipeline
    WGPUFragmentState pbrTexBlendFragmentState = {};
    pbrTexBlendFragmentState.module = pbrTexShaderModule;
    pbrTexBlendFragmentState.entryPoint = toStringView("fs_main");
    pbrTexBlendFragmentState.targetCount = 1;
    pbrTexBlendFragmentState.targets = &blendColorTarget;

    // Depth stencil for transparent objects (write disabled for proper ordering)
    WGPUDepthStencilState blendDepthStencil = depthStencil;
    blendDepthStencil.depthWriteEnabled = WGPUOptionalBool_False;  // Don't write depth for transparent objects

    // Create textured PBR blend pipeline (back-face culled)
    pbrTexPipelineDesc.label = toStringView("Render3D PBR Textured Blend Pipeline");
    pbrTexPipelineDesc.fragment = &pbrTexBlendFragmentState;
    pbrTexPipelineDesc.depthStencil = &blendDepthStencil;
    pbrTexPipelineDesc.primitive.cullMode = WGPUCullMode_Back;
    m_pbrTexturedBlendPipeline = wgpuDeviceCreateRenderPipeline(device, &pbrTexPipelineDesc);

    // Create textured PBR double-sided pipeline (no culling, opaque)
    pbrTexPipelineDesc.label = toStringView("Render3D PBR Textured Double-Sided Pipeline");
    pbrTexPipelineDesc.fragment = &pbrTexFragmentState;
    pbrTexPipelineDesc.depthStencil = &depthStencil;  // Restore depth write
    pbrTexPipelineDesc.primitive.cullMode = WGPUCullMode_None;
    m_pbrTexturedDoubleSidedPipeline = wgpuDeviceCreateRenderPipeline(device, &pbrTexPipelineDesc);

    // Create textured PBR blend + double-sided pipeline
    pbrTexPipelineDesc.label = toStringView("Render3D PBR Textured Blend Double-Sided Pipeline");
    pbrTexPipelineDesc.fragment = &pbrTexBlendFragmentState;
    pbrTexPipelineDesc.depthStencil = &blendDepthStencil;
    pbrTexPipelineDesc.primitive.cullMode = WGPUCullMode_None;
    m_pbrTexturedBlendDoubleSidedPipeline = wgpuDeviceCreateRenderPipeline(device, &pbrTexPipelineDesc);

    // Cleanup textured PBR resources
    wgpuPipelineLayoutRelease(pbrTexPipelineLayout);
    wgpuShaderModuleRelease(pbrTexShaderModule);

    // =========================================================================
    // PBR with IBL Pipeline
    // =========================================================================

    // Create IBL shader module
    WGPUShaderSourceWGSL iblWgslDesc = {};
    iblWgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    iblWgslDesc.code = toStringView(PBR_IBL_SHADER_SOURCE);

    WGPUShaderModuleDescriptor iblModuleDesc = {};
    iblModuleDesc.nextInChain = reinterpret_cast<WGPUChainedStruct*>(&iblWgslDesc);
    WGPUShaderModule iblShaderModule = wgpuDeviceCreateShaderModule(device, &iblModuleDesc);

    // Create IBL bind group layout (group 1)
    // @binding(0) = sampler
    // @binding(1) = brdfLUT (2D)
    // @binding(2) = irradianceMap (cube)
    // @binding(3) = prefilteredMap (cube)
    WGPUBindGroupLayoutEntry iblLayoutEntries[4] = {};

    // Sampler
    iblLayoutEntries[0].binding = 0;
    iblLayoutEntries[0].visibility = WGPUShaderStage_Fragment;
    iblLayoutEntries[0].sampler.type = WGPUSamplerBindingType_Filtering;

    // BRDF LUT
    iblLayoutEntries[1].binding = 1;
    iblLayoutEntries[1].visibility = WGPUShaderStage_Fragment;
    iblLayoutEntries[1].texture.sampleType = WGPUTextureSampleType_Float;
    iblLayoutEntries[1].texture.viewDimension = WGPUTextureViewDimension_2D;
    iblLayoutEntries[1].texture.multisampled = false;

    // Irradiance cubemap
    iblLayoutEntries[2].binding = 2;
    iblLayoutEntries[2].visibility = WGPUShaderStage_Fragment;
    iblLayoutEntries[2].texture.sampleType = WGPUTextureSampleType_Float;
    iblLayoutEntries[2].texture.viewDimension = WGPUTextureViewDimension_Cube;
    iblLayoutEntries[2].texture.multisampled = false;

    // Prefiltered cubemap
    iblLayoutEntries[3].binding = 3;
    iblLayoutEntries[3].visibility = WGPUShaderStage_Fragment;
    iblLayoutEntries[3].texture.sampleType = WGPUTextureSampleType_Float;
    iblLayoutEntries[3].texture.viewDimension = WGPUTextureViewDimension_Cube;
    iblLayoutEntries[3].texture.multisampled = false;

    WGPUBindGroupLayoutDescriptor iblLayoutDesc = {};
    iblLayoutDesc.label = toStringView("Render3D IBL Bind Group Layout");
    iblLayoutDesc.entryCount = 4;
    iblLayoutDesc.entries = iblLayoutEntries;
    m_iblBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &iblLayoutDesc);

    // Create IBL sampler
    WGPUSamplerDescriptor iblSamplerDesc = {};
    iblSamplerDesc.addressModeU = WGPUAddressMode_ClampToEdge;
    iblSamplerDesc.addressModeV = WGPUAddressMode_ClampToEdge;
    iblSamplerDesc.addressModeW = WGPUAddressMode_ClampToEdge;
    iblSamplerDesc.magFilter = WGPUFilterMode_Linear;
    iblSamplerDesc.minFilter = WGPUFilterMode_Linear;
    iblSamplerDesc.mipmapFilter = WGPUMipmapFilterMode_Linear;
    iblSamplerDesc.maxAnisotropy = 1;
    m_iblSampler = wgpuDeviceCreateSampler(device, &iblSamplerDesc);

    // Create IBL pipeline layout (2 bind groups: material + IBL)
    WGPUBindGroupLayout iblBindGroupLayouts[2] = { m_pbrTexturedBindGroupLayout, m_iblBindGroupLayout };
    WGPUPipelineLayoutDescriptor iblPipelineLayoutDesc = {};
    iblPipelineLayoutDesc.bindGroupLayoutCount = 2;
    iblPipelineLayoutDesc.bindGroupLayouts = iblBindGroupLayouts;
    WGPUPipelineLayout iblPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &iblPipelineLayoutDesc);

    // IBL fragment state
    WGPUFragmentState iblFragmentState = {};
    iblFragmentState.module = iblShaderModule;
    iblFragmentState.entryPoint = toStringView("fs_main");
    iblFragmentState.targetCount = 1;
    iblFragmentState.targets = &colorTarget;

    // IBL pipeline descriptor
    WGPURenderPipelineDescriptor iblPipelineDesc = {};
    iblPipelineDesc.label = toStringView("Render3D PBR IBL Pipeline");
    iblPipelineDesc.layout = iblPipelineLayout;
    iblPipelineDesc.vertex.module = iblShaderModule;
    iblPipelineDesc.vertex.entryPoint = toStringView("vs_main");
    iblPipelineDesc.vertex.bufferCount = 1;
    iblPipelineDesc.vertex.buffers = &vertexLayout;
    iblPipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    iblPipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    iblPipelineDesc.primitive.cullMode = WGPUCullMode_Back;
    iblPipelineDesc.depthStencil = &depthStencil;
    iblPipelineDesc.multisample.count = 1;
    iblPipelineDesc.multisample.mask = ~0u;
    iblPipelineDesc.fragment = &iblFragmentState;

    m_pbrIBLPipeline = wgpuDeviceCreateRenderPipeline(device, &iblPipelineDesc);

    // IBL fragment state with blending
    WGPUFragmentState iblBlendFragmentState = {};
    iblBlendFragmentState.module = iblShaderModule;
    iblBlendFragmentState.entryPoint = toStringView("fs_main");
    iblBlendFragmentState.targetCount = 1;
    iblBlendFragmentState.targets = &blendColorTarget;

    // Create IBL blend pipeline (back-face culled)
    iblPipelineDesc.label = toStringView("Render3D PBR IBL Blend Pipeline");
    iblPipelineDesc.fragment = &iblBlendFragmentState;
    iblPipelineDesc.depthStencil = &blendDepthStencil;
    iblPipelineDesc.primitive.cullMode = WGPUCullMode_Back;
    m_pbrIBLBlendPipeline = wgpuDeviceCreateRenderPipeline(device, &iblPipelineDesc);

    // Create IBL double-sided pipeline (no culling, opaque)
    iblPipelineDesc.label = toStringView("Render3D PBR IBL Double-Sided Pipeline");
    iblPipelineDesc.fragment = &iblFragmentState;
    iblPipelineDesc.depthStencil = &depthStencil;
    iblPipelineDesc.primitive.cullMode = WGPUCullMode_None;
    m_pbrIBLDoubleSidedPipeline = wgpuDeviceCreateRenderPipeline(device, &iblPipelineDesc);

    // Create IBL blend + double-sided pipeline
    iblPipelineDesc.label = toStringView("Render3D PBR IBL Blend Double-Sided Pipeline");
    iblPipelineDesc.fragment = &iblBlendFragmentState;
    iblPipelineDesc.depthStencil = &blendDepthStencil;
    iblPipelineDesc.primitive.cullMode = WGPUCullMode_None;
    m_pbrIBLBlendDoubleSidedPipeline = wgpuDeviceCreateRenderPipeline(device, &iblPipelineDesc);

    // Cleanup IBL pipeline resources
    wgpuPipelineLayoutRelease(iblPipelineLayout);
    wgpuShaderModuleRelease(iblShaderModule);

    // =========================================================================
    // Skybox Pipeline
    // =========================================================================

    // Create skybox shader module
    WGPUShaderSourceWGSL skyboxWgslDesc = {};
    skyboxWgslDesc.chain.sType = WGPUSType_ShaderSourceWGSL;
    skyboxWgslDesc.code = toStringView(SKYBOX_SHADER_SOURCE);

    WGPUShaderModuleDescriptor skyboxShaderDesc = {};
    skyboxShaderDesc.nextInChain = &skyboxWgslDesc.chain;
    skyboxShaderDesc.label = toStringView("Skybox Shader");

    WGPUShaderModule skyboxShaderModule = wgpuDeviceCreateShaderModule(device, &skyboxShaderDesc);

    // Create skybox uniform buffer
    WGPUBufferDescriptor skyboxBufDesc = {};
    skyboxBufDesc.label = toStringView("Skybox Uniforms");
    skyboxBufDesc.size = sizeof(SkyboxUniforms);
    skyboxBufDesc.usage = WGPUBufferUsage_Uniform | WGPUBufferUsage_CopyDst;
    m_skyboxUniformBuffer = wgpuDeviceCreateBuffer(device, &skyboxBufDesc);

    // Create skybox bind group layout
    WGPUBindGroupLayoutEntry skyboxLayoutEntries[3] = {};

    // Uniform buffer
    skyboxLayoutEntries[0].binding = 0;
    skyboxLayoutEntries[0].visibility = WGPUShaderStage_Vertex;
    skyboxLayoutEntries[0].buffer.type = WGPUBufferBindingType_Uniform;
    skyboxLayoutEntries[0].buffer.minBindingSize = sizeof(SkyboxUniforms);

    // Sampler
    skyboxLayoutEntries[1].binding = 1;
    skyboxLayoutEntries[1].visibility = WGPUShaderStage_Fragment;
    skyboxLayoutEntries[1].sampler.type = WGPUSamplerBindingType_Filtering;

    // Cubemap texture
    skyboxLayoutEntries[2].binding = 2;
    skyboxLayoutEntries[2].visibility = WGPUShaderStage_Fragment;
    skyboxLayoutEntries[2].texture.sampleType = WGPUTextureSampleType_Float;
    skyboxLayoutEntries[2].texture.viewDimension = WGPUTextureViewDimension_Cube;

    WGPUBindGroupLayoutDescriptor skyboxLayoutDesc = {};
    skyboxLayoutDesc.label = toStringView("Skybox Bind Group Layout");
    skyboxLayoutDesc.entryCount = 3;
    skyboxLayoutDesc.entries = skyboxLayoutEntries;
    m_skyboxBindGroupLayout = wgpuDeviceCreateBindGroupLayout(device, &skyboxLayoutDesc);

    // Create skybox pipeline layout
    WGPUPipelineLayoutDescriptor skyboxPipelineLayoutDesc = {};
    skyboxPipelineLayoutDesc.bindGroupLayoutCount = 1;
    skyboxPipelineLayoutDesc.bindGroupLayouts = &m_skyboxBindGroupLayout;
    WGPUPipelineLayout skyboxPipelineLayout = wgpuDeviceCreatePipelineLayout(device, &skyboxPipelineLayoutDesc);

    // Fragment state for skybox
    WGPUColorTargetState skyboxColorTarget = {};
    skyboxColorTarget.format = EFFECTS_FORMAT;
    skyboxColorTarget.writeMask = WGPUColorWriteMask_All;

    WGPUFragmentState skyboxFragmentState = {};
    skyboxFragmentState.module = skyboxShaderModule;
    skyboxFragmentState.entryPoint = toStringView("fs_main");
    skyboxFragmentState.targetCount = 1;
    skyboxFragmentState.targets = &skyboxColorTarget;

    // Depth stencil for skybox (write disabled, test enabled to render behind objects)
    WGPUDepthStencilState skyboxDepthStencil = {};
    skyboxDepthStencil.format = DEPTH_FORMAT;
    skyboxDepthStencil.depthWriteEnabled = WGPUOptionalBool_False;  // Don't write to depth buffer
    skyboxDepthStencil.depthCompare = WGPUCompareFunction_LessEqual;

    // Create skybox pipeline
    WGPURenderPipelineDescriptor skyboxPipelineDesc = {};
    skyboxPipelineDesc.label = toStringView("Skybox Pipeline");
    skyboxPipelineDesc.layout = skyboxPipelineLayout;
    skyboxPipelineDesc.vertex.module = skyboxShaderModule;
    skyboxPipelineDesc.vertex.entryPoint = toStringView("vs_main");
    skyboxPipelineDesc.vertex.bufferCount = 0;  // No vertex buffer - fullscreen triangle
    skyboxPipelineDesc.primitive.topology = WGPUPrimitiveTopology_TriangleList;
    skyboxPipelineDesc.primitive.frontFace = WGPUFrontFace_CCW;
    skyboxPipelineDesc.primitive.cullMode = WGPUCullMode_None;
    skyboxPipelineDesc.depthStencil = &skyboxDepthStencil;
    skyboxPipelineDesc.multisample.count = 1;
    skyboxPipelineDesc.multisample.mask = ~0u;
    skyboxPipelineDesc.fragment = &skyboxFragmentState;

    m_skyboxPipeline = wgpuDeviceCreateRenderPipeline(device, &skyboxPipelineDesc);

    // Cleanup skybox pipeline resources
    wgpuPipelineLayoutRelease(skyboxPipelineLayout);
    wgpuShaderModuleRelease(skyboxShaderModule);
}

void Render3D::process(Context& ctx) {
    if (!m_initialized) {
        init(ctx);
    }

    checkResize(ctx);

    if (!needsCook()) return;

    WGPUDevice device = ctx.device();

    // Handle pending HDR loading
    if (!m_pendingHDRPath.empty() && !m_iblEnvironment) {
        // Create environment and load HDR
        // Note: User should manage IBLEnvironment lifetime, this is a convenience path
        static IBLEnvironment defaultEnv;
        if (!defaultEnv.isInitialized()) {
            defaultEnv.init(ctx);
        }
        if (defaultEnv.loadHDR(ctx, m_pendingHDRPath)) {
            m_iblEnvironment = &defaultEnv;
        }
        m_pendingHDRPath.clear();
    }

    // Use environment operator if set and loaded
    if (m_iblEnvironmentOp && m_iblEnvironmentOp->isLoaded()) {
        m_iblEnvironment = m_iblEnvironmentOp;
    }

    // Check if IBL is ready to use
    bool useIBL = m_iblEnabled && m_iblEnvironment && m_iblEnvironment->isLoaded();

    // Create IBL bind group if needed and not already created
    if (useIBL && !m_iblBindGroup) {
        WGPUBindGroupEntry iblEntries[4] = {};

        iblEntries[0].binding = 0;
        iblEntries[0].sampler = m_iblSampler;

        iblEntries[1].binding = 1;
        iblEntries[1].textureView = m_iblEnvironment->brdfLUTView();

        iblEntries[2].binding = 2;
        iblEntries[2].textureView = m_iblEnvironment->irradianceView();

        iblEntries[3].binding = 3;
        iblEntries[3].textureView = m_iblEnvironment->prefilteredView();

        WGPUBindGroupDescriptor iblBindDesc = {};
        iblBindDesc.layout = m_iblBindGroupLayout;
        iblBindDesc.entryCount = 4;
        iblBindDesc.entries = iblEntries;
        m_iblBindGroup = wgpuDeviceCreateBindGroup(device, &iblBindDesc);
    }

    // Create skybox bind group if needed
    bool renderSkybox = m_showSkybox && m_iblEnvironment && m_iblEnvironment->isLoaded();
    if (renderSkybox && !m_skyboxBindGroup) {
        WGPUBindGroupEntry skyboxEntries[3] = {};

        skyboxEntries[0].binding = 0;
        skyboxEntries[0].buffer = m_skyboxUniformBuffer;
        skyboxEntries[0].size = sizeof(SkyboxUniforms);

        skyboxEntries[1].binding = 1;
        skyboxEntries[1].sampler = m_iblSampler;

        skyboxEntries[2].binding = 2;
        skyboxEntries[2].textureView = m_iblEnvironment->prefilteredView();

        WGPUBindGroupDescriptor skyboxBindDesc = {};
        skyboxBindDesc.layout = m_skyboxBindGroupLayout;
        skyboxBindDesc.entryCount = 3;
        skyboxBindDesc.entries = skyboxEntries;
        m_skyboxBindGroup = wgpuDeviceCreateBindGroup(device, &skyboxBindDesc);
    }

    // If using a composer, get the scene from it
    Scene* sceneToRender = m_scene;
    if (m_composer) {
        sceneToRender = &m_composer->outputScene();
    }

    if (!sceneToRender || sceneToRender->empty()) {
        return;
    }

    // Get camera from operator if connected, otherwise use direct camera
    Camera3D activeCamera = m_camera;
    if (m_cameraOp) {
        activeCamera = m_cameraOp->outputCamera();
    }

    // Collect lights from operators
    GPULight gpuLights[MAX_LIGHTS] = {};
    uint32_t lightCount = 0;

    // First add lights from light operators
    for (size_t i = 0; i < m_lightOps.size() && lightCount < MAX_LIGHTS; i++) {
        if (m_lightOps[i]) {
            gpuLights[lightCount++] = lightDataToGPU(m_lightOps[i]->outputLight());
        }
    }

    // If no lights are connected, add a default directional light
    if (lightCount == 0) {
        LightData defaultLight;
        defaultLight.type = LightType::Directional;
        defaultLight.direction = m_lightDirection;
        defaultLight.color = m_lightColor;
        defaultLight.intensity = 1.0f;
        gpuLights[lightCount++] = lightDataToGPU(defaultLight);
    }

    // Update camera aspect ratio
    activeCamera.aspect(static_cast<float>(m_width) / m_height);
    glm::mat4 viewProj = activeCamera.viewProjectionMatrix();

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

    // Render skybox first (if enabled)
    if (renderSkybox && m_skyboxBindGroup) {
        // Update skybox uniforms with inverse view-projection matrix
        glm::mat4 invViewProj = glm::inverse(viewProj);
        SkyboxUniforms skyboxUniforms;
        memcpy(skyboxUniforms.invViewProj, glm::value_ptr(invViewProj), sizeof(skyboxUniforms.invViewProj));
        wgpuQueueWriteBuffer(ctx.queue(), m_skyboxUniformBuffer, 0, &skyboxUniforms, sizeof(skyboxUniforms));

        wgpuRenderPassEncoderSetPipeline(pass, m_skyboxPipeline);
        wgpuRenderPassEncoderSetBindGroup(pass, 0, m_skyboxBindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(pass, 3, 1, 0, 0);  // Fullscreen triangle
    }

    // Check if using PBR mode
    bool usePBR = (m_shadingMode == ShadingMode::PBR);

    // Get camera position for PBR
    glm::vec3 cameraPos = activeCamera.getPosition();

    // First pass: write all uniform data to buffer at different offsets
    size_t numObjects = sceneToRender->objects().size();
    for (size_t i = 0; i < numObjects && i < MAX_OBJECTS; i++) {
        const auto& obj = sceneToRender->objects()[i];
        if (!obj.mesh || !obj.mesh->valid()) {
            continue;
        }

        // Check for per-object or global material
        TexturedMaterial* activeMaterial = obj.material ? obj.material : m_material;
        bool objUseTexturedPBR = usePBR && activeMaterial != nullptr;

        // Compute MVP matrix for this object
        glm::mat4 mvp = viewProj * obj.transform;
        glm::vec4 objColor = obj.color * m_defaultColor;

        if (objUseTexturedPBR) {
            // Textured PBR uniforms - get material properties from TexturedMaterial
            glm::mat4 normalMatrix = glm::transpose(glm::inverse(obj.transform));
            const glm::vec4& baseColorFactor = activeMaterial->getBaseColorFactor();
            const glm::vec3& emissiveFactor = activeMaterial->getEmissiveFactor();

            PBRTexturedUniforms uniforms = {};
            memcpy(uniforms.mvp, glm::value_ptr(mvp), sizeof(uniforms.mvp));
            memcpy(uniforms.model, glm::value_ptr(obj.transform), sizeof(uniforms.model));
            memcpy(uniforms.normalMatrix, glm::value_ptr(normalMatrix), sizeof(uniforms.normalMatrix));
            uniforms.cameraPos[0] = cameraPos.x;
            uniforms.cameraPos[1] = cameraPos.y;
            uniforms.cameraPos[2] = cameraPos.z;
            uniforms.ambientIntensity = m_ambient;
            uniforms.baseColorFactor[0] = baseColorFactor.r * objColor.r;
            uniforms.baseColorFactor[1] = baseColorFactor.g * objColor.g;
            uniforms.baseColorFactor[2] = baseColorFactor.b * objColor.b;
            uniforms.baseColorFactor[3] = baseColorFactor.a * objColor.a;
            uniforms.metallicFactor = activeMaterial->getMetallicFactor();
            uniforms.roughnessFactor = activeMaterial->getRoughnessFactor();
            uniforms.normalScale = activeMaterial->getNormalScale();
            uniforms.aoStrength = activeMaterial->getAoStrength();
            uniforms.emissiveFactor[0] = emissiveFactor.r;
            uniforms.emissiveFactor[1] = emissiveFactor.g;
            uniforms.emissiveFactor[2] = emissiveFactor.b;
            uniforms.emissiveStrength = activeMaterial->getEmissiveStrength();
            uniforms.textureFlags = 0;
            uniforms.lightCount = lightCount;
            uniforms.alphaCutoff = activeMaterial->getAlphaCutoff();
            uniforms.alphaMode = static_cast<uint32_t>(activeMaterial->getAlphaMode());
            memcpy(uniforms.lights, gpuLights, sizeof(gpuLights));

            size_t offset = i * m_pbrUniformAlignment;
            wgpuQueueWriteBuffer(ctx.queue(), m_pbrUniformBuffer, offset, &uniforms, sizeof(uniforms));
        } else if (usePBR) {
            // Scalar PBR uniforms
            glm::mat4 normalMatrix = glm::transpose(glm::inverse(obj.transform));

            PBRUniforms uniforms = {};
            memcpy(uniforms.mvp, glm::value_ptr(mvp), sizeof(uniforms.mvp));
            memcpy(uniforms.model, glm::value_ptr(obj.transform), sizeof(uniforms.model));
            memcpy(uniforms.normalMatrix, glm::value_ptr(normalMatrix), sizeof(uniforms.normalMatrix));
            uniforms.cameraPos[0] = cameraPos.x;
            uniforms.cameraPos[1] = cameraPos.y;
            uniforms.cameraPos[2] = cameraPos.z;
            uniforms.ambientIntensity = m_ambient;
            uniforms.baseColor[0] = objColor.r;
            uniforms.baseColor[1] = objColor.g;
            uniforms.baseColor[2] = objColor.b;
            uniforms.baseColor[3] = objColor.a;
            uniforms.metallic = m_metallic;
            uniforms.roughness = m_roughness;
            uniforms.lightCount = lightCount;
            memcpy(uniforms.lights, gpuLights, sizeof(gpuLights));

            size_t offset = i * m_pbrUniformAlignment;
            wgpuQueueWriteBuffer(ctx.queue(), m_pbrUniformBuffer, offset, &uniforms, sizeof(uniforms));
        } else {
            // Flat/Gouraud uniforms
            Uniforms uniforms = {};
            memcpy(uniforms.mvp, glm::value_ptr(mvp), sizeof(uniforms.mvp));
            memcpy(uniforms.model, glm::value_ptr(obj.transform), sizeof(uniforms.model));
            uniforms.ambient = m_ambient;
            uniforms.baseColor[0] = objColor.r;
            uniforms.baseColor[1] = objColor.g;
            uniforms.baseColor[2] = objColor.b;
            uniforms.baseColor[3] = objColor.a;
            uniforms.shadingMode = static_cast<uint32_t>(m_shadingMode);
            uniforms.lightCount = lightCount;
            uniforms.toonLevels = static_cast<uint32_t>(m_toonLevels);
            memcpy(uniforms.lights, gpuLights, sizeof(gpuLights));

            size_t offset = i * m_uniformAlignment;
            wgpuQueueWriteBuffer(ctx.queue(), m_uniformBuffer, offset, &uniforms, sizeof(uniforms));
        }
    }

    // Helper to create a textured bind group for a material
    auto createTexturedBindGroup = [&](TexturedMaterial* mat) -> WGPUBindGroup {
        WGPUBindGroupEntry bindEntries[8] = {};

        bindEntries[0].binding = 0;
        bindEntries[0].buffer = m_pbrUniformBuffer;
        bindEntries[0].offset = 0;
        bindEntries[0].size = sizeof(PBRTexturedUniforms);

        bindEntries[1].binding = 1;
        bindEntries[1].sampler = mat->sampler();

        bindEntries[2].binding = 2;
        bindEntries[2].textureView = mat->baseColorView();

        bindEntries[3].binding = 3;
        bindEntries[3].textureView = mat->normalView();

        bindEntries[4].binding = 4;
        bindEntries[4].textureView = mat->metallicView();

        bindEntries[5].binding = 5;
        bindEntries[5].textureView = mat->roughnessView();

        bindEntries[6].binding = 6;
        bindEntries[6].textureView = mat->aoView();

        bindEntries[7].binding = 7;
        bindEntries[7].textureView = mat->emissiveView();

        WGPUBindGroupDescriptor bindDesc = {};
        bindDesc.layout = m_pbrTexturedBindGroupLayout;
        bindDesc.entryCount = 8;
        bindDesc.entries = bindEntries;
        return wgpuDeviceCreateBindGroup(device, &bindDesc);
    };

    // Create scalar bind groups (shared across objects without textures)
    WGPUBindGroup scalarPbrBindGroup = nullptr;
    WGPUBindGroup flatBindGroup = nullptr;

    if (usePBR) {
        WGPUBindGroupEntry bindEntry = {};
        bindEntry.binding = 0;
        bindEntry.buffer = m_pbrUniformBuffer;
        bindEntry.offset = 0;
        bindEntry.size = sizeof(PBRUniforms);

        WGPUBindGroupDescriptor bindDesc = {};
        bindDesc.layout = m_pbrBindGroupLayout;
        bindDesc.entryCount = 1;
        bindDesc.entries = &bindEntry;
        scalarPbrBindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);
    } else {
        WGPUBindGroupEntry bindEntry = {};
        bindEntry.binding = 0;
        bindEntry.buffer = m_uniformBuffer;
        bindEntry.offset = 0;
        bindEntry.size = sizeof(Uniforms);

        WGPUBindGroupDescriptor bindDesc = {};
        bindDesc.layout = m_bindGroupLayout;
        bindDesc.entryCount = 1;
        bindDesc.entries = &bindEntry;
        flatBindGroup = wgpuDeviceCreateBindGroup(device, &bindDesc);
    }

    // Track bind groups to release later
    std::vector<WGPUBindGroup> texturedBindGroups;

    // Second pass: render each object with appropriate pipeline and bind group
    TexturedMaterial* lastMaterial = nullptr;
    WGPUBindGroup currentTexturedBindGroup = nullptr;

    for (size_t i = 0; i < numObjects && i < MAX_OBJECTS; i++) {
        const auto& obj = sceneToRender->objects()[i];
        if (!obj.mesh || !obj.mesh->valid()) {
            continue;
        }

        // Check for per-object or global material
        TexturedMaterial* activeMaterial = obj.material ? obj.material : m_material;
        bool objUseTexturedPBR = usePBR && activeMaterial != nullptr;
        bool objUseIBL = useIBL && objUseTexturedPBR;  // IBL only works with textured PBR

        // Determine material properties for pipeline selection
        bool useBlend = false;
        bool doubleSided = false;
        if (activeMaterial) {
            useBlend = activeMaterial->getAlphaMode() == TexturedMaterial::AlphaMode::Blend;
            doubleSided = activeMaterial->isDoubleSided();
        }

        // Set pipeline for this object
        if (m_wireframe) {
            wgpuRenderPassEncoderSetPipeline(pass, m_wireframePipeline);
        } else if (objUseIBL) {
            // Select IBL pipeline variant
            if (useBlend && doubleSided) {
                wgpuRenderPassEncoderSetPipeline(pass, m_pbrIBLBlendDoubleSidedPipeline);
            } else if (useBlend) {
                wgpuRenderPassEncoderSetPipeline(pass, m_pbrIBLBlendPipeline);
            } else if (doubleSided) {
                wgpuRenderPassEncoderSetPipeline(pass, m_pbrIBLDoubleSidedPipeline);
            } else {
                wgpuRenderPassEncoderSetPipeline(pass, m_pbrIBLPipeline);
            }
        } else if (objUseTexturedPBR) {
            // Select textured PBR pipeline variant
            if (useBlend && doubleSided) {
                wgpuRenderPassEncoderSetPipeline(pass, m_pbrTexturedBlendDoubleSidedPipeline);
            } else if (useBlend) {
                wgpuRenderPassEncoderSetPipeline(pass, m_pbrTexturedBlendPipeline);
            } else if (doubleSided) {
                wgpuRenderPassEncoderSetPipeline(pass, m_pbrTexturedDoubleSidedPipeline);
            } else {
                wgpuRenderPassEncoderSetPipeline(pass, m_pbrTexturedPipeline);
            }
        } else if (usePBR) {
            wgpuRenderPassEncoderSetPipeline(pass, m_pbrPipeline);
        } else {
            wgpuRenderPassEncoderSetPipeline(pass, m_pipeline);
        }

        // Set bind group
        uint32_t dynamicOffset = static_cast<uint32_t>(i * m_pbrUniformAlignment);

        if (objUseTexturedPBR) {
            // Create bind group for this material if different from last
            if (activeMaterial != lastMaterial) {
                currentTexturedBindGroup = createTexturedBindGroup(activeMaterial);
                texturedBindGroups.push_back(currentTexturedBindGroup);
                lastMaterial = activeMaterial;
            }
            wgpuRenderPassEncoderSetBindGroup(pass, 0, currentTexturedBindGroup, 1, &dynamicOffset);

            // Bind IBL textures if using IBL
            if (objUseIBL && m_iblBindGroup) {
                wgpuRenderPassEncoderSetBindGroup(pass, 1, m_iblBindGroup, 0, nullptr);
            }
        } else if (usePBR) {
            wgpuRenderPassEncoderSetBindGroup(pass, 0, scalarPbrBindGroup, 1, &dynamicOffset);
        } else {
            uint32_t flatOffset = static_cast<uint32_t>(i * m_uniformAlignment);
            wgpuRenderPassEncoderSetBindGroup(pass, 0, flatBindGroup, 1, &flatOffset);
        }

        // Draw
        wgpuRenderPassEncoderSetVertexBuffer(pass, 0, obj.mesh->vertexBuffer(), 0,
                                              obj.mesh->vertexCount() * sizeof(Vertex3D));
        wgpuRenderPassEncoderSetIndexBuffer(pass, obj.mesh->indexBuffer(),
                                             WGPUIndexFormat_Uint32, 0,
                                             obj.mesh->indexCount() * sizeof(uint32_t));
        wgpuRenderPassEncoderDrawIndexed(pass, obj.mesh->indexCount(), 1, 0, 0, 0);
    }

    // Cleanup bind groups
    for (auto bg : texturedBindGroups) {
        wgpuBindGroupRelease(bg);
    }
    if (scalarPbrBindGroup) wgpuBindGroupRelease(scalarPbrBindGroup);
    if (flatBindGroup) wgpuBindGroupRelease(flatBindGroup);

    // End render pass
    wgpuRenderPassEncoderEnd(pass);
    wgpuRenderPassEncoderRelease(pass);

    // Depth copy pass - copy depth buffer to linear depth output texture
    if (m_depthOutputEnabled && m_depthCopyPipeline && m_depthOutputView) {
        // Update near/far uniforms from camera
        struct DepthCopyUniforms {
            float near;
            float far;
            float _pad[2];
        } depthUniforms;
        depthUniforms.near = m_camera.getNear();
        depthUniforms.far = m_camera.getFar();
        wgpuQueueWriteBuffer(ctx.queue(), m_depthCopyUniformBuffer, 0, &depthUniforms, sizeof(depthUniforms));

        WGPURenderPassColorAttachment depthCopyColorAttachment = {};
        depthCopyColorAttachment.view = m_depthOutputView;
        depthCopyColorAttachment.depthSlice = WGPU_DEPTH_SLICE_UNDEFINED;
        depthCopyColorAttachment.loadOp = WGPULoadOp_Clear;
        depthCopyColorAttachment.storeOp = WGPUStoreOp_Store;
        depthCopyColorAttachment.clearValue = {1.0, 0.0, 0.0, 1.0};  // Clear to far depth

        WGPURenderPassDescriptor depthCopyPassDesc = {};
        depthCopyPassDesc.colorAttachmentCount = 1;
        depthCopyPassDesc.colorAttachments = &depthCopyColorAttachment;

        WGPURenderPassEncoder depthCopyPass = wgpuCommandEncoderBeginRenderPass(encoder, &depthCopyPassDesc);
        wgpuRenderPassEncoderSetPipeline(depthCopyPass, m_depthCopyPipeline);
        wgpuRenderPassEncoderSetBindGroup(depthCopyPass, 0, m_depthCopyBindGroup, 0, nullptr);
        wgpuRenderPassEncoderDraw(depthCopyPass, 3, 1, 0, 0);  // Fullscreen triangle
        wgpuRenderPassEncoderEnd(depthCopyPass);
        wgpuRenderPassEncoderRelease(depthCopyPass);
    }

    // Submit commands
    WGPUCommandBufferDescriptor cmdDesc = {};
    WGPUCommandBuffer cmdBuffer = wgpuCommandEncoderFinish(encoder, &cmdDesc);
    wgpuQueueSubmit(ctx.queue(), 1, &cmdBuffer);
    wgpuCommandBufferRelease(cmdBuffer);
    wgpuCommandEncoderRelease(encoder);

    didCook();
}

void Render3D::cleanup() {
    if (m_pipeline) {
        wgpuRenderPipelineRelease(m_pipeline);
        m_pipeline = nullptr;
    }
    if (m_pbrPipeline) {
        wgpuRenderPipelineRelease(m_pbrPipeline);
        m_pbrPipeline = nullptr;
    }
    if (m_pbrTexturedPipeline) {
        wgpuRenderPipelineRelease(m_pbrTexturedPipeline);
        m_pbrTexturedPipeline = nullptr;
    }
    if (m_pbrTexturedBlendPipeline) {
        wgpuRenderPipelineRelease(m_pbrTexturedBlendPipeline);
        m_pbrTexturedBlendPipeline = nullptr;
    }
    if (m_pbrTexturedDoubleSidedPipeline) {
        wgpuRenderPipelineRelease(m_pbrTexturedDoubleSidedPipeline);
        m_pbrTexturedDoubleSidedPipeline = nullptr;
    }
    if (m_pbrTexturedBlendDoubleSidedPipeline) {
        wgpuRenderPipelineRelease(m_pbrTexturedBlendDoubleSidedPipeline);
        m_pbrTexturedBlendDoubleSidedPipeline = nullptr;
    }
    if (m_wireframePipeline) {
        wgpuRenderPipelineRelease(m_wireframePipeline);
        m_wireframePipeline = nullptr;
    }
    if (m_bindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_bindGroupLayout);
        m_bindGroupLayout = nullptr;
    }
    if (m_pbrBindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_pbrBindGroupLayout);
        m_pbrBindGroupLayout = nullptr;
    }
    if (m_pbrTexturedBindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_pbrTexturedBindGroupLayout);
        m_pbrTexturedBindGroupLayout = nullptr;
    }
    // IBL resources
    if (m_pbrIBLPipeline) {
        wgpuRenderPipelineRelease(m_pbrIBLPipeline);
        m_pbrIBLPipeline = nullptr;
    }
    if (m_pbrIBLBlendPipeline) {
        wgpuRenderPipelineRelease(m_pbrIBLBlendPipeline);
        m_pbrIBLBlendPipeline = nullptr;
    }
    if (m_pbrIBLDoubleSidedPipeline) {
        wgpuRenderPipelineRelease(m_pbrIBLDoubleSidedPipeline);
        m_pbrIBLDoubleSidedPipeline = nullptr;
    }
    if (m_pbrIBLBlendDoubleSidedPipeline) {
        wgpuRenderPipelineRelease(m_pbrIBLBlendDoubleSidedPipeline);
        m_pbrIBLBlendDoubleSidedPipeline = nullptr;
    }
    if (m_iblBindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_iblBindGroupLayout);
        m_iblBindGroupLayout = nullptr;
    }
    if (m_iblBindGroup) {
        wgpuBindGroupRelease(m_iblBindGroup);
        m_iblBindGroup = nullptr;
    }
    if (m_iblSampler) {
        wgpuSamplerRelease(m_iblSampler);
        m_iblSampler = nullptr;
    }
    // Skybox resources
    if (m_skyboxPipeline) {
        wgpuRenderPipelineRelease(m_skyboxPipeline);
        m_skyboxPipeline = nullptr;
    }
    if (m_skyboxBindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_skyboxBindGroupLayout);
        m_skyboxBindGroupLayout = nullptr;
    }
    if (m_skyboxBindGroup) {
        wgpuBindGroupRelease(m_skyboxBindGroup);
        m_skyboxBindGroup = nullptr;
    }
    if (m_skyboxUniformBuffer) {
        wgpuBufferRelease(m_skyboxUniformBuffer);
        m_skyboxUniformBuffer = nullptr;
    }
    if (m_uniformBuffer) {
        wgpuBufferRelease(m_uniformBuffer);
        m_uniformBuffer = nullptr;
    }
    if (m_pbrUniformBuffer) {
        wgpuBufferRelease(m_pbrUniformBuffer);
        m_pbrUniformBuffer = nullptr;
    }
    if (m_depthCopyPipeline) {
        wgpuRenderPipelineRelease(m_depthCopyPipeline);
        m_depthCopyPipeline = nullptr;
    }
    if (m_depthCopyBindGroup) {
        wgpuBindGroupRelease(m_depthCopyBindGroup);
        m_depthCopyBindGroup = nullptr;
    }
    if (m_depthCopyBindGroupLayout) {
        wgpuBindGroupLayoutRelease(m_depthCopyBindGroupLayout);
        m_depthCopyBindGroupLayout = nullptr;
    }
    if (m_depthCopySampler) {
        wgpuSamplerRelease(m_depthCopySampler);
        m_depthCopySampler = nullptr;
    }
    if (m_depthCopyUniformBuffer) {
        wgpuBufferRelease(m_depthCopyUniformBuffer);
        m_depthCopyUniformBuffer = nullptr;
    }
    if (m_depthOutputView) {
        wgpuTextureViewRelease(m_depthOutputView);
        m_depthOutputView = nullptr;
    }
    if (m_depthOutputTexture) {
        wgpuTextureDestroy(m_depthOutputTexture);
        wgpuTextureRelease(m_depthOutputTexture);
        m_depthOutputTexture = nullptr;
    }
    if (m_depthView) {
        wgpuTextureViewRelease(m_depthView);
        m_depthView = nullptr;
    }
    if (m_depthTexture) {
        wgpuTextureDestroy(m_depthTexture);
        wgpuTextureRelease(m_depthTexture);
        m_depthTexture = nullptr;
    }

    // Use inherited releaseOutput() from TextureOperator
    releaseOutput();

    m_initialized = false;
}

} // namespace vivid::render3d
