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
    float toneVal = static_cast<float>(tone);
    float freq = 1000.0f + toneVal * 9000.0f;
    m_toneFilterL.setLowpassCutoff(freq);
    m_toneFilterR.setLowpassCutoff(freq);
    m_cachedTone = toneVal;
}

float Overdrive::saturate(float sample) {
    // Soft clipping using tanh
    // Higher drive = more harmonics
    float driveVal = static_cast<float>(drive);
    return std::tanh(sample * driveVal) / std::tanh(driveVal);
}

void Overdrive::processEffect(const float* input, float* output, uint32_t frames) {
    // Update tone filter if changed
    float toneVal = static_cast<float>(tone);
    if (toneVal != m_cachedTone) {
        updateToneFilter();
    }

    float levelVal = static_cast<float>(level);

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
        float outL = filteredL * (1.0f - toneVal * 0.3f) + satL * (toneVal * 0.3f);
        float outR = filteredR * (1.0f - toneVal * 0.3f) + satR * (toneVal * 0.3f);

        // Apply output level
        output[i * 2] = outL * levelVal;
        output[i * 2 + 1] = outR * levelVal;
    }
}

void Overdrive::cleanupEffect() {
    m_toneFilterL.reset();
    m_toneFilterR.reset();
}

} // namespace vivid::audio
