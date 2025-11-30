#pragma once
#include <vivid/animation.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/base/containers/vector.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/maths/simd_math.h>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vector>
#include <memory>
#include <string>

namespace vivid {

/**
 * @brief ozz-based skeletal animation system.
 *
 * This class converts Assimp skeleton/animation data to ozz format
 * and handles runtime animation sampling and bone matrix computation.
 */
class OzzAnimationSystem {
public:
    OzzAnimationSystem() = default;
    ~OzzAnimationSystem() = default;

    // Non-copyable, non-movable (due to ozz types)
    OzzAnimationSystem(const OzzAnimationSystem&) = delete;
    OzzAnimationSystem& operator=(const OzzAnimationSystem&) = delete;
    OzzAnimationSystem(OzzAnimationSystem&&) = delete;
    OzzAnimationSystem& operator=(OzzAnimationSystem&&) = delete;

    /**
     * @brief Build ozz skeleton from our Skeleton structure.
     */
    bool buildSkeleton(const Skeleton& skeleton);

    /**
     * @brief Build ozz animation from our AnimationClip structure.
     * @param clip The animation clip to build
     * @param skeleton The skeleton structure
     * @param originalIndex The index in the original animation array (for index mapping)
     */
    bool buildAnimation(const AnimationClip& clip, const Skeleton& skeleton, int originalIndex = -1);

    /**
     * @brief Get number of loaded animations.
     */
    size_t animationCount() const { return animations_.size(); }

    /**
     * @brief Get animation name by index.
     */
    const std::string& animationName(size_t index) const;

    /**
     * @brief Get animation duration by index.
     */
    float animationDuration(size_t index) const;

    /**
     * @brief Sample animation and compute skinning matrices.
     *
     * @param animIndex Ozz animation index to sample
     * @param time Current time in seconds
     * @param boneMatrices Output skinning matrices (model-space * inverse-bind-pose)
     */
    void sample(size_t animIndex, float time, std::vector<glm::mat4>& boneMatrices);

    /**
     * @brief Sample animation by original AnimationClip index.
     *
     * This method handles the mapping from original animation indices to ozz indices,
     * accounting for animations that may have failed to build.
     */
    void sampleByOriginalIndex(int originalIndex, float time, std::vector<glm::mat4>& boneMatrices);

    /**
     * @brief Get the ozz animation index for an original AnimationClip index.
     * @return The ozz index, or -1 if that animation failed to build.
     */
    int getOzzIndex(int originalIndex) const;

    /**
     * @brief Get bind pose matrices (for when no animation is playing).
     */
    void getBindPose(std::vector<glm::mat4>& boneMatrices);

    /**
     * @brief Check if skeleton is valid.
     */
    bool valid() const { return skeleton_.num_joints() > 0; }

    /**
     * @brief Get number of joints.
     */
    int numJoints() const { return skeleton_.num_joints(); }

private:
    // ozz skeleton
    ozz::animation::Skeleton skeleton_;

    // ozz animations
    std::vector<ozz::animation::Animation> animations_;
    std::vector<std::string> animationNames_;

    // Mapping from original animation index to ozz index (-1 if failed to build)
    std::vector<int> originalToOzzIndex_;

    // Sampling cache (reused each frame)
    ozz::animation::SamplingJob::Context samplingContext_;

    // Buffers for sampling (SoA format)
    ozz::vector<ozz::math::SoaTransform> localTransforms_;

    // Model-space matrices from LocalToModelJob
    ozz::vector<ozz::math::Float4x4> modelMatrices_;

    // Inverse bind pose matrices (for skinning) - indexed by original bone index
    std::vector<glm::mat4> inverseBindPose_;

    // Mapping from original bone index to ozz joint index
    std::vector<int> boneToJoint_;

    // Global preTransform (from non-bone ancestors like FBX scale node)
    glm::mat4 globalPreTransform_ = glm::mat4(1.0f);

    // Convert ozz Float4x4 to glm mat4
    static glm::mat4 ozzToGlm(const ozz::math::Float4x4& m);
};

/**
 * @brief Build ozz skeleton from Assimp scene.
 *
 * This function traverses the Assimp scene and builds an ozz skeleton
 * that properly handles FBX/glTF coordinate systems.
 */
bool buildOzzSkeletonFromAssimp(
    const struct aiScene* scene,
    ozz::animation::Skeleton& outSkeleton,
    std::vector<glm::mat4>& outInverseBindPose,
    std::vector<std::string>& outJointNames);

/**
 * @brief Build ozz animation from Assimp animation.
 */
bool buildOzzAnimationFromAssimp(
    const struct aiAnimation* anim,
    const ozz::animation::Skeleton& skeleton,
    const std::vector<std::string>& jointNames,
    ozz::animation::Animation& outAnimation);

} // namespace vivid
