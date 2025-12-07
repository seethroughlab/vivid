#pragma once

#include <vivid/effects/texture_operator.h>
#include <vivid/render3d/mesh.h>
#include <vivid/render3d/camera.h>
#include <vivid/render3d/scene.h>
#include <glm/glm.hpp>

namespace vivid {
class Context;
}

namespace vivid::render3d {

/// Shading mode for 3D rendering
enum class ShadingMode {
    Unlit,    ///< No lighting, just color/texture
    Flat,     ///< Per-fragment lighting (faceted look)
    Gouraud   ///< Per-vertex lighting (smooth, PS1-style)
};

/// 3D renderer operator - extends TextureOperator for consistent architecture
class Render3D : public effects::TextureOperator {
public:
    Render3D();
    ~Render3D();

    // -------------------------------------------------------------------------
    /// @name Scene Setup
    /// @{

    /// Set the scene to render
    Render3D& scene(Scene& s);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Camera
    /// @{

    /// Set the camera
    Render3D& camera(const Camera3D& cam);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Shading
    /// @{

    /// Set shading mode
    Render3D& shadingMode(ShadingMode mode);

    /// Set default object color
    Render3D& color(float r, float g, float b, float a = 1.0f);
    Render3D& color(const glm::vec4& c);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Lighting
    /// @{

    /// Set light direction (normalized)
    Render3D& lightDirection(glm::vec3 dir);

    /// Set light color
    Render3D& lightColor(glm::vec3 color);

    /// Set ambient light level
    Render3D& ambient(float a);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Output
    /// @{

    /// Set output resolution (overrides TextureOperator::resolution)
    Render3D& resolution(int width, int height) {
        m_width = width;
        m_height = height;
        return *this;
    }

    /// Set clear/background color
    Render3D& clearColor(float r, float g, float b, float a = 1.0f);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Debug
    /// @{

    /// Enable wireframe rendering
    Render3D& wireframe(bool enabled);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Render3D"; }
    // outputKind() and outputView() inherited from TextureOperator

    /// @}

private:
    void createPipeline(Context& ctx);
    void createDepthBuffer(Context& ctx);

    // Scene
    Scene* m_scene = nullptr;
    Camera3D m_camera;

    // Shading
    ShadingMode m_shadingMode = ShadingMode::Flat;
    glm::vec4 m_defaultColor = glm::vec4(0.8f, 0.8f, 0.8f, 1.0f);
    glm::vec3 m_lightDirection = glm::normalize(glm::vec3(1, 1, 1));
    glm::vec3 m_lightColor = glm::vec3(1, 1, 1);
    float m_ambient = 0.1f;

    // Output - m_output, m_outputView, m_width, m_height inherited from TextureOperator
    glm::vec4 m_clearColor = glm::vec4(0.1f, 0.1f, 0.15f, 1.0f);

    // Debug
    bool m_wireframe = false;

    // GPU resources (depth buffer is 3D-specific, not in TextureOperator)
    WGPUTexture m_depthTexture = nullptr;
    WGPUTextureView m_depthView = nullptr;
    WGPURenderPipeline m_pipeline = nullptr;
    WGPURenderPipeline m_wireframePipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::render3d
