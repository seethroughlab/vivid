# PLAN-06: 3D Graphics, Lighting & Shading

Full 3D rendering pipeline with Phong and PBR shading models for Vivid.

## Overview

This plan extends Vivid from a 2D texture-processing system to support full 3D rendering with proper lighting and materials.

```
Geometry → Material → Lights → Camera → Shading → Output Texture
```

### Core Components

1. **Geometry System** - Meshes with vertex attributes (position, normal, UV, tangent)
2. **Material System** - Surface properties (Phong and PBR workflows)
3. **Light System** - Light sources (directional, point, spot)
4. **Camera System** - View/projection matrices for 3D perspective
5. **Render Pipeline** - 3D render pass with depth buffer

---

## Geometry System

### Vertex Format

All 3D geometry uses a standard vertex format supporting normal mapping:

```cpp
struct Vertex3D {
    glm::vec3 position;   // World-space position
    glm::vec3 normal;     // Surface normal
    glm::vec2 uv;         // Texture coordinates
    glm::vec4 tangent;    // Tangent vector (w = handedness for bitangent)
};
```

### Mesh Class

```cpp
class Mesh {
public:
    bool loadFromVertices(const std::vector<Vertex3D>& vertices,
                          const std::vector<uint32_t>& indices);
    void draw(WGPURenderPassEncoder encoder);

private:
    WGPUBuffer vertexBuffer_;
    WGPUBuffer indexBuffer_;
    uint32_t indexCount_;
    BoundingBox bounds_;  // For frustum culling
};
```

### Primitive Generation

Built-in primitive generators with proper normals and tangents:

| Primitive | Parameters |
|-----------|------------|
| `Plane` | width, height, subdivisions |
| `Cube` | size |
| `Sphere` | radius, segments, rings (UV sphere or icosphere) |
| `Cylinder` | radius, height, segments |
| `Cone` | radius, height, segments |
| `Torus` | majorRadius, minorRadius, majorSegments, minorSegments |

### Model Loading

- **OBJ** - Simple mesh format, widely supported
- **GLTF** - Modern format with embedded materials, animations, scenes

---

## Material System

### Base Material

```cpp
struct Material {
    enum class Type { Phong, PBR };
    Type type;

    Texture albedoMap;    // Base color texture
    Texture normalMap;    // Normal map (optional)

    virtual void bind(WGPURenderPassEncoder encoder) = 0;
};
```

### Phong Material

Classic ambient/diffuse/specular model:

```cpp
struct PhongMaterial : Material {
    glm::vec3 ambient = {0.1f, 0.1f, 0.1f};   // Ambient reflectivity
    glm::vec3 diffuse = {0.8f, 0.8f, 0.8f};   // Diffuse reflectivity
    glm::vec3 specular = {1.0f, 1.0f, 1.0f};  // Specular reflectivity
    float shininess = 32.0f;                   // Specular exponent
};
```

### PBR Material (Metallic-Roughness Workflow)

Physically-based rendering using the metallic-roughness workflow (glTF standard):

```cpp
struct PBRMaterial : Material {
    // Scalar values (used when textures not present)
    glm::vec3 albedo = {1.0f, 1.0f, 1.0f};  // Base color
    float metallic = 0.0f;                   // 0 = dielectric, 1 = metal
    float roughness = 0.5f;                  // 0 = smooth, 1 = rough
    float ao = 1.0f;                         // Ambient occlusion
    glm::vec3 emissive = {0.0f, 0.0f, 0.0f}; // Self-illumination

    // Texture maps (override scalars when present)
    Texture metallicRoughnessMap;  // G = roughness, B = metallic
    Texture aoMap;                 // Ambient occlusion
    Texture emissiveMap;           // Emission
};
```

---

## Light System

### Light Types

```cpp
struct Light {
    enum class Type { Directional, Point, Spot };
    Type type;
    glm::vec3 color = {1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;
    bool castShadows = false;  // Future
};

struct DirectionalLight : Light {
    glm::vec3 direction = {0.0f, -1.0f, 0.0f};  // Normalized direction
};

struct PointLight : Light {
    glm::vec3 position;
    float radius = 10.0f;  // Attenuation radius (light fades to zero at this distance)
};

struct SpotLight : Light {
    glm::vec3 position;
    glm::vec3 direction;
    float innerAngle = 15.0f;  // Full intensity cone (degrees)
    float outerAngle = 30.0f;  // Falloff cone (degrees)
};
```

### Light Uniform Buffer

Support multiple lights in a single draw call:

```wgsl
const MAX_LIGHTS: u32 = 16;

struct LightData {
    lightType: u32,       // 0 = directional, 1 = point, 2 = spot
    _pad1: u32,
    _pad2: u32,
    _pad3: u32,
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
    lightCount: u32,
    ambientColor: vec3f,
    ambientIntensity: f32,
}
```

### Attenuation

Point and spot lights use inverse-square falloff with a radius cutoff:

```wgsl
fn getAttenuation(distance: f32, radius: f32) -> f32 {
    let d = distance / radius;
    let d2 = d * d;
    let falloff = saturate(1.0 - d2 * d2);
    return falloff * falloff / (distance * distance + 1.0);
}
```

---

## Camera System

### Camera3D Class

```cpp
class Camera3D {
public:
    // Transform
    glm::vec3 position = {0, 0, 5};
    glm::vec3 target = {0, 0, 0};     // Look-at point
    glm::vec3 up = {0, 1, 0};

    // Projection
    float fov = 60.0f;                // Vertical FOV in degrees
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    // Computed matrices
    glm::mat4 viewMatrix() const;
    glm::mat4 projectionMatrix(float aspectRatio) const;
    glm::mat4 viewProjectionMatrix(float aspectRatio) const;
};
```

### Camera Uniform

```wgsl
struct CameraUniform {
    view: mat4x4f,
    projection: mat4x4f,
    viewProjection: mat4x4f,
    cameraPosition: vec3f,  // For specular calculations
    _pad: f32,
}
```

---

## Shading Implementation

### Phong/Blinn-Phong Shader

`shaders/phong.wgsl`:

```wgsl
fn blinnPhong(
    worldPos: vec3f,
    normal: vec3f,
    viewDir: vec3f,
    material: PhongMaterial,
    light: LightData,
) -> vec3f {
    let lightDir = getLightDirection(light, worldPos);
    let attenuation = getLightAttenuation(light, worldPos);

    // Ambient
    var result = material.ambient;

    // Diffuse (Lambertian)
    let NdotL = max(dot(normal, lightDir), 0.0);
    result += material.diffuse * NdotL * light.color * light.intensity * attenuation;

    // Specular (Blinn-Phong)
    let halfDir = normalize(lightDir + viewDir);
    let NdotH = max(dot(normal, halfDir), 0.0);
    let spec = pow(NdotH, material.shininess);
    result += material.specular * spec * light.color * light.intensity * attenuation;

    return result;
}
```

### PBR Shader (Cook-Torrance BRDF)

`shaders/pbr.wgsl`:

```wgsl
const PI: f32 = 3.14159265359;

// Normal Distribution Function (GGX/Trowbridge-Reitz)
fn distributionGGX(N: vec3f, H: vec3f, roughness: f32) -> f32 {
    let a = roughness * roughness;
    let a2 = a * a;
    let NdotH = max(dot(N, H), 0.0);
    let NdotH2 = NdotH * NdotH;

    let denom = NdotH2 * (a2 - 1.0) + 1.0;
    return a2 / (PI * denom * denom);
}

// Geometry Function (Smith's method with Schlick-GGX)
fn geometrySchlickGGX(NdotV: f32, roughness: f32) -> f32 {
    let r = roughness + 1.0;
    let k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

fn geometrySmith(N: vec3f, V: vec3f, L: vec3f, roughness: f32) -> f32 {
    let NdotV = max(dot(N, V), 0.0);
    let NdotL = max(dot(N, L), 0.0);
    return geometrySchlickGGX(NdotV, roughness) * geometrySchlickGGX(NdotL, roughness);
}

// Fresnel (Schlick approximation)
fn fresnelSchlick(cosTheta: f32, F0: vec3f) -> vec3f {
    return F0 + (1.0 - F0) * pow(saturate(1.0 - cosTheta), 5.0);
}

// Main PBR function
fn pbrShading(
    worldPos: vec3f,
    N: vec3f,           // Normal
    V: vec3f,           // View direction
    albedo: vec3f,
    metallic: f32,
    roughness: f32,
    ao: f32,
    light: LightData,
) -> vec3f {
    // Calculate reflectance at normal incidence
    // Dielectrics use 0.04, metals use albedo
    let F0 = mix(vec3f(0.04), albedo, metallic);

    let L = getLightDirection(light, worldPos);
    let H = normalize(V + L);
    let radiance = getLightRadiance(light, worldPos);

    // Cook-Torrance BRDF
    let NDF = distributionGGX(N, H, roughness);
    let G = geometrySmith(N, V, L, roughness);
    let F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    // Specular contribution
    let numerator = NDF * G * F;
    let denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001;
    let specular = numerator / denominator;

    // Energy conservation: diffuse + specular <= 1
    let kS = F;  // Specular contribution
    let kD = (vec3f(1.0) - kS) * (1.0 - metallic);  // Metals have no diffuse

    let NdotL = max(dot(N, L), 0.0);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}
```

### Normal Mapping

Transform normal map samples from tangent space to world space:

```wgsl
fn getNormalFromMap(
    normalMap: texture_2d<f32>,
    samp: sampler,
    uv: vec2f,
    worldNormal: vec3f,
    worldTangent: vec4f,
) -> vec3f {
    // Sample normal map (stored as RGB, need to convert to [-1, 1])
    let tangentNormal = textureSample(normalMap, samp, uv).rgb * 2.0 - 1.0;

    // Construct TBN matrix
    let N = normalize(worldNormal);
    let T = normalize(worldTangent.xyz);
    let B = cross(N, T) * worldTangent.w;  // w contains handedness
    let TBN = mat3x3f(T, B, N);

    return normalize(TBN * tangentNormal);
}
```

---

## Image-Based Lighting (IBL)

IBL provides realistic ambient lighting by sampling environment maps. Without IBL, PBR materials look flat because metallic surfaces have nothing to reflect.

### Environment Structure

```cpp
struct Environment {
    Texture irradianceMap;   // Diffuse IBL (low-res blurred cubemap)
    Texture radianceMap;     // Specular IBL (mip-mapped cubemap)
    Texture brdfLUT;         // Pre-computed BRDF lookup table (256x256)
    float intensity = 1.0f;
};
```

### Cubemap Processing Pipeline

1. **Load HDR panorama** — Equirectangular format (.hdr files)
2. **Convert to cubemap** — Render to 6 faces using projection shader
3. **Compute irradiance map** — Hemispherical integral for diffuse lighting (64x64 per face)
4. **Compute radiance map** — Pre-filtered environment map with roughness mip levels (512x512, 5 mip levels)
5. **Generate BRDF LUT** — Pre-computed Fresnel-geometry term lookup (256x256, once globally)

### IBL Shader Functions

```wgsl
// Sample irradiance for diffuse IBL
fn sampleIrradiance(N: vec3f) -> vec3f {
    return textureSample(irradianceMap, envSampler, N).rgb;
}

// Sample pre-filtered radiance for specular IBL
fn sampleRadiance(R: vec3f, roughness: f32) -> vec3f {
    let mipLevel = roughness * f32(textureNumLevels(radianceMap) - 1);
    return textureSampleLevel(radianceMap, envSampler, R, mipLevel).rgb;
}

// Combine IBL with material
fn applyIBL(N: vec3f, V: vec3f, albedo: vec3f, metallic: f32, roughness: f32) -> vec3f {
    let R = reflect(-V, N);
    let NdotV = max(dot(N, V), 0.0);
    let F0 = mix(vec3f(0.04), albedo, metallic);

    // Diffuse IBL
    let irradiance = sampleIrradiance(N);
    let diffuse = irradiance * albedo * (1.0 - metallic);

    // Specular IBL
    let prefilteredColor = sampleRadiance(R, roughness);
    let brdf = textureSample(brdfLUT, lutSampler, vec2f(NdotV, roughness)).rg;
    let specular = prefilteredColor * (F0 * brdf.x + brdf.y);

    return diffuse + specular;
}
```

### API Integration

```cpp
// Load environment from HDR file
Environment loadEnvironment(const std::string& hdrPath);

// Add to SceneLighting for optional IBL
struct SceneLighting {
    // ... existing fields ...
    Environment* environment = nullptr;  // Optional IBL
};
```

---

## Operators

### Geometry Operators

| Operator | Description |
|----------|-------------|
| `Cube` | Generate cube mesh |
| `Sphere` | Generate sphere mesh (UV or ico) |
| `Plane` | Generate subdivided plane |
| `Cylinder` | Generate cylinder mesh |
| `Torus` | Generate torus mesh |
| `OBJLoader` | Load OBJ file |
| `GLTFLoader` | Load GLTF file with materials |

### Material Operators

| Operator | Description |
|----------|-------------|
| `PhongMaterial` | Configure Phong material properties |
| `PBRMaterial` | Configure PBR material properties |

### Light Operators

| Operator | Description |
|----------|-------------|
| `DirectionalLight` | Sun/moon parallel light |
| `PointLight` | Omnidirectional light with falloff |
| `SpotLight` | Cone light with falloff |

### Camera Operator

| Operator | Description |
|----------|-------------|
| `Camera3D` | Perspective camera with position/target |

### Render Operator

| Operator | Description |
|----------|-------------|
| `Render3D` | Combine geometry + material + lights + camera → texture |

---

## Implementation Order

1. **Renderer changes** - Add depth buffer, 3D render pass creation
2. **Mesh class** - Vertex buffers, index buffers, draw calls
3. **Primitive generators** - Cube first, then others
4. **Camera3D** - View/projection matrices, uniform buffer
5. **Basic Phong** - Single directional light
6. **Multiple lights** - Light uniform buffer
7. **Point/spot lights** - Attenuation calculations
8. **PBR shader** - Cook-Torrance implementation
9. **Normal mapping** - TBN matrix, tangent generation
10. **Texture maps** - Albedo, metallic/roughness, AO, emissive
11. **Model loading** - OBJ parser, then GLTF

---

## Shadow Mapping

Real-time shadows using shadow maps for directional, point, and spot lights.

### Shadow Map Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    SHADOW MAPPING PIPELINE                       │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  Pass 1: Shadow Map                Pass 2: Main Render          │
│  ┌──────────────────┐              ┌──────────────────┐         │
│  │ Render from      │              │ Sample shadow    │         │
│  │ light's POV      │─────────────▶│ map to determine │         │
│  │ (depth only)     │              │ visibility       │         │
│  └──────────────────┘              └──────────────────┘         │
│                                                                 │
│  Light Types:                                                   │
│  ┌────────────┐  ┌────────────┐  ┌────────────┐                │
│  │Directional │  │   Point    │  │   Spot     │                │
│  │ (2D map)   │  │ (cubemap)  │  │ (2D map)   │                │
│  └────────────┘  └────────────┘  └────────────┘                │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### Shadow Map Texture

```cpp
struct ShadowMap {
    Texture depthTexture;      // Depth-only texture
    int resolution = 2048;     // Shadow map resolution
    glm::mat4 lightViewProj;   // Light's view-projection matrix
};

// For point lights: cubemap with 6 faces
struct PointShadowMap {
    Texture depthCubemap;
    int resolution = 1024;
    glm::mat4 lightViewProj[6]; // One per face
    float farPlane = 25.0f;
};
```

### Directional Light Shadows

Orthographic projection from light direction:

```cpp
glm::mat4 getLightViewProj(const DirectionalLight& light, const BoundingBox& sceneBounds) {
    // View: look along light direction
    glm::vec3 lightPos = sceneBounds.center - light.direction * sceneBounds.radius * 2.0f;
    glm::mat4 view = glm::lookAt(lightPos, sceneBounds.center, glm::vec3(0, 1, 0));

    // Projection: orthographic to cover scene
    float size = sceneBounds.radius * 1.5f;
    glm::mat4 proj = glm::ortho(-size, size, -size, size, 0.1f, sceneBounds.radius * 4.0f);

    return proj * view;
}
```

### Shadow Map Shader (Depth Pass)

`shaders/shadow_map.wgsl`:

```wgsl
struct Uniforms {
    lightViewProj: mat4x4f,
    model: mat4x4f,
}

@group(0) @binding(0) var<uniform> u: Uniforms;

struct VertexInput {
    @location(0) position: vec3f,
}

struct VertexOutput {
    @builtin(position) position: vec4f,
}

@vertex
fn vs_main(in: VertexInput) -> VertexOutput {
    var out: VertexOutput;
    let worldPos = u.model * vec4f(in.position, 1.0);
    out.position = u.lightViewProj * worldPos;
    return out;
}

// No fragment shader needed for depth-only pass
// Or minimal fragment shader:
@fragment
fn fs_main() {
    // Empty - depth is written automatically
}
```

### Shadow Sampling (Main Pass)

```wgsl
@group(1) @binding(0) var shadowMap: texture_depth_2d;
@group(1) @binding(1) var shadowSampler: sampler_comparison;

struct ShadowUniforms {
    lightViewProj: mat4x4f,
    shadowBias: f32,
    pcfRadius: f32,
}

@group(1) @binding(2) var<uniform> shadow: ShadowUniforms;

fn getShadow(worldPos: vec3f) -> f32 {
    // Transform to light space
    let lightSpacePos = shadow.lightViewProj * vec4f(worldPos, 1.0);
    var projCoords = lightSpacePos.xyz / lightSpacePos.w;

    // Convert to [0, 1] UV space
    projCoords = projCoords * 0.5 + 0.5;
    projCoords.y = 1.0 - projCoords.y;  // Flip Y

    // Check bounds
    if (projCoords.x < 0.0 || projCoords.x > 1.0 ||
        projCoords.y < 0.0 || projCoords.y > 1.0 ||
        projCoords.z < 0.0 || projCoords.z > 1.0) {
        return 1.0;  // Outside shadow map = lit
    }

    // Apply bias to reduce shadow acne
    let currentDepth = projCoords.z - shadow.shadowBias;

    // PCF (Percentage Closer Filtering) for soft shadows
    var shadowSum = 0.0;
    let texelSize = 1.0 / f32(textureDimensions(shadowMap).x);

    for (var x = -1; x <= 1; x++) {
        for (var y = -1; y <= 1; y++) {
            let offset = vec2f(f32(x), f32(y)) * texelSize * shadow.pcfRadius;
            shadowSum += textureSampleCompare(
                shadowMap,
                shadowSampler,
                projCoords.xy + offset,
                currentDepth
            );
        }
    }

    return shadowSum / 9.0;
}

// In fragment shader:
fn fs_main(in: VertexOutput) -> @location(0) vec4f {
    // ... existing lighting code ...

    let shadow = getShadow(in.worldPos);
    let finalColor = ambient + (diffuse + specular) * shadow;

    return vec4f(finalColor, 1.0);
}
```

### Point Light Shadows (Omnidirectional)

Use a cubemap for 360° shadow coverage:

```cpp
// Render 6 faces of cubemap
const glm::vec3 directions[6] = {
    { 1,  0,  0}, {-1,  0,  0},  // +X, -X
    { 0,  1,  0}, { 0, -1,  0},  // +Y, -Y
    { 0,  0,  1}, { 0,  0, -1}   // +Z, -Z
};

const glm::vec3 ups[6] = {
    {0, -1,  0}, {0, -1,  0},
    {0,  0,  1}, {0,  0, -1},
    {0, -1,  0}, {0, -1,  0}
};

for (int face = 0; face < 6; face++) {
    glm::mat4 view = glm::lookAt(light.position,
                                  light.position + directions[face],
                                  ups[face]);
    glm::mat4 proj = glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, light.radius);
    renderShadowPass(proj * view, face);
}
```

```wgsl
// Point light shadow sampling
fn getPointShadow(worldPos: vec3f, lightPos: vec3f, farPlane: f32) -> f32 {
    let fragToLight = worldPos - lightPos;
    let currentDepth = length(fragToLight);

    // Sample cubemap with direction vector
    let shadow = textureSampleCompare(
        shadowCubemap,
        shadowSampler,
        fragToLight,
        (currentDepth - bias) / farPlane
    );

    return shadow;
}
```

### Spot Light Shadows

Same as directional but with perspective projection:

```cpp
glm::mat4 getSpotLightViewProj(const SpotLight& light) {
    glm::mat4 view = glm::lookAt(light.position,
                                  light.position + light.direction,
                                  glm::vec3(0, 1, 0));
    glm::mat4 proj = glm::perspective(
        glm::radians(light.outerAngle * 2.0f),
        1.0f,
        0.1f,
        light.radius
    );
    return proj * view;
}
```

### Shadow Quality Settings

| Setting | Resolution | PCF Samples | Use Case |
|---------|------------|-------------|----------|
| Low | 512 | 1 (hard) | Performance |
| Medium | 1024 | 4 | Balanced |
| High | 2048 | 9 | Quality |
| Ultra | 4096 | 16+ | Cinematics |

### Cascaded Shadow Maps (CSM)

For large outdoor scenes, use multiple shadow maps at different distances:

```cpp
struct CascadedShadowMap {
    static const int CASCADE_COUNT = 4;

    Texture cascades[CASCADE_COUNT];       // Depth textures
    glm::mat4 viewProj[CASCADE_COUNT];     // View-proj per cascade
    float splitDistances[CASCADE_COUNT];    // Split planes

    // Typical splits: 10m, 30m, 100m, 500m
};
```

### Shadow Configuration API

```cpp
struct ShadowSettings {
    bool enabled = true;
    int resolution = 2048;
    float bias = 0.001f;
    float normalBias = 0.01f;
    int pcfSamples = 9;
    float pcfRadius = 1.5f;
    float maxDistance = 100.0f;  // Fade shadows at distance
    bool softShadows = true;
};

// Per-light shadow control
struct Light {
    // ... existing fields ...
    bool castShadows = true;
    float shadowStrength = 1.0f;  // 0 = no shadow, 1 = full shadow
};
```

### Render3D Operator with Shadows

```cpp
class Render3D : public Operator {
public:
    Render3D& shadows(bool enable);
    Render3D& shadowResolution(int res);
    Render3D& shadowBias(float bias);
    Render3D& shadowSoftness(float softness);
    Render3D& cascades(int count);  // For directional lights

    // ... existing methods ...
};
```

### Implementation Tasks

- [ ] Create depth-only render pass for shadow maps
- [ ] Implement directional light shadow mapping
- [ ] Add shadow sampling to Phong/PBR shaders
- [ ] Implement PCF soft shadows
- [ ] Add shadow bias controls (depth bias, normal bias)
- [ ] Implement spot light shadows
- [ ] Implement point light cubemap shadows
- [ ] Add cascaded shadow maps for directional lights
- [ ] Shadow distance fade
- [ ] Per-object shadow cast/receive flags
- [ ] Shadow map caching (static objects)

---

## Future Extensions

- **Screen-space ambient occlusion (SSAO)**
- **Screen-space reflections (SSR)**
- **Bloom and tone mapping** - HDR rendering pipeline
- **Skeletal animation** - Bone transforms, skinning
