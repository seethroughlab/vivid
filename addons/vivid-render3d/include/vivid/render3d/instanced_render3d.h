#pragma once

/**
 * @file instanced_render3d.h
 * @brief GPU-instanced 3D mesh rendering
 *
 * Renders thousands of identical meshes in a single draw call using GPU instancing.
 * Each instance can have its own transform, color, and material properties.
 *
 * Use cases:
 * - Forests (trees, grass, rocks)
 * - Crowds and swarms
 * - Debris and particles
 * - Procedural cities
 * - Asteroids and space debris
 */

#include <vivid/effects/texture_operator.h>
#include <vivid/render3d/mesh.h>
#include <vivid/render3d/mesh_operator.h>
#include <vivid/render3d/camera.h>
#include <vivid/render3d/camera_operator.h>
#include <vivid/render3d/light_operators.h>
#include <vivid/render3d/textured_material.h>
#include <vivid/param.h>
#include <glm/glm.hpp>
#include <vector>

namespace vivid::render3d {

/**
 * @brief Per-instance data for instanced rendering
 */
struct Instance3D {
    glm::mat4 transform = glm::mat4(1.0f);  ///< World transform matrix
    glm::vec4 color = glm::vec4(1.0f);      ///< Instance color (multiplied with material)
    float metallic = 0.0f;                   ///< Per-instance metallic override
    float roughness = 0.5f;                  ///< Per-instance roughness override
    float boundingRadius = 0.0f;             ///< Bounding sphere radius (0 = auto from mesh)
    float _pad;                              ///< Padding for alignment
};

/**
 * @brief GPU-instanced 3D mesh renderer
 *
 * Renders thousands of identical meshes efficiently using a single draw call.
 * Supports PBR shading with per-instance color and material variations.
 *
 * @par Example
 * @code
 * auto& sphere = chain.add<Sphere>("sphere").radius(0.5f).segments(16);
 *
 * auto& instanced = chain.add<InstancedRender3D>("asteroids")
 *     .mesh(&sphere)
 *     .cameraInput(&camera)
 *     .lightInput(&sun)
 *     .clearColor(0.02f, 0.02f, 0.05f);
 *
 * // In update():
 * std::vector<Instance3D> instances;
 * for (int i = 0; i < 1000; i++) {
 *     Instance3D inst;
 *     inst.transform = glm::translate(glm::mat4(1.0f), positions[i]);
 *     inst.color = glm::vec4(colors[i], 1.0f);
 *     instances.push_back(inst);
 * }
 * instanced.setInstances(instances);
 * @endcode
 */
class InstancedRender3D : public vivid::effects::TextureOperator {
public:
    Param<float> metallic{"metallic", 0.0f, 0.0f, 1.0f};    ///< Base metallic value
    Param<float> roughness{"roughness", 0.5f, 0.0f, 1.0f};  ///< Base roughness value
    Param<float> ambient{"ambient", 0.3f, 0.0f, 2.0f};      ///< Ambient light intensity

    InstancedRender3D();
    ~InstancedRender3D() override;

    // === Mesh Input ===

    /// Set the mesh to instance (from a MeshOperator)
    void setMesh(MeshOperator* geom);

    /// Set the mesh directly
    void setMesh(Mesh* m);

    // === Instance Data ===

    /// Set all instances (replaces existing)
    void setInstances(const std::vector<Instance3D>& instances);

    /// Add a single instance
    void addInstance(const Instance3D& instance);

    /// Add instance with transform and color
    void addInstance(const glm::mat4& transform, const glm::vec4& color = glm::vec4(1.0f));

    /// Add instance at position with uniform scale
    void addInstance(const glm::vec3& position, float scale = 1.0f,
                     const glm::vec4& color = glm::vec4(1.0f));

    /// Clear all instances
    void clearInstances();

    /// Reserve capacity for instances (optimization)
    void reserve(size_t count);

    /// Get current instance count
    size_t instanceCount() const { return m_instances.size(); }

    // === Camera & Lighting ===

    /// Set camera operator input
    void setCameraInput(CameraOperator* cam);

    /// Set camera directly
    void setCamera(const Camera3D& cam);

    /// Set primary light input
    void setLightInput(LightOperator* light);

    /// Add additional light (up to 4 total)
    void addLight(LightOperator* light);

    // === Material Properties (defaults for all instances) ===

    /// Set textured PBR material (albedo, normal, metallic, roughness, AO maps)
    void setMaterial(TexturedMaterial* mat);

    /// Base color multiplier
    void setBaseColor(float r, float g, float b, float a = 1.0f) {
        glm::vec4 newColor(r, g, b, a);
        if (m_baseColor != newColor) {
            m_baseColor = newColor;
            markDirty();
        }
    }

    // === Rendering Options ===

    /// Clear color for the render target
    void setClearColor(float r, float g, float b, float a = 1.0f) {
        glm::vec4 newColor(r, g, b, a);
        if (m_clearColor != newColor) {
            m_clearColor = newColor;
            markDirty();
        }
    }

    /// Enable/disable depth testing
    void setDepthTest(bool enable) {
        if (m_depthTest != enable) {
            m_depthTest = enable;
            markDirty();
        }
    }

    /// Enable/disable backface culling
    void setCullBack(bool enable) {
        if (m_cullBack != enable) {
            m_cullBack = enable;
            markDirty();
        }
    }

    /// Enable/disable frustum culling (default: enabled)
    /// When enabled, instances outside the camera frustum are skipped
    void setFrustumCulling(bool enable) {
        m_frustumCulling = enable;
    }

    /// Get frustum culling stats from last frame
    /// @return Pair of (visible instances, total instances)
    std::pair<size_t, size_t> getCullingStats() const {
        return {m_visibleCount, m_instances.size()};
    }

    // === Operator Interface ===
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "InstancedRender3D"; }

private:
    void createPipeline(Context& ctx);
    void createTexturedPipeline(Context& ctx);
    void createDepthBuffer(Context& ctx);
    void ensureInstanceCapacity(size_t count);
    void uploadInstances();

    // Mesh source
    MeshOperator* m_meshOp = nullptr;
    Mesh* m_mesh = nullptr;

    // Textured material
    TexturedMaterial* m_material = nullptr;

    // Instance data
    std::vector<Instance3D> m_instances;
    bool m_instancesDirty = true;

    // Camera
    CameraOperator* m_cameraOp = nullptr;
    Camera3D m_camera;

    // Lighting
    std::vector<LightOperator*> m_lightOps;

    // Material defaults
    glm::vec4 m_baseColor{1.0f, 1.0f, 1.0f, 1.0f};

    // Rendering options
    glm::vec4 m_clearColor{0.1f, 0.1f, 0.15f, 1.0f};
    bool m_depthTest = true;
    bool m_cullBack = true;
    bool m_frustumCulling = true;

    // Frustum culling stats
    size_t m_visibleCount = 0;
    float m_meshBoundingRadius = 0.0f;  // Cached from mesh

    // GPU resources - untextured pipeline
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUBuffer m_instanceBuffer = nullptr;
    size_t m_instanceCapacity = 0;

    // GPU resources - textured pipeline
    WGPURenderPipeline m_texturedPipeline = nullptr;
    WGPUBindGroupLayout m_texturedBindGroupLayout = nullptr;
    WGPUBindGroup m_texturedBindGroup = nullptr;
    WGPUSampler m_sampler = nullptr;

    // Depth buffer
    WGPUTexture m_depthTexture = nullptr;
    WGPUTextureView m_depthView = nullptr;
    int m_depthWidth = 0;
    int m_depthHeight = 0;

    bool m_pipelineCreated = false;
    bool m_texturedPipelineCreated = false;
};

} // namespace vivid::render3d
