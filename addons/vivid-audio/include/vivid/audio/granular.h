#pragma once

/**
 * @file granular.h
 * @brief Granular synthesizer for textural and atmospheric sounds
 *
 * Creates clouds of tiny audio grains from a sample, enabling:
 * - Time stretching without pitch change
 * - Pitch shifting without time change
 * - Frozen textures and drones
 * - Atmospheric soundscapes
 */

#include <vivid/audio_operator.h>
#include <vivid/param.h>
#include <string>
#include <vector>
#include <array>
#include <cmath>
#include <random>

namespace vivid::audio {

/**
 * @brief Grain window/envelope shape
 */
enum class GrainWindow {
    Hann,       ///< Smooth cosine window (default, no clicks)
    Triangle,   ///< Linear fade in/out
    Rectangle,  ///< No fade (harsh, for effect)
    Gaussian,   ///< Bell curve (soft, diffuse)
    Tukey       ///< Flat middle with cosine edges
};

/**
 * @brief Granular synthesizer
 *
 * Splits audio into small grains (10-500ms) and recombines them with
 * randomization for unique textures. Great for ambient pads, time
 * stretching, freeze effects, and sound design.
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | grainSize | float | 10-500 | 100 | Grain length in ms |
 * | density | float | 1-100 | 10 | Grains per second |
 * | position | float | 0-1 | 0.5 | Playhead position in sample |
 * | positionSpray | float | 0-0.5 | 0.1 | Random position variation |
 * | pitch | float | 0.25-4 | 1.0 | Grain pitch multiplier |
 * | pitchSpray | float | 0-1 | 0.0 | Random pitch variation |
 * | panSpray | float | 0-1 | 0.0 | Random stereo spread |
 * | volume | float | 0-2 | 0.5 | Output volume |
 *
 * @par Example
 * @code
 * auto& grain = chain.add<Granular>("clouds");
 * grain.loadSample("assets/audio/texture.wav");
 * grain.grainSize = 80.0f;   // 80ms grains
 * grain.density = 15.0f;     // 15 grains/sec
 * grain.position = 0.3f;     // Start at 30% through sample
 * grain.positionSpray = 0.1f;
 * grain.pitch = 0.5f;        // Octave down
 * grain.freeze = true;       // Hold position, just spray
 * @endcode
 */
class Granular : public AudioOperator {
public:
    static constexpr int MAX_GRAINS = 64;

    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> grainSize{"grainSize", 100.0f, 10.0f, 500.0f};      ///< Grain size in ms
    Param<float> density{"density", 10.0f, 1.0f, 100.0f};             ///< Grains per second
    Param<float> position{"position", 0.5f, 0.0f, 1.0f};              ///< Playhead position
    Param<float> positionSpray{"positionSpray", 0.1f, 0.0f, 0.5f};    ///< Position randomization
    Param<float> pitch{"pitch", 1.0f, 0.25f, 4.0f};                   ///< Pitch multiplier
    Param<float> pitchSpray{"pitchSpray", 0.0f, 0.0f, 1.0f};          ///< Pitch randomization
    Param<float> panSpray{"panSpray", 0.0f, 0.0f, 1.0f};              ///< Stereo spread
    Param<float> volume{"volume", 0.5f, 0.0f, 2.0f};                  ///< Output volume

    /// @}
    // -------------------------------------------------------------------------

    Granular();
    ~Granular() override = default;

    // -------------------------------------------------------------------------
    /// @name Sample Loading
    /// @{

    /**
     * @brief Load sample from audio file
     * @param path Path to WAV file
     * @return true if loaded successfully
     */
    bool loadSample(const std::string& path);

    /**
     * @brief Load sample from existing buffer
     * @param samples Interleaved stereo samples
     * @param frameCount Number of frames
     */
    void loadBuffer(const float* samples, uint32_t frameCount);

    /**
     * @brief Check if sample is loaded
     */
    bool isLoaded() const { return !m_sample.empty(); }

    /**
     * @brief Get sample duration in seconds
     */
    float sampleDuration() const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Playback Control
    /// @{

    /**
     * @brief Set grain window shape
     */
    void setWindow(GrainWindow w) { m_window = w; }

    /**
     * @brief Enable/disable freeze mode
     *
     * When frozen, position doesn't advance automatically - only spray
     * randomizes the playhead. Great for drones and sustained textures.
     */
    void setFreeze(bool f) { m_freeze = f; }

    /**
     * @brief Check if frozen
     */
    bool isFrozen() const { return m_freeze; }

    /**
     * @brief Enable/disable auto-advance
     *
     * When enabled (and not frozen), position advances automatically
     * through the sample at 1x speed.
     */
    void setAutoAdvance(bool a) { m_autoAdvance = a; }

    /**
     * @brief Trigger a single grain manually
     */
    void triggerGrain();

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;
    std::string name() const override { return "Granular"; }

    void generateBlock(uint32_t frameCount) override;

    /// @}

private:
    // Grain state
    struct Grain {
        bool active = false;
        double samplePos = 0.0;     // Current position in sample (fractional)
        double pitch = 1.0;          // Playback rate
        float panL = 1.0f;           // Left channel gain
        float panR = 1.0f;           // Right channel gain
        uint32_t age = 0;            // Samples since grain start
        uint32_t duration = 0;       // Total grain duration in samples
    };

    // Sample buffer
    std::vector<float> m_sample;     // Interleaved stereo
    uint32_t m_sampleFrames = 0;
    std::string m_pendingPath;

    // Grain pool
    std::array<Grain, MAX_GRAINS> m_grains;
    uint32_t m_nextGrainIndex = 0;

    // Scheduling
    float m_grainTimer = 0.0f;       // Time until next grain
    float m_positionPhase = 0.5f;    // Current position for auto-advance

    // Settings
    GrainWindow m_window = GrainWindow::Hann;
    bool m_freeze = false;
    bool m_autoAdvance = false;

    // Random generator
    std::mt19937 m_rng;
    std::uniform_real_distribution<float> m_dist{0.0f, 1.0f};

    uint32_t m_sampleRate = 48000;

    // Helpers
    void spawnGrain();
    float windowFunction(float t) const;
    float sampleAt(double pos, int channel) const;
    float randomBipolar() { return m_dist(m_rng) * 2.0f - 1.0f; }
    float randomUnipolar() { return m_dist(m_rng); }

    // WAV loading
    bool loadWAV(const std::string& path);

    static constexpr float PI = 3.14159265358979323846f;
};

} // namespace vivid::audio
