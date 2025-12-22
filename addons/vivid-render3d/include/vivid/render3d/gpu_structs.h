#pragma once

/**
 * @file gpu_structs.h
 * @brief GPU uniform buffer structures for 3D rendering
 *
 * Contains struct definitions that match WGSL shader uniform layouts.
 * All structs have static_assert size checks to ensure alignment.
 */

#include <vivid/render3d/light_operators.h>
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cstdint>
#include <cmath>

namespace vivid::render3d {

// Maximum lights supported per draw call
constexpr uint32_t MAX_LIGHTS = 4;

// Light types (must match WGSL constants)
constexpr uint32_t LIGHT_TYPE_DIRECTIONAL = 0;
constexpr uint32_t LIGHT_TYPE_POINT = 1;
constexpr uint32_t LIGHT_TYPE_SPOT = 2;

// GPU Light structure (64 bytes, 16-byte aligned)
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
    uint32_t receiveShadow;     // u32: 4 bytes, offset 176 (1=receive shadows, 0=ignore)
    float _pad1a[3];            // 12 bytes padding, offset 180 (align _pad1 to 192)
    alignas(16) float _pad1[3]; // vec3f: 12 bytes, offset 192 (WGSL vec3f needs 16-byte align)
    float _pad1b;               // 4 bytes, offset 204 (align lights to 208)
    GPULight lights[MAX_LIGHTS]; // 64 * 4 = 256 bytes, offset 208
};                               // Total: 464 bytes

static_assert(sizeof(Uniforms) == 464, "Uniforms struct must be 464 bytes");

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

// Skybox uniform buffer structure
struct SkyboxUniforms {
    float invViewProj[16];  // mat4x4f: 64 bytes
};

// Helper function to convert LightData to GPULight
inline GPULight lightDataToGPU(const LightData& light) {
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

} // namespace vivid::render3d
