#include <vivid/audio/overdrive.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::audio {

void Overdrive::initEffect(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    m_toneFilterL.initLowpass(m_sampleRate, 5000.0f);
    m_toneFilterR.initLowpass(m_sampleRate, 5000.0f);
    updateToneFilter();
}

void Overdrive::updateToneFilter() {
    // Map tone 0-1 to frequency 1000-10000 Hz
    float freq = 1000.0f + m_tone * 9000.0f;
    m_toneFilterL.setLowpassCutoff(freq);
    m_toneFilterR.setLowpassCutoff(freq);
}

float Overdrive::saturate(float sample) {
    // Soft clipping using tanh
    // Higher drive = more harmonics
    return std::tanh(sample * m_drive) / std::tanh(m_drive);
}

void Overdrive::processEffect(const float* input, float* output, uint32_t frames) {
    for (uint32_t i = 0; i < frames; ++i) {
        float inL = input[i * 2];
        float inR = input[i * 2 + 1];

        // Apply saturation
        float satL = saturate(inL);
        float satR = saturate(inR);

        // Apply tone filter
        float filteredL = m_toneFilterL.process(satL);
        float filteredR = m_toneFilterR.process(satR);

        // Mix filtered and unfiltered based on tone
        // Higher tone = more high frequencies
        float outL = filteredL * (1.0f - m_tone * 0.3f) + satL * (m_tone * 0.3f);
        float outR = filteredR * (1.0f - m_tone * 0.3f) + satR * (m_tone * 0.3f);

        // Apply output level
        output[i * 2] = outL * m_level;
        output[i * 2 + 1] = outR * m_level;
    }
}

void Overdrive::cleanupEffect() {
    m_toneFilterL.reset();
    m_toneFilterR.reset();
}

} // namespace vivid::audio
