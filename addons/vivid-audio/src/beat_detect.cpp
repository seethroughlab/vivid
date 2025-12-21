#include <vivid/audio/beat_detect.h>
#include <vivid/context.h>
#include <imgui.h>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace vivid::audio {

void BeatDetect::initAnalyzer(Context& ctx) {
    m_energyHistory.resize(HISTORY_SIZE, 0.0f);
    m_historyPos = 0;
    m_avgEnergy = 0.0f;
    m_holdTimer = 0.0f;
    m_lastFrameTime = 0.0f;
}

void BeatDetect::analyze(const float* input, uint32_t frames, uint32_t channels) {
    if (frames == 0) return;

    // Calculate RMS energy for this buffer
    float sumSq = 0.0f;
    uint32_t totalSamples = frames * channels;

    for (uint32_t i = 0; i < totalSamples; i++) {
        sumSq += input[i] * input[i];
    }

    m_rawEnergy = std::sqrt(sumSq / totalSamples);

    // Update energy history (circular buffer)
    m_energyHistory[m_historyPos] = m_rawEnergy;
    m_historyPos = (m_historyPos + 1) % HISTORY_SIZE;

    // Calculate average energy from history
    float sum = std::accumulate(m_energyHistory.begin(), m_energyHistory.end(), 0.0f);
    m_avgEnergy = sum / HISTORY_SIZE;

    // Calculate variance for adaptive threshold
    float variance = 0.0f;
    for (float e : m_energyHistory) {
        float diff = e - m_avgEnergy;
        variance += diff * diff;
    }
    variance /= HISTORY_SIZE;
    float stdDev = std::sqrt(variance);

    // Adaptive threshold: avg + sensitivity * stdDev
    // With minimum threshold to avoid false triggers on silence
    float sens = static_cast<float>(sensitivity);
    float threshold = m_avgEnergy + sens * stdDev;
    threshold = std::max(threshold, 0.01f);

    // Update hold timer (approximate frame time from audio)
    float frameTimeMs = (frames * 1000.0f) / 48000.0f;
    m_holdTimer = std::max(0.0f, m_holdTimer - frameTimeMs);
    m_timeSinceBeat += frameTimeMs / 1000.0f;

    // Beat detection
    m_beat = false;
    if (m_rawEnergy > threshold && m_holdTimer <= 0.0f) {
        m_beat = true;
        m_holdTimer = static_cast<float>(holdTime);
        m_timeSinceBeat = 0.0f;

        // Intensity based on how much we exceeded threshold
        m_intensity = std::min(1.0f, (m_rawEnergy - threshold) / threshold + 0.5f);
    }

    // Decay intensity
    float decayVal = static_cast<float>(decay);
    m_intensity *= decayVal;

    // Smooth energy with decay (for visual effects)
    if (m_rawEnergy > m_energy) {
        // Fast attack
        m_energy = m_rawEnergy;
    } else {
        // Smooth decay
        m_energy = m_energy * decayVal + m_rawEnergy * (1.0f - decayVal);
    }
}

bool BeatDetect::drawVisualization(ImDrawList* dl, float minX, float minY, float maxX, float maxY) {
    ImVec2 min(minX, minY);
    ImVec2 max(maxX, maxY);
    float width = maxX - minX;
    float height = maxY - minY;
    float cx = (minX + maxX) * 0.5f;
    float cy = (minY + maxY) * 0.5f;
    float maxRadius = std::min(width, height) * 0.4f;

    // Dark purple background
    dl->AddRectFilled(min, max, IM_COL32(40, 30, 50, 255), 4.0f);

    float energy_ = m_energy;
    float intensity_ = m_intensity;
    bool isBeat = m_beat;

    // Outer ring (intensity)
    float outerR = maxRadius * (0.6f + intensity_ * 0.4f);
    dl->AddCircle(ImVec2(cx, cy), outerR,
                 IM_COL32(100, 150, 200, static_cast<int>(100 + intensity_ * 155)), 24, 2.0f);

    // Inner circle (energy)
    float innerR = maxRadius * 0.3f * (0.5f + energy_ * 1.5f);
    innerR = std::min(innerR, outerR - 2.0f);

    // Flash white on beat
    ImU32 fillColor = isBeat ?
        IM_COL32(255, 255, 255, 255) :
        IM_COL32(80 + static_cast<int>(energy_ * 100),
                 120 + static_cast<int>(energy_ * 80),
                 180, 220);

    dl->AddCircleFilled(ImVec2(cx, cy), innerR, fillColor, 24);

    return true;
}

} // namespace vivid::audio
