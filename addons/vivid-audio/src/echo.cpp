#include <vivid/audio/echo.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::audio {

void Echo::initEffect(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;

    // Max delay: 2000ms * 8 taps = 16 seconds total
    uint32_t maxDelaySamples = (m_sampleRate * 2000 * 8) / 1000;
    m_delayLine.init(maxDelaySamples);
}

void Echo::processEffect(const float* input, float* output, uint32_t frames) {
    uint32_t baseSamples = static_cast<uint32_t>(
        (m_delayTimeMs * static_cast<float>(m_sampleRate)) / 1000.0f
    );

    for (uint32_t i = 0; i < frames; ++i) {
        float inL = input[i * 2];
        float inR = input[i * 2 + 1];

        // Write input to delay line
        m_delayLine.write(inL, inR);

        // Sum all taps
        float outL = 0.0f;
        float outR = 0.0f;
        float level = 1.0f;

        for (int tap = 1; tap <= m_taps; ++tap) {
            level *= m_decay;
            uint32_t delaySamples = baseSamples * static_cast<uint32_t>(tap);

            float tapL, tapR;
            m_delayLine.read(delaySamples, tapL, tapR);

            outL += tapL * level;
            outR += tapR * level;
        }

        output[i * 2] = outL;
        output[i * 2 + 1] = outR;
    }
}

void Echo::cleanupEffect() {
    m_delayLine.clear();
}

} // namespace vivid::audio
