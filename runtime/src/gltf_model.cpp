// GLTF Model Wrapper Implementation

#include "vivid/gltf_model.h"
#include "vivid/context.h"

#include <iostream>

#include "GLTFLoader.hpp"
#include "RenderDevice.h"
#include "DeviceContext.h"

namespace vivid {

using namespace Diligent;

GLTFModel::GLTFModel() = default;

GLTFModel::~GLTFModel() = default;

GLTFModel::GLTFModel(GLTFModel&& other) noexcept
    : model_(std::move(other.model_))
    , transforms_(std::move(other.transforms_))
    , boundsMin_(other.boundsMin_)
    , boundsMax_(other.boundsMax_)
    , loadedPath_(std::move(other.loadedPath_))
{
}

GLTFModel& GLTFModel::operator=(GLTFModel&& other) noexcept {
    if (this != &other) {
        model_ = std::move(other.model_);
        transforms_ = std::move(other.transforms_);
        boundsMin_ = other.boundsMin_;
        boundsMax_ = other.boundsMax_;
        loadedPath_ = std::move(other.loadedPath_);
    }
    return *this;
}

bool GLTFModel::load(Context& ctx, const std::string& path) {
    try {
        // Create model from file
        GLTF::ModelCreateInfo createInfo(path.c_str());

        model_ = std::make_unique<GLTF::Model>(
            ctx.device(),
            ctx.immediateContext(),
            createInfo
        );

        if (!model_) {
            std::cerr << "Failed to create GLTF model from: " << path << std::endl;
            return false;
        }

        // Create transforms for animation/rendering
        transforms_ = std::make_unique<GLTF::ModelTransforms>();

        // Initialize transform vectors with the model's node count
        transforms_->NodeLocalMatrices.resize(model_->Nodes.size());
        transforms_->NodeGlobalMatrices.resize(model_->Nodes.size());
        transforms_->NodeAnimations.resize(model_->Nodes.size());

        // Compute bounding box using default scene
        int sceneIdx = model_->DefaultSceneId >= 0 ? model_->DefaultSceneId : 0;
        if (sceneIdx < static_cast<int>(model_->Scenes.size())) {
            // Compute initial transforms (no animation)
            model_->ComputeTransforms(sceneIdx, *transforms_);

            // Compute bounding box
            BoundBox bb = model_->ComputeBoundingBox(sceneIdx, *transforms_);
            boundsMin_ = glm::vec3(bb.Min.x, bb.Min.y, bb.Min.z);
            boundsMax_ = glm::vec3(bb.Max.x, bb.Max.y, bb.Max.z);
        }

        loadedPath_ = path;
        std::cout << "Loaded GLTF model: " << path << std::endl;
        std::cout << "  Scenes: " << model_->Scenes.size() << std::endl;
        std::cout << "  Meshes: " << model_->Meshes.size() << std::endl;
        std::cout << "  Materials: " << model_->Materials.size() << std::endl;
        std::cout << "  Animations: " << model_->Animations.size() << std::endl;
        std::cout << "  Bounds: (" << boundsMin_.x << ", " << boundsMin_.y << ", " << boundsMin_.z << ") to ("
                  << boundsMax_.x << ", " << boundsMax_.y << ", " << boundsMax_.z << ")" << std::endl;

        return true;

    } catch (const std::exception& e) {
        std::cerr << "Exception loading GLTF: " << e.what() << std::endl;
        return false;
    }
}

int GLTFModel::sceneCount() const {
    return model_ ? static_cast<int>(model_->Scenes.size()) : 0;
}

int GLTFModel::defaultSceneIndex() const {
    return model_ ? model_->DefaultSceneId : -1;
}

int GLTFModel::animationCount() const {
    return model_ ? static_cast<int>(model_->Animations.size()) : 0;
}

void GLTFModel::updateAnimation(int sceneIndex, int animationIndex, float time) {
    if (!model_ || !transforms_) return;
    if (sceneIndex < 0 || sceneIndex >= static_cast<int>(model_->Scenes.size())) return;
    if (animationIndex < 0 || animationIndex >= static_cast<int>(model_->Animations.size())) return;

    // ComputeTransforms handles animation when AnimationIndex and Time are provided
    using namespace Diligent;
    model_->ComputeTransforms(sceneIndex, *transforms_, float4x4::Identity(), animationIndex, time);

    // Update bounding box after animation
    BoundBox bb = model_->ComputeBoundingBox(sceneIndex, *transforms_);
    boundsMin_ = glm::vec3(bb.Min.x, bb.Min.y, bb.Min.z);
    boundsMax_ = glm::vec3(bb.Max.x, bb.Max.y, bb.Max.z);
}

} // namespace vivid
