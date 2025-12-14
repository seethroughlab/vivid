#pragma once

/**
 * @file band_split.h
 * @brief Frequency band analysis
 *
 * BandSplit provides easy access to frequency bands:
 * - Sub-bass, bass, low-mids, mids, high-mids, highs
 * - Built-in FFT analysis
 * - Smoothing for stable readings
 */

#include <vivid/audio/audio_analyzer.h>
#include <vivid/param.h>
#include <vector>
#include <memory>

namespace vivid::audio {

/**
 * @brief Frequency band analyzer
 *
 * Analyzes audio into frequency bands for audio-reactive visuals.
 * Uses built-in FFT, no need for separate FFT operator.
 *
 * @par Default frequency ranges:
 * - subBass: 20-60 Hz
 * - bass: 60-250 Hz
 * - lowMid: 250-500 Hz
 * - mid: 500-2000 Hz
 * - highMid: 2000-4000 Hz
 * - high: 4000-20000 Hz
 *
 * @par Example
 * @code
 * chain.add<BandSplit>("bands").input("audio").smoothing(0.9f);
 *
 * // In update():
 * float bass = chain.get<BandSplit>("bands").bass();
 * float mids = chain.get<BandSplit>("bands").mid();
 * chain.get<Circle>("circle").radius(0.2f + bass * 0.3f);
 * @endcode
 */
class BandSplit : public AudioAnalyzer {
public:
    // -------------------------------------------------------------------------
    /// @name Parameters (public for direct access)
    /// @{

    Param<float> smoothing{"smoothing", 0.9f, 0.0f, 0.999f};  ///< Smoothing factor

    /// @}
    // -------------------------------------------------------------------------

    BandSplit();
    ~BandSplit() override;

    // -------------------------------------------------------------------------
    /// @name Configuration
    /// @{

    /**
     * @brief Connect to audio source
     */
    BandSplit& input(const std::string& name) {
        AudioAnalyzer::input(name);
        return *this;
    }

    /**
     * @brief Set FFT size for analysis
     * @param n FFT size (256, 512, 1024, 2048)
     */
    BandSplit& fftSize(int n);

    /// @}
    // -------------------------------------------------------------------------
    /// @name Frequency Bands (0-1 normalized)
    /// @{

    /** @brief Sub-bass (20-60 Hz) - rumble, kick drum fundamentals */
    float subBass() const { return m_subBass; }

    /** @brief Bass (60-250 Hz) - kick, bass guitar, bass synth */
    float bass() const { return m_bass; }

    /** @brief Low-mids (250-500 Hz) - warmth, body of instruments */
    float lowMid() const { return m_lowMid; }

    /** @brief Mids (500-2000 Hz) - vocals, snare, guitars */
    float mid() const { return m_mid; }

    /** @brief High-mids (2000-4000 Hz) - presence, clarity */
    float highMid() const { return m_highMid; }

    /** @brief Highs (4000-20000 Hz) - cymbals, air, brilliance */
    float high() const { return m_high; }

    /**
     * @brief Get all 6 bands as array
     * @return Pointer to 6 floats [subBass, bass, lowMid, mid, highMid, high]
     */
    const float* bands() const { return m_bands; }

    /**
     * @brief Get custom frequency range
     * @param lowHz Low frequency in Hz
     * @param highHz High frequency in Hz
     * @return Average magnitude in range (0-1)
     */
    float band(float lowHz, float highHz) const;

    /// @}
    // -------------------------------------------------------------------------
    /// @name Operator Interface
    /// @{

    std::string name() const override { return "BandSplit"; }

    /// @}

protected:
    void initAnalyzer(Context& ctx) override;
    void analyze(const float* input, uint32_t frames, uint32_t channels) override;
    void cleanupAnalyzer() override;

private:
    void allocateBuffers();
    float computeBand(int lowBin, int highBin) const;
    int frequencyToBin(float hz) const;

    int m_fftSize = 1024;
    uint32_t m_sampleRate = 48000;

    // FFT state
    struct Impl;
    std::unique_ptr<Impl> m_impl;

    // Input accumulation
    std::vector<float> m_inputBuffer;
    int m_inputWritePos = 0;

    // Spectrum
    std::vector<float> m_spectrum;

    // Band values
    float m_bands[6] = {0};  // Array form
    float& m_subBass = m_bands[0];
    float& m_bass = m_bands[1];
    float& m_lowMid = m_bands[2];
    float& m_mid = m_bands[3];
    float& m_highMid = m_bands[4];
    float& m_high = m_bands[5];

    // Pre-computed bin ranges
    int m_subBassBins[2] = {0, 0};
    int m_bassBins[2] = {0, 0};
    int m_lowMidBins[2] = {0, 0};
    int m_midBins[2] = {0, 0};
    int m_highMidBins[2] = {0, 0};
    int m_highBins[2] = {0, 0};
};

} // namespace vivid::audio
