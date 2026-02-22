// Vivid - Audio-Visual Reactivity Analysis
// Cross-domain metrics: correlation, onset response, mutual information

#include <vivid/av_analysis.h>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <sstream>

namespace vivid {

// =============================================================================
// Helpers
// =============================================================================

static constexpr float EPSILON = 1e-10f;

/// Pearson correlation between two float vectors of length n
/// Returns 0 if either standard deviation is below epsilon
static float pearsonCorrelation(const float* x, const float* y, int n) {
    if (n < 2) return 0.0f;

    double sumX = 0.0, sumY = 0.0;
    for (int i = 0; i < n; i++) {
        sumX += x[i];
        sumY += y[i];
    }
    double meanX = sumX / n;
    double meanY = sumY / n;

    double cov = 0.0, varX = 0.0, varY = 0.0;
    for (int i = 0; i < n; i++) {
        double dx = x[i] - meanX;
        double dy = y[i] - meanY;
        cov += dx * dy;
        varX += dx * dx;
        varY += dy * dy;
    }

    double stdX = std::sqrt(varX / n);
    double stdY = std::sqrt(varY / n);
    if (stdX < EPSILON || stdY < EPSILON) return 0.0f;

    return static_cast<float>(cov / (n * stdX * stdY));
}

/// Pearson correlation with lag: corr(x[i], y[i + lag])
/// Positive lag means y is shifted forward (visual lags audio)
static float laggedCorrelation(const float* x, const float* y, int n, int lag) {
    int absLag = std::abs(lag);
    int effectiveN = n - absLag;
    if (effectiveN < 2) return 0.0f;

    const float* xStart = (lag >= 0) ? x : x + absLag;
    const float* yStart = (lag >= 0) ? y + absLag : y;

    return pearsonCorrelation(xStart, yStart, effectiveN);
}

static const char* bandName(int idx) {
    static const char* names[] = {"subBass", "bass", "lowMid", "mid", "highMid", "high"};
    return (idx >= 0 && idx < 6) ? names[idx] : "unknown";
}

static const char* visualMetricName(int idx) {
    static const char* names[] = {"brightness", "motion", "contrast"};
    return (idx >= 0 && idx < 3) ? names[idx] : "unknown";
}

// =============================================================================
// AudioVisualAnalyzer
// =============================================================================

void AudioVisualAnalyzer::pushSample(const Sample& sample) {
    m_samples.push_back(sample);
}

void AudioVisualAnalyzer::reset() {
    m_samples.clear();
}

AudioVisualAnalysis AudioVisualAnalyzer::analyze() const {
    AudioVisualAnalysis result;
    int n = static_cast<int>(m_samples.size());
    result.sampleCount = n;

    // Validity checks
    if (n < m_config.minSamples) {
        result.valid = false;
        result.invalidReason = "< " + std::to_string(m_config.minSamples) + " samples";
        return result;
    }

    // Extract time-series into contiguous arrays
    std::vector<float> audioRms(n), brightness(n), motion(n), contrast(n), frameDelta(n);
    std::array<std::vector<float>, 6> bandSeries;
    for (auto& v : bandSeries) v.resize(n);

    for (int i = 0; i < n; i++) {
        audioRms[i] = m_samples[i].audioRms;
        brightness[i] = m_samples[i].brightness;
        motion[i] = m_samples[i].motionMagnitude;
        contrast[i] = m_samples[i].contrast;
        frameDelta[i] = m_samples[i].frameDelta;
        for (int b = 0; b < 6; b++) {
            bandSeries[b][i] = m_samples[i].audioSpectrum[b];
        }
    }

    // Check for degenerate signals
    auto stdDev = [](const float* data, int len) -> float {
        double sum = 0.0;
        for (int i = 0; i < len; i++) sum += data[i];
        double mean = sum / len;
        double var = 0.0;
        for (int i = 0; i < len; i++) {
            double d = data[i] - mean;
            var += d * d;
        }
        return static_cast<float>(std::sqrt(var / len));
    };

    float audioStd = stdDev(audioRms.data(), n);
    float brightnessStd = stdDev(brightness.data(), n);
    float motionStd = stdDev(motion.data(), n);

    if (audioStd < EPSILON) {
        result.valid = false;
        result.invalidReason = "audio is silent or constant";
        return result;
    }

    if (brightnessStd < EPSILON && motionStd < EPSILON) {
        result.valid = false;
        result.invalidReason = "visual is static";
        return result;
    }

    result.valid = true;

    // =========================================================================
    // Tier 1: Cross-Correlation
    // =========================================================================

    // AV Correlation (audio RMS vs brightness and motion)
    result.avCorrelationBrightness = pearsonCorrelation(audioRms.data(), brightness.data(), n);
    result.avCorrelationMotion = pearsonCorrelation(audioRms.data(), motion.data(), n);
    result.avCorrelation = std::max(std::fabs(result.avCorrelationBrightness),
                                     std::fabs(result.avCorrelationMotion));

    // Band-Visual Correlation
    // For each band, find the strongest correlation with any visual metric
    float* visualMetrics[3] = {brightness.data(), motion.data(), contrast.data()};

    for (int b = 0; b < 6; b++) {
        float bestAbsCorr = 0.0f;
        float bestRawCorr = 0.0f;
        int bestMetric = 0;

        for (int v = 0; v < 3; v++) {
            float r = pearsonCorrelation(bandSeries[b].data(), visualMetrics[v], n);
            if (std::fabs(r) > bestAbsCorr) {
                bestAbsCorr = std::fabs(r);
                bestRawCorr = r;
                bestMetric = v;
            }
        }

        result.bandCorrelations[b].correlation = bestAbsCorr;
        result.bandCorrelations[b].rawCorrelation = bestRawCorr;
        result.bandCorrelations[b].drivesMetric = visualMetricName(bestMetric);
    }

    // Reactivity Latency (cross-correlation at different lags)
    {
        float bestCorr = 0.0f;
        int bestLag = 0;

        for (int lag = -m_config.maxLag; lag <= m_config.maxLag; lag++) {
            float r = laggedCorrelation(audioRms.data(), brightness.data(), n, lag);
            if (std::fabs(r) > bestCorr) {
                bestCorr = std::fabs(r);
                bestLag = lag;
            }
            r = laggedCorrelation(audioRms.data(), motion.data(), n, lag);
            if (std::fabs(r) > bestCorr) {
                bestCorr = std::fabs(r);
                bestLag = lag;
            }
        }

        result.reactivityLatencyFrames = bestLag;
        result.reactivityLatencyMs = static_cast<float>(bestLag) * (1000.0f / m_config.fps);
        result.reactivityPeakCorrelation = bestCorr;
    }

    // =========================================================================
    // Tier 2: Event-Based (Onset Response)
    // =========================================================================

    {
        // Collect onset indices
        std::vector<int> onsetIndices;
        for (int i = 0; i < n; i++) {
            if (m_samples[i].isOnset) {
                onsetIndices.push_back(i);
            }
        }

        // Track which frames are post-onset
        std::vector<bool> isPostOnset(n, false);

        int responded = 0;
        int evaluated = 0;

        for (int onsetIdx : onsetIndices) {
            // Skip onsets too close to end (no full response window)
            if (onsetIdx + m_config.responseWindowFrames >= n) continue;

            evaluated++;

            // Check for visual response in response window
            float maxDelta = 0.0f;
            for (int j = 1; j <= m_config.responseWindowFrames && onsetIdx + j < n; j++) {
                maxDelta = std::max(maxDelta, frameDelta[onsetIdx + j]);
                isPostOnset[onsetIdx + j] = true;
            }

            if (maxDelta > m_config.responseThreshold) {
                responded++;
            }
        }

        result.totalOnsetsEvaluated = evaluated;
        result.onsetResponseCount = responded;
        result.onsetResponseRate = evaluated > 0
            ? static_cast<float>(responded) / static_cast<float>(evaluated)
            : 0.0f;

        // Response magnitude: mean delta for post-onset vs baseline frames
        double postOnsetSum = 0.0;
        int postOnsetCount = 0;
        double baselineSum = 0.0;
        int baselineCount = 0;

        for (int i = 0; i < n; i++) {
            if (isPostOnset[i]) {
                postOnsetSum += frameDelta[i];
                postOnsetCount++;
            } else {
                baselineSum += frameDelta[i];
                baselineCount++;
            }
        }

        result.avgPostOnsetDelta = postOnsetCount > 0
            ? static_cast<float>(postOnsetSum / postOnsetCount) : 0.0f;
        result.avgBaselineDelta = baselineCount > 0
            ? static_cast<float>(baselineSum / baselineCount) : 0.0f;
        result.responseMagnitude = result.avgPostOnsetDelta - result.avgBaselineDelta;
        result.responseMagnitudeRatio = result.avgBaselineDelta > EPSILON
            ? result.avgPostOnsetDelta / result.avgBaselineDelta : 1.0f;
    }

    // =========================================================================
    // Tier 3: Mutual Information
    // =========================================================================

    {
        int bins = m_config.miBins;

        // Quantize audio RMS and brightness into bins
        auto quantize = [bins](const float* data, int len) -> std::vector<int> {
            float minVal = *std::min_element(data, data + len);
            float maxVal = *std::max_element(data, data + len);
            float range = maxVal - minVal;
            std::vector<int> q(len);
            for (int i = 0; i < len; i++) {
                if (range < EPSILON) {
                    q[i] = 0;
                } else {
                    int bin = static_cast<int>((data[i] - minVal) / range * (bins - 1));
                    q[i] = std::clamp(bin, 0, bins - 1);
                }
            }
            return q;
        };

        auto qAudio = quantize(audioRms.data(), n);
        auto qVisual = quantize(brightness.data(), n);

        // Build joint histogram
        std::vector<int> joint(bins * bins, 0);
        std::vector<int> margA(bins, 0);
        std::vector<int> margV(bins, 0);

        for (int i = 0; i < n; i++) {
            joint[qAudio[i] * bins + qVisual[i]]++;
            margA[qAudio[i]]++;
            margV[qVisual[i]]++;
        }

        // Compute MI = sum p(a,v) * log2(p(a,v) / (p(a) * p(v)))
        double mi = 0.0;
        double hA = 0.0, hV = 0.0;

        for (int a = 0; a < bins; a++) {
            double pA = static_cast<double>(margA[a]) / n;
            if (pA > EPSILON) hA -= pA * std::log2(pA);
            for (int v = 0; v < bins; v++) {
                double pAV = static_cast<double>(joint[a * bins + v]) / n;
                double pV = static_cast<double>(margV[v]) / n;
                if (pAV > EPSILON && pA > EPSILON && pV > EPSILON) {
                    mi += pAV * std::log2(pAV / (pA * pV));
                }
            }
        }

        // Compute marginal entropy for visual (for NMI denominator)
        for (int v = 0; v < bins; v++) {
            double pV = static_cast<double>(margV[v]) / n;
            if (pV > EPSILON) hV -= pV * std::log2(pV);
        }

        result.avMutualInformationRaw = static_cast<float>(mi);

        // Normalized MI: MI / min(H(A), H(V))
        double minEntropy = std::min(hA, hV);
        result.avMutualInformation = minEntropy > EPSILON
            ? static_cast<float>(std::clamp(mi / minEntropy, 0.0, 1.0))
            : 0.0f;
    }

    return result;
}

// =============================================================================
// JSON serialization
// =============================================================================

std::string AudioVisualAnalysis::toJSON() const {
    std::ostringstream ss;
    ss << "{";
    ss << "\"avCorrelation\":" << avCorrelation;
    ss << ",\"avCorrelationBrightness\":" << avCorrelationBrightness;
    ss << ",\"avCorrelationMotion\":" << avCorrelationMotion;

    // Band correlations as named object
    ss << ",\"bandCorrelations\":{";
    static const char* bandNames[] = {"subBass", "bass", "lowMid", "mid", "highMid", "high"};
    for (int i = 0; i < 6; i++) {
        if (i > 0) ss << ",";
        ss << "\"" << bandNames[i] << "\":{";
        ss << "\"correlation\":" << bandCorrelations[i].correlation;
        ss << ",\"drivesMetric\":\"" << bandCorrelations[i].drivesMetric << "\"";
        ss << ",\"rawCorrelation\":" << bandCorrelations[i].rawCorrelation;
        ss << "}";
    }
    ss << "}";

    ss << ",\"reactivityLatencyFrames\":" << reactivityLatencyFrames;
    ss << ",\"reactivityLatencyMs\":" << reactivityLatencyMs;
    ss << ",\"reactivityPeakCorrelation\":" << reactivityPeakCorrelation;

    ss << ",\"onsetResponseRate\":" << onsetResponseRate;
    ss << ",\"onsetResponseCount\":" << onsetResponseCount;
    ss << ",\"totalOnsetsEvaluated\":" << totalOnsetsEvaluated;
    ss << ",\"responseMagnitude\":" << responseMagnitude;
    ss << ",\"responseMagnitudeRatio\":" << responseMagnitudeRatio;
    ss << ",\"avgPostOnsetDelta\":" << avgPostOnsetDelta;
    ss << ",\"avgBaselineDelta\":" << avgBaselineDelta;

    ss << ",\"avMutualInformation\":" << avMutualInformation;
    ss << ",\"avMutualInformationRaw\":" << avMutualInformationRaw;

    ss << ",\"sampleCount\":" << sampleCount;
    ss << ",\"durationSeconds\":" << durationSeconds;
    ss << ",\"valid\":" << (valid ? "true" : "false");
    if (!invalidReason.empty()) {
        ss << ",\"invalidReason\":\"" << invalidReason << "\"";
    }
    ss << "}";
    return ss.str();
}

} // namespace vivid
