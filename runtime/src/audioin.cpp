#include "../include/vivid/operators/audioin.h"
#include "audio_capture.h"
#include "fft.h"
#include <iostream>

namespace vivid {

AudioIn::AudioIn()
    : capture_(std::make_unique<AudioCapture>())
    , fft_(std::make_unique<FFT>(1024))
    , bands_(std::make_unique<AudioBandAnalyzer>())
{
}

AudioIn::~AudioIn() {
    cleanup();
}

void AudioIn::init(Context& ctx) {
    (void)ctx;

    // Re-create FFT with configured size
    fft_ = std::make_unique<FFT>(fftSize_);
    audioBuffer_.resize(fftSize_);
    spectrum_.resize(fftSize_ / 2);

    // Configure band analyzer
    bands_->setSmoothing(smoothing_);

    // Initialize audio capture
    if (!capture_->init(sampleRate_, 1, deviceIndex_)) {
        std::cerr << "[AudioIn] Failed to initialize audio capture\n";
        return;
    }

    capture_->setGain(gain_);

    if (autoStart_) {
        capture_->start();
    }

    initialized_ = true;
    std::cout << "[AudioIn] Initialized (FFT size: " << fftSize_ << ")\n";
}

void AudioIn::process(Context& ctx) {
    if (!initialized_) return;

    // Update gain in case it changed
    capture_->setGain(gain_);
    bands_->setSmoothing(smoothing_);

    // Get audio samples for FFT
    updateAnalysis();

    // Set outputs for other operators to read
    ctx.setOutput("level", level_);
    ctx.setOutput("peak", peak_);
    ctx.setOutput("bass", bass_);
    ctx.setOutput("mid", mid_);
    ctx.setOutput("high", high_);
    ctx.setOutput("subBass", subBass_);
    ctx.setOutput("lowMid", lowMid_);
    ctx.setOutput("highMid", highMid_);

    // Also provide 3-band simplified output
    ctx.setOutput("low", (subBass_ + bass_) * 0.5f);
    ctx.setOutput("treble", (highMid_ + high_) * 0.5f);
}

void AudioIn::cleanup() {
    if (capture_) {
        capture_->stop();
        capture_->shutdown();
    }
    initialized_ = false;
}

void AudioIn::start() {
    if (capture_ && initialized_) {
        capture_->start();
    }
}

void AudioIn::stop() {
    if (capture_) {
        capture_->stop();
    }
}

bool AudioIn::isCapturing() const {
    return capture_ && capture_->isCapturing();
}

void AudioIn::updateAnalysis() {
    if (!capture_ || !capture_->isCapturing()) return;

    // Get current levels from capture
    level_ = capture_->getRMSLevel();
    peak_ = capture_->getPeakLevel();

    // Get samples for FFT (peek without consuming)
    uint32_t framesRead = capture_->peekSamples(audioBuffer_.data(), fftSize_);

    if (framesRead >= static_cast<uint32_t>(fftSize_ / 2)) {
        // Run FFT
        fft_->process(audioBuffer_.data(), framesRead);

        // Copy spectrum
        const auto& mags = fft_->getMagnitudes();
        for (size_t i = 0; i < spectrum_.size() && i < mags.size(); i++) {
            spectrum_[i] = mags[i];
        }

        // Update band analysis
        bands_->process(*fft_, sampleRate_);

        // Get band values
        subBass_ = bands_->getSubBass();
        bass_ = bands_->getBass();
        lowMid_ = bands_->getLowMid();
        mid_ = bands_->getMid();
        highMid_ = bands_->getHighMid();
        high_ = bands_->getHigh();
    }

    // Consume some samples to keep buffer fresh
    // (read and discard older samples)
    uint32_t toConsume = capture_->getBufferedFrames() / 2;
    if (toConsume > 0) {
        std::vector<float> discard(toConsume);
        capture_->getSamples(discard.data(), toConsume);
    }
}

} // namespace vivid
