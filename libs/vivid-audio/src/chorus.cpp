#include <vivid/audio/chorus.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::audio {

void Chorus::initEffect(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;

    // Max delay: base + max depth = 20 + 20 = 40ms
    uint32_t maxDelaySamples = (m_sampleRate * 50) / 1000;
    m_delayL.init(maxDelaySamples);
    m_delayR.init(maxDelaySamples);

    // Initialize LFOs with different phase offsets for each voice
    float rateHz = static_cast<float>(rate);
    for (int i = 0; i < 4; ++i) {
        m_lfoL[i].init(m_sampleRate, rateHz, dsp::LFOWaveform::Sine);
        m_lfoR[i].init(m_sampleRate, rateHz, dsp::LFOWaveform::Sine);

        // Offset phases for stereo spread
        // Left and right have slight phase difference per voice
        float phaseL = static_cast<float>(i) / 4.0f;
        float phaseR = phaseL + 0.25f;  // 90 degree offset for stereo
    }
}

void Chorus::processEffect(const float* input, float* output, uint32_t frames) {
    // Update LFO rates in case param changed
    float rateHz = static_cast<float>(rate);
    for (int i = 0; i < 4; ++i) {
        m_lfoL[i].setFrequency(rateHz);
        m_lfoR[i].setFrequency(rateHz);
    }

    float baseDelaySamples = (BASE_DELAY_MS * static_cast<float>(m_sampleRate)) / 1000.0f;
    float depthSamples = (static_cast<float>(depth) * static_cast<float>(m_sampleRate)) / 1000.0f;
    int numVoices = static_cast<int>(voices);

    for (uint32_t i = 0; i < frames; ++i) {
        float inL = input[i * 2];
        float inR = input[i * 2 + 1];

        // Write input to delay lines
        m_delayL.write(inL);
        m_delayR.write(inR);

        // Sum all voices
        float outL = 0.0f;
        float outR = 0.0f;

        for (int v = 0; v < numVoices; ++v) {
            // Get LFO values (range -1 to 1)
            float lfoL = m_lfoL[v].process();
            float lfoR = m_lfoR[v].process();

            // Calculate modulated delay time
            float delayL = baseDelaySamples + lfoL * depthSamples;
            float delayR = baseDelaySamples + lfoR * depthSamples;

            // Read from delay lines with interpolation
            outL += m_delayL.readInterpolated(delayL);
            outR += m_delayR.readInterpolated(delayR);
        }

        // Normalize by number of voices
        float voiceScale = 1.0f / static_cast<float>(numVoices);
        output[i * 2] = outL * voiceScale;
        output[i * 2 + 1] = outR * voiceScale;
    }
}

void Chorus::cleanupEffect() {
    m_delayL.clear();
    m_delayR.clear();
    for (int i = 0; i < 4; ++i) {
        m_lfoL[i].reset();
        m_lfoR[i].reset();
    }
}

} // namespace vivid::audio
