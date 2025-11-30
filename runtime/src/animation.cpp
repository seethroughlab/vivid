#include <vivid/animation.h>
#include <glm/gtc/matrix_transform.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/matrix_decompose.hpp>
#include <algorithm>
#include <functional>
#include <unordered_map>
#include <iostream>

namespace vivid {

// Helper to find keyframe index for time t
template<typename T>
static int findKeyframeIndex(const std::vector<Keyframe<T>>& keys, float t) {
    if (keys.empty()) return -1;
    for (size_t i = 0; i < keys.size() - 1; ++i) {
        if (t < keys[i + 1].time) return static_cast<int>(i);
    }
    return static_cast<int>(keys.size() - 1);
}

// Linear interpolation factor between two keyframes
template<typename T>
static float getInterpolationFactor(const std::vector<Keyframe<T>>& keys, int index, float t) {
    if (index < 0 || index >= static_cast<int>(keys.size()) - 1) return 0.0f;
    float t0 = keys[index].time;
    float t1 = keys[index + 1].time;
    float dt = t1 - t0;
    if (dt <= 0.0f) return 0.0f;
    return glm::clamp((t - t0) / dt, 0.0f, 1.0f);
}

glm::vec3 AnimationChannel::interpolatePosition(float t) const {
    if (positionKeys.empty()) return glm::vec3(0.0f);
    if (positionKeys.size() == 1) return positionKeys[0].value;

    int idx = findKeyframeIndex(positionKeys, t);
    if (idx < 0) return positionKeys[0].value;
    if (idx >= static_cast<int>(positionKeys.size()) - 1) {
        return positionKeys.back().value;
    }

    float factor = getInterpolationFactor(positionKeys, idx, t);
    return glm::mix(positionKeys[idx].value, positionKeys[idx + 1].value, factor);
}

glm::quat AnimationChannel::interpolateRotation(float t) const {
    if (rotationKeys.empty()) return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    if (rotationKeys.size() == 1) return rotationKeys[0].value;

    int idx = findKeyframeIndex(rotationKeys, t);
    if (idx < 0) return rotationKeys[0].value;
    if (idx >= static_cast<int>(rotationKeys.size()) - 1) {
        return rotationKeys.back().value;
    }

    float factor = getInterpolationFactor(rotationKeys, idx, t);
    return glm::slerp(rotationKeys[idx].value, rotationKeys[idx + 1].value, factor);
}

glm::vec3 AnimationChannel::interpolateScale(float t) const {
    if (scaleKeys.empty()) return glm::vec3(1.0f);
    if (scaleKeys.size() == 1) return scaleKeys[0].value;

    int idx = findKeyframeIndex(scaleKeys, t);
    if (idx < 0) return scaleKeys[0].value;
    if (idx >= static_cast<int>(scaleKeys.size()) - 1) {
        return scaleKeys.back().value;
    }

    float factor = getInterpolationFactor(scaleKeys, idx, t);
    return glm::mix(scaleKeys[idx].value, scaleKeys[idx + 1].value, factor);
}

glm::mat4 AnimationChannel::getLocalTransform(float t) const {
    glm::vec3 pos = interpolatePosition(t);
    glm::quat rot = interpolateRotation(t);
    glm::vec3 scale = interpolateScale(t);

    glm::mat4 T = glm::translate(glm::mat4(1.0f), pos);
    glm::mat4 R = glm::mat4_cast(rot);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);

    return T * R * S;
}

glm::mat4 AnimationChannel::getLocalTransformWithFallback(float t, const glm::mat4& bindPose) const {
    // Decompose bind pose to get fallback values
    glm::vec3 bindPos, bindScale, bindSkew;
    glm::quat bindRot;
    glm::vec4 bindPerspective;
    glm::decompose(bindPose, bindScale, bindRot, bindPos, bindSkew, bindPerspective);

    // Use animation values if available, otherwise use bind pose
    glm::vec3 pos = positionKeys.empty() ? bindPos : interpolatePosition(t);
    glm::quat rot = rotationKeys.empty() ? bindRot : interpolateRotation(t);
    glm::vec3 scale = scaleKeys.empty() ? bindScale : interpolateScale(t);

    glm::mat4 T = glm::translate(glm::mat4(1.0f), pos);
    glm::mat4 R = glm::mat4_cast(rot);
    glm::mat4 S = glm::scale(glm::mat4(1.0f), scale);

    return T * R * S;
}

void AnimationPlayer::setClip(const AnimationClip* clip, bool loop) {
    clip_ = clip;
    loop_ = loop;
    currentTime_ = 0.0f;
}

void AnimationPlayer::play() {
    playing_ = true;
}

void AnimationPlayer::pause() {
    playing_ = false;
}

void AnimationPlayer::stop() {
    playing_ = false;
    currentTime_ = 0.0f;
}

void AnimationPlayer::setTime(float time) {
    if (clip_) {
        currentTime_ = glm::clamp(time, 0.0f, clip_->duration);
    }
}

void AnimationPlayer::update(float deltaTime) {
    if (!playing_ || !clip_) return;

    currentTime_ += deltaTime * speed_;

    if (currentTime_ >= clip_->duration) {
        if (loop_) {
            currentTime_ = std::fmod(currentTime_, clip_->duration);
        } else {
            currentTime_ = clip_->duration;
            playing_ = false;
        }
    }
}

void AnimationPlayer::computeBoneMatrices(const Skeleton& skeleton,
                                          std::vector<glm::mat4>& boneMatrices) const {
    size_t numBones = skeleton.bones.size();
    if (boneMatrices.size() != numBones) {
        boneMatrices.resize(numBones);
    }

    if (numBones == 0) return;

    // If no animation clip, use identity matrices (bind pose)
    if (!clip_ || clip_->channels.empty()) {
        for (size_t i = 0; i < numBones; ++i) {
            boneMatrices[i] = glm::mat4(1.0f);
        }
        return;
    }

    // Build a map from bone name to animation channel for quick lookup
    std::unordered_map<std::string, const AnimationChannel*> channelMap;
    for (const auto& channel : clip_->channels) {
        channelMap[channel.boneName] = &channel;
    }

    // Temporary storage for global transforms (world space)
    std::vector<glm::mat4> globalTransforms(numBones);

    // Process bones in order (assumes parent comes before children in array)
    for (size_t i = 0; i < numBones; ++i) {
        const Bone& bone = skeleton.bones[i];

        // Get the node-local transform for this bone
        // Use animated transform if available, otherwise use bind pose local transform
        glm::mat4 nodeLocalTransform = bone.localTransform;
        auto it = channelMap.find(bone.name);
        if (it != channelMap.end()) {
            // Use animation with bind pose fallback for missing position/rotation/scale keys
            nodeLocalTransform = it->second->getLocalTransformWithFallback(currentTime_, bone.localTransform);
        }

        // Note: We do NOT apply preTransform here because Assimp's offset matrices
        // already include the full bind pose transform chain. Applying preTransform
        // would double-apply ancestor transforms.
        glm::mat4 fullLocalTransform = nodeLocalTransform;

        // Compute global transform by combining with parent
        if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int>(numBones)) {
            globalTransforms[i] = globalTransforms[bone.parentIndex] * fullLocalTransform;
        } else {
            // Root bone - use full local transform directly
            globalTransforms[i] = fullLocalTransform;
        }

        // Final skinning matrix = globalTransform * offsetMatrix
        // This transforms vertices from bind pose to current animated pose
        boneMatrices[i] = globalTransforms[i] * bone.offsetMatrix;
    }
}

// SkinnedMesh3D::update - update animation time and compute bone matrices with fallback system
// Note: For better animation quality, use the vivid-models addon's AnimationSystem
// and set boneMatrices directly from animSystem.getBoneMatrices()
void SkinnedMesh3D::update(float deltaTime) {
    // Update time if playing
    if (playing && currentAnimIndex >= 0) {
        currentTime += deltaTime * speed;

        // Handle looping
        if (currentAnimIndex < static_cast<int>(animations.size())) {
            float duration = animations[currentAnimIndex].duration;
            if (currentTime >= duration) {
                if (looping) {
                    currentTime = std::fmod(currentTime, duration);
                } else {
                    currentTime = duration;
                    playing = false;
                }
            }
        }
    }

    // Update using built-in animation player (basic interpolation)
    player.update(deltaTime);
    if (hasSkeleton()) {
        boneMatrices.resize(skeleton.bones.size());
        player.computeBoneMatrices(skeleton, boneMatrices);
    }
}

} // namespace vivid
