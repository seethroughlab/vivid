#include <vivid/audio/limiter.h>
#include <vivid/context.h>
#include <cmath>
#include <algorithm>

namespace vivid::audio {

void Limiter::initEffect(Context& ctx) {
    // Very fast attack for brick-wall limiting
    float releaseMs = static_cast<float>(release);
    m_envelope.init(AUDIO_SAMPLE_RATE, 0.1f, releaseMs, dsp::EnvelopeMode::Peak);
    m_cachedRelease = releaseMs;
    m_initialized = true;
}

void Limiter::processEffect(const float* input, float* output, uint32_t frames) {
    // Update release if changed
    float releaseMs = static_cast<float>(release);
    if (releaseMs != m_cachedRelease) {
        m_envelope.setRelease(releaseMs);
        m_cachedRelease = releaseMs;
    }

    float ceilingDb = static_cast<float>(ceiling);
    float ceilingLinear = dsp::EnvelopeFollower::dbToLinear(ceilingDb);

    for (uint32_t i = 0; i < frames; ++i) {
        float inL = input[i * 2];
        float inR = input[i * 2 + 1];

        // Peak detection
        float peak = std::max(std::abs(inL), std::abs(inR));
        float envelope = m_envelope.process(peak);

        // Calculate required gain reduction
        float gain = 1.0f;
        if (envelope > ceilingLinear) {
            gain = ceilingLinear / envelope;
            m_currentGainReductionDb = dsp::EnvelopeFollower::linearToDb(gain);
        } else {
            m_currentGainReductionDb = 0.0f;
        }

        output[i * 2] = inL * gain;
        output[i * 2 + 1] = inR * gain;
    }
}

void Limiter::cleanupEffect() {
    m_envelope.reset();
    m_initialized = false;
}

} // namespace vivid::audio
