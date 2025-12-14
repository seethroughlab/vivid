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
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> morph{"morph", 0.0f, 0.0f, 1.0f};            ///< Morph to next vowel
    Param<float> resonance{"resonance", 5.0f, 1.0f, 20.0f};   ///< Filter Q/resonance
    Param<float> f1{"f1", 800.0f, 100.0f, 5000.0f};           ///< First formant freq (custom)
    Param<float> f2{"f2", 1200.0f, 100.0f, 5000.0f};          ///< Second formant freq (custom)
    Param<float> f3{"f3", 2500.0f, 100.0f, 5000.0f};          ///< Third formant freq (custom)
    Param<float> mix{"mix", 1.0f, 0.0f, 1.0f};                ///< Dry/wet mix

    /// @}
    // -------------------------------------------------------------------------

    Formant() {
        registerParam(morph);
        registerParam(resonance);
        registerParam(f1);
        registerParam(f2);
        registerParam(f3);
        registerParam(mix);
    }
    ~Formant() override = default;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Set vowel preset
     * @param v Vowel preset (A, E, I, O, U, or Custom)
     */
    void setVowel(Vowel v) {
        m_vowel = v;
        m_needsUpdate = true;
    }

    /**
     * @brief Set formant amplitudes (relative levels)
     * @param a1 First formant amplitude
     * @param a2 Second formant amplitude
     * @param a3 Third formant amplitude
     */
    void setAmplitudes(float a1, float a2, float a3) {
        m_a1 = a1; m_a2 = a2; m_a3 = a3;
    }

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "Formant"; }

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
    void getFormantFreqs(Vowel v, float& outF1, float& outF2, float& outF3) const;

    // Vowel preset (enum, not a Param)
    Vowel m_vowel = Vowel::A;

    // Cached values for detecting changes
    float m_cachedMorph = 0.0f;
    float m_cachedResonance = 5.0f;
    float m_cachedF1 = 800.0f;
    float m_cachedF2 = 1200.0f;
    float m_cachedF3 = 2500.0f;

    // Formant amplitudes
    float m_a1 = 1.0f, m_a2 = 0.7f, m_a3 = 0.5f;

    // Three parallel bandpass filters
    BiquadBP m_filter1, m_filter2, m_filter3;

    uint32_t m_sampleRate = 48000;
    bool m_needsUpdate = true;
    bool m_initialized = false;
};

} // namespace vivid::audio
