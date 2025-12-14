#include <vivid/audio/delay.h>
#include <vivid/context.h>

namespace vivid::audio {

void Delay::initEffect(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;

    // Max delay: 2000ms = 2 seconds
    uint32_t maxDelaySamples = (m_sampleRate * 2000) / 1000;
    m_delayLine.init(maxDelaySamples);

    updateDelaySamples();
}

void Delay::updateDelaySamples() {
    m_delaySamples = static_cast<uint32_t>(
        (static_cast<float>(delayTime) * static_cast<float>(m_sampleRate)) / 1000.0f
    );
}

void Delay::processEffect(const float* input, float* output, uint32_t frames) {
    // Update delay samples in case param changed
    updateDelaySamples();

    float fb = static_cast<float>(feedback);

    for (uint32_t i = 0; i < frames; ++i) {
        float inL = input[i * 2];
        float inR = input[i * 2 + 1];

        // Read delayed samples
        float delayedL, delayedR;
        m_delayLine.read(m_delaySamples, delayedL, delayedR);

        // Simple DC blocking on the feedback path to prevent DC buildup
        // High-pass filter: y[n] = x[n] - x[n-1] + 0.995 * y[n-1]
        float dcBlockedL = delayedL - m_prevDelayL + 0.995f * m_dcBlockL;
        float dcBlockedR = delayedR - m_prevDelayR + 0.995f * m_dcBlockR;
        m_prevDelayL = delayedL;
        m_prevDelayR = delayedR;
        m_dcBlockL = dcBlockedL;
        m_dcBlockR = dcBlockedR;

        // Write input + feedback to delay line (using DC-blocked feedback)
        m_delayLine.write(
            inL + dcBlockedL * fb,
            inR + dcBlockedR * fb
        );

        // Output is the delayed signal
        output[i * 2] = delayedL;
        output[i * 2 + 1] = delayedR;
    }
}

void Delay::cleanupEffect() {
    m_delayLine.clear();
    m_prevDelayL = 0.0f;
    m_prevDelayR = 0.0f;
    m_dcBlockL = 0.0f;
    m_dcBlockR = 0.0f;
}

} // namespace vivid::audio
