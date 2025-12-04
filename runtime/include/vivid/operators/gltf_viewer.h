// GLTF Viewer Operator - Renders GLTF models with PBR materials
#pragma once

#include "vivid/operator.h"
#include "vivid/camera.h"
#include "vivid/gltf_model.h"
#include <memory>
#include <vector>
#include <string>

namespace vivid {

// Maximum number of lights supported (must match PBR_MAX_LIGHTS in shader)
static constexpr int MAX_LIGHTS = 8;

// Light types matching GLTF spec
enum class LightType {
    Directional = 1,  // Infinite light from a direction
    Point = 2,        // Point light with attenuation
    Spot = 3          // Spot light with cone angles
};

// Light source for PBR rendering
struct Light {
    LightType type = LightType::Directional;

    // Position (point and spot lights)
    glm::vec3 position{0.0f, 0.0f, 0.0f};

    // Direction (directional and spot lights)
    glm::vec3 direction{0.0f, -1.0f, 0.0f};

    // Color (linear RGB)
    glm::vec3 color{1.0f, 1.0f, 1.0f};

    // Intensity (lumens for point/spot, lux for directional)
    float intensity = 1.0f;

    // Range for point/spot lights (0 = infinite)
    float range = 0.0f;

    // Spot light cone angles (radians)
    float innerConeAngle = 0.0f;
    float outerConeAngle = 0.7854f;  // 45 degrees

    // Factory methods for convenience
    static Light directional(const glm::vec3& dir, float intensity = 3.0f, const glm::vec3& color = glm::vec3(1.0f)) {
        Light l;
        l.type = LightType::Directional;
        l.direction = glm::normalize(dir);
        l.intensity = intensity;
        l.color = color;
        return l;
    }

    static Light point(const glm::vec3& pos, float intensity = 100.0f, float range = 10.0f, const glm::vec3& color = glm::vec3(1.0f)) {
        Light l;
        l.type = LightType::Point;
        l.position = pos;
        l.intensity = intensity;
        l.range = range;
        l.color = color;
        return l;
    }

    static Light spot(const glm::vec3& pos, const glm::vec3& dir, float intensity = 100.0f,
                      float innerAngle = 0.349f, float outerAngle = 0.785f, float range = 10.0f,
                      const glm::vec3& color = glm::vec3(1.0f)) {
        Light l;
        l.type = LightType::Spot;
        l.position = pos;
        l.direction = glm::normalize(dir);
        l.intensity = intensity;
        l.innerConeAngle = innerAngle;
        l.outerConeAngle = outerAngle;
        l.range = range;
        l.color = color;
        return l;
    }
};

// GLTFViewer operator - displays GLTF models with DiligentFX PBR rendering
class GLTFViewer : public Operator {
public:
    GLTFViewer();
    ~GLTFViewer() override;

    // Load a model and return its index
    int loadModel(Context& ctx, const std::string& path);

    // Set which model to display
    void setCurrentModel(int index);
    int currentModel() const { return currentModelIndex_; }
    int modelCount() const { return static_cast<int>(models_.size()); }

    // Cycle to next model
    void nextModel();

    // Get model name at index
    std::string modelName(int index) const;

    // Camera access
    Camera3D& camera() { return camera_; }
    const Camera3D& camera() const { return camera_; }

    // Legacy single light API (for backward compatibility)
    void lightDirection(float x, float y, float z);
    void lightIntensity(float intensity);

    // Multi-light API
    int addLight(const Light& light);  // Returns light index, -1 if full
    void setLight(int index, const Light& light);
    void removeLight(int index);
    void clearLights();
    int lightCount() const { return static_cast<int>(lights_.size()); }
    const Light& getLight(int index) const { return lights_[index]; }

    // Background color
    void backgroundColor(float r, float g, float b);

    // Environment map (for IBL reflections)
    bool loadEnvironment(Context& ctx, const std::string& hdrPath);
    bool hasEnvironment() const;

    // Operator interface
    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

    // Check if properly initialized
    bool isInitialized() const;

    // Type name
    std::string typeName() const override { return "GLTFViewer"; }

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;

    std::vector<GLTFModel> models_;
    std::vector<std::string> modelNames_;
    int currentModelIndex_ = 0;

    Camera3D camera_;
    std::vector<Light> lights_;  // Multi-light support
    glm::vec3 bgColor_ = glm::vec3(0.1f, 0.1f, 0.15f);
    bool hasEnvironment_ = false;
};

} // namespace vivid
