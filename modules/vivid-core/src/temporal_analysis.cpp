// Temporal analysis implementation
// Multi-frame metrics: flicker, convergence, motion, diversity, loop detection, novelty

#include <vivid/temporal_analysis.h>
#include <cmath>
#include <algorithm>
#include <numeric>

namespace vivid {

void TemporalAnalyzer::downsampleToLuminance(const uint8_t* pixels, int width, int height,
                                              std::vector<float>& out) const {
    int dsW = m_config.downsampleWidth;
    int dsH = m_config.downsampleHeight;
    out.assign(dsW * dsH, 0.0f);
    std::vector<int> counts(dsW * dsH, 0);

    for (int y = 0; y < height; ++y) {
        int dsY = y * dsH / height;
        if (dsY >= dsH) dsY = dsH - 1;
        for (int x = 0; x < width; ++x) {
            int dsX = x * dsW / width;
            if (dsX >= dsW) dsX = dsW - 1;
            const uint8_t* p = pixels + (y * width + x) * 4;
            float lum = 0.2126f * (p[0] / 255.0f) + 0.7152f * (p[1] / 255.0f) + 0.0722f * (p[2] / 255.0f);
            int idx = dsY * dsW + dsX;
            out[idx] += lum;
            counts[idx]++;
        }
    }
    for (int i = 0; i < dsW * dsH; ++i) {
        if (counts[i] > 0) out[i] /= counts[i];
    }
}

void TemporalAnalyzer::pushFrame(const uint8_t* pixels, int width, int height) {
    int dsW = m_config.downsampleWidth;
    int dsH = m_config.downsampleHeight;
    int dsPixels = dsW * dsH;

    // Initialize ring buffers on first use
    if (m_luminanceRing.empty()) {
        m_luminanceRing.resize(m_config.windowSize);
        m_brightnessHistory.resize(m_config.brightnessHistorySize, 0.0f);
        m_deltaHistory.resize(m_config.windowSize, 0.0f);
        m_keyframes.resize(m_config.keyframeBufferSize);
        m_noveltyHistory.resize(m_config.windowSize, 0.0f);
    }

    // Downsample current frame
    std::vector<float> currentLum;
    downsampleToLuminance(pixels, width, height, currentLum);

    // Compute mean brightness
    float meanBrightness = 0.0f;
    for (int i = 0; i < dsPixels; ++i) meanBrightness += currentLum[i];
    meanBrightness /= dsPixels;

    // Compute frame delta against previous frame
    float frameDelta = 0.0f;
    if (m_frameCount > 0) {
        int prevIdx = (m_writeIndex + m_config.windowSize - 1) % m_config.windowSize;
        if (!m_luminanceRing[prevIdx].empty()) {
            double sumDelta = 0.0;
            for (int i = 0; i < dsPixels; ++i) {
                sumDelta += std::fabs(currentLum[i] - m_luminanceRing[prevIdx][i]);
            }
            frameDelta = static_cast<float>(sumDelta / dsPixels);
        }
    }

    // Store in ring buffers
    m_luminanceRing[m_writeIndex] = std::move(currentLum);
    m_writeIndex = (m_writeIndex + 1) % m_config.windowSize;

    m_brightnessHistory[m_brightnessWriteIndex] = meanBrightness;
    m_brightnessWriteIndex = (m_brightnessWriteIndex + 1) % m_config.brightnessHistorySize;

    m_deltaHistory[m_deltaWriteIndex] = frameDelta;
    m_deltaWriteIndex = (m_deltaWriteIndex + 1) % m_config.windowSize;

    // Update convergence tracking
    const float alpha = 0.1f;
    m_smoothDelta = alpha * frameDelta + (1.0f - alpha) * m_smoothDelta;
    if (m_smoothDelta < 0.005f) {
        m_convergedFrames++;
    } else {
        m_convergedFrames = 0;
    }

    // Keyframe sampling for novelty
    float currentNovelty = 0.0f;
    int keyframeCount = std::min(m_frameCount / std::max(1, m_config.keyframeInterval),
                                  m_config.keyframeBufferSize);
    if (keyframeCount > 0) {
        double totalDist = 0.0;
        int validKeyframes = 0;
        for (int k = 0; k < m_config.keyframeBufferSize; ++k) {
            if (m_keyframes[k].empty()) continue;
            double dist = 0.0;
            const auto& currentRef = m_luminanceRing[(m_writeIndex + m_config.windowSize - 1) % m_config.windowSize];
            for (int i = 0; i < dsPixels; ++i) {
                dist += std::fabs(currentRef[i] - m_keyframes[k][i]);
            }
            totalDist += dist / dsPixels;
            validKeyframes++;
        }
        if (validKeyframes > 0) {
            currentNovelty = static_cast<float>(totalDist / validKeyframes);
        }
    }
    m_noveltyHistory[m_noveltyWriteIndex] = currentNovelty;
    m_noveltyWriteIndex = (m_noveltyWriteIndex + 1) % m_config.windowSize;

    // Store keyframe at intervals
    if (m_config.keyframeInterval > 0 && m_frameCount % m_config.keyframeInterval == 0) {
        const auto& latestFrame = m_luminanceRing[(m_writeIndex + m_config.windowSize - 1) % m_config.windowSize];
        m_keyframes[m_keyframeWriteIndex] = latestFrame;
        m_keyframeWriteIndex = (m_keyframeWriteIndex + 1) % m_config.keyframeBufferSize;
    }

    m_frameCount++;
}

TemporalAnalysis TemporalAnalyzer::analyze() const {
    TemporalAnalysis result;

    if (m_frameCount < 2) return result;

    int dsW = m_config.downsampleWidth;
    int dsH = m_config.downsampleHeight;
    int dsPixels = dsW * dsH;

    // Get the number of valid entries in each ring buffer
    int validWindow = std::min(m_frameCount, m_config.windowSize);
    int validBrightness = std::min(m_frameCount, m_config.brightnessHistorySize);

    // --- Frame Delta (most recent) ---
    {
        int lastIdx = (m_deltaWriteIndex + m_config.windowSize - 1) % m_config.windowSize;
        result.frameDelta = m_deltaHistory[lastIdx];
    }

    // --- Convergence ---
    {
        result.convergenceScore = std::clamp(1.0f - m_smoothDelta * 100.0f, 0.0f, 1.0f);
        result.isConverged = m_convergedFrames >= 8;
    }

    // --- Motion Magnitude + Regional Motion ---
    if (validWindow >= 2) {
        int curIdx = (m_writeIndex + m_config.windowSize - 1) % m_config.windowSize;
        int prevIdx = (m_writeIndex + m_config.windowSize - 2) % m_config.windowSize;

        if (!m_luminanceRing[curIdx].empty() && !m_luminanceRing[prevIdx].empty()) {
            double totalMotion = 0.0;
            std::array<double, 9> regionMotionSum = {};
            std::array<int, 9> regionMotionCount = {};
            regionMotionSum.fill(0.0);
            regionMotionCount.fill(0);

            for (int y = 0; y < dsH; ++y) {
                int ry = y * 3 / dsH;
                if (ry > 2) ry = 2;
                for (int x = 0; x < dsW; ++x) {
                    int rx = x * 3 / dsW;
                    if (rx > 2) rx = 2;
                    int idx = y * dsW + x;
                    float delta = std::fabs(m_luminanceRing[curIdx][idx] - m_luminanceRing[prevIdx][idx]);
                    totalMotion += delta;
                    int region = ry * 3 + rx;
                    regionMotionSum[region] += delta;
                    regionMotionCount[region]++;
                }
            }
            result.motionMagnitude = static_cast<float>(totalMotion / dsPixels);
            for (int i = 0; i < 9; ++i) {
                result.regionMotion[i] = (regionMotionCount[i] > 0)
                    ? static_cast<float>(regionMotionSum[i] / regionMotionCount[i])
                    : 0.0f;
            }
        }
    }

    // --- Flicker Detection ---
    if (!m_sparseMode && validBrightness >= 3) {
        // Count oscillations in brightness history
        int oscillations = 0;
        int windowForFlicker = std::min(validBrightness, m_config.windowSize);

        std::vector<float> recentBrightness(windowForFlicker);
        for (int i = 0; i < windowForFlicker; ++i) {
            int idx = (m_brightnessWriteIndex - windowForFlicker + i + m_config.brightnessHistorySize) % m_config.brightnessHistorySize;
            recentBrightness[i] = m_brightnessHistory[idx];
        }

        for (int i = 1; i < windowForFlicker - 1; ++i) {
            float d1 = recentBrightness[i] - recentBrightness[i - 1];
            float d2 = recentBrightness[i + 1] - recentBrightness[i];
            if ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) {
                oscillations++;
            }
        }

        if (windowForFlicker > 2) {
            result.flickerScore = static_cast<float>(oscillations) / (windowForFlicker - 2);
        }

        // Flicker frequency via zero-crossings of brightness delta
        int zeroCrossings = 0;
        for (int i = 2; i < windowForFlicker; ++i) {
            float d1 = recentBrightness[i - 1] - recentBrightness[i - 2];
            float d2 = recentBrightness[i] - recentBrightness[i - 1];
            if ((d1 > 0 && d2 < 0) || (d1 < 0 && d2 > 0)) {
                zeroCrossings++;
            }
        }
        float windowDuration = static_cast<float>(windowForFlicker) / m_config.fps;
        if (windowDuration > 0) {
            result.flickerFrequency = static_cast<float>(zeroCrossings) / (2.0f * windowDuration);
        }
    }

    // --- Frame Diversity + Frozen Detection ---
    {
        int validDeltas = std::min(m_frameCount - 1, m_config.windowSize);
        if (validDeltas > 0) {
            double sumDelta = 0.0;
            double sumDeltaSq = 0.0;
            bool allFrozen = true;

            for (int i = 0; i < validDeltas; ++i) {
                int idx = (m_deltaWriteIndex - validDeltas + i + m_config.windowSize) % m_config.windowSize;
                float d = m_deltaHistory[idx];
                sumDelta += d;
                sumDeltaSq += d * d;
                if (d >= 0.001f) allFrozen = false;
            }

            double meanDelta = sumDelta / validDeltas;
            result.frameDiversity = static_cast<float>(sumDeltaSq / validDeltas - meanDelta * meanDelta);
            result.isFrozen = allFrozen;
        }
    }

    // --- Loop Detection (autocorrelation on brightness history) ---
    if (validBrightness >= 12) {
        int n = validBrightness;
        std::vector<float> bh(n);
        for (int i = 0; i < n; ++i) {
            int idx = (m_brightnessWriteIndex - n + i + m_config.brightnessHistorySize) % m_config.brightnessHistorySize;
            bh[i] = m_brightnessHistory[idx];
        }

        // Compute mean and variance
        double mean = 0.0;
        for (int i = 0; i < n; ++i) mean += bh[i];
        mean /= n;

        double variance = 0.0;
        for (int i = 0; i < n; ++i) {
            double d = bh[i] - mean;
            variance += d * d;
        }
        variance /= n;

        if (variance > 1e-10) {
            int minLag = 6;  // Minimum ~100ms at 60fps
            int maxLag = n / 2;
            float bestCorr = 0.0f;
            int bestLag = 0;

            for (int lag = minLag; lag <= maxLag; ++lag) {
                double corr = 0.0;
                for (int i = 0; i < n - lag; ++i) {
                    corr += (bh[i] - mean) * (bh[i + lag] - mean);
                }
                corr /= (n * variance);
                if (static_cast<float>(corr) > bestCorr) {
                    bestCorr = static_cast<float>(corr);
                    bestLag = lag;
                }
            }

            result.loopConfidence = bestCorr;
            if (bestCorr > 0.7f && bestLag > 0) {
                result.isLooping = true;
                result.loopPeriodFrames = bestLag;
                result.loopPeriodSeconds = static_cast<float>(bestLag) / m_config.fps;
            }
        }
    }

    // --- Visual Novelty ---
    {
        int validNovelty = std::min(m_frameCount, m_config.windowSize);
        if (validNovelty > 0) {
            int lastIdx = (m_noveltyWriteIndex + m_config.windowSize - 1) % m_config.windowSize;
            result.noveltyScore = m_noveltyHistory[lastIdx];
        }

        // Count valid keyframes
        result.keyframeCount = 0;
        for (int k = 0; k < m_config.keyframeBufferSize; ++k) {
            if (!m_keyframes[k].empty()) result.keyframeCount++;
        }

        // Novelty trend: compare recent novelty to earlier novelty
        int validNoveltyWindow = std::min(m_frameCount, m_config.windowSize);
        if (validNoveltyWindow >= 4) {
            int half = validNoveltyWindow / 2;
            float recentSum = 0.0f, earlierSum = 0.0f;
            for (int i = 0; i < half; ++i) {
                int recentIdx = (m_noveltyWriteIndex - 1 - i + m_config.windowSize) % m_config.windowSize;
                int earlierIdx = (m_noveltyWriteIndex - half - 1 - i + m_config.windowSize) % m_config.windowSize;
                recentSum += m_noveltyHistory[recentIdx];
                earlierSum += m_noveltyHistory[earlierIdx];
            }
            float recentAvg = recentSum / half;
            float earlierAvg = earlierSum / half;
            // Map difference to 0-1: negative = collapsing, positive = exploring
            float diff = recentAvg - earlierAvg;
            result.noveltyTrend = std::clamp(0.5f + diff * 10.0f, 0.0f, 1.0f);
        }
    }

    return result;
}

void TemporalAnalyzer::reset() {
    m_luminanceRing.clear();
    m_brightnessHistory.clear();
    m_deltaHistory.clear();
    m_keyframes.clear();
    m_noveltyHistory.clear();
    m_frameCount = 0;
    m_writeIndex = 0;
    m_brightnessWriteIndex = 0;
    m_deltaWriteIndex = 0;
    m_keyframeWriteIndex = 0;
    m_noveltyWriteIndex = 0;
    m_smoothDelta = 0.0f;
    m_convergedFrames = 0;
}

} // namespace vivid
