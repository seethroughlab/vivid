// GLTF Viewer Operator - Renders GLTF models with PBR materials
#pragma once

#include "vivid/operator.h"
#include "vivid/camera.h"
#include "vivid/gltf_model.h"
#include <memory>
#include <vector>
#include <string>

namespace vivid {

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

    // Light direction
    void lightDirection(float x, float y, float z);
    void lightIntensity(float intensity);

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
    glm::vec3 lightDir_ = glm::normalize(glm::vec3(0.5f, 0.6f, -0.2f));
    float lightIntensity_ = 3.0f;
    glm::vec3 bgColor_ = glm::vec3(0.1f, 0.1f, 0.15f);
    bool hasEnvironment_ = false;
};

} // namespace vivid
