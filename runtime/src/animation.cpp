#include <vivid/animation.h>
#include <glm/gtc/matrix_transform.hpp>
#include <algorithm>

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

    // Initialize with identity
    std::vector<glm::mat4> globalTransforms(numBones, glm::mat4(1.0f));

    // Build map from bone index to animation channel
    std::vector<const AnimationChannel*> boneChannels(numBones, nullptr);
    if (clip_) {
        for (const auto& channel : clip_->channels) {
            if (channel.boneIndex >= 0 && channel.boneIndex < static_cast<int>(numBones)) {
                boneChannels[channel.boneIndex] = &channel;
            }
        }
    }

    // Compute global transforms (assumes bones are sorted parent-first)
    for (size_t i = 0; i < numBones; ++i) {
        const Bone& bone = skeleton.bones[i];

        // Get local transform from animation or default
        glm::mat4 localTransform;
        if (boneChannels[i] && clip_) {
            localTransform = boneChannels[i]->getLocalTransform(currentTime_);
        } else {
            localTransform = bone.localTransform;
        }

        // Combine with parent transform
        if (bone.parentIndex >= 0 && bone.parentIndex < static_cast<int>(i)) {
            globalTransforms[i] = globalTransforms[bone.parentIndex] * localTransform;
        } else {
            globalTransforms[i] = localTransform;
        }

        // Final bone matrix = global transform * offset matrix (inverse bind pose)
        boneMatrices[i] = globalTransforms[i] * bone.offsetMatrix;
    }
}

} // namespace vivid
