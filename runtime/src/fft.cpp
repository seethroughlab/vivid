#include "fft.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace vivid {

FFT::FFT(uint32_t size) : size_(size) {
    // Ensure size is power of 2
    uint32_t powerOf2 = 1;
    while (powerOf2 < size_) powerOf2 <<= 1;
    size_ = powerOf2;

    buffer_.resize(size_);
    magnitudes_.resize(size_ / 2);

    // Pre-compute Hann window
    window_.resize(size_);
    for (uint32_t i = 0; i < size_; i++) {
        window_[i] = 0.5f * (1.0f - std::cos(2.0f * M_PI * i / (size_ - 1)));
    }
}

void FFT::process(const float* samples, uint32_t frameCount) {
    // Copy samples to buffer with windowing
    for (uint32_t i = 0; i < size_; i++) {
        float sample = (i < frameCount) ? samples[i] : 0.0f;
        if (windowEnabled_) {
            sample *= window_[i];
        }
        buffer_[i] = std::complex<float>(sample, 0.0f);
    }

    // Perform FFT
    computeFFT();

    // Compute magnitudes for positive frequencies
    float normFactor = 2.0f / size_;
    for (uint32_t i = 0; i < size_ / 2; i++) {
        magnitudes_[i] = std::abs(buffer_[i]) * normFactor;
    }
}

void FFT::bitReverse() {
    uint32_t n = size_;
    uint32_t bits = 0;
    while ((1u << bits) < n) bits++;

    for (uint32_t i = 0; i < n; i++) {
        uint32_t j = 0;
        for (uint32_t k = 0; k < bits; k++) {
            j |= ((i >> k) & 1) << (bits - 1 - k);
        }
        if (i < j) {
            std::swap(buffer_[i], buffer_[j]);
        }
    }
}

void FFT::computeFFT() {
    // Cooley-Tukey iterative FFT
    bitReverse();

    for (uint32_t len = 2; len <= size_; len <<= 1) {
        float angle = -2.0f * M_PI / len;
        std::complex<float> wn(std::cos(angle), std::sin(angle));

        for (uint32_t i = 0; i < size_; i += len) {
            std::complex<float> w(1.0f, 0.0f);
            for (uint32_t j = 0; j < len / 2; j++) {
                std::complex<float> u = buffer_[i + j];
                std::complex<float> t = w * buffer_[i + j + len / 2];
                buffer_[i + j] = u + t;
                buffer_[i + j + len / 2] = u - t;
                w *= wn;
            }
        }
    }
}

float FFT::getMagnitude(uint32_t bin) const {
    if (bin >= magnitudes_.size()) return 0.0f;
    return magnitudes_[bin];
}

float FFT::binToFrequency(uint32_t bin, uint32_t sampleRate) const {
    return static_cast<float>(bin * sampleRate) / size_;
}

uint32_t FFT::frequencyToBin(float frequency, uint32_t sampleRate) const {
    uint32_t bin = static_cast<uint32_t>(frequency * size_ / sampleRate);
    return std::min(bin, static_cast<uint32_t>(magnitudes_.size() - 1));
}

float FFT::getFrequencyRangeEnergy(float lowFreq, float highFreq, uint32_t sampleRate) const {
    uint32_t lowBin = frequencyToBin(lowFreq, sampleRate);
    uint32_t highBin = frequencyToBin(highFreq, sampleRate);

    if (lowBin >= highBin) return 0.0f;

    float sum = 0.0f;
    for (uint32_t i = lowBin; i <= highBin && i < magnitudes_.size(); i++) {
        sum += magnitudes_[i];
    }

    return sum / (highBin - lowBin + 1);
}

// AudioBandAnalyzer implementation

AudioBandAnalyzer::AudioBandAnalyzer() {}

void AudioBandAnalyzer::process(const FFT& fft, uint32_t sampleRate) {
    // Get energy for each band
    float newSubBass = fft.getFrequencyRangeEnergy(config_.subBassLow, config_.subBassHigh, sampleRate);
    float newBass = fft.getFrequencyRangeEnergy(config_.bassLow, config_.bassHigh, sampleRate);
    float newLowMid = fft.getFrequencyRangeEnergy(config_.lowMidLow, config_.lowMidHigh, sampleRate);
    float newMid = fft.getFrequencyRangeEnergy(config_.midLow, config_.midHigh, sampleRate);
    float newHighMid = fft.getFrequencyRangeEnergy(config_.highMidLow, config_.highMidHigh, sampleRate);
    float newHigh = fft.getFrequencyRangeEnergy(config_.highLow, config_.highHigh, sampleRate);

    // Apply smoothing
    subBass_ = subBass_ * smoothing_ + newSubBass * (1.0f - smoothing_);
    bass_ = bass_ * smoothing_ + newBass * (1.0f - smoothing_);
    lowMid_ = lowMid_ * smoothing_ + newLowMid * (1.0f - smoothing_);
    mid_ = mid_ * smoothing_ + newMid * (1.0f - smoothing_);
    highMid_ = highMid_ * smoothing_ + newHighMid * (1.0f - smoothing_);
    high_ = high_ * smoothing_ + newHigh * (1.0f - smoothing_);

    // Calculate overall energy
    overall_ = (subBass_ + bass_ + lowMid_ + mid_ + highMid_ + high_) / 6.0f;
}

} // namespace vivid
