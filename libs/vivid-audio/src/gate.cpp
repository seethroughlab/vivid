#include <vivid/audio/gate.h>
#include <vivid/context.h>
#include <vivid/viz_helpers.h>

#include <cmath>
#include <algorithm>

namespace vivid::audio {

void Gate::initEffect(Context& ctx) {
    m_sampleRate = AUDIO_SAMPLE_RATE;
    m_envelope.init(m_sampleRate, 0.1f, 10.0f, dsp::EnvelopeMode::Peak);
    float rangeLinear = dsp::EnvelopeFollower::dbToLinear(static_cast<float>(range));
    m_gateGain = rangeLinear;
    m_holdCounter = 0.0f;
    m_gateOpen = false;
}

void Gate::processEffect(const float* input, float* output, uint32_t frames) {
    float attackMs = static_cast<float>(attack);
    float releaseMs = static_cast<float>(release);
    float holdMs = static_cast<float>(hold);
    float thresholdLinear = dsp::EnvelopeFollower::dbToLinear(static_cast<float>(threshold));
    float rangeLinear = dsp::EnvelopeFollower::dbToLinear(static_cast<float>(range));

    // Coefficients for attack/release smoothing
    float attackCoef = std::exp(-1.0f / (attackMs * 0.001f * static_cast<float>(m_sampleRate)));
    float releaseCoef = std::exp(-1.0f / (releaseMs * 0.001f * static_cast<float>(m_sampleRate)));
    float holdSamples = holdMs * 0.001f * static_cast<float>(m_sampleRate);

    for (uint32_t i = 0; i < frames; ++i) {
        float inL = input[i * 2];
        float inR = input[i * 2 + 1];

        // Peak detection
        float peak = std::max(std::abs(inL), std::abs(inR));
        float envelope = m_envelope.process(peak);

        // Gate logic with hysteresis
        float targetGain;
        if (envelope > thresholdLinear) {
            // Above threshold: open gate
            m_gateOpen = true;
            m_holdCounter = holdSamples;
            targetGain = 1.0f;
        } else if (m_holdCounter > 0) {
            // In hold period
            m_holdCounter -= 1.0f;
            targetGain = 1.0f;
        } else {
            // Below threshold and hold expired: close gate
            m_gateOpen = false;
            targetGain = rangeLinear;
        }

        // Smooth gain changes
        if (targetGain > m_gateGain) {
            // Opening: use attack time
            m_gateGain = attackCoef * m_gateGain + (1.0f - attackCoef) * targetGain;
        } else {
            // Closing: use release time
            m_gateGain = releaseCoef * m_gateGain + (1.0f - releaseCoef) * targetGain;
        }

        output[i * 2] = inL * m_gateGain;
        output[i * 2 + 1] = inR * m_gateGain;
    }
}

void Gate::cleanupEffect() {
    m_envelope.reset();
    float rangeLinear = dsp::EnvelopeFollower::dbToLinear(static_cast<float>(range));
    m_gateGain = rangeLinear;
    m_holdCounter = 0.0f;
    m_gateOpen = false;
}

bool Gate::drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) {
    VizHelpers viz(dl);
    VizBounds bounds{minX, minY, maxX - minX, maxY - minY};

    viz.drawBackground(bounds, VIZ_COL32(30, 40, 35, 255));
    viz.drawGate(bounds.inset(4), m_gateOpen, m_gateGain);
    return true;
}

} // namespace vivid::audio
