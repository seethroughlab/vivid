#include <vivid/audio/gate.h>
#include <vivid/context.h>

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
    VizVec2 min(minX, minY);
    VizVec2 max(maxX, maxY);
    float width = maxX - minX;
    float height = maxY - minY;
    float cx = (minX + maxX) * 0.5f;
    float cy = (minY + maxY) * 0.5f;

    // Dark green background
    dl->AddRectFilled(min, max, VIZ_COL32(30, 40, 35, 255), 4.0f);

    // Draw gate visualization (door/barrier metaphor)
    float gateW = width * 0.6f;
    float gateH = height * 0.5f;
    float gateX = cx - gateW * 0.5f;
    float gateY = cy - gateH * 0.5f;

    // Gate frame
    uint32_t frameColor = VIZ_COL32(100, 120, 100, 200);
    dl->AddRect(VizVec2(gateX, gateY), VizVec2(gateX + gateW, gateY + gateH),
        frameColor, 2.0f, 0, 1.5f);

    // Gate "bars" that open/close based on gate gain
    float openAmount = m_gateGain;  // 0 = closed, 1 = open
    int numBars = 5;
    float barSpacing = gateW / (numBars + 1);

    for (int i = 0; i < numBars; i++) {
        float bX = gateX + barSpacing * (i + 1);
        float barTop = gateY + 4 + (gateH - 8) * (1.0f - openAmount) * 0.5f;
        float barBot = gateY + gateH - 4 - (gateH - 8) * (1.0f - openAmount) * 0.5f;

        uint32_t barColor = m_gateOpen
            ? VIZ_COL32(100, 200, 120, 255)   // Green when open
            : VIZ_COL32(150, 100, 100, 200);  // Red-ish when closed

        dl->AddLine(VizVec2(bX, barTop), VizVec2(bX, barBot), barColor, 2.0f);
    }

    // Status text
    const char* status = m_gateOpen ? "OPEN" : "GATE";
    VizTextSize textSize = dl->CalcTextSize(status);
    uint32_t textColor = m_gateOpen ? VIZ_COL32(100, 255, 120, 255) : VIZ_COL32(180, 100, 100, 200);
    dl->AddText(VizVec2(cx - textSize.x * 0.5f, max.y - 16), textColor, status);

    return true;
}

} // namespace vivid::audio
