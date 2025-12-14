#pragma once

/**
 * @file fft.h
 * @brief Fast Fourier Transform analysis
 *
 * FFT provides frequency spectrum analysis:
 * - Configurable FFT size (256, 512, 1024, 2048, 4096)
 * - Magnitude spectrum with optional smoothing
 * - Frequency band queries
 */

#include <vivid/audio/audio_analyzer.h>
#include <vivid/param.h>
#include <vector>
#include <memory>

namespace vivid::audio {

/**
 * @brief FFT frequency analyzer
 *
 * Computes frequency spectrum from audio input using KissFFT.
 *
 * @par Example
 * @code
 * chain.add<FFT>("fft").input("audio").size(1024).smoothing(0.8f);
 *
 * // In update():
 * const float* spectrum = chain.get<FFT>("fft").spectrum();
 * float bass = chain.get<FFT>("fft").band(20, 250);
 * @endcode
 */
class FFT : public AudioAnalyzer {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> smoothing{"smoothing", 0.8f, 0.0f, 0.999f};  ///< Spectrum smoothing factor

    /// @}
    // -------------------------------------------------------------------------

    FFT();
    ~FFT() override;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Connect to audio source
     */
    FFT& input(const std::string& name) {
        AudioAnalyzer::input(name);
        return *this;
    }

    /**
     * @brief Set FFT size
     * @param n FFT size (must be power of 2: 256, 512, 1024, 2048, 4096)
     *
     * Larger sizes give better frequency resolution but slower time response.
     * Default is 1024 (21ms at 48kHz).
     */
    FFT& size(int n);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Analysis Results
    /// @{

    /**
     * @brief Get magnitude spectrum
     * @return Pointer to magnitude values (binCount() elements, 0-1 normalized)
     */
    const float* spectrum() const { return m_spectrum.data(); }

    /**
     * @brief Get number of frequency bins
     * @return Number of bins (fftSize / 2)
     */
    int binCount() const { return m_fftSize / 2; }

    /**
     * @brief Get FFT size
     */
    int fftSize() const { return m_fftSize; }

    /**
     * @brief Get magnitude of a specific bin
     * @param index Bin index (0 to binCount()-1)
     */
    float bin(int index) const;

    /**
     * @brief Get frequency of a bin in Hz
     * @param index Bin index
     */
    float binFrequency(int index) const;

    /**
     * @brief Get average magnitude in a frequency range
     * @param lowHz Low frequency in Hz
     * @param highHz High frequency in Hz
     * @return Average magnitude in range (0-1)
     */
    float band(float lowHz, float highHz) const;

    /**
     * @brief Get bin index for a frequency
     * @param hz Frequency in Hz
     * @return Nearest bin index
     */
    int frequencyToBin(float hz) const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "FFT"; }

    /// @}

protected:
    void initAnalyzer(Context& ctx) override;
    void analyze(const float* input, uint32_t frames, uint32_t channels) override;
    void cleanupAnalyzer() override;

private:
    void allocateBuffers();

    int m_fftSize = 1024;
    uint32_t m_sampleRate = 48000;

    // FFT state (pimpl to hide KissFFT types)
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    // Input accumulation buffer
    std::vector<float> m_inputBuffer;
    int m_inputWritePos = 0;

    // Output spectrum (magnitude, normalized 0-1)
    std::vector<float> m_spectrum;
    std::vector<float> m_smoothedSpectrum;
};

} // namespace vivid::audio
