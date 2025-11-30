#pragma once
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <string>
#include <vector>
#include <unordered_map>

namespace vivid {

// Maximum bones per vertex (GPU skinning limit)
constexpr int MAX_BONES_PER_VERTEX = 4;
// Maximum bones in a skeleton (affects uniform buffer size)
constexpr int MAX_BONES = 128;

/**
 * @brief Skinned vertex with bone weights for skeletal animation.
 */
struct SkinnedVertex3D {
    glm::vec3 position{0.0f};
    glm::vec3 normal{0.0f, 1.0f, 0.0f};
    glm::vec2 uv{0.0f};
    glm::vec4 tangent{1.0f, 0.0f, 0.0f, 1.0f};

    // Bone influences (up to 4 bones per vertex)
    glm::ivec4 boneIds{0};      // Bone indices
    glm::vec4 boneWeights{0.0f}; // Bone weights (should sum to 1.0)

    void addBoneInfluence(int boneId, float weight) {
        // Find empty slot
        for (int i = 0; i < MAX_BONES_PER_VERTEX; ++i) {
            if (boneWeights[i] == 0.0f) {
                boneIds[i] = boneId;
                boneWeights[i] = weight;
                return;
            }
        }
        // If all slots full, replace smallest weight
        int minIdx = 0;
        for (int i = 1; i < MAX_BONES_PER_VERTEX; ++i) {
            if (boneWeights[i] < boneWeights[minIdx]) minIdx = i;
        }
        if (weight > boneWeights[minIdx]) {
            boneIds[minIdx] = boneId;
            boneWeights[minIdx] = weight;
        }
    }

    void normalizeBoneWeights() {
        float sum = boneWeights.x + boneWeights.y + boneWeights.z + boneWeights.w;
        if (sum > 0.0f) {
            boneWeights /= sum;
        }
    }
};

/**
 * @brief A bone in the skeleton hierarchy.
 */
struct Bone {
    std::string name;
    int parentIndex = -1;  // -1 for root bones
    glm::mat4 offsetMatrix{1.0f};  // Inverse bind pose (mesh space to bone space)
    glm::mat4 localTransform{1.0f}; // Node's own local transform (bind pose)
    glm::mat4 preTransform{1.0f};   // Accumulated transforms from non-bone ancestors
};

/**
 * @brief Skeleton containing bone hierarchy.
 */
struct Skeleton {
    std::vector<Bone> bones;
    std::unordered_map<std::string, int> boneNameToIndex;

    int findBone(const std::string& name) const {
        auto it = boneNameToIndex.find(name);
        return it != boneNameToIndex.end() ? it->second : -1;
    }

    int addBone(const Bone& bone) {
        int index = static_cast<int>(bones.size());
        boneNameToIndex[bone.name] = index;
        bones.push_back(bone);
        return index;
    }
};

/**
 * @brief Keyframe for a single property at a point in time.
 */
template<typename T>
struct Keyframe {
    float time;
    T value;
};

/**
 * @brief Animation channel for one bone (position, rotation, scale tracks).
 */
struct AnimationChannel {
    std::string boneName;
    int boneIndex = -1;  // Cached bone index

    std::vector<Keyframe<glm::vec3>> positionKeys;
    std::vector<Keyframe<glm::quat>> rotationKeys;
    std::vector<Keyframe<glm::vec3>> scaleKeys;

    // Interpolate position at time t
    glm::vec3 interpolatePosition(float t) const;
    // Interpolate rotation at time t
    glm::quat interpolateRotation(float t) const;
    // Interpolate scale at time t
    glm::vec3 interpolateScale(float t) const;
    // Get local transform matrix at time t
    glm::mat4 getLocalTransform(float t) const;
    // Get local transform with bind pose fallback for missing keyframes
    glm::mat4 getLocalTransformWithFallback(float t, const glm::mat4& bindPose) const;
};

/**
 * @brief Animation clip containing keyframe data for bones.
 */
struct AnimationClip {
    std::string name;
    float duration = 0.0f;  // Duration in seconds
    float ticksPerSecond = 25.0f;  // Original animation rate
    std::vector<AnimationChannel> channels;

    // Link channels to skeleton bone indices
    void linkToSkeleton(const Skeleton& skeleton) {
        for (auto& channel : channels) {
            channel.boneIndex = skeleton.findBone(channel.boneName);
        }
    }
};

/**
 * @brief Animation player state for a skinned mesh.
 */
class AnimationPlayer {
public:
    void setClip(const AnimationClip* clip, bool loop = true);
    void play();
    void pause();
    void stop();
    void update(float deltaTime);
    void setSpeed(float speed) { speed_ = speed; }
    void setTime(float time);

    bool isPlaying() const { return playing_; }
    float currentTime() const { return currentTime_; }
    float duration() const { return clip_ ? clip_->duration : 0.0f; }

    // Calculate bone matrices for current animation state
    void computeBoneMatrices(const Skeleton& skeleton,
                             std::vector<glm::mat4>& boneMatrices) const;

private:
    const AnimationClip* clip_ = nullptr;
    float currentTime_ = 0.0f;
    float speed_ = 1.0f;
    bool playing_ = false;
    bool loop_ = true;
};

// Forward declaration for ozz animation system
class OzzAnimationSystem;

/**
 * @brief Skinned mesh with skeleton and animations.
 */
struct SkinnedMesh3D {
    void* handle = nullptr;  // GPU buffer handle
    uint32_t vertexCount = 0;
    uint32_t indexCount = 0;

    Skeleton skeleton;
    std::vector<AnimationClip> animations;
    AnimationPlayer player;

    // ozz-animation system (optional, used if available)
    OzzAnimationSystem* ozzSystem = nullptr;

    // Current bone matrices (computed each frame)
    std::vector<glm::mat4> boneMatrices;

    // Current animation state
    int currentAnimIndex = -1;
    float currentTime = 0.0f;
    float speed = 1.0f;
    bool playing = false;
    bool looping = true;

    bool valid() const { return handle != nullptr; }
    bool hasSkeleton() const { return !skeleton.bones.empty(); }
    bool hasAnimations() const { return !animations.empty(); }

    void playAnimation(int index, bool loop = true) {
        if (index >= 0 && index < static_cast<int>(animations.size())) {
            currentAnimIndex = index;
            currentTime = 0.0f;
            looping = loop;
            playing = true;
            // Also update old player for fallback
            player.setClip(&animations[index], loop);
            player.play();
        }
    }

    void playAnimation(const std::string& name, bool loop = true) {
        for (size_t i = 0; i < animations.size(); ++i) {
            if (animations[i].name == name) {
                playAnimation(static_cast<int>(i), loop);
                return;
            }
        }
    }

    // Update animation - implemented in context.cpp to use ozz
    void update(float deltaTime);
};

} // namespace vivid
