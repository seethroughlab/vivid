#pragma once

#include "../operator.h"
#include "../context.h"
#include "../types.h"
#include "../params.h"
#include <memory>
#include <vector>
#include <string>

namespace vivid {

// Forward declarations
class AudioCapture;
class FFT;
class AudioBandAnalyzer;

/**
 * @brief Audio input operator with FFT analysis.
 *
 * Captures audio from microphone or line-in and provides:
 * - RMS and peak levels
 * - Frequency spectrum (FFT magnitudes)
 * - Frequency band energies (bass, mid, treble, etc.)
 *
 * Example:
 * @code
 * chain.add<AudioIn>("audio")
 *     .device(-1)           // Default device
 *     .gain(1.0f)           // Input gain
 *     .fftSize(1024)        // FFT window size
 *     .smoothing(0.8f);     // Band smoothing
 *
 * // In update():
 * float bass = ctx.getValue("audio", "bass");
 * float mid = ctx.getValue("audio", "mid");
 * float high = ctx.getValue("audio", "high");
 * float level = ctx.getValue("audio", "level");
 * @endcode
 */
class AudioIn : public Operator {
public:
    AudioIn();
    ~AudioIn();

    /// Set audio input device index (-1 for default)
    AudioIn& device(int deviceIndex) { deviceIndex_ = deviceIndex; return *this; }

    /// Set input gain multiplier (default 1.0)
    AudioIn& gain(float g) { gain_ = g; return *this; }

    /// Set FFT size (must be power of 2, default 1024)
    AudioIn& fftSize(int size) { fftSize_ = size; return *this; }

    /// Set sample rate (default 44100)
    AudioIn& sampleRate(int rate) { sampleRate_ = rate; return *this; }

    /// Set band smoothing factor (0-1, higher = smoother, default 0.8)
    AudioIn& smoothing(float s) { smoothing_ = s; return *this; }

    /// Set whether to start capture automatically (default true)
    AudioIn& autoStart(bool auto_) { autoStart_ = auto_; return *this; }

    // Manual control
    void start();
    void stop();
    bool isCapturing() const;

    // Direct access to analysis results (for advanced use)
    float getLevel() const { return level_; }
    float getPeak() const { return peak_; }
    float getBass() const { return bass_; }
    float getMid() const { return mid_; }
    float getHigh() const { return high_; }
    float getSubBass() const { return subBass_; }
    float getLowMid() const { return lowMid_; }
    float getHighMid() const { return highMid_; }
    const std::vector<float>& getSpectrum() const { return spectrum_; }

    void init(Context& ctx) override;
    void process(Context& ctx) override;
    void cleanup() override;

    std::vector<ParamDecl> params() override {
        return {
            intParam("device", deviceIndex_, -1, 10),
            floatParam("gain", gain_, 0.0f, 10.0f),
            intParam("fftSize", fftSize_, 256, 4096),
            floatParam("smoothing", smoothing_, 0.0f, 1.0f)
        };
    }

    OutputKind outputKind() override { return OutputKind::Value; }

private:
    void updateAnalysis();

    int deviceIndex_ = -1;
    float gain_ = 1.0f;
    int fftSize_ = 1024;
    int sampleRate_ = 44100;
    float smoothing_ = 0.8f;
    bool autoStart_ = true;

    // Analysis results
    float level_ = 0.0f;
    float peak_ = 0.0f;
    float bass_ = 0.0f;
    float mid_ = 0.0f;
    float high_ = 0.0f;
    float subBass_ = 0.0f;
    float lowMid_ = 0.0f;
    float highMid_ = 0.0f;
    std::vector<float> spectrum_;

    // Audio capture buffer
    std::vector<float> audioBuffer_;

    // Internal implementations (pimpl to avoid header dependencies)
    std::unique_ptr<AudioCapture> capture_;
    std::unique_ptr<FFT> fft_;
    std::unique_ptr<AudioBandAnalyzer> bands_;

    bool initialized_ = false;
};

} // namespace vivid
