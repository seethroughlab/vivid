#include <vivid/audio/compressor.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::audio {

void Compressor::initEffect(Context& ctx) {
    float attackMs = static_cast<float>(attack);
    float releaseMs = static_cast<float>(release);
    m_envelope.init(AUDIO_SAMPLE_RATE, attackMs, releaseMs, dsp::EnvelopeMode::Peak);
    m_cachedAttack = attackMs;
    m_cachedRelease = releaseMs;
    m_initialized = true;
}

float Compressor::computeGain(float inputDb) {
    float thresholdDb = static_cast<float>(threshold);
    float ratioVal = static_cast<float>(ratio);
    float kneeDb = static_cast<float>(knee);

    // Below threshold: no compression
    if (inputDb < thresholdDb - kneeDb / 2.0f) {
        return 0.0f;  // 0 dB gain reduction
    }

    // Above threshold + knee: full compression
    if (inputDb > thresholdDb + kneeDb / 2.0f) {
        float overshoot = inputDb - thresholdDb;
        float gainReduction = overshoot * (1.0f - 1.0f / ratioVal);
        return -gainReduction;
    }

    // In the knee region: soft transition
    if (kneeDb > 0.0f) {
        float x = inputDb - thresholdDb + kneeDb / 2.0f;
        float kneeGain = (1.0f / ratioVal - 1.0f) * x * x / (2.0f * kneeDb);
        return kneeGain;
    }

    return 0.0f;
}

void Compressor::processEffect(const float* input, float* output, uint32_t frames) {
    // Update envelope times if changed
    float attackMs = static_cast<float>(attack);
    float releaseMs = static_cast<float>(release);
    if (attackMs != m_cachedAttack) {
        m_envelope.setAttack(attackMs);
        m_cachedAttack = attackMs;
    }
    if (releaseMs != m_cachedRelease) {
        m_envelope.setRelease(releaseMs);
        m_cachedRelease = releaseMs;
    }

    float makeupDb = static_cast<float>(makeupGain);

    for (uint32_t i = 0; i < frames; ++i) {
        float inL = input[i * 2];
        float inR = input[i * 2 + 1];

        // Detect envelope (stereo-linked)
        float envelope = m_envelope.processStereo(inL, inR);
        float inputDb = dsp::EnvelopeFollower::linearToDb(envelope);

        // Compute gain reduction
        float gainDb = computeGain(inputDb);
        m_currentGainReductionDb = -gainDb;

        // Apply gain (including makeup gain)
        float totalGainDb = gainDb + makeupDb;
        float gain = dsp::EnvelopeFollower::dbToLinear(totalGainDb);

        output[i * 2] = inL * gain;
        output[i * 2 + 1] = inR * gain;
    }
}

void Compressor::cleanupEffect() {
    m_envelope.reset();
    m_initialized = false;
}

} // namespace vivid::audio
