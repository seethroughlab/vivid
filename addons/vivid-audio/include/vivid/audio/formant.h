#pragma once

/**
 * @file formant.h
 * @brief Formant filter for vocal synthesis
 *
 * Creates vowel-like sounds by applying parallel bandpass filters
 * at formant frequencies. Can morph smoothly between vowel presets.
 */

#include <vivid/audio/audio_effect.h>
#include <vivid/param.h>
#include <string>
#include <vector>
#include <cmath>
#include <array>

namespace vivid::audio {

/**
 * @brief Vowel presets for formant synthesis
 */
enum class Vowel {
    A,      ///< "ah" as in "father"
    E,      ///< "eh" as in "bed"
    I,      ///< "ee" as in "feet"
    O,      ///< "oh" as in "boat"
    U,      ///< "oo" as in "boot"
    Custom  ///< User-defined formants
};

/**
 * @brief Formant filter for vocal/vowel synthesis
 *
 * Applies parallel bandpass filters at formant frequencies to create
 * vowel-like timbres. Best used with harmonically rich input sources
 * like sawtooth or pulse waves.
 *
 * Each vowel has 3 formants (F1, F2, F3) at characteristic frequencies:
 * - A (ah): 800, 1200, 2500 Hz
 * - E (eh): 400, 2000, 2600 Hz
 * - I (ee): 300, 2300, 3000 Hz
 * - O (oh): 500, 800, 2500 Hz
 * - U (oo): 350, 600, 2400 Hz
 *
 * @par Parameters
 * | Name | Type | Range | Default | Description |
 * |------|------|-------|---------|-------------|
 * | vowel | int | 0-5 | 0 | Vowel preset (A=0, E=1, I=2, O=3, U=4, Custom=5) |
 * | morph | float | 0-1 | 0 | Morph to next vowel (wraps A->E->I->O->U->A) |
 * | resonance | float | 1-20 | 5 | Filter Q/resonance |
 * | mix | float | 0-1 | 1 | Dry/wet mix |
 * | f1-f3 | float | 100-5000 | varies | Custom formant frequencies |
 *
 * @par Example - Basic vowel filter
 * @code
 * chain.add<Oscillator>("osc")
 *     .waveform(Waveform::Saw)
 *     .frequency(110.0f);
 *
 * chain.add<Formant>("formant")
 *     .input("osc")
 *     .vowel(Vowel::A)
 *     .resonance(8.0f);
 * @endcode
 *
 * @par Example - Morphing between vowels
 * @code
 * chain.add<LFO>("morph_lfo").frequency(0.2f);
 * chain.add<Formant>("formant")
 *     .input("osc")
 *     .vowel(Vowel::A)
 *     .morphInput("morph_lfo");
 * @endcode
 */
class Formant : public AudioEffect {
public:
    Formant() = default;
    ~Formant() override = default;

    // -------------------------------------------------------------------------
    /// @name Fluent API
    /// @{

    /**
     * @brief Set vowel preset
     * @param v Vowel preset (A, E, I, O, U, or Custom)
     */
    Formant& vowel(Vowel v) {
        m_vowel = v;
        m_needsUpdate = true;
        return *this;
    }

    /**
     * @brief Set morph amount to next vowel
     * @param m Morph amount (0-1, 0=current vowel, 1=next vowel)
     */
    Formant& morph(float m) { m_morph = m; m_needsUpdate = true; return *this; }

    /**
     * @brief Set filter resonance/Q
     * @param q Q value (1-20, higher = more resonant)
     */
    Formant& resonance(float q) { m_resonance = q; m_needsUpdate = true; return *this; }

    // Override base class methods to return Formant&
    Formant& input(const std::string& name) { AudioEffect::input(name); return *this; }
    Formant& mix(float amount) { AudioEffect::mix(amount); return *this; }
    Formant& bypass(bool b) { AudioEffect::bypass(b); return *this; }

    /**
     * @brief Set first formant frequency (custom mode)
     * @param hz Frequency in Hz
     */
    Formant& f1(float hz) { m_f1 = hz; m_needsUpdate = true; return *this; }

    /**
     * @brief Set second formant frequency (custom mode)
     * @param hz Frequency in Hz
     */
    Formant& f2(float hz) { m_f2 = hz; m_needsUpdate = true; return *this; }

    /**
     * @brief Set third formant frequency (custom mode)
     * @param hz Frequency in Hz
     */
    Formant& f3(float hz) { m_f3 = hz; m_needsUpdate = true; return *this; }

    /**
     * @brief Set formant amplitudes (relative levels)
     * @param a1 First formant amplitude
     * @param a2 Second formant amplitude
     * @param a3 Third formant amplitude
     */
    Formant& amplitudes(float a1, float a2, float a3) {
        m_a1 = a1; m_a2 = a2; m_a3 = a3;
        return *this;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Formant"; }

    std::vector<ParamDecl> params() override {
        return {
            {"vowel", ParamType::Int, 0.0f, 5.0f, {static_cast<float>(m_vowel), 0, 0, 0}},
            m_morph.decl(), m_resonance.decl(),
            {"mix", ParamType::Float, 0.0f, 1.0f, {getMix(), 0, 0, 0}},
            m_f1.decl(), m_f2.decl(), m_f3.decl()
        };
    }

    bool getParam(const std::string& name, float out[4]) override {
        if (name == "vowel") { out[0] = static_cast<float>(m_vowel); return true; }
        if (name == "morph") { out[0] = m_morph; return true; }
        if (name == "resonance") { out[0] = m_resonance; return true; }
        if (name == "mix") { out[0] = getMix(); return true; }
        if (name == "f1") { out[0] = m_f1; return true; }
        if (name == "f2") { out[0] = m_f2; return true; }
        if (name == "f3") { out[0] = m_f3; return true; }
        return false;
    }

    bool setParam(const std::string& name, const float value[4]) override {
        if (name == "vowel") { m_vowel = static_cast<Vowel>(static_cast<int>(value[0])); m_needsUpdate = true; return true; }
        if (name == "morph") { m_morph = value[0]; m_needsUpdate = true; return true; }
        if (name == "resonance") { m_resonance = value[0]; m_needsUpdate = true; return true; }
        if (name == "mix") { mix(value[0]); return true; }
        if (name == "f1") { m_f1 = value[0]; m_needsUpdate = true; return true; }
        if (name == "f2") { m_f2 = value[0]; m_needsUpdate = true; return true; }
        if (name == "f3") { m_f3 = value[0]; m_needsUpdate = true; return true; }
        return false;
    }

    /// @}

protected:
    // AudioEffect overrides
    void initEffect(Context& ctx) override;
    void processEffect(const float* input, float* output, uint32_t frames) override;
    void cleanupEffect() override;

private:
    // Biquad bandpass filter state
    struct BiquadBP {
        float b0 = 0, b1 = 0, b2 = 0;
        float a1 = 0, a2 = 0;
        float x1[2] = {0, 0}, x2[2] = {0, 0};
        float y1[2] = {0, 0}, y2[2] = {0, 0};

        void setParams(float freq, float q, uint32_t sampleRate);
        float process(float in, int channel);
        void reset();
    };

    void updateFilters();
    void getFormantFreqs(Vowel v, float& f1, float& f2, float& f3) const;

    // Parameters
    Vowel m_vowel = Vowel::A;
    Param<float> m_morph{"morph", 0.0f, 0.0f, 1.0f};
    Param<float> m_resonance{"resonance", 5.0f, 1.0f, 20.0f};

    // Custom formant frequencies
    Param<float> m_f1{"f1", 800.0f, 100.0f, 5000.0f};
    Param<float> m_f2{"f2", 1200.0f, 100.0f, 5000.0f};
    Param<float> m_f3{"f3", 2500.0f, 100.0f, 5000.0f};

    // Formant amplitudes
    float m_a1 = 1.0f, m_a2 = 0.7f, m_a3 = 0.5f;

    // Three parallel bandpass filters
    BiquadBP m_filter1, m_filter2, m_filter3;

    uint32_t m_sampleRate = 48000;
    bool m_needsUpdate = true;
    bool m_initialized = false;
};

} // namespace vivid::audio
