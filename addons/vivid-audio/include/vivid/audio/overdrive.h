#pragma once

/**
 * @file overdrive.h
 * @brief Overdrive/saturation effect
 *
 * Adds harmonic distortion using soft clipping
 * for a warm, tube-like character.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/audio/dsp/filters.h>
#include <vivid/param.h>

namespace vivid::audio {

/**
 * @brief Overdrive/saturation effect
 *
 * Adds harmonic distortion using soft clipping (tanh waveshaping)
 * for a warm, musical distortion character.
 *
 * @par Parameters
 * - `drive(d)` - Drive amount (1-10, more = more distortion)
 * - `tone(t)` - Tone control (0-1, 0=dark, 1=bright)
 * - `level(l)` - Output level (0-2)
 * - `mix(m)` - Dry/wet mix (0-1)
 *
 * @par Example
 * @code
 * chain.add<Overdrive>("overdrive")
 *     .input("audio")
 *     .drive(3.0f)      // Medium drive
 *     .tone(0.6f)       // Slightly bright
 *     .level(0.8f)      // Reduce output level
 *     .mix(1.0f);       // Fully wet
 * @endcode
 */
class Overdrive : public AudioEffect {
public:
    Overdrive() = default;
    ~Overdrive() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    Overdrive& drive(float d) {
        m_drive = std::max(1.0f, std::min(10.0f, d));
        return *this;
    }

    Overdrive& tone(float t) {
        m_tone = std::max(0.0f, std::min(1.0f, t));
        updateToneFilter();
        return *this;
    }

    Overdrive& level(float l) {
        m_level = std::max(0.0f, std::min(2.0f, l));
        return *this;
    }

    // Override base class methods to return Overdrive&
    Overdrive& input(const std::string& name) { AudioEffect::input(name); return *this; }
    Overdrive& mix(float amount) { AudioEffect::mix(amount); return *this; }
    Overdrive& bypass(bool b) { AudioEffect::bypass(b); return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    float getDrive() const { return m_drive; }
    float getTone() const { return m_tone; }
    float getLevel() const { return m_level; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Overdrive"; }

    std::vector<ParamDecl> params() override {
        return { m_driveParam.decl(), m_toneParam.decl(), m_levelParam.decl(), m_mixParam.decl() };
    }

    bool getParam(const std::string& pname, float out[4]) override {
        if (pname == "drive") { out[0] = m_drive; return true; }
        if (pname == "tone") { out[0] = m_tone; return true; }
        if (pname == "level") { out[0] = m_level; return true; }
        if (pname == "mix") { out[0] = m_mix; return true; }
        return false;
    }

    bool setParam(const std::string& pname, const float value[4]) override {
        if (pname == "drive") { drive(value[0]); return true; }
        if (pname == "tone") { tone(value[0]); return true; }
        if (pname == "level") { level(value[0]); return true; }
        if (pname == "mix") { mix(value[0]); return true; }
        return false;
    }

    /// @}

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;
    void cleanupEffect() override;

private:
    void updateToneFilter();
    float saturate(float sample);

    // Parameters (raw values used in processing)
    float m_drive = 3.0f;
    float m_tone = 0.5f;
    float m_level = 0.8f;

    // Parameter declarations for UI
    Param<float> m_driveParam{"drive", 3.0f, 1.0f, 10.0f};
    Param<float> m_toneParam{"tone", 0.5f, 0.0f, 1.0f};
    Param<float> m_levelParam{"level", 0.8f, 0.0f, 2.0f};
    Param<float> m_mixParam{"mix", 1.0f, 0.0f, 1.0f};

    // DSP
    dsp::OnePoleFilter m_toneFilterL;
    dsp::OnePoleFilter m_toneFilterR;
    uint32_t m_sampleRate = 48000;
};

} // namespace vivid::audio
