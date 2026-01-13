#pragma once

#include <vivid/effects/texture_operator.h>
#include <vivid/param.h>
#include <glm/glm.hpp>

namespace vivid::render3d {

class LightOperator;
class CameraOperator;

/**
 * Radial blur god rays effect (light shafts)
 *
 * Creates bright light beams radiating from a light source by:
 * 1. Creating an occlusion mask from scene brightness
 * 2. Applying radial blur toward the light's screen position
 * 3. Compositing additively over the scene
 *
 * This produces the classic "sunbeams through trees" effect with
 * BRIGHT rays against a darker background.
 */
class GodRays : public vivid::effects::TextureOperator {
public:
    GodRays();
    ~GodRays() override;

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "GodRays"; }

    // Input connections
    void setInput(Operator* op) { m_input = op; }
    void setCameraInput(CameraOperator* camera) { m_camera = camera; }
    void setLightInput(LightOperator* light) { m_light = light; }

    // Alternative: set light position directly
    void setLightPosition(float x, float y, float z) {
        m_lightPos = glm::vec3(x, y, z);
        m_useLightInput = false;
    }

    // God ray parameters
    Param<float> exposure{"exposure", 0.3f, 0.0f, 1.0f};      // Overall brightness
    Param<float> decay{"decay", 0.95f, 0.8f, 1.0f};           // Falloff per sample
    Param<float> density{"density", 1.0f, 0.1f, 2.0f};        // Sample spacing
    Param<float> weight{"weight", 0.5f, 0.0f, 1.0f};          // Per-sample weight
    Param<int> samples{"samples", 64, 16, 128};               // Number of blur samples

    // Occlusion threshold - pixels brighter than this contribute to rays
    Param<float> threshold{"threshold", 0.5f, 0.0f, 1.0f};

    // Blend mode
    Param<float> blend{"blend", 0.8f, 0.0f, 1.0f};            // How much to blend rays

private:
    void createPipeline(Context& ctx);

    Operator* m_input = nullptr;
    CameraOperator* m_camera = nullptr;
    LightOperator* m_light = nullptr;
    glm::vec3 m_lightPos{0.0f, 10.0f, 0.0f};
    bool m_useLightInput = true;

    // GPU resources
    WGPURenderPipeline m_pipeline = nullptr;
    WGPUBindGroupLayout m_bindGroupLayout = nullptr;
    WGPUBuffer m_uniformBuffer = nullptr;
    WGPUBindGroup m_bindGroup = nullptr;
    WGPUSampler m_sampler = nullptr;

    bool m_initialized = false;
};

} // namespace vivid::render3d
