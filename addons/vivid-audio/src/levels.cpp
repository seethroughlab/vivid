#include <vivid/audio/levels.h>
#include <imgui.h>
#include <cmath>
#include <algorithm>

namespace vivid::audio {

void Levels::analyze(const float* input, uint32_t frames, uint32_t channels) {
    if (frames == 0) return;

    float sumSqLeft = 0.0f;
    float sumSqRight = 0.0f;
    float peakLeft = 0.0f;
    float peakRight = 0.0f;

    if (channels == 2) {
        // Stereo: analyze left and right separately
        for (uint32_t i = 0; i < frames; i++) {
            float left = input[i * 2];
            float right = input[i * 2 + 1];

            sumSqLeft += left * left;
            sumSqRight += right * right;

            peakLeft = std::max(peakLeft, std::abs(left));
            peakRight = std::max(peakRight, std::abs(right));
        }
    } else {
        // Mono: use same value for both channels
        for (uint32_t i = 0; i < frames; i++) {
            float sample = input[i];
            sumSqLeft += sample * sample;
            peakLeft = std::max(peakLeft, std::abs(sample));
        }
        sumSqRight = sumSqLeft;
        peakRight = peakLeft;
    }

    // Calculate RMS
    float rmsLeft = std::sqrt(sumSqLeft / frames);
    float rmsRight = std::sqrt(sumSqRight / frames);
    float rmsTotal = std::sqrt((sumSqLeft + sumSqRight) / (frames * 2));
    float peakTotal = std::max(peakLeft, peakRight);

    // Apply smoothing
    float smooth = static_cast<float>(smoothing);
    float attack = 1.0f - smooth;

    m_rmsLeft = m_rmsLeft * smooth + rmsLeft * attack;
    m_rmsRight = m_rmsRight * smooth + rmsRight * attack;
    m_rms = m_rms * smooth + rmsTotal * attack;

    // Peak has faster attack, slower release
    if (peakTotal > m_peak) {
        m_peak = peakTotal;  // Instant attack
    } else {
        m_peak = m_peak * smooth + peakTotal * attack;  // Smooth release
    }
}

float Levels::rmsDb() const {
    if (m_rms <= 0.0f) return -100.0f;
    return 20.0f * std::log10(m_rms);
}

float Levels::peakDb() const {
    if (m_peak <= 0.0f) return -100.0f;
    return 20.0f * std::log10(m_peak);
}

bool Levels::drawVisualization(ImDrawList* dl, float minX, float minY, float maxX, float maxY) {
    ImVec2 min(minX, minY);
    ImVec2 max(maxX, maxY);
    float width = maxX - minX;
    float height = maxY - minY;
    float startX = minX + 4.0f;
    float startY = minY + 4.0f;
    float h = height - 8.0f;
    float w = width - 8.0f;

    // Dark purple background
    dl->AddRectFilled(min, max, IM_COL32(40, 30, 50, 255), 4.0f);

    float rmsVal = m_rms;
    float peakVal = m_peak;

    float barWidth = w * 0.35f;
    float gap = w * 0.1f;
    float leftX = startX + w * 0.1f;
    float rightX = leftX + barWidth + gap;

    // Draw RMS bar (left) with gradient
    float rmsHeight = rmsVal * h;
    for (int i = 0; i < static_cast<int>(rmsHeight); i++) {
        float t = static_cast<float>(i) / h;
        ImU32 col;
        if (t < 0.5f) col = IM_COL32(80, 180, 80, 255);       // Green
        else if (t < 0.8f) col = IM_COL32(200, 180, 60, 255); // Yellow
        else col = IM_COL32(200, 80, 80, 255);                 // Red
        float y = max.y - 4.0f - i;
        dl->AddLine(ImVec2(leftX, y), ImVec2(leftX + barWidth, y), col);
    }
    dl->AddRect(ImVec2(leftX, startY), ImVec2(leftX + barWidth, max.y - 4.0f),
               IM_COL32(80, 80, 100, 150), 2.0f);

    // Draw Peak bar (right)
    float peakHeight = peakVal * h;
    for (int i = 0; i < static_cast<int>(peakHeight); i++) {
        float t = static_cast<float>(i) / h;
        ImU32 col;
        if (t < 0.5f) col = IM_COL32(80, 180, 80, 255);
        else if (t < 0.8f) col = IM_COL32(200, 180, 60, 255);
        else col = IM_COL32(200, 80, 80, 255);
        float y = max.y - 4.0f - i;
        dl->AddLine(ImVec2(rightX, y), ImVec2(rightX + barWidth, y), col);
    }
    dl->AddRect(ImVec2(rightX, startY), ImVec2(rightX + barWidth, max.y - 4.0f),
               IM_COL32(80, 80, 100, 150), 2.0f);

    return true;
}

} // namespace vivid::audio
