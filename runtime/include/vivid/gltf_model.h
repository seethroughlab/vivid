#pragma once

#include <string>
#include <memory>
#include <glm/glm.hpp>
#include "export.h"

// Forward declarations
namespace Diligent {
    class IRenderDevice;
    class IDeviceContext;
    namespace GLTF {
        class Model;
        struct ModelTransforms;
    }
}

namespace vivid {

class Context;

/// GLTF Model wrapper for loading and managing 3D models
class VIVID_API GLTFModel {
public:
    GLTFModel();
    ~GLTFModel();

    // Non-copyable
    GLTFModel(const GLTFModel&) = delete;
    GLTFModel& operator=(const GLTFModel&) = delete;

    // Movable
    GLTFModel(GLTFModel&& other) noexcept;
    GLTFModel& operator=(GLTFModel&& other) noexcept;

    /// Load a GLTF/GLB model from file
    /// @param ctx Vivid context
    /// @param path Path to the GLTF/GLB file
    /// @return true if loaded successfully
    bool load(Context& ctx, const std::string& path);

    /// Check if model is loaded
    bool isLoaded() const { return model_ != nullptr; }

    /// Get bounding box min point
    glm::vec3 boundsMin() const { return boundsMin_; }

    /// Get bounding box max point
    glm::vec3 boundsMax() const { return boundsMax_; }

    /// Get bounding box center
    glm::vec3 center() const { return (boundsMin_ + boundsMax_) * 0.5f; }

    /// Get bounding box size
    glm::vec3 size() const { return boundsMax_ - boundsMin_; }

    /// Get number of scenes in the model
    int sceneCount() const;

    /// Get default scene index
    int defaultSceneIndex() const;

    /// Get number of animations
    int animationCount() const;

    /// Update animation
    /// @param sceneIndex Scene to animate
    /// @param animationIndex Animation index
    /// @param time Current time in seconds
    void updateAnimation(int sceneIndex, int animationIndex, float time);

    /// Get the underlying Diligent GLTF::Model (for advanced use)
    Diligent::GLTF::Model* diligentModel() { return model_.get(); }
    const Diligent::GLTF::Model* diligentModel() const { return model_.get(); }

    /// Get model transforms (for rendering)
    Diligent::GLTF::ModelTransforms* transforms() { return transforms_.get(); }
    const Diligent::GLTF::ModelTransforms* transforms() const { return transforms_.get(); }

private:
    std::unique_ptr<Diligent::GLTF::Model> model_;
    std::unique_ptr<Diligent::GLTF::ModelTransforms> transforms_;
    glm::vec3 boundsMin_{0.0f};
    glm::vec3 boundsMax_{0.0f};
    std::string loadedPath_;
};

} // namespace vivid
