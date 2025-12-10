#pragma once

/**
 * @file compressor.h
 * @brief Dynamic range compressor
 *
 * Reduces the dynamic range of audio by attenuating
 * loud signals that exceed a threshold.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/audio/dsp/envelope.h>
#include <vivid/param.h>

namespace vivid::audio {

/**
 * @brief Dynamic range compressor
 *
 * Reduces the dynamic range by reducing the gain of
 * signals that exceed a threshold.
 *
 * @par Parameters
 * - `threshold(dB)` - Level above which compression starts (-60 to 0)
 * - `ratio(r)` - Compression ratio (1=no compression, 20=hard limiting)
 * - `attack(ms)` - Attack time (0.1-100ms)
 * - `release(ms)` - Release time (10-1000ms)
 * - `makeupGain(dB)` - Output gain boost to compensate for compression
 * - `mix(m)` - Dry/wet mix (0-1)
 *
 * @par Example
 * @code
 * chain.add<Compressor>("comp")
 *     .input("audio")
 *     .threshold(-12)     // Compress above -12 dB
 *     .ratio(4)           // 4:1 compression
 *     .attack(10)         // 10ms attack
 *     .release(100)       // 100ms release
 *     .makeupGain(6);     // +6dB makeup gain
 * @endcode
 */
class Compressor : public AudioEffect {
public:
    Compressor() = default;
    ~Compressor() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    Compressor& threshold(float dB) {
        m_thresholdDb = std::max(-60.0f, std::min(0.0f, dB));
        return *this;
    }

    Compressor& ratio(float r) {
        m_ratio = std::max(1.0f, std::min(20.0f, r));
        return *this;
    }

    Compressor& attack(float ms) {
        m_attackMs = std::max(0.1f, std::min(100.0f, ms));
        if (m_initialized) {
            m_envelope.setAttack(m_attackMs);
        }
        return *this;
    }

    Compressor& release(float ms) {
        m_releaseMs = std::max(10.0f, std::min(1000.0f, ms));
        if (m_initialized) {
            m_envelope.setRelease(m_releaseMs);
        }
        return *this;
    }

    Compressor& makeupGain(float dB) {
        m_makeupGainDb = std::max(-20.0f, std::min(40.0f, dB));
        m_makeupGainLinear = dsp::EnvelopeFollower::dbToLinear(m_makeupGainDb);
        return *this;
    }

    Compressor& knee(float dB) {
        m_kneeDb = std::max(0.0f, std::min(12.0f, dB));
        return *this;
    }

    // Override base class methods to return Compressor&
    Compressor& input(const std::string& name) { AudioEffect::input(name); return *this; }
    Compressor& mix(float amount) { AudioEffect::mix(amount); return *this; }
    Compressor& bypass(bool b) { AudioEffect::bypass(b); return *this; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name State Queries
    /// @{

    float getThreshold() const { return m_thresholdDb; }
    float getRatio() const { return m_ratio; }
    float getAttack() const { return m_attackMs; }
    float getRelease() const { return m_releaseMs; }
    float getMakeupGain() const { return m_makeupGainDb; }
    float getGainReduction() const { return m_currentGainReductionDb; }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Compressor"; }

    std::vector<ParamDecl> params() override {
        return { m_thresholdParam.decl(), m_ratioParam.decl(), m_attackParam.decl(),
                 m_releaseParam.decl(), m_makeupParam.decl(), m_mixParam.decl() };
    }

    bool getParam(const std::string& pname, float out[4]) override {
        if (pname == "threshold") { out[0] = m_thresholdDb; return true; }
        if (pname == "ratio") { out[0] = m_ratio; return true; }
        if (pname == "attack") { out[0] = m_attackMs; return true; }
        if (pname == "release") { out[0] = m_releaseMs; return true; }
        if (pname == "makeupGain") { out[0] = m_makeupGainDb; return true; }
        if (pname == "mix") { out[0] = m_mix; return true; }
        return false;
    }

    bool setParam(const std::string& pname, const float value[4]) override {
        if (pname == "threshold") { threshold(value[0]); return true; }
        if (pname == "ratio") { ratio(value[0]); return true; }
        if (pname == "attack") { attack(value[0]); return true; }
        if (pname == "release") { release(value[0]); return true; }
        if (pname == "makeupGain") { makeupGain(value[0]); return true; }
        if (pname == "mix") { mix(value[0]); return true; }
        return false;
    }

    /// @}

protected:
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;
    void cleanupEffect() override;

private:
    float computeGain(float inputDb);

    // Parameters (raw values used in processing)
    float m_thresholdDb = -12.0f;
    float m_ratio = 4.0f;
    float m_attackMs = 10.0f;
    float m_releaseMs = 100.0f;
    float m_makeupGainDb = 0.0f;
    float m_makeupGainLinear = 1.0f;
    float m_kneeDb = 0.0f;

    // Parameter declarations for UI
    Param<float> m_thresholdParam{"threshold", -12.0f, -60.0f, 0.0f};
    Param<float> m_ratioParam{"ratio", 4.0f, 1.0f, 20.0f};
    Param<float> m_attackParam{"attack", 10.0f, 0.1f, 100.0f};
    Param<float> m_releaseParam{"release", 100.0f, 10.0f, 1000.0f};
    Param<float> m_makeupParam{"makeupGain", 0.0f, -20.0f, 40.0f};
    Param<float> m_mixParam{"mix", 1.0f, 0.0f, 1.0f};

    // State
    dsp::EnvelopeFollower m_envelope;
    float m_currentGainReductionDb = 0.0f;
    bool m_initialized = false;
};

} // namespace vivid::audio
