#pragma once

/**
 * @file audio_param.h
 * @brief Thread-safe parameter for audio operators with optional smoothing
 *
 * AudioParam is a drop-in replacement for Param<float> in audio operators.
 * It uses std::atomic<float> for thread-safe main-thread writes and provides
 * one-pole smoothing to prevent clicks when parameters change.
 *
 * Usage patterns:
 * - Block-rate smoothing: Call advance(frameCount) at top of generateBlock(),
 *   then read the smoothed value once for the block.
 * - Per-sample smoothing: Call tick() inside the sample loop for frequency-
 *   sensitive parameters (e.g., oscillator frequency).
 * - No smoothing: Set smoothTimeMs = 0 or call snap().
 */

#include <vivid/operator.h>
#include <atomic>
#include <cmath>
#include <string>

namespace vivid {

/**
 * @brief Thread-safe audio parameter with one-pole smoothing
 *
 * Main thread writes target via operator=. Audio thread reads smoothed
 * value via operator float() / tick() / advance().
 *
 * @par Example
 * @code
 * class MyOsc : public AudioOperator {
 *     AudioParam frequency{"frequency", 440.0f, 20.0f, 20000.0f};
 *     AudioParam volume{"volume", 0.5f, 0.0f, 1.0f};
 *
 *     MyOsc() {
 *         registerAudioParam(frequency);
 *         registerAudioParam(volume);
 *     }
 *
 *     void generateBlock(uint32_t frameCount) override {
 *         volume.advance(frameCount);   // Block-rate smooth
 *         float vol = volume;
 *         for (uint32_t i = 0; i < frameCount; ++i) {
 *             float freq = frequency.tick();  // Per-sample smooth
 *             // ... generate sample ...
 *         }
 *     }
 * };
 * @endcode
 */
class AudioParam {
public:
    /**
     * @brief Construct an audio parameter
     * @param name Display name for UI
     * @param defaultVal Default value
     * @param minVal Minimum allowed value
     * @param maxVal Maximum allowed value
     * @param smoothTimeMs Smoothing time in milliseconds (0 = no smoothing)
     */
    AudioParam(const char* name, float defaultVal, float minVal = 0.0f,
               float maxVal = 1.0f, float smoothTimeMs = 5.0f)
        : m_name(name)
        , m_target(defaultVal)
        , m_current(defaultVal)
        , m_min(minVal)
        , m_max(maxVal)
        , m_defaultVal(defaultVal)
    {
        setSmoothTime(smoothTimeMs);
    }

    // -------------------------------------------------------------------------
    /// @name Main Thread Interface (write target)
    /// @{

    /**
     * @brief Set target value (thread-safe, called from main thread)
     *
     * Sets the atomic target and also updates the current value so that
     * main-thread reads (e.g., introspection, tests) see the new value
     * immediately. The audio thread will smooth from this point.
     */
    AudioParam& operator=(float v) {
        m_target.store(v, std::memory_order_release);
        m_current = v;  // Snap for immediate main-thread readback
        if (m_owner) m_owner->markDirty();
        return *this;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Audio Thread Interface (read smoothed value)
    /// @{

    /**
     * @brief Get current smoothed value (audio thread)
     *
     * Returns the last smoothed value without advancing. Use tick() or
     * advance() to update the smoothed value first.
     */
    operator float() const { return m_current; }

    /**
     * @brief Get current smoothed value explicitly
     */
    float get() const { return m_current; }

    /**
     * @brief Get the target value (what the main thread set)
     */
    float target() const { return m_target.load(std::memory_order_acquire); }

    /**
     * @brief Advance smoothing by one sample (audio thread)
     * @return Current smoothed value after advancing
     *
     * Use inside the sample loop for per-sample smoothing:
     * @code
     * for (uint32_t i = 0; i < frameCount; ++i) {
     *     float freq = m_frequency.tick();
     *     // ... use freq ...
     * }
     * @endcode
     */
    float tick() {
        float t = m_target.load(std::memory_order_relaxed);
        m_current += m_coeff * (t - m_current);
        return m_current;
    }

    /**
     * @brief Advance smoothing by N samples (audio thread)
     * @param samples Number of samples to advance
     *
     * Approximates N steps of one-pole smoothing. Use at the top of
     * generateBlock() for block-rate smoothing:
     * @code
     * void generateBlock(uint32_t frameCount) override {
     *     m_volume.advance(frameCount);
     *     float vol = m_volume;  // Read once for the block
     *     // ...
     * }
     * @endcode
     */
    void advance(uint32_t samples) {
        float t = m_target.load(std::memory_order_relaxed);
        // Apply N steps of one-pole: current += coeff * (target - current)
        // Equivalent to: current = target + (current - target) * (1 - coeff)^N
        float retention = std::pow(1.0f - m_coeff, static_cast<float>(samples));
        m_current = t + (m_current - t) * retention;
    }

    /**
     * @brief Jump to target instantly (no smoothing)
     *
     * Call during init or reset to avoid initial ramp-up.
     */
    void snap() {
        m_current = m_target.load(std::memory_order_relaxed);
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set smoothing time
     * @param ms Smoothing time in milliseconds (0 = instant)
     * @param sampleRate Sample rate for coefficient calculation
     */
    void setSmoothTime(float ms, uint32_t sampleRate = 48000) {
        if (ms <= 0.0f) {
            m_coeff = 1.0f;  // Instant
        } else {
            // One-pole coefficient: reach ~63% of target in `ms` milliseconds
            float samples = ms * 0.001f * static_cast<float>(sampleRate);
            m_coeff = 1.0f - std::exp(-1.0f / samples);
        }
    }

    /// @brief Set owner operator (for dirty tracking)
    void setOwner(Operator* owner) { m_owner = owner; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Metadata (same API as Param<float>)
    /// @{

    const char* name() const { return m_name; }
    float min() const { return m_min; }
    float max() const { return m_max; }

    /**
     * @brief Generate ParamDecl for introspection
     */
    ParamDecl decl() const {
        ParamDecl d;
        d.name = m_name;
        d.type = ParamType::Float;
        d.minVal = m_min;
        d.maxVal = m_max;
        d.defaultVal[0] = m_defaultVal;
        return d;
    }

    /// @}

private:
    const char* m_name;
    std::atomic<float> m_target;  // Main thread writes (atomic)
    float m_current;              // Audio thread only (smoothed value)
    float m_coeff = 0.01f;       // One-pole smoothing coefficient
    float m_min, m_max;
    float m_defaultVal;
    Operator* m_owner = nullptr;
};

} // namespace vivid
