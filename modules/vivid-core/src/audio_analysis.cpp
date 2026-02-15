// Vivid - Audio Analysis Implementation

#include <vivid/audio_analysis.h>
#include <cmath>
#include <algorithm>
#include <sstream>
#include <vector>
#include <numeric>

namespace vivid {

// Frequency band edges (Hz) matching BandSplit operator
static constexpr float BAND_EDGES[] = {0.0f, 60.0f, 250.0f, 500.0f, 2000.0f, 4000.0f, 20000.0f};
static constexpr int NUM_BANDS = 6;

// Compute energy in a frequency range using Goertzel algorithm
// More efficient than full FFT for a small number of frequency bins
static float bandEnergy(const float* monoSamples, uint32_t frameCount,
                        uint32_t sampleRate, float lowHz, float highHz) {
    if (frameCount == 0 || lowHz >= highHz) return 0.0f;

    // Number of frequency bins to sample within this band
    // Use enough bins for reasonable coverage but keep it cheap
    float bandWidth = highHz - lowHz;
    int numBins = std::max(2, std::min(16, static_cast<int>(bandWidth / 20.0f)));

    double totalEnergy = 0.0;

    for (int b = 0; b < numBins; b++) {
        float freq = lowHz + (bandWidth * (b + 0.5f)) / numBins;
        if (freq <= 0.0f || freq >= sampleRate / 2.0f) continue;

        // Goertzel algorithm for single frequency bin
        double k = 0.5 + ((double)frameCount * freq / sampleRate);
        double w = (2.0 * M_PI * k) / frameCount;
        double cosw = std::cos(w);
        double coeff = 2.0 * cosw;

        double s0 = 0.0, s1 = 0.0, s2 = 0.0;
        for (uint32_t i = 0; i < frameCount; i++) {
            s0 = monoSamples[i] + coeff * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        double power = s1 * s1 + s2 * s2 - coeff * s1 * s2;
        totalEnergy += power;
    }

    // Normalize by frame count and number of bins
    return static_cast<float>(std::sqrt(totalEnergy / (numBins * (double)frameCount * frameCount)));
}

AudioAnalysis analyzeAudioBuffer(const float* samples, uint32_t frameCount,
                                  uint32_t channels, uint32_t sampleRate) {
    AudioAnalysis result;

    if (!samples || frameCount == 0 || channels == 0 || sampleRate == 0) {
        return result;
    }

    result.duration = static_cast<float>(frameCount) / sampleRate;

    uint32_t totalSamples = frameCount * channels;

    // Compute RMS and peak (overall and per-channel)
    double sumSq = 0.0;
    double sumSqLeft = 0.0;
    double sumSqRight = 0.0;
    float peak = 0.0f;

    for (uint32_t i = 0; i < totalSamples; i++) {
        float s = samples[i];
        float absS = std::fabs(s);
        sumSq += (double)s * s;
        peak = std::max(peak, absS);

        if (channels >= 2) {
            uint32_t ch = i % channels;
            if (ch == 0) sumSqLeft += (double)s * s;
            else if (ch == 1) sumSqRight += (double)s * s;
        }
    }

    result.rmsLevel = static_cast<float>(std::sqrt(sumSq / totalSamples));
    result.peakLevel = peak;
    result.isSilent = result.rmsLevel < 0.001f;
    result.crestFactor = result.rmsLevel > 0.0001f ? peak / result.rmsLevel : 0.0f;

    if (channels >= 2) {
        result.rmsLeft = static_cast<float>(std::sqrt(sumSqLeft / frameCount));
        result.rmsRight = static_cast<float>(std::sqrt(sumSqRight / frameCount));
    } else {
        result.rmsLeft = result.rmsLevel;
        result.rmsRight = result.rmsLevel;
    }

    // Mix down to mono for spectrum analysis
    std::vector<float> mono(frameCount);
    if (channels == 1) {
        std::copy(samples, samples + frameCount, mono.begin());
    } else {
        for (uint32_t i = 0; i < frameCount; i++) {
            float sum = 0.0f;
            for (uint32_t ch = 0; ch < channels; ch++) {
                sum += samples[i * channels + ch];
            }
            mono[i] = sum / channels;
        }
    }

    // Compute 6-band spectrum using Goertzel
    for (int b = 0; b < NUM_BANDS; b++) {
        result.spectrum[b] = bandEnergy(mono.data(), frameCount,
                                         sampleRate, BAND_EDGES[b], BAND_EDGES[b + 1]);
    }

    return result;
}

std::string AudioAnalysis::toJSON() const {
    std::ostringstream ss;
    ss << "{";
    ss << "\"rmsLevel\":" << rmsLevel;
    ss << ",\"peakLevel\":" << peakLevel;
    ss << ",\"rmsLeft\":" << rmsLeft;
    ss << ",\"rmsRight\":" << rmsRight;
    ss << ",\"isSilent\":" << (isSilent ? "true" : "false");
    ss << ",\"crestFactor\":" << crestFactor;
    ss << ",\"spectrum\":{";
    ss << "\"subBass\":" << spectrum[0];
    ss << ",\"bass\":" << spectrum[1];
    ss << ",\"lowMid\":" << spectrum[2];
    ss << ",\"mid\":" << spectrum[3];
    ss << ",\"highMid\":" << spectrum[4];
    ss << ",\"high\":" << spectrum[5];
    ss << "}";
    ss << ",\"duration\":" << duration;
    ss << "}";
    return ss.str();
}

} // namespace vivid
