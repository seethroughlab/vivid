#include <vivid/audio/flanger.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::audio {

void Flanger::initEffect(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;

    // Max delay: 10ms + some headroom
    uint32_t maxDelaySamples = (m_sampleRate * 20) / 1000;
    m_delayL.init(maxDelaySamples);
    m_delayR.init(maxDelaySamples);

    m_lfoL.init(m_sampleRate, m_rateHz, dsp::LFOWaveform::Sine);
    m_lfoR.init(m_sampleRate, m_rateHz, dsp::LFOWaveform::Sine);

    m_feedbackL = 0.0f;
    m_feedbackR = 0.0f;
}

void Flanger::processEffect(const float* input, float* output, uint32_t frames) {
    float minDelaySamples = (MIN_DELAY_MS * static_cast<float>(m_sampleRate)) / 1000.0f;
    float maxDelaySamples = (MAX_DELAY_MS * static_cast<float>(m_sampleRate)) / 1000.0f;
    float delayRange = maxDelaySamples - minDelaySamples;

    for (uint32_t i = 0; i < frames; ++i) {
        float inL = input[i * 2];
        float inR = input[i * 2 + 1];

        // Write input + feedback to delay lines
        m_delayL.write(inL + m_feedbackL * m_feedback);
        m_delayR.write(inR + m_feedbackR * m_feedback);

        // Get LFO values (range 0 to 1)
        float lfoL = (m_lfoL.process() + 1.0f) * 0.5f;
        float lfoR = (m_lfoR.process() + 1.0f) * 0.5f;

        // Calculate modulated delay time
        float delayL = minDelaySamples + lfoL * delayRange * m_depth;
        float delayR = minDelaySamples + lfoR * delayRange * m_depth;

        // Read from delay lines
        float delayedL = m_delayL.readInterpolated(delayL);
        float delayedR = m_delayR.readInterpolated(delayR);

        // Store for next iteration's feedback
        m_feedbackL = delayedL;
        m_feedbackR = delayedR;

        // Output is the delayed signal
        output[i * 2] = delayedL;
        output[i * 2 + 1] = delayedR;
    }
}

void Flanger::cleanupEffect() {
    m_delayL.clear();
    m_delayR.clear();
    m_lfoL.reset();
    m_lfoR.reset();
    m_feedbackL = 0.0f;
    m_feedbackR = 0.0f;
}

} // namespace vivid::audio
