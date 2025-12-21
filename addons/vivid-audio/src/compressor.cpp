#include <vivid/audio/compressor.h>
#include <vivid/context.h>
#include <imgui.h>
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

bool Compressor::drawVisualization(ImDrawList* dl, float minX, float minY, float maxX, float maxY) {
    ImVec2 min(minX, minY);
    ImVec2 max(maxX, maxY);
    float width = maxX - minX;
    float height = maxY - minY;
    float cx = (minX + maxX) * 0.5f;

    // Warm brown background
    dl->AddRectFilled(min, max, IM_COL32(40, 35, 30, 255), 4.0f);

    float grDb = m_currentGainReductionDb;  // Negative dB value
    float grNorm = std::min(1.0f, std::abs(grDb) / 20.0f);  // Normalize to 0-1 for up to -20dB

    // Draw threshold line (horizontal)
    float threshNorm = (static_cast<float>(threshold) + 60.0f) / 60.0f;
    float threshY = min.y + height * 0.1f + (height * 0.8f) * (1.0f - threshNorm);
    dl->AddLine(ImVec2(min.x + 4, threshY), ImVec2(max.x - 4, threshY),
        IM_COL32(255, 200, 100, 150), 1.0f);

    // Draw gain reduction bar (vertical, from top down)
    float barW = width * 0.3f;
    float barMaxH = height * 0.8f;
    float barH = barMaxH * grNorm;
    float barX = cx - barW * 0.5f;

    // Color: green when no reduction, orange/red when compressing
    int r = (int)(100 + 155 * grNorm);
    int g = (int)(200 - 100 * grNorm);
    ImU32 grColor = IM_COL32(r, g, 50, 255);

    dl->AddRectFilled(
        ImVec2(barX, min.y + height * 0.1f),
        ImVec2(barX + barW, min.y + height * 0.1f + barH),
        grColor, 2.0f);

    // GR label
    if (grDb < -0.5f) {
        char grLabel[16];
        snprintf(grLabel, sizeof(grLabel), "%.1f", grDb);
        ImVec2 textSize = ImGui::CalcTextSize(grLabel);
        dl->AddText(ImVec2(cx - textSize.x * 0.5f, max.y - 18),
            IM_COL32(255, 180, 100, 255), grLabel);
    }

    return true;
}

} // namespace vivid::audio
