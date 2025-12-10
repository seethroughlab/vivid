#pragma once

/**
 * @file reverb.h
 * @brief Freeverb-style algorithmic reverb
 *
 * Simulates the acoustic properties of a room using
 * parallel comb filters and series all-pass filters.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/audio/dsp/filters.h>
#include <vivid/param.h>
#include <array>

namespace vivid::audio {

/**
 * @brief Freeverb-style algorithmic reverb
 *
 * Creates a realistic room simulation using:
 * - 8 parallel comb filters (for density)
 * - 4 series all-pass filters (for diffusion)
 *
 * @par Parameters
 * - `roomSize(s)` - Room size (0-1, larger = longer tail)
 * - `damping(d)` - High frequency damping (0-1)
 * - `width(w)` - Stereo width (0-1)
 * - `mix(m)` - Dry/wet mix (0-1)
 *
 * @par Example
 * @code
 * chain.add<Reverb>("reverb")
 *     .input("audio")
 *     .roomSize(0.7f)    // Large room
 *     .damping(0.5f)     // Moderate damping
 *     .width(1.0f)       // Full stereo
 *     .mix(0.3f);        // 30% wet
 * @endcode
 */
class Reverb : public AudioEffect {
public:
    Reverb() = default;
    ~Reverb() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    Reverb& roomSize(float s) {
        m_roomSize = std::max(0.0f, std::min(1.0f, s));
        updateParameters();
        return *this;
    }

    Reverb& damping(float d) {
        m_damping = std::max(0.0f, std::min(1.0f, d));
        updateParameters();
        return *this;
    }

    Reverb& width(float w) {
        m_width = std::max(0.0f, std::min(1.0f, w));
        return *this;
    }

    // Override base class methods to return Reverb&
    Reverb& input(const std::string& name) { AudioEffect::input(name); return *this; }
    Reverb& mix(float amount) { AudioEffect::mix(amount); return *this; }
    Reverb& bypass(bool b) { AudioEffect::bypass(b); return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    float getRoomSize() const { return m_roomSize; }
    float getDamping() const { return m_damping; }
    float getWidth() const { return m_width; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Reverb"; }

    std::vector<ParamDecl> params() override {
        return { m_roomSizeParam.decl(), m_dampingParam.decl(), m_widthParam.decl(), m_mixParam.decl() };
    }

    bool getParam(const std::string& pname, float out[4]) override {
        if (pname == "roomSize") { out[0] = m_roomSize; return true; }
        if (pname == "damping") { out[0] = m_damping; return true; }
        if (pname == "width") { out[0] = m_width; return true; }
        if (pname == "mix") { out[0] = m_mix; return true; }
        return false;
    }

    bool setParam(const std::string& pname, const float value[4]) override {
        if (pname == "roomSize") { roomSize(value[0]); return true; }
        if (pname == "damping") { damping(value[0]); return true; }
        if (pname == "width") { width(value[0]); return true; }
        if (pname == "mix") { mix(value[0]); return true; }
        return false;
    }

    /// @}

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;
    void cleanupEffect() override;

private:
    void updateParameters();

    // Parameters (raw values used in processing)
    float m_roomSize = 0.5f;
    float m_damping = 0.5f;
    float m_width = 1.0f;

    // Parameter declarations for UI
    Param<float> m_roomSizeParam{"roomSize", 0.5f, 0.0f, 1.0f};
    Param<float> m_dampingParam{"damping", 0.5f, 0.0f, 1.0f};
    Param<float> m_widthParam{"width", 1.0f, 0.0f, 1.0f};
    Param<float> m_mixParam{"mix", 0.3f, 0.0f, 1.0f};

    // Freeverb constants (delay lengths in samples at 44.1kHz, scaled for 48kHz)
    static constexpr int NUM_COMBS = 8;
    static constexpr int NUM_ALLPASS = 4;

    // Comb filter delay times (scaled from 44.1kHz to 48kHz)
    static constexpr int COMB_DELAYS_L[NUM_COMBS] = {
        1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617
    };
    static constexpr int COMB_DELAYS_R[NUM_COMBS] = {
        1116 + 23, 1188 + 23, 1277 + 23, 1356 + 23,
        1422 + 23, 1491 + 23, 1557 + 23, 1617 + 23
    };

    // All-pass delay times
    static constexpr int ALLPASS_DELAYS[NUM_ALLPASS] = {
        556, 441, 341, 225
    };

    // DSP
    std::array<dsp::CombFilter, NUM_COMBS> m_combsL;
    std::array<dsp::CombFilter, NUM_COMBS> m_combsR;
    std::array<dsp::AllPassDelay, NUM_ALLPASS> m_allpassL;
    std::array<dsp::AllPassDelay, NUM_ALLPASS> m_allpassR;

    uint32_t m_sampleRate = 48000;
};

} // namespace vivid::audio
