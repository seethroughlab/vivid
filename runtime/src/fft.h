#pragma once
#include <vector>
#include <complex>
#include <cstdint>

namespace vivid {

/**
 * @brief Fast Fourier Transform utility.
 *
 * Performs FFT on audio samples to extract frequency spectrum.
 * Uses Cooley-Tukey algorithm with no external dependencies.
 */
class FFT {
public:
    /**
     * @brief Initialize FFT with a specific size.
     * @param size FFT size (must be power of 2, e.g., 512, 1024, 2048).
     */
    explicit FFT(uint32_t size = 1024);

    /**
     * @brief Process audio samples and compute magnitude spectrum.
     * @param samples Input audio samples (mono, float -1.0 to 1.0).
     * @param frameCount Number of samples (will be zero-padded or truncated to FFT size).
     */
    void process(const float* samples, uint32_t frameCount);

    /**
     * @brief Get the magnitude spectrum (0 to ~1, normalized).
     * @return Const reference to magnitude array (size/2 values for positive frequencies).
     */
    const std::vector<float>& getMagnitudes() const { return magnitudes_; }

    /**
     * @brief Get a specific frequency bin magnitude.
     * @param bin Bin index (0 to size/2 - 1).
     * @return Magnitude value (0 to ~1).
     */
    float getMagnitude(uint32_t bin) const;

    /**
     * @brief Get the frequency for a given bin index.
     * @param bin Bin index.
     * @param sampleRate Sample rate in Hz.
     * @return Frequency in Hz.
     */
    float binToFrequency(uint32_t bin, uint32_t sampleRate) const;

    /**
     * @brief Get bin index for a given frequency.
     * @param frequency Frequency in Hz.
     * @param sampleRate Sample rate in Hz.
     * @return Bin index (clamped to valid range).
     */
    uint32_t frequencyToBin(float frequency, uint32_t sampleRate) const;

    /**
     * @brief Get the sum of magnitudes in a frequency range.
     * @param lowFreq Low frequency in Hz.
     * @param highFreq High frequency in Hz.
     * @param sampleRate Sample rate in Hz.
     * @return Sum of magnitudes in range (normalized by bin count).
     */
    float getFrequencyRangeEnergy(float lowFreq, float highFreq, uint32_t sampleRate) const;

    /**
     * @brief Get FFT size.
     */
    uint32_t getSize() const { return size_; }

    /**
     * @brief Get number of frequency bins (size/2).
     */
    uint32_t getBinCount() const { return size_ / 2; }

    /**
     * @brief Apply Hann window to reduce spectral leakage.
     * Called automatically before FFT.
     */
    void setWindowEnabled(bool enabled) { windowEnabled_ = enabled; }

private:
    void computeFFT();
    void bitReverse();

    uint32_t size_;
    std::vector<std::complex<float>> buffer_;
    std::vector<float> magnitudes_;
    std::vector<float> window_;
    bool windowEnabled_ = true;
};

/**
 * @brief Pre-defined frequency bands for audio analysis.
 */
struct AudioBandConfig {
    float subBassLow = 20.0f;
    float subBassHigh = 60.0f;
    float bassLow = 60.0f;
    float bassHigh = 250.0f;
    float lowMidLow = 250.0f;
    float lowMidHigh = 500.0f;
    float midLow = 500.0f;
    float midHigh = 2000.0f;
    float highMidLow = 2000.0f;
    float highMidHigh = 4000.0f;
    float highLow = 4000.0f;
    float highHigh = 20000.0f;
};

/**
 * @brief Audio frequency band analyzer.
 *
 * Splits FFT output into frequency bands for visualization and reactivity.
 */
class AudioBandAnalyzer {
public:
    AudioBandAnalyzer();

    /**
     * @brief Process FFT data and compute band energies.
     * @param fft FFT instance with computed magnitudes.
     * @param sampleRate Sample rate in Hz.
     */
    void process(const FFT& fft, uint32_t sampleRate);

    /**
     * @brief Get individual band values.
     */
    float getSubBass() const { return subBass_; }
    float getBass() const { return bass_; }
    float getLowMid() const { return lowMid_; }
    float getMid() const { return mid_; }
    float getHighMid() const { return highMid_; }
    float getHigh() const { return high_; }

    /**
     * @brief Get simplified 3-band values.
     */
    float getLow() const { return (subBass_ + bass_) * 0.5f; }
    float getMidRange() const { return (lowMid_ + mid_) * 0.5f; }
    float getHighRange() const { return (highMid_ + high_) * 0.5f; }

    /**
     * @brief Get overall energy level.
     */
    float getOverall() const { return overall_; }

    /**
     * @brief Set smoothing factor (0-1, higher = smoother).
     */
    void setSmoothing(float smoothing) { smoothing_ = smoothing; }

    /**
     * @brief Configure frequency band ranges.
     */
    void setConfig(const AudioBandConfig& config) { config_ = config; }

private:
    AudioBandConfig config_;
    float smoothing_ = 0.7f;

    float subBass_ = 0.0f;
    float bass_ = 0.0f;
    float lowMid_ = 0.0f;
    float mid_ = 0.0f;
    float highMid_ = 0.0f;
    float high_ = 0.0f;
    float overall_ = 0.0f;
};

} // namespace vivid
