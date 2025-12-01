#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <cstdint>
#include <limits>

namespace vivid {

/**
 * @brief Standard 3D vertex format supporting normal mapping.
 */
struct Vertex3D {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};
};

/**
 * @brief Axis-aligned bounding box.
 */
struct BoundingBox {
    glm::vec3 min{std::numeric_limits<float>::max()};
    glm::vec3 max{std::numeric_limits<float>::lowest()};

    void expand(const glm::vec3& point) {
        min = glm::min(min, point);
        max = glm::max(max, point);
    }

    glm::vec3 center() const { return (min + max) * 0.5f; }
    glm::vec3 size() const { return max - min; }
};

/**
 * @brief 3D perspective camera.
 */
class Camera3D {
public:
    glm::vec3 position{0.0f, 0.0f, 5.0f};
    glm::vec3 target{0.0f, 0.0f, 0.0f};
    glm::vec3 up{0.0f, 1.0f, 0.0f};

    float fov = 60.0f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    glm::mat4 viewMatrix() const {
        return glm::lookAt(position, target, up);
    }

    glm::mat4 projectionMatrix(float aspectRatio) const {
        return glm::perspective(glm::radians(fov), aspectRatio, nearPlane, farPlane);
    }

    glm::mat4 viewProjectionMatrix(float aspectRatio) const {
        return projectionMatrix(aspectRatio) * viewMatrix();
    }

    glm::vec3 forward() const {
        return glm::normalize(target - position);
    }

    void orbit(float yawDelta, float pitchDelta) {
        glm::vec3 offset = position - target;
        float distance = glm::length(offset);
        float theta = std::atan2(offset.x, offset.z);
        float phi = std::acos(offset.y / distance);

        theta += yawDelta;
        phi = glm::clamp(phi + pitchDelta, 0.01f, 3.13f);

        position = target + glm::vec3(
            distance * std::sin(phi) * std::sin(theta),
            distance * std::cos(phi),
            distance * std::sin(phi) * std::cos(theta)
        );
    }

    void zoom(float delta) {
        glm::vec3 offset = position - target;
        float distance = glm::length(offset);
        float newDistance = glm::max(0.1f, distance - delta);
        position = target + glm::normalize(offset) * newDistance;
    }
};

/**
 * @brief Opaque handle to a 3D mesh.
 */
struct Mesh3D {
    void* handle = nullptr;
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;
    BoundingBox bounds;

    bool valid() const { return handle != nullptr; }
};

/**
 * @brief Per-instance data for GPU instanced rendering.
 *
 * Contains transform and color for each instance. Used with drawMeshInstanced().
 */
struct Instance3D {
    glm::mat4 model{1.0f};     // Model transform matrix (64 bytes)
    glm::vec4 color{1.0f};     // Instance color (16 bytes)

    Instance3D() = default;

    Instance3D(const glm::mat4& m, const glm::vec4& c)
        : model(m), color(c) {}

    Instance3D(const glm::vec3& position, const glm::vec3& scale, const glm::vec4& c)
        : color(c) {
        model = glm::translate(glm::mat4(1.0f), position);
        model = glm::scale(model, scale);
    }

    Instance3D(const glm::vec3& position, float uniformScale, const glm::vec4& c)
        : Instance3D(position, glm::vec3(uniformScale), c) {}
};

/**
 * @brief Opaque handle to a 3D render pipeline.
 */
struct Pipeline3D {
    void* handle = nullptr;
    bool valid() const { return handle != nullptr; }
};

// Primitive generators - populate vertex and index buffers
namespace primitives {

void generateCube(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices);

void generatePlane(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                   float width = 1.0f, float height = 1.0f,
                   int subdivisionsX = 1, int subdivisionsZ = 1);

void generateSphere(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                    float radius = 0.5f, int segments = 32, int rings = 16);

void generateCylinder(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                      float radius = 0.5f, float height = 1.0f, int segments = 32);

void generateTorus(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                   float majorRadius = 0.5f, float minorRadius = 0.2f,
                   int majorSegments = 32, int minorSegments = 16);

void generateEllipticTorus(std::vector<Vertex3D>& vertices, std::vector<uint32_t>& indices,
                           float majorRadiusX = 0.6f, float majorRadiusZ = 0.4f,
                           float minorRadius = 0.15f,
                           int majorSegments = 32, int minorSegments = 16);

} // namespace primitives

// ============================================================================
// Lighting System
// ============================================================================

/**
 * @brief Light type enumeration.
 */
enum class LightType : int {
    Directional = 0,  ///< Parallel rays (sun, moon)
    Point = 1,        ///< Omnidirectional with falloff
    Spot = 2          ///< Cone-shaped with falloff
};

/**
 * @brief Light source for 3D rendering.
 *
 * Supports directional, point, and spot lights with configurable
 * color, intensity, and attenuation parameters.
 */
struct Light {
    LightType type = LightType::Directional;
    glm::vec3 color{1.0f, 1.0f, 1.0f};
    float intensity = 1.0f;

    // Position (for point/spot lights)
    glm::vec3 position{0.0f, 5.0f, 0.0f};

    // Direction (for directional/spot lights, normalized)
    glm::vec3 direction{0.0f, -1.0f, 0.0f};

    // Attenuation (for point/spot lights)
    float radius = 10.0f;  ///< Light influence radius

    // Spot light cone angles (in radians)
    float innerAngle = glm::radians(15.0f);  ///< Full intensity cone
    float outerAngle = glm::radians(30.0f);  ///< Falloff cone

    // Factory methods for common light types
    static Light directional(const glm::vec3& dir, const glm::vec3& col = {1, 1, 1}, float intensity = 1.0f) {
        Light l;
        l.type = LightType::Directional;
        l.direction = glm::normalize(dir);
        l.color = col;
        l.intensity = intensity;
        return l;
    }

    static Light point(const glm::vec3& pos, const glm::vec3& col = {1, 1, 1}, float intensity = 1.0f, float radius = 10.0f) {
        Light l;
        l.type = LightType::Point;
        l.position = pos;
        l.color = col;
        l.intensity = intensity;
        l.radius = radius;
        return l;
    }

    static Light spot(const glm::vec3& pos, const glm::vec3& dir, float innerDeg = 15.0f, float outerDeg = 30.0f,
                      const glm::vec3& col = {1, 1, 1}, float intensity = 1.0f) {
        Light l;
        l.type = LightType::Spot;
        l.position = pos;
        l.direction = glm::normalize(dir);
        l.innerAngle = glm::radians(innerDeg);
        l.outerAngle = glm::radians(outerDeg);
        l.color = col;
        l.intensity = intensity;
        return l;
    }
};

// ============================================================================
// Material System
// ============================================================================

/**
 * @brief Phong material properties.
 *
 * Classic ambient/diffuse/specular shading model. Simple and fast.
 */
struct PhongMaterial {
    glm::vec3 ambient{0.1f, 0.1f, 0.1f};    ///< Ambient reflectivity
    glm::vec3 diffuse{0.8f, 0.8f, 0.8f};    ///< Diffuse reflectivity (base color)
    glm::vec3 specular{1.0f, 1.0f, 1.0f};   ///< Specular reflectivity
    float shininess = 32.0f;                 ///< Specular exponent (higher = tighter highlight)
    glm::vec3 emissive{0.0f, 0.0f, 0.0f};   ///< Self-illumination

    static PhongMaterial matte(const glm::vec3& color) {
        PhongMaterial m;
        m.diffuse = color;
        m.specular = glm::vec3(0.2f);
        m.shininess = 8.0f;
        return m;
    }

    static PhongMaterial shiny(const glm::vec3& color) {
        PhongMaterial m;
        m.diffuse = color;
        m.specular = glm::vec3(1.0f);
        m.shininess = 64.0f;
        return m;
    }

    static PhongMaterial metallic(const glm::vec3& color) {
        PhongMaterial m;
        m.ambient = color * 0.1f;
        m.diffuse = color * 0.3f;
        m.specular = color;
        m.shininess = 128.0f;
        return m;
    }
};

/**
 * @brief PBR material properties (metallic-roughness workflow).
 *
 * Physically-based rendering using the metallic-roughness workflow
 * common in glTF and modern game engines.
 */
struct PBRMaterial {
    glm::vec3 albedo{1.0f, 1.0f, 1.0f};     ///< Base color
    float metallic = 0.0f;                   ///< 0 = dielectric, 1 = metal
    float roughness = 0.5f;                  ///< 0 = smooth/mirror, 1 = rough/diffuse
    float ao = 1.0f;                         ///< Ambient occlusion (1 = no occlusion)
    glm::vec3 emissive{0.0f, 0.0f, 0.0f};   ///< Self-illumination

    static PBRMaterial plastic(const glm::vec3& color) {
        PBRMaterial m;
        m.albedo = color;
        m.metallic = 0.0f;
        m.roughness = 0.4f;
        return m;
    }

    static PBRMaterial metal(const glm::vec3& color, float roughness = 0.3f) {
        PBRMaterial m;
        m.albedo = color;
        m.metallic = 1.0f;
        m.roughness = roughness;
        return m;
    }

    static PBRMaterial gold() {
        return metal(glm::vec3(1.0f, 0.765f, 0.336f), 0.2f);
    }

    static PBRMaterial silver() {
        return metal(glm::vec3(0.972f, 0.960f, 0.915f), 0.1f);
    }

    static PBRMaterial copper() {
        return metal(glm::vec3(0.955f, 0.637f, 0.538f), 0.25f);
    }

    static PBRMaterial rubber(const glm::vec3& color) {
        PBRMaterial m;
        m.albedo = color;
        m.metallic = 0.0f;
        m.roughness = 0.9f;
        return m;
    }
};

// Forward declaration for Texture handle
struct Texture;

/**
 * @brief Textured PBR material with full texture map support.
 *
 * Supports albedo, normal, metallic-roughness, AO, and emissive maps
 * following the glTF 2.0 material model.
 */
struct TexturedPBRMaterial {
    // Base values (used when texture is null or as multipliers)
    glm::vec3 albedo{1.0f, 1.0f, 1.0f};     ///< Base color (multiplied with albedoMap)
    float metallic = 0.0f;                   ///< Base metallic (multiplied with map)
    float roughness = 0.5f;                  ///< Base roughness (multiplied with map)
    float ao = 1.0f;                         ///< Base AO (multiplied with map)
    glm::vec3 emissive{0.0f, 0.0f, 0.0f};   ///< Emissive color (multiplied with map)
    float emissiveStrength = 1.0f;           ///< Emissive intensity multiplier
    float normalStrength = 1.0f;             ///< Normal map intensity (0 = flat)

    // Texture maps (nullptr = use base values)
    Texture* albedoMap = nullptr;            ///< RGB: base color, A: opacity
    Texture* normalMap = nullptr;            ///< RGB: tangent-space normal
    Texture* metallicRoughnessMap = nullptr; ///< G: roughness, B: metallic (glTF style)
    Texture* roughnessMap = nullptr;         ///< R: roughness (separate map, overrides metallicRoughnessMap)
    Texture* metallicMap = nullptr;          ///< R: metallic (separate map, overrides metallicRoughnessMap)
    Texture* aoMap = nullptr;                ///< R: ambient occlusion
    Texture* emissiveMap = nullptr;          ///< RGB: emissive color

    // Convenience constructors
    static TexturedPBRMaterial fromBase(const PBRMaterial& base) {
        TexturedPBRMaterial m;
        m.albedo = base.albedo;
        m.metallic = base.metallic;
        m.roughness = base.roughness;
        m.ao = base.ao;
        m.emissive = base.emissive;
        return m;
    }
};

/**
 * @brief Scene lighting configuration.
 *
 * Contains ambient lighting and up to MAX_LIGHTS light sources.
 */
struct SceneLighting {
    static constexpr int MAX_LIGHTS = 8;

    glm::vec3 ambientColor{0.1f, 0.1f, 0.15f};  ///< Global ambient light color
    float ambientIntensity = 0.3f;               ///< Global ambient intensity

    std::vector<Light> lights;                   ///< Active light sources

    SceneLighting() = default;

    /// Add a light to the scene
    SceneLighting& addLight(const Light& light) {
        if (lights.size() < MAX_LIGHTS) {
            lights.push_back(light);
        }
        return *this;
    }

    /// Clear all lights
    SceneLighting& clearLights() {
        lights.clear();
        return *this;
    }

    /// Set ambient lighting
    SceneLighting& setAmbient(const glm::vec3& color, float intensity = 0.3f) {
        ambientColor = color;
        ambientIntensity = intensity;
        return *this;
    }

    /// Create default outdoor lighting (sun + sky ambient)
    static SceneLighting outdoor() {
        SceneLighting s;
        s.ambientColor = glm::vec3(0.4f, 0.5f, 0.7f);  // Sky blue tint
        s.ambientIntensity = 0.3f;
        s.addLight(Light::directional(glm::vec3(-0.5f, -1.0f, -0.3f), glm::vec3(1.0f, 0.95f, 0.8f), 1.0f));
        return s;
    }

    /// Create default indoor lighting (warm point light + cool ambient)
    static SceneLighting indoor() {
        SceneLighting s;
        s.ambientColor = glm::vec3(0.15f, 0.15f, 0.2f);
        s.ambientIntensity = 0.2f;
        s.addLight(Light::point(glm::vec3(0, 3, 0), glm::vec3(1.0f, 0.9f, 0.7f), 1.5f, 10.0f));
        return s;
    }

    /// Create simple three-point lighting (key, fill, rim)
    static SceneLighting threePoint() {
        SceneLighting s;
        s.ambientColor = glm::vec3(0.1f, 0.1f, 0.1f);
        s.ambientIntensity = 0.1f;
        // Key light (main, warm)
        s.addLight(Light::directional(glm::vec3(-1, -1, -0.5f), glm::vec3(1.0f, 0.95f, 0.9f), 1.0f));
        // Fill light (soft, cool)
        s.addLight(Light::directional(glm::vec3(0.8f, -0.3f, 0.5f), glm::vec3(0.5f, 0.6f, 0.8f), 0.4f));
        // Rim light (back, bright)
        s.addLight(Light::directional(glm::vec3(0.2f, -0.5f, 1.0f), glm::vec3(1.0f, 1.0f, 1.0f), 0.6f));
        return s;
    }
};

// ============================================================================
// Image-Based Lighting (IBL)
// ============================================================================

/**
 * @brief Opaque handle to a cubemap texture.
 *
 * Cubemaps are 6-faced textures used for environment mapping and IBL.
 * Created via Context::loadEnvironment() or Context::createCubemap().
 */
struct Cubemap {
    void* handle = nullptr;     ///< Opaque pointer to internal GPU cubemap.
    int size = 0;               ///< Size of each face in pixels (cubemaps are square).
    int mipLevels = 1;          ///< Number of mip levels (for radiance maps).

    bool valid() const { return handle != nullptr && size > 0; }
};

/**
 * @brief Environment map for Image-Based Lighting (IBL).
 *
 * Contains pre-computed cubemaps for diffuse and specular IBL, plus
 * a BRDF lookup table. Created via Context::loadEnvironment().
 *
 * IBL dramatically improves PBR rendering by providing realistic ambient
 * lighting from an environment (e.g., sky, studio, outdoor scene).
 *
 * Components:
 * - irradianceMap: Low-res cubemap for diffuse lighting (hemispherical integral)
 * - radianceMap: Mip-mapped cubemap for specular reflections (roughness-based LOD)
 * - brdfLUT: 2D lookup table for the split-sum approximation
 */
struct Environment {
    Cubemap irradianceMap;      ///< Diffuse IBL (64x64 per face, blurred)
    Cubemap radianceMap;        ///< Specular IBL (512x512, 5+ mip levels)
    void* brdfLUT = nullptr;    ///< BRDF LUT texture handle (256x256 2D texture)
    float intensity = 1.0f;     ///< Environment intensity multiplier

    bool valid() const {
        return irradianceMap.valid() && radianceMap.valid() && brdfLUT != nullptr;
    }

    /// Create an invalid/empty environment (for optional IBL)
    static Environment none() {
        return Environment{};
    }
};

} // namespace vivid
