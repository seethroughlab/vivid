#include <vivid/audio/compressor.h>
#include <vivid/context.h>
#include <cmath>

namespace vivid::audio {

void Compressor::initEffect(Context& ctx) {
    m_envelope.init(AUDIO_SAMPLE_RATE, m_attackMs, m_releaseMs, dsp::EnvelopeMode::Peak);
    m_initialized = true;
}

float Compressor::computeGain(float inputDb) {
    // Below threshold: no compression
    if (inputDb < m_thresholdDb - m_kneeDb / 2.0f) {
        return 0.0f;  // 0 dB gain reduction
    }

    // Above threshold + knee: full compression
    if (inputDb > m_thresholdDb + m_kneeDb / 2.0f) {
        float overshoot = inputDb - m_thresholdDb;
        float gainReduction = overshoot * (1.0f - 1.0f / m_ratio);
        return -gainReduction;
    }

    // In the knee region: soft transition
    if (m_kneeDb > 0.0f) {
        float x = inputDb - m_thresholdDb + m_kneeDb / 2.0f;
        float kneeGain = (1.0f / m_ratio - 1.0f) * x * x / (2.0f * m_kneeDb);
        return kneeGain;
    }

    return 0.0f;
}

void Compressor::processEffect(const float* input, float* output, uint32_t frames) {
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
        float totalGainDb = gainDb + m_makeupGainDb;
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
