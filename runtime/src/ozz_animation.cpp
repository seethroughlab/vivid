#include "ozz_animation.h"
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/animation_builder.h>
#include <ozz/base/maths/math_ex.h>
#include <assimp/scene.h>
#include <iostream>
#include <unordered_set>
#include <unordered_map>
#include <queue>

namespace vivid {

// Static empty string for error cases
static const std::string emptyString;

glm::mat4 OzzAnimationSystem::ozzToGlm(const ozz::math::Float4x4& m) {
    // ozz uses column-major like GLM
    glm::mat4 result;
    for (int col = 0; col < 4; ++col) {
        ozz::math::SimdFloat4 column = m.cols[col];
        float values[4];
        ozz::math::StorePtrU(column, values);
        result[col] = glm::vec4(values[0], values[1], values[2], values[3]);
    }
    return result;
}

bool OzzAnimationSystem::buildSkeleton(const Skeleton& skeleton) {
    // Build raw skeleton for ozz offline builder
    ozz::animation::offline::RawSkeleton rawSkeleton;

    if (skeleton.bones.empty()) {
        std::cerr << "[OzzAnimation] Empty skeleton\n";
        return false;
    }

    // Find root bones (parentIndex == -1)
    std::vector<int> rootBones;
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        if (skeleton.bones[i].parentIndex < 0) {
            rootBones.push_back(static_cast<int>(i));
        }
    }

    // Build children map
    std::vector<std::vector<int>> children(skeleton.bones.size());
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        int parent = skeleton.bones[i].parentIndex;
        if (parent >= 0 && parent < static_cast<int>(skeleton.bones.size())) {
            children[parent].push_back(static_cast<int>(i));
        }
    }

    // Track mapping from original bone index to ozz joint index
    boneToJoint_.resize(skeleton.bones.size(), -1);
    int ozzJointIndex = 0;

    // Recursive function to build raw skeleton joints
    std::function<void(int, ozz::animation::offline::RawSkeleton::Joint&, bool)> buildJoint;
    buildJoint = [&](int boneIndex, ozz::animation::offline::RawSkeleton::Joint& joint, bool isRoot) {
        const Bone& bone = skeleton.bones[boneIndex];
        joint.name = bone.name;

        // Record the mapping
        boneToJoint_[boneIndex] = ozzJointIndex++;

        // Use pure bone-local transform (without preTransform)
        // preTransform will be applied separately after animation sampling
        glm::mat4 m = bone.localTransform;

        // Store preTransform from root bone (only need to do once)
        if (isRoot && globalPreTransform_ == glm::mat4(1.0f)) {
            globalPreTransform_ = bone.preTransform;
        }

        // Extract translation (column 3)
        joint.transform.translation = ozz::math::Float3(m[3][0], m[3][1], m[3][2]);

        // Extract scale from column lengths
        float sx = glm::length(glm::vec3(m[0]));
        float sy = glm::length(glm::vec3(m[1]));
        float sz = glm::length(glm::vec3(m[2]));
        joint.transform.scale = ozz::math::Float3(sx, sy, sz);

        // Extract rotation matrix (remove scale)
        glm::mat3 rotMat(
            glm::vec3(m[0]) / sx,
            glm::vec3(m[1]) / sy,
            glm::vec3(m[2]) / sz
        );
        glm::quat q = glm::quat_cast(rotMat);
        joint.transform.rotation = ozz::math::Quaternion(q.x, q.y, q.z, q.w);

        // Add children (children are never roots)
        for (int childIndex : children[boneIndex]) {
            joint.children.resize(joint.children.size() + 1);
            buildJoint(childIndex, joint.children.back(), false);
        }
    };

    // Build from root bones (these are the actual roots)
    rawSkeleton.roots.resize(rootBones.size());
    for (size_t i = 0; i < rootBones.size(); ++i) {
        buildJoint(rootBones[i], rawSkeleton.roots[i], true);
    }

    // Validate raw skeleton
    if (!rawSkeleton.Validate()) {
        std::cerr << "[OzzAnimation] Raw skeleton validation failed\n";
        return false;
    }

    // Build runtime skeleton
    ozz::animation::offline::SkeletonBuilder builder;
    auto skeletonPtr = builder(rawSkeleton);
    if (!skeletonPtr) {
        std::cerr << "[OzzAnimation] Skeleton build failed\n";
        return false;
    }
    skeleton_ = std::move(*skeletonPtr);

    // Allocate sampling buffers
    int numSoaJoints = (skeleton_.num_joints() + 3) / 4;
    localTransforms_.resize(numSoaJoints);
    modelMatrices_.resize(skeleton_.num_joints());

    // Use Assimp's offset matrices as inverse bind pose (indexed by bone index)
    // These are the authoritative inverse bind poses from the model file
    inverseBindPose_.resize(skeleton.bones.size());
    for (size_t i = 0; i < skeleton.bones.size(); ++i) {
        inverseBindPose_[i] = skeleton.bones[i].offsetMatrix;
    }

    std::cout << "[OzzAnimation] Using Assimp offset matrices for " << skeleton.bones.size() << " bones\n";

    // Debug: print bone to joint mapping
    std::cout << "[OzzAnimation] Bone to joint mapping:\n";
    for (size_t i = 0; i < std::min(size_t(10), boneToJoint_.size()); ++i) {
        int j = boneToJoint_[i];
        std::cout << "  Bone " << i << " (" << skeleton.bones[i].name << ") -> Joint " << j;
        if (j >= 0 && j < skeleton_.num_joints()) {
            std::cout << " (" << skeleton_.joint_names()[j] << ")";
        }
        std::cout << "\n";
    }

    // Initialize sampling context
    samplingContext_.Resize(skeleton_.num_joints());

    std::cout << "[OzzAnimation] Built skeleton with " << skeleton_.num_joints() << " joints\n";
    return true;
}

bool OzzAnimationSystem::buildAnimation(const AnimationClip& clip, const Skeleton& skeleton) {
    if (!valid()) {
        std::cerr << "[OzzAnimation] Cannot build animation without skeleton\n";
        return false;
    }

    ozz::animation::offline::RawAnimation rawAnim;
    rawAnim.duration = clip.duration;
    rawAnim.name = clip.name;

    // Create tracks for each joint in the skeleton
    rawAnim.tracks.resize(skeleton_.num_joints());

    // Map joint names to ozz skeleton indices
    std::unordered_map<std::string, int> jointNameToOzzIndex;
    for (int i = 0; i < skeleton_.num_joints(); ++i) {
        jointNameToOzzIndex[skeleton_.joint_names()[i]] = i;
    }

    // Fill in animation tracks from our AnimationClip channels
    for (const auto& channel : clip.channels) {
        auto it = jointNameToOzzIndex.find(channel.boneName);
        if (it == jointNameToOzzIndex.end()) {
            continue;  // Channel for joint not in skeleton
        }
        int trackIndex = it->second;
        auto& track = rawAnim.tracks[trackIndex];

        // Copy translation keyframes
        for (const auto& key : channel.positionKeys) {
            ozz::animation::offline::RawAnimation::TranslationKey tk;
            tk.time = key.time / clip.duration;  // Normalize to [0, 1]
            tk.value = ozz::math::Float3(key.value.x, key.value.y, key.value.z);
            track.translations.push_back(tk);
        }

        // Copy rotation keyframes
        for (const auto& key : channel.rotationKeys) {
            ozz::animation::offline::RawAnimation::RotationKey rk;
            rk.time = key.time / clip.duration;
            rk.value = ozz::math::Quaternion(key.value.x, key.value.y, key.value.z, key.value.w);
            track.rotations.push_back(rk);
        }

        // Copy scale keyframes
        for (const auto& key : channel.scaleKeys) {
            ozz::animation::offline::RawAnimation::ScaleKey sk;
            sk.time = key.time / clip.duration;
            sk.value = ozz::math::Float3(key.value.x, key.value.y, key.value.z);
            track.scales.push_back(sk);
        }
    }

    // For joints without animation data, add identity keyframes at the bind pose
    for (int i = 0; i < skeleton_.num_joints(); ++i) {
        auto& track = rawAnim.tracks[i];

        // Get bind pose for this joint
        const ozz::math::SoaTransform& soaBind = skeleton_.joint_rest_poses()[i / 4];
        int lane = i % 4;

        float bindTx, bindTy, bindTz;
        float bindRx, bindRy, bindRz, bindRw;
        float bindSx, bindSy, bindSz;

        // Extract lane from SoA data
        ozz::math::StorePtrU(soaBind.translation.x, &bindTx - lane);
        ozz::math::StorePtrU(soaBind.translation.y, &bindTy - lane);
        ozz::math::StorePtrU(soaBind.translation.z, &bindTz - lane);
        ozz::math::StorePtrU(soaBind.rotation.x, &bindRx - lane);
        ozz::math::StorePtrU(soaBind.rotation.y, &bindRy - lane);
        ozz::math::StorePtrU(soaBind.rotation.z, &bindRz - lane);
        ozz::math::StorePtrU(soaBind.rotation.w, &bindRw - lane);
        ozz::math::StorePtrU(soaBind.scale.x, &bindSx - lane);
        ozz::math::StorePtrU(soaBind.scale.y, &bindSy - lane);
        ozz::math::StorePtrU(soaBind.scale.z, &bindSz - lane);

        // Add bind pose keyframes if track is empty
        if (track.translations.empty()) {
            ozz::animation::offline::RawAnimation::TranslationKey tk;
            tk.time = 0.0f;
            tk.value = ozz::math::Float3(bindTx, bindTy, bindTz);
            track.translations.push_back(tk);
        }
        if (track.rotations.empty()) {
            ozz::animation::offline::RawAnimation::RotationKey rk;
            rk.time = 0.0f;
            rk.value = ozz::math::Quaternion(bindRx, bindRy, bindRz, bindRw);
            track.rotations.push_back(rk);
        }
        if (track.scales.empty()) {
            ozz::animation::offline::RawAnimation::ScaleKey sk;
            sk.time = 0.0f;
            sk.value = ozz::math::Float3(bindSx, bindSy, bindSz);
            track.scales.push_back(sk);
        }
    }

    // Validate raw animation
    if (!rawAnim.Validate()) {
        std::cerr << "[OzzAnimation] Raw animation validation failed for: " << clip.name << "\n";
        return false;
    }

    // Build runtime animation
    ozz::animation::offline::AnimationBuilder builder;
    auto animPtr = builder(rawAnim);
    if (!animPtr) {
        std::cerr << "[OzzAnimation] Animation build failed for: " << clip.name << "\n";
        return false;
    }

    animations_.push_back(std::move(*animPtr));
    animationNames_.push_back(clip.name);

    std::cout << "[OzzAnimation] Built animation: " << clip.name
              << " (" << clip.duration << "s)\n";
    return true;
}

const std::string& OzzAnimationSystem::animationName(size_t index) const {
    if (index < animationNames_.size()) {
        return animationNames_[index];
    }
    return emptyString;
}

float OzzAnimationSystem::animationDuration(size_t index) const {
    if (index < animations_.size()) {
        return animations_[index].duration();
    }
    return 0.0f;
}

void OzzAnimationSystem::sample(size_t animIndex, float time,
                                 std::vector<glm::mat4>& boneMatrices) {
    if (animIndex >= animations_.size() || !valid()) {
        getBindPose(boneMatrices);
        return;
    }

    const auto& animation = animations_[animIndex];

    // Normalize time to animation duration and handle looping
    float duration = animation.duration();
    float ratio = (duration > 0.0f) ? std::fmod(time, duration) / duration : 0.0f;
    ratio = ozz::math::Clamp(0.0f, ratio, 1.0f);

    // Sample animation
    ozz::animation::SamplingJob samplingJob;
    samplingJob.animation = &animation;
    samplingJob.context = &samplingContext_;
    samplingJob.ratio = ratio;
    samplingJob.output = ozz::make_span(localTransforms_);

    if (!samplingJob.Run()) {
        std::cerr << "[OzzAnimation] Sampling job failed\n";
        getBindPose(boneMatrices);
        return;
    }

    // Convert local transforms to model-space matrices
    ozz::animation::LocalToModelJob ltmJob;
    ltmJob.skeleton = &skeleton_;
    ltmJob.input = ozz::make_span(localTransforms_);
    ltmJob.output = ozz::make_span(modelMatrices_);

    if (!ltmJob.Run()) {
        std::cerr << "[OzzAnimation] Local-to-model job failed\n";
        getBindPose(boneMatrices);
        return;
    }

    // Convert to skinning matrices (model-space * inverse-bind-pose)
    // Output is indexed by original bone index, not ozz joint index
    int numBones = static_cast<int>(boneToJoint_.size());
    boneMatrices.resize(numBones);

    for (int boneIndex = 0; boneIndex < numBones; ++boneIndex) {
        int jointIndex = boneToJoint_[boneIndex];
        if (jointIndex >= 0 && jointIndex < skeleton_.num_joints()) {
            glm::mat4 modelMatrix = ozzToGlm(modelMatrices_[jointIndex]);
            // Apply global preTransform to convert from bone hierarchy space to world space
            // This includes the FBX global scale that Assimp's offset matrices expect
            glm::mat4 worldMatrix = globalPreTransform_ * modelMatrix;
            // Skinning matrix = world-space transform * inverse bind pose
            boneMatrices[boneIndex] = worldMatrix * inverseBindPose_[boneIndex];
        } else {
            boneMatrices[boneIndex] = glm::mat4(1.0f);
        }
    }

}

void OzzAnimationSystem::getBindPose(std::vector<glm::mat4>& boneMatrices) {
    if (!valid()) {
        boneMatrices.clear();
        return;
    }

    // Use rest poses to compute model-space matrices
    ozz::animation::LocalToModelJob ltmJob;
    ltmJob.skeleton = &skeleton_;
    ltmJob.input = skeleton_.joint_rest_poses();
    ltmJob.output = ozz::make_span(modelMatrices_);

    if (!ltmJob.Run()) {
        int numBones = static_cast<int>(boneToJoint_.size());
        boneMatrices.resize(numBones, glm::mat4(1.0f));
        return;
    }

    // Output indexed by bone index (for mesh vertex bone IDs)
    int numBones = static_cast<int>(boneToJoint_.size());
    boneMatrices.resize(numBones);

    for (int boneIndex = 0; boneIndex < numBones; ++boneIndex) {
        int jointIndex = boneToJoint_[boneIndex];
        if (jointIndex >= 0 && jointIndex < skeleton_.num_joints()) {
            glm::mat4 modelMatrix = ozzToGlm(modelMatrices_[jointIndex]);
            // Apply global preTransform to convert from bone hierarchy space to world space
            glm::mat4 worldMatrix = globalPreTransform_ * modelMatrix;
            boneMatrices[boneIndex] = worldMatrix * inverseBindPose_[boneIndex];
        } else {
            boneMatrices[boneIndex] = glm::mat4(1.0f);
        }
    }
}

} // namespace vivid
