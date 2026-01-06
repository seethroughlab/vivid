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

bool Limiter::drawVisualization(VizDrawList* dl, float minX, float minY, float maxX, float maxY) {
    VizVec2 min(minX, minY);
    VizVec2 max(maxX, maxY);
    float width = maxX - minX;
    float height = maxY - minY;
    float cx = (minX + maxX) * 0.5f;

    // Dark red background
    dl->AddRectFilled(min, max, VIZ_COL32(45, 30, 30, 255), 4.0f);

    float grDb = m_currentGainReductionDb;
    float grNorm = std::min(1.0f, std::abs(grDb) / 12.0f);  // Up to -12dB

    // Draw ceiling line (horizontal, near top)
    float ceilingY = min.y + height * 0.15f;
    dl->AddLine(VizVec2(min.x + 4, ceilingY), VizVec2(max.x - 4, ceilingY),
        VIZ_COL32(255, 80, 80, 200), 2.0f);

    // Draw gain reduction bar
    float barW = width * 0.4f;
    float barMaxH = height * 0.7f;
    float barH = barMaxH * grNorm;
    float barX = cx - barW * 0.5f;

    // Flash red when limiting hard
    uint32_t grColor = VIZ_COL32(200 + (int)(55 * grNorm), 80, 80, 255);

    dl->AddRectFilled(
        VizVec2(barX, min.y + height * 0.2f),
        VizVec2(barX + barW, min.y + height * 0.2f + barH),
        grColor, 2.0f);

    // "LIMIT" indicator when active
    if (grDb < -0.5f) {
        dl->AddText(VizVec2(cx - 15, max.y - 16),
            VIZ_COL32(255, 100, 100, 255), "LIM");
    }

    return true;
}

} // namespace vivid::audio
