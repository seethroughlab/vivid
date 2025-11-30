#pragma once

#include <vivid/animation.h>
#include <glm/glm.hpp>
#include <vector>
#include <memory>
#include <string>

namespace vivid::models {

/**
 * @brief High-performance skeletal animation system based on ozz-animation.
 *
 * This class handles runtime animation sampling and bone matrix computation
 * using SIMD-optimized operations from the ozz-animation library.
 *
 * Usage:
 *   AnimationSystem anim;
 *   anim.init(skeleton, animations);
 *   anim.playAnimation(0, true);  // Play first animation, looping
 *
 *   // In update loop:
 *   anim.update(deltaTime);
 *   auto matrices = anim.getBoneMatrices();
 *   ctx.renderSkinned3D(mesh, camera, transform, output, matrices);
 */
class AnimationSystem {
public:
    AnimationSystem();
    ~AnimationSystem();

    // Non-copyable (due to internal ozz types)
    AnimationSystem(const AnimationSystem&) = delete;
    AnimationSystem& operator=(const AnimationSystem&) = delete;
    AnimationSystem(AnimationSystem&&) noexcept;
    AnimationSystem& operator=(AnimationSystem&&) noexcept;

    /**
     * @brief Initialize with skeleton and animations.
     * @param skeleton The skeleton hierarchy.
     * @param animations Available animation clips.
     * @return true if initialization succeeded.
     */
    bool init(const Skeleton& skeleton, const std::vector<AnimationClip>& animations);

    /**
     * @brief Check if system is initialized and valid.
     */
    bool valid() const;

    /**
     * @brief Get number of available animations.
     */
    size_t animationCount() const;

    /**
     * @brief Get animation name by index.
     */
    std::string animationName(size_t index) const;

    /**
     * @brief Get animation duration by index (in seconds).
     */
    float animationDuration(size_t index) const;

    /**
     * @brief Start playing an animation.
     * @param index Animation index.
     * @param loop Whether to loop the animation.
     */
    void playAnimation(size_t index, bool loop = true);

    /**
     * @brief Stop current animation.
     */
    void stop();

    /**
     * @brief Pause/resume current animation.
     */
    void pause();
    void resume();
    bool isPaused() const;
    bool isPlaying() const;

    /**
     * @brief Get current animation index (-1 if none).
     */
    int currentAnimation() const;

    /**
     * @brief Get current playback time in seconds.
     */
    float currentTime() const;

    /**
     * @brief Set playback speed multiplier (1.0 = normal).
     */
    void setSpeed(float speed);
    float speed() const;

    /**
     * @brief Update animation state.
     * @param deltaTime Time since last update in seconds.
     */
    void update(float deltaTime);

    /**
     * @brief Get the current bone matrices for GPU skinning.
     *
     * These matrices are: modelSpaceTransform * inverseBindPose
     * Ready to be passed directly to the skinning shader.
     *
     * @return Vector of bone matrices indexed by bone ID.
     */
    const std::vector<glm::mat4>& getBoneMatrices() const;

    /**
     * @brief Sample a specific animation at a specific time.
     *
     * This bypasses the playback system for direct control.
     * Useful for blending or procedural animation.
     *
     * @param animIndex Animation to sample.
     * @param time Time in seconds.
     * @param outMatrices Output bone matrices.
     */
    void sample(size_t animIndex, float time, std::vector<glm::mat4>& outMatrices);

    /**
     * @brief Get bind pose matrices (for when no animation is playing).
     */
    void getBindPose(std::vector<glm::mat4>& outMatrices);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vivid::models
