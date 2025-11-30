#include <vivid/models/animation_system.h>
#include <ozz/animation/runtime/skeleton.h>
#include <ozz/animation/runtime/animation.h>
#include <ozz/animation/runtime/sampling_job.h>
#include <ozz/animation/runtime/local_to_model_job.h>
#include <ozz/animation/offline/raw_skeleton.h>
#include <ozz/animation/offline/skeleton_builder.h>
#include <ozz/animation/offline/raw_animation.h>
#include <ozz/animation/offline/animation_builder.h>
#include <ozz/base/containers/vector.h>
#include <ozz/base/maths/soa_transform.h>
#include <ozz/base/maths/simd_math.h>
#include <ozz/base/maths/math_ex.h>
#include <glm/gtc/quaternion.hpp>
#include <iostream>
#include <unordered_map>
#include <functional>

namespace vivid::models {

// Convert ozz Float4x4 to glm mat4
static glm::mat4 ozzToGlm(const ozz::math::Float4x4& m) {
    glm::mat4 result;
    for (int col = 0; col < 4; ++col) {
        ozz::math::SimdFloat4 column = m.cols[col];
        float values[4];
        ozz::math::StorePtrU(column, values);
        result[col] = glm::vec4(values[0], values[1], values[2], values[3]);
    }
    return result;
}

struct AnimationSystem::Impl {
    ozz::animation::Skeleton skeleton;
    std::vector<ozz::animation::Animation> animations;
    std::vector<std::string> animationNames;
    std::vector<float> animationDurations;

    // Mapping from original animation index to ozz index
    std::vector<int> originalToOzzIndex;

    // Sampling cache
    ozz::animation::SamplingJob::Context samplingContext;
    ozz::vector<ozz::math::SoaTransform> localTransforms;
    ozz::vector<ozz::math::Float4x4> modelMatrices;

    // Bone data
    std::vector<glm::mat4> inverseBindPose;
    std::vector<int> boneToJoint;
    std::vector<glm::mat4> currentBoneMatrices;

    // Playback state
    int currentAnimIndex = -1;
    float currentTime = 0.0f;
    float playbackSpeed = 1.0f;
    bool playing = false;
    bool paused = false;
    bool looping = true;

    bool buildSkeleton(const Skeleton& skel) {
        ozz::animation::offline::RawSkeleton rawSkeleton;

        if (skel.bones.empty()) {
            std::cerr << "[AnimationSystem] Empty skeleton\n";
            return false;
        }

        // Find root bones
        std::vector<int> rootBones;
        for (size_t i = 0; i < skel.bones.size(); ++i) {
            if (skel.bones[i].parentIndex < 0) {
                rootBones.push_back(static_cast<int>(i));
            }
        }

        // Build children map
        std::vector<std::vector<int>> children(skel.bones.size());
        for (size_t i = 0; i < skel.bones.size(); ++i) {
            int parent = skel.bones[i].parentIndex;
            if (parent >= 0 && parent < static_cast<int>(skel.bones.size())) {
                children[parent].push_back(static_cast<int>(i));
            }
        }

        boneToJoint.resize(skel.bones.size(), -1);
        int ozzJointIndex = 0;

        // Recursive joint builder
        std::function<void(int, ozz::animation::offline::RawSkeleton::Joint&)> buildJoint;
        buildJoint = [&](int boneIndex, ozz::animation::offline::RawSkeleton::Joint& joint) {
            const Bone& bone = skel.bones[boneIndex];
            joint.name = bone.name;
            boneToJoint[boneIndex] = ozzJointIndex++;

            glm::mat4 m = bone.localTransform;

            // Extract translation
            joint.transform.translation = ozz::math::Float3(m[3][0], m[3][1], m[3][2]);

            // Extract scale
            float sx = glm::length(glm::vec3(m[0]));
            float sy = glm::length(glm::vec3(m[1]));
            float sz = glm::length(glm::vec3(m[2]));
            joint.transform.scale = ozz::math::Float3(sx, sy, sz);

            // Extract rotation
            glm::mat3 rotMat(
                glm::vec3(m[0]) / sx,
                glm::vec3(m[1]) / sy,
                glm::vec3(m[2]) / sz
            );
            glm::quat q = glm::quat_cast(rotMat);
            joint.transform.rotation = ozz::math::Quaternion(q.x, q.y, q.z, q.w);

            // Add children
            for (int childIndex : children[boneIndex]) {
                joint.children.resize(joint.children.size() + 1);
                buildJoint(childIndex, joint.children.back());
            }
        };

        // Build from roots
        rawSkeleton.roots.resize(rootBones.size());
        for (size_t i = 0; i < rootBones.size(); ++i) {
            buildJoint(rootBones[i], rawSkeleton.roots[i]);
        }

        if (!rawSkeleton.Validate()) {
            std::cerr << "[AnimationSystem] Raw skeleton validation failed\n";
            return false;
        }

        ozz::animation::offline::SkeletonBuilder builder;
        auto skeletonPtr = builder(rawSkeleton);
        if (!skeletonPtr) {
            std::cerr << "[AnimationSystem] Skeleton build failed\n";
            return false;
        }
        skeleton = std::move(*skeletonPtr);

        // Allocate buffers
        int numSoaJoints = (skeleton.num_joints() + 3) / 4;
        localTransforms.resize(numSoaJoints);
        modelMatrices.resize(skeleton.num_joints());

        // Compute bind pose
        ozz::animation::LocalToModelJob ltmJob;
        ltmJob.skeleton = &skeleton;
        ltmJob.input = skeleton.joint_rest_poses();
        ltmJob.output = ozz::make_span(modelMatrices);

        if (!ltmJob.Run()) {
            std::cerr << "[AnimationSystem] Failed to compute bind pose\n";
            return false;
        }

        // Compute inverse bind pose
        inverseBindPose.resize(skel.bones.size());
        for (size_t i = 0; i < skel.bones.size(); ++i) {
            int jointIndex = boneToJoint[i];
            if (jointIndex >= 0 && jointIndex < skeleton.num_joints()) {
                glm::mat4 bindPose = ozzToGlm(modelMatrices[jointIndex]);
                inverseBindPose[i] = glm::inverse(bindPose);
            } else {
                inverseBindPose[i] = glm::mat4(1.0f);
            }
        }

        samplingContext.Resize(skeleton.num_joints());
        currentBoneMatrices.resize(skel.bones.size(), glm::mat4(1.0f));

        std::cout << "[AnimationSystem] Built skeleton with " << skeleton.num_joints() << " joints\n";
        return true;
    }

    bool buildAnimation(const AnimationClip& clip, const Skeleton& skel, int originalIndex) {
        if (skeleton.num_joints() == 0) {
            std::cerr << "[AnimationSystem] Cannot build animation without skeleton\n";
            return false;
        }

        ozz::animation::offline::RawAnimation rawAnim;
        rawAnim.duration = clip.duration;
        rawAnim.name = clip.name;
        rawAnim.tracks.resize(skeleton.num_joints());

        // Map joint names to indices
        std::unordered_map<std::string, int> jointNameToIndex;
        for (int i = 0; i < skeleton.num_joints(); ++i) {
            jointNameToIndex[skeleton.joint_names()[i]] = i;
        }

        // Fill tracks from animation channels
        for (const auto& channel : clip.channels) {
            auto it = jointNameToIndex.find(channel.boneName);
            if (it == jointNameToIndex.end()) continue;

            int trackIndex = it->second;
            auto& track = rawAnim.tracks[trackIndex];

            for (const auto& key : channel.positionKeys) {
                ozz::animation::offline::RawAnimation::TranslationKey tk;
                tk.time = std::min(key.time, clip.duration);
                tk.value = ozz::math::Float3(key.value.x, key.value.y, key.value.z);
                track.translations.push_back(tk);
            }

            for (const auto& key : channel.rotationKeys) {
                ozz::animation::offline::RawAnimation::RotationKey rk;
                rk.time = std::min(key.time, clip.duration);
                rk.value = ozz::math::Quaternion(key.value.x, key.value.y, key.value.z, key.value.w);
                track.rotations.push_back(rk);
            }

            for (const auto& key : channel.scaleKeys) {
                ozz::animation::offline::RawAnimation::ScaleKey sk;
                sk.time = std::min(key.time, clip.duration);
                sk.value = ozz::math::Float3(key.value.x, key.value.y, key.value.z);
                track.scales.push_back(sk);
            }
        }

        // Fill empty tracks with bind pose
        for (int i = 0; i < skeleton.num_joints(); ++i) {
            auto& track = rawAnim.tracks[i];
            const ozz::math::SoaTransform& soaBind = skeleton.joint_rest_poses()[i / 4];
            int lane = i % 4;

            float txArr[4], tyArr[4], tzArr[4];
            float rxArr[4], ryArr[4], rzArr[4], rwArr[4];
            float sxArr[4], syArr[4], szArr[4];

            ozz::math::StorePtrU(soaBind.translation.x, txArr);
            ozz::math::StorePtrU(soaBind.translation.y, tyArr);
            ozz::math::StorePtrU(soaBind.translation.z, tzArr);
            ozz::math::StorePtrU(soaBind.rotation.x, rxArr);
            ozz::math::StorePtrU(soaBind.rotation.y, ryArr);
            ozz::math::StorePtrU(soaBind.rotation.z, rzArr);
            ozz::math::StorePtrU(soaBind.rotation.w, rwArr);
            ozz::math::StorePtrU(soaBind.scale.x, sxArr);
            ozz::math::StorePtrU(soaBind.scale.y, syArr);
            ozz::math::StorePtrU(soaBind.scale.z, szArr);

            if (track.translations.empty()) {
                ozz::animation::offline::RawAnimation::TranslationKey tk;
                tk.time = 0.0f;
                tk.value = ozz::math::Float3(txArr[lane], tyArr[lane], tzArr[lane]);
                track.translations.push_back(tk);
            }
            if (track.rotations.empty()) {
                ozz::animation::offline::RawAnimation::RotationKey rk;
                rk.time = 0.0f;
                rk.value = ozz::math::Quaternion(rxArr[lane], ryArr[lane], rzArr[lane], rwArr[lane]);
                track.rotations.push_back(rk);
            }
            if (track.scales.empty()) {
                ozz::animation::offline::RawAnimation::ScaleKey sk;
                sk.time = 0.0f;
                sk.value = ozz::math::Float3(sxArr[lane], syArr[lane], szArr[lane]);
                track.scales.push_back(sk);
            }
        }

        // Ensure mapping is large enough
        if (originalIndex >= 0) {
            while (originalToOzzIndex.size() <= static_cast<size_t>(originalIndex)) {
                originalToOzzIndex.push_back(-1);
            }
        }

        if (!rawAnim.Validate()) {
            std::cerr << "[AnimationSystem] Animation validation failed: " << clip.name << "\n";
            return false;
        }

        ozz::animation::offline::AnimationBuilder animBuilder;
        auto animPtr = animBuilder(rawAnim);
        if (!animPtr) {
            std::cerr << "[AnimationSystem] Animation build failed: " << clip.name << "\n";
            return false;
        }

        int ozzIndex = static_cast<int>(animations.size());
        if (originalIndex >= 0) {
            originalToOzzIndex[originalIndex] = ozzIndex;
        }

        animations.push_back(std::move(*animPtr));
        animationNames.push_back(clip.name);
        animationDurations.push_back(clip.duration);

        std::cout << "[AnimationSystem] Built animation: " << clip.name
                  << " (" << clip.duration << "s)\n";
        return true;
    }

    void sample(size_t animIndex, float time, std::vector<glm::mat4>& outMatrices) {
        if (animIndex >= animations.size() || skeleton.num_joints() == 0) {
            getBindPose(outMatrices);
            return;
        }

        const auto& animation = animations[animIndex];
        float duration = animation.duration();
        float ratio = (duration > 0.0f) ? std::fmod(time, duration) / duration : 0.0f;
        ratio = ozz::math::Clamp(0.0f, ratio, 1.0f);

        ozz::animation::SamplingJob samplingJob;
        samplingJob.animation = &animation;
        samplingJob.context = &samplingContext;
        samplingJob.ratio = ratio;
        samplingJob.output = ozz::make_span(localTransforms);

        if (!samplingJob.Run()) {
            getBindPose(outMatrices);
            return;
        }

        ozz::animation::LocalToModelJob ltmJob;
        ltmJob.skeleton = &skeleton;
        ltmJob.input = ozz::make_span(localTransforms);
        ltmJob.output = ozz::make_span(modelMatrices);

        if (!ltmJob.Run()) {
            getBindPose(outMatrices);
            return;
        }

        int numBones = static_cast<int>(boneToJoint.size());
        outMatrices.resize(numBones);

        for (int boneIndex = 0; boneIndex < numBones; ++boneIndex) {
            int jointIndex = boneToJoint[boneIndex];
            if (jointIndex >= 0 && jointIndex < skeleton.num_joints()) {
                glm::mat4 modelMatrix = ozzToGlm(modelMatrices[jointIndex]);
                outMatrices[boneIndex] = modelMatrix * inverseBindPose[boneIndex];
            } else {
                outMatrices[boneIndex] = glm::mat4(1.0f);
            }
        }
    }

    void getBindPose(std::vector<glm::mat4>& outMatrices) {
        if (skeleton.num_joints() == 0) {
            outMatrices.clear();
            return;
        }

        ozz::animation::LocalToModelJob ltmJob;
        ltmJob.skeleton = &skeleton;
        ltmJob.input = skeleton.joint_rest_poses();
        ltmJob.output = ozz::make_span(modelMatrices);

        if (!ltmJob.Run()) {
            int numBones = static_cast<int>(boneToJoint.size());
            outMatrices.resize(numBones, glm::mat4(1.0f));
            return;
        }

        int numBones = static_cast<int>(boneToJoint.size());
        outMatrices.resize(numBones);

        for (int boneIndex = 0; boneIndex < numBones; ++boneIndex) {
            int jointIndex = boneToJoint[boneIndex];
            if (jointIndex >= 0 && jointIndex < skeleton.num_joints()) {
                glm::mat4 modelMatrix = ozzToGlm(modelMatrices[jointIndex]);
                outMatrices[boneIndex] = modelMatrix * inverseBindPose[boneIndex];
            } else {
                outMatrices[boneIndex] = glm::mat4(1.0f);
            }
        }
    }
};

AnimationSystem::AnimationSystem() : impl_(std::make_unique<Impl>()) {}
AnimationSystem::~AnimationSystem() = default;
AnimationSystem::AnimationSystem(AnimationSystem&&) noexcept = default;
AnimationSystem& AnimationSystem::operator=(AnimationSystem&&) noexcept = default;

bool AnimationSystem::init(const Skeleton& skeleton, const std::vector<AnimationClip>& animations) {
    if (!impl_->buildSkeleton(skeleton)) {
        return false;
    }

    for (size_t i = 0; i < animations.size(); ++i) {
        impl_->buildAnimation(animations[i], skeleton, static_cast<int>(i));
    }

    // Start with bind pose
    impl_->getBindPose(impl_->currentBoneMatrices);

    return true;
}

bool AnimationSystem::valid() const {
    return impl_->skeleton.num_joints() > 0;
}

size_t AnimationSystem::animationCount() const {
    return impl_->animations.size();
}

std::string AnimationSystem::animationName(size_t index) const {
    if (index < impl_->animationNames.size()) {
        return impl_->animationNames[index];
    }
    return "";
}

float AnimationSystem::animationDuration(size_t index) const {
    if (index < impl_->animationDurations.size()) {
        return impl_->animationDurations[index];
    }
    return 0.0f;
}

void AnimationSystem::playAnimation(size_t index, bool loop) {
    if (index < impl_->animations.size()) {
        impl_->currentAnimIndex = static_cast<int>(index);
        impl_->currentTime = 0.0f;
        impl_->playing = true;
        impl_->paused = false;
        impl_->looping = loop;
    }
}

void AnimationSystem::stop() {
    impl_->playing = false;
    impl_->currentAnimIndex = -1;
    impl_->currentTime = 0.0f;
    impl_->getBindPose(impl_->currentBoneMatrices);
}

void AnimationSystem::pause() {
    impl_->paused = true;
}

void AnimationSystem::resume() {
    impl_->paused = false;
}

bool AnimationSystem::isPaused() const {
    return impl_->paused;
}

bool AnimationSystem::isPlaying() const {
    return impl_->playing && !impl_->paused;
}

int AnimationSystem::currentAnimation() const {
    return impl_->currentAnimIndex;
}

float AnimationSystem::currentTime() const {
    return impl_->currentTime;
}

void AnimationSystem::setSpeed(float speed) {
    impl_->playbackSpeed = speed;
}

float AnimationSystem::speed() const {
    return impl_->playbackSpeed;
}

void AnimationSystem::update(float deltaTime) {
    if (!impl_->playing || impl_->paused || impl_->currentAnimIndex < 0) {
        return;
    }

    impl_->currentTime += deltaTime * impl_->playbackSpeed;

    float duration = animationDuration(impl_->currentAnimIndex);
    if (duration > 0.0f) {
        if (impl_->looping) {
            impl_->currentTime = std::fmod(impl_->currentTime, duration);
        } else if (impl_->currentTime >= duration) {
            impl_->currentTime = duration;
            impl_->playing = false;
        }
    }

    impl_->sample(impl_->currentAnimIndex, impl_->currentTime, impl_->currentBoneMatrices);
}

const std::vector<glm::mat4>& AnimationSystem::getBoneMatrices() const {
    return impl_->currentBoneMatrices;
}

void AnimationSystem::sample(size_t animIndex, float time, std::vector<glm::mat4>& outMatrices) {
    impl_->sample(animIndex, time, outMatrices);
}

void AnimationSystem::getBindPose(std::vector<glm::mat4>& outMatrices) {
    impl_->getBindPose(outMatrices);
}

} // namespace vivid::models
