#include <vivid/audio/formant.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::audio {

// Formant frequencies for each vowel (F1, F2, F3 in Hz)
// Based on typical male voice formants
static constexpr float FORMANT_FREQS[5][3] = {
    {800.0f, 1200.0f, 2500.0f},   // A (ah) - "father"
    {400.0f, 2000.0f, 2600.0f},   // E (eh) - "bed"
    {300.0f, 2300.0f, 3000.0f},   // I (ee) - "feet"
    {500.0f,  800.0f, 2500.0f},   // O (oh) - "boat"
    {350.0f,  600.0f, 2400.0f}    // U (oo) - "boot"
};

void Formant::BiquadBP::setParams(float freq, float q, uint32_t sampleRate) {
    // Clamp frequency to valid range
    freq = std::max(20.0f, std::min(freq, sampleRate * 0.49f));

    float omega = 2.0f * 3.14159265358979f * freq / sampleRate;
    float sinOmega = std::sin(omega);
    float cosOmega = std::cos(omega);
    float alpha = sinOmega / (2.0f * q);

    // Bandpass filter coefficients (constant skirt gain, peak gain = Q)
    float a0 = 1.0f + alpha;
    b0 = alpha / a0;
    b1 = 0.0f;
    b2 = -alpha / a0;
    a1 = -2.0f * cosOmega / a0;
    a2 = (1.0f - alpha) / a0;
}

float Formant::BiquadBP::process(float in, int channel) {
    // Direct Form II Transposed
    float out = b0 * in + x1[channel];
    x1[channel] = b1 * in - a1 * out + x2[channel];
    x2[channel] = b2 * in - a2 * out;
    return out;
}

void Formant::BiquadBP::reset() {
    x1[0] = x1[1] = 0.0f;
    x2[0] = x2[1] = 0.0f;
    y1[0] = y1[1] = 0.0f;
    y2[0] = y2[1] = 0.0f;
}

void Formant::getFormantFreqs(Vowel v, float& outF1, float& outF2, float& outF3) const {
    if (v == Vowel::Custom) {
        outF1 = static_cast<float>(f1);
        outF2 = static_cast<float>(f2);
        outF3 = static_cast<float>(f3);
        return;
    }

    int idx = static_cast<int>(v);
    if (idx >= 0 && idx < 5) {
        outF1 = FORMANT_FREQS[idx][0];
        outF2 = FORMANT_FREQS[idx][1];
        outF3 = FORMANT_FREQS[idx][2];
    }
}

void Formant::updateFilters() {
    float formantF1, formantF2, formantF3;
    getFormantFreqs(m_vowel, formantF1, formantF2, formantF3);

    // If morphing, interpolate to next vowel
    float morphAmt = static_cast<float>(morph);
    if (morphAmt > 0.0f && m_vowel != Vowel::Custom) {
        // Get next vowel in cycle (A->E->I->O->U->A)
        int currentIdx = static_cast<int>(m_vowel);
        int nextIdx = (currentIdx + 1) % 5;

        float nf1 = FORMANT_FREQS[nextIdx][0];
        float nf2 = FORMANT_FREQS[nextIdx][1];
        float nf3 = FORMANT_FREQS[nextIdx][2];

        // Linear interpolation
        formantF1 = formantF1 + morphAmt * (nf1 - formantF1);
        formantF2 = formantF2 + morphAmt * (nf2 - formantF2);
        formantF3 = formantF3 + morphAmt * (nf3 - formantF3);
    }

    float q = static_cast<float>(resonance);
    m_filter1.setParams(formantF1, q, m_sampleRate);
    m_filter2.setParams(formantF2, q, m_sampleRate);
    m_filter3.setParams(formantF3, q, m_sampleRate);
}

void Formant::initEffect(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;

    // Reset filter states
    m_filter1.reset();
    m_filter2.reset();
    m_filter3.reset();

    m_needsUpdate = true;
    m_initialized = true;
}

void Formant::processEffect(const float* input, float* output, uint32_t frames) {
    if (!m_initialized) return;

    // Check if params changed
    float morphVal = static_cast<float>(morph);
    float resVal = static_cast<float>(resonance);
    float f1Val = static_cast<float>(f1);
    float f2Val = static_cast<float>(f2);
    float f3Val = static_cast<float>(f3);
    if (morphVal != m_cachedMorph || resVal != m_cachedResonance ||
        f1Val != m_cachedF1 || f2Val != m_cachedF2 || f3Val != m_cachedF3) {
        m_cachedMorph = morphVal;
        m_cachedResonance = resVal;
        m_cachedF1 = f1Val;
        m_cachedF2 = f2Val;
        m_cachedF3 = f3Val;
        m_needsUpdate = true;
    }

    if (m_needsUpdate) {
        updateFilters();
        m_needsUpdate = false;
    }

    for (uint32_t i = 0; i < frames; ++i) {
        for (int ch = 0; ch < 2; ++ch) {
            float sample = input[i * 2 + ch];

            // Process through three parallel bandpass filters
            float f1Out = m_filter1.process(sample, ch) * m_a1;
            float f2Out = m_filter2.process(sample, ch) * m_a2;
            float f3Out = m_filter3.process(sample, ch) * m_a3;

            // Sum the formant bands
            output[i * 2 + ch] = f1Out + f2Out + f3Out;
        }
    }
}

void Formant::cleanupEffect() {
    m_initialized = false;
}

} // namespace vivid::audio
