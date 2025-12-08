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
    float _pad[2];                           ///< Padding for alignment
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
    InstancedRender3D();
    ~InstancedRender3D() override;

    // === Mesh Input ===

    /// Set the mesh to instance (from a MeshOperator)
    InstancedRender3D& mesh(MeshOperator* geom);

    /// Set the mesh directly
    InstancedRender3D& mesh(Mesh* m);

    // === Instance Data ===

    /// Set all instances (replaces existing)
    InstancedRender3D& setInstances(const std::vector<Instance3D>& instances);

    /// Add a single instance
    InstancedRender3D& addInstance(const Instance3D& instance);

    /// Add instance with transform and color
    InstancedRender3D& addInstance(const glm::mat4& transform, const glm::vec4& color = glm::vec4(1.0f));

    /// Add instance at position with uniform scale
    InstancedRender3D& addInstance(const glm::vec3& position, float scale = 1.0f,
                                    const glm::vec4& color = glm::vec4(1.0f));

    /// Clear all instances
    InstancedRender3D& clearInstances();

    /// Reserve capacity for instances (optimization)
    InstancedRender3D& reserve(size_t count);

    /// Get current instance count
    size_t instanceCount() const { return m_instances.size(); }

    // === Camera & Lighting ===

    /// Set camera operator input
    InstancedRender3D& cameraInput(CameraOperator* cam);

    /// Set camera directly
    InstancedRender3D& camera(const Camera3D& cam);

    /// Set primary light input
    InstancedRender3D& lightInput(LightOperator* light);

    /// Add additional light (up to 4 total)
    InstancedRender3D& addLight(LightOperator* light);

    // === Material Properties (defaults for all instances) ===

    /// Set textured PBR material (albedo, normal, metallic, roughness, AO maps)
    InstancedRender3D& material(TexturedMaterial* mat);

    /// Base metallic value (can be overridden per-instance, ignored if material set)
    InstancedRender3D& metallic(float m) { m_metallic = m; return *this; }

    /// Base roughness value (can be overridden per-instance, ignored if material set)
    InstancedRender3D& roughness(float r) { m_roughness = r; return *this; }

    /// Ambient light intensity
    InstancedRender3D& ambient(float a) { m_ambient = a; return *this; }

    /// Base color multiplier
    InstancedRender3D& baseColor(float r, float g, float b, float a = 1.0f) {
        m_baseColor = glm::vec4(r, g, b, a);
        return *this;
    }

    // === Rendering Options ===

    /// Clear color for the render target
    InstancedRender3D& clearColor(float r, float g, float b, float a = 1.0f) {
        m_clearColor = glm::vec4(r, g, b, a);
        return *this;
    }

    /// Enable/disable depth testing
    InstancedRender3D& depthTest(bool enable) { m_depthTest = enable; return *this; }

    /// Enable/disable backface culling
    InstancedRender3D& cullBack(bool enable) { m_cullBack = enable; return *this; }

    // === Operator Interface ===
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "InstancedRender3D"; }

    std::vector<ParamDecl> params() override {
        return {
            {"instanceCount", ParamType::Int, 0.0f, 100000.0f,
             {static_cast<float>(m_instances.size()), 0, 0, 0}},
            {"metallic", ParamType::Float, 0.0f, 1.0f, {m_metallic, 0, 0, 0}},
            {"roughness", ParamType::Float, 0.0f, 1.0f, {m_roughness, 0, 0, 0}},
            {"ambient", ParamType::Float, 0.0f, 2.0f, {m_ambient, 0, 0, 0}}
        };
    }

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
    float m_metallic = 0.0f;
    float m_roughness = 0.5f;
    float m_ambient = 0.3f;
    glm::vec4 m_baseColor{1.0f, 1.0f, 1.0f, 1.0f};

    // Rendering options
    glm::vec4 m_clearColor{0.1f, 0.1f, 0.15f, 1.0f};
    bool m_depthTest = true;
    bool m_cullBack = true;

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
