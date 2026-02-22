/**
 * @file test_audio_analysis.cpp
 * @brief Unit tests for AudioAnalysis struct and analyzeAudioBuffer()
 *
 * Tests silence detection, sine wave spectrum, RMS/peak accuracy,
 * and crest factor computation.
 */

#define _USE_MATH_DEFINES
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vivid/audio_analysis.h>
#include <vivid/wav_writer.h>
#include <cmath>
#include <vector>
#include <filesystem>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

static constexpr uint32_t SAMPLE_RATE = 48000;
static constexpr uint32_t CHANNELS = 2;

// Generate silence (all zeros)
static std::vector<float> generateSilence(uint32_t frames) {
    return std::vector<float>(frames * CHANNELS, 0.0f);
}

// Generate a sine wave at a given frequency (stereo, same both channels)
static std::vector<float> generateSine(float freq, uint32_t frames, float amplitude = 0.5f) {
    std::vector<float> samples(frames * CHANNELS);
    for (uint32_t i = 0; i < frames; i++) {
        float s = amplitude * std::sin(2.0f * M_PI * freq * i / SAMPLE_RATE);
        samples[i * CHANNELS] = s;       // Left
        samples[i * CHANNELS + 1] = s;   // Right
    }
    return samples;
}

// Generate white noise
static std::vector<float> generateNoise(uint32_t frames, float amplitude = 0.5f) {
    std::vector<float> samples(frames * CHANNELS);
    // Simple LCG for deterministic pseudo-random
    uint32_t state = 12345;
    for (uint32_t i = 0; i < frames * CHANNELS; i++) {
        state = state * 1664525 + 1013904223;
        float r = (static_cast<float>(state) / static_cast<float>(UINT32_MAX)) * 2.0f - 1.0f;
        samples[i] = r * amplitude;
    }
    return samples;
}

// =============================================================================
// Silence Detection
// =============================================================================

TEST_CASE("AudioAnalysis: Silence", "[audio_analysis]") {
    auto silence = generateSilence(SAMPLE_RATE);  // 1 second
    auto result = vivid::analyzeAudioBuffer(silence.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    CHECK(result.isSilent == true);
    CHECK(result.rmsLevel < 0.001f);
    CHECK(result.peakLevel == 0.0f);
    CHECK(result.crestFactor == 0.0f);
    CHECK_THAT(result.duration, WithinAbs(1.0f, 0.001f));
}

// =============================================================================
// Sine Wave
// =============================================================================

TEST_CASE("AudioAnalysis: Sine wave RMS", "[audio_analysis]") {
    float amplitude = 0.5f;
    auto sine = generateSine(440.0f, SAMPLE_RATE, amplitude);
    auto result = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    // RMS of a sine wave = amplitude / sqrt(2) ≈ 0.3536
    float expectedRms = amplitude / std::sqrt(2.0f);

    CHECK(result.isSilent == false);
    CHECK_THAT(result.rmsLevel, WithinAbs(expectedRms, 0.01f));
    CHECK_THAT(result.peakLevel, WithinAbs(amplitude, 0.01f));
    CHECK_THAT(result.rmsLeft, WithinAbs(expectedRms, 0.01f));
    CHECK_THAT(result.rmsRight, WithinAbs(expectedRms, 0.01f));
    // Crest factor for sine = sqrt(2) ≈ 1.414
    CHECK_THAT(result.crestFactor, WithinAbs(std::sqrt(2.0f), 0.1f));
}

TEST_CASE("AudioAnalysis: 100Hz sine in bass band", "[audio_analysis]") {
    // 100Hz should show up in bass band (60-250Hz)
    auto sine = generateSine(100.0f, SAMPLE_RATE, 0.5f);
    auto result = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    // Bass band (index 1) should have the most energy
    float bassEnergy = result.spectrum[1];
    CHECK(bassEnergy > 0.001f);  // Non-trivial energy in bass

    // Should be significantly larger than sub-bass and high bands
    CHECK(bassEnergy > result.spectrum[0]);  // More than sub-bass
    CHECK(bassEnergy > result.spectrum[4]);  // More than high-mid
    CHECK(bassEnergy > result.spectrum[5]);  // More than high
}

TEST_CASE("AudioAnalysis: 1kHz sine in mid band", "[audio_analysis]") {
    // 1kHz should show up in mid band (500-2000Hz)
    auto sine = generateSine(1000.0f, SAMPLE_RATE, 0.5f);
    auto result = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    float midEnergy = result.spectrum[3];
    CHECK(midEnergy > 0.0001f);

    // Mid band should dominate
    CHECK(midEnergy > result.spectrum[0]);  // More than sub-bass
    CHECK(midEnergy > result.spectrum[1]);  // More than bass
    CHECK(midEnergy > result.spectrum[5]);  // More than high
}

// =============================================================================
// White Noise
// =============================================================================

TEST_CASE("AudioAnalysis: White noise has energy across all bands", "[audio_analysis]") {
    auto noise = generateNoise(SAMPLE_RATE * 2, 0.3f);  // 2 seconds for stability
    auto result = vivid::analyzeAudioBuffer(noise.data(), SAMPLE_RATE * 2, CHANNELS, SAMPLE_RATE);

    CHECK(result.isSilent == false);

    // All bands should have non-trivial energy
    for (int i = 0; i < 6; i++) {
        CHECK(result.spectrum[i] > 0.0001f);
    }
}

// =============================================================================
// DC Offset
// =============================================================================

TEST_CASE("AudioAnalysis: DC offset of zero-mean sine", "[audio_analysis]") {
    auto sine = generateSine(440.0f, SAMPLE_RATE, 0.5f);
    auto result = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    // A pure sine has zero DC offset
    CHECK_THAT(result.dcOffset, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("AudioAnalysis: DC offset with bias", "[audio_analysis]") {
    // Generate sine + DC offset of 0.1
    auto sine = generateSine(440.0f, SAMPLE_RATE, 0.5f);
    for (auto& s : sine) s += 0.1f;
    auto result = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    CHECK_THAT(result.dcOffset, WithinAbs(0.1f, 0.01f));
}

// =============================================================================
// Clipping Detection
// =============================================================================

TEST_CASE("AudioAnalysis: No clipping for clean sine", "[audio_analysis]") {
    auto sine = generateSine(440.0f, SAMPLE_RATE, 0.5f);
    auto result = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    CHECK(result.clippedSampleCount == 0);
    CHECK(result.clippedSamplePct == 0.0f);
}

TEST_CASE("AudioAnalysis: Clipping detected for hot signal", "[audio_analysis]") {
    auto sine = generateSine(440.0f, SAMPLE_RATE, 1.0f);  // amplitude = 1.0 → will hit >= 0.99
    auto result = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    CHECK(result.clippedSampleCount > 0);
    CHECK(result.clippedSamplePct > 0.0f);
}

// =============================================================================
// Zero Crossing Rate
// =============================================================================

TEST_CASE("AudioAnalysis: ZCR for 100Hz sine", "[audio_analysis]") {
    auto sine = generateSine(100.0f, SAMPLE_RATE, 0.5f);
    auto result = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    // 100Hz sine: ~200 zero crossings per second (2 per cycle)
    CHECK(result.zeroCrossingRate > 150.0f);
    CHECK(result.zeroCrossingRate < 250.0f);
}

TEST_CASE("AudioAnalysis: ZCR for noise is high", "[audio_analysis]") {
    auto noise = generateNoise(SAMPLE_RATE, 0.5f);
    auto result = vivid::analyzeAudioBuffer(noise.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    // White noise has very high ZCR
    CHECK(result.zeroCrossingRate > 5000.0f);
}

// =============================================================================
// Stereo Width and Correlation
// =============================================================================

TEST_CASE("AudioAnalysis: Identical L/R has correlation ~1, width ~0", "[audio_analysis]") {
    // generateSine produces identical L and R channels
    auto sine = generateSine(440.0f, SAMPLE_RATE, 0.5f);
    auto result = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    CHECK_THAT(result.stereoCorrelation, WithinAbs(1.0f, 0.05f));
    CHECK_THAT(result.stereoWidth, WithinAbs(0.0f, 0.05f));
}

TEST_CASE("AudioAnalysis: Independent noise L/R has low correlation", "[audio_analysis]") {
    // Generate independent noise in L and R
    uint32_t frames = SAMPLE_RATE;
    std::vector<float> stereo(frames * 2);
    uint32_t stateL = 12345, stateR = 67890;
    for (uint32_t i = 0; i < frames; i++) {
        stateL = stateL * 1664525 + 1013904223;
        stateR = stateR * 1664525 + 1013904223;
        stereo[i * 2] = (static_cast<float>(stateL) / static_cast<float>(UINT32_MAX)) * 2.0f - 1.0f;
        stereo[i * 2 + 1] = (static_cast<float>(stateR) / static_cast<float>(UINT32_MAX)) * 2.0f - 1.0f;
    }
    auto result = vivid::analyzeAudioBuffer(stereo.data(), frames, 2, SAMPLE_RATE);

    CHECK(result.stereoCorrelation < 0.3f);
    CHECK(result.stereoWidth > 0.3f);
}

TEST_CASE("AudioAnalysis: Different sines L vs R has non-zero width", "[audio_analysis]") {
    uint32_t frames = SAMPLE_RATE;
    std::vector<float> stereo(frames * 2);
    for (uint32_t i = 0; i < frames; i++) {
        stereo[i * 2] = 0.5f * std::sin(2.0f * M_PI * 440.0f * i / SAMPLE_RATE);      // L: 440Hz
        stereo[i * 2 + 1] = 0.5f * std::sin(2.0f * M_PI * 880.0f * i / SAMPLE_RATE);  // R: 880Hz
    }
    auto result = vivid::analyzeAudioBuffer(stereo.data(), frames, 2, SAMPLE_RATE);

    CHECK(result.stereoWidth > 0.3f);
}

TEST_CASE("AudioAnalysis: Mono signal has correlation 1 and width 0", "[audio_analysis]") {
    uint32_t frames = SAMPLE_RATE;
    std::vector<float> mono(frames);
    for (uint32_t i = 0; i < frames; i++) {
        mono[i] = 0.5f * std::sin(2.0f * M_PI * 440.0f * i / SAMPLE_RATE);
    }
    auto result = vivid::analyzeAudioBuffer(mono.data(), frames, 1, SAMPLE_RATE);

    CHECK_THAT(result.stereoCorrelation, WithinAbs(1.0f, 0.001f));
    CHECK_THAT(result.stereoWidth, WithinAbs(0.0f, 0.001f));
}

// =============================================================================
// Spectral Metrics (STFT-based)
// =============================================================================

TEST_CASE("AudioAnalysis: Spectral centroid of 200Hz sine", "[audio_analysis]") {
    auto sine = generateSine(200.0f, SAMPLE_RATE, 0.5f);
    auto result = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    // Centroid should be near 200Hz
    CHECK(result.spectralCentroid > 100.0f);
    CHECK(result.spectralCentroid < 400.0f);
}

TEST_CASE("AudioAnalysis: Spectral centroid of 5kHz sine", "[audio_analysis]") {
    auto sine = generateSine(5000.0f, SAMPLE_RATE, 0.5f);
    auto result = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    // Centroid should be near 5000Hz
    CHECK(result.spectralCentroid > 3000.0f);
    CHECK(result.spectralCentroid < 7000.0f);
}

TEST_CASE("AudioAnalysis: Spectral spread of sine vs noise", "[audio_analysis]") {
    auto sine = generateSine(1000.0f, SAMPLE_RATE, 0.5f);
    auto noise = generateNoise(SAMPLE_RATE, 0.5f);
    auto sinResult = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);
    auto noiseResult = vivid::analyzeAudioBuffer(noise.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    // Sine has narrow spread, noise has wide spread
    CHECK(sinResult.spectralSpread < 500.0f);
    CHECK(noiseResult.spectralSpread > 3000.0f);
}

TEST_CASE("AudioAnalysis: Spectral flux of constant sine is low", "[audio_analysis]") {
    auto sine = generateSine(440.0f, SAMPLE_RATE, 0.5f);
    auto result = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    CHECK(result.spectralFlux < 0.01f);
}

TEST_CASE("AudioAnalysis: Spectral flatness of sine vs noise", "[audio_analysis]") {
    auto sine = generateSine(440.0f, SAMPLE_RATE, 0.5f);
    auto noise = generateNoise(SAMPLE_RATE, 0.5f);
    auto sinResult = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);
    auto noiseResult = vivid::analyzeAudioBuffer(noise.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    // Sine is tonal (low flatness), noise is noise-like (high flatness)
    CHECK(sinResult.spectralFlatness < 0.1f);
    CHECK(noiseResult.spectralFlatness > 0.3f);
}

TEST_CASE("AudioAnalysis: Spectral rolloff of 200Hz sine is low", "[audio_analysis]") {
    auto sine = generateSine(200.0f, SAMPLE_RATE, 0.5f);
    auto result = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    CHECK(result.spectralRolloff < 2000.0f);
}

TEST_CASE("AudioAnalysis: Spectral rolloff of noise is high", "[audio_analysis]") {
    auto noise = generateNoise(SAMPLE_RATE, 0.5f);
    auto result = vivid::analyzeAudioBuffer(noise.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    CHECK(result.spectralRolloff > 4000.0f);
}

TEST_CASE("AudioAnalysis: Short buffer has zero spectral metrics", "[audio_analysis]") {
    // 512 samples = too short for 2048-point FFT
    auto sine = generateSine(440.0f, 512, 0.5f);
    auto result = vivid::analyzeAudioBuffer(sine.data(), 512, CHANNELS, SAMPLE_RATE);

    CHECK(result.spectralCentroid == 0.0f);
    CHECK(result.spectralSpread == 0.0f);
    CHECK(result.spectralFlatness == 0.0f);
}

// =============================================================================
// Onset Detection
// =============================================================================

TEST_CASE("AudioAnalysis: Click impulses detected as onsets", "[audio_analysis]") {
    // Generate 1 second of silence with 4 impulse clicks
    uint32_t frames = SAMPLE_RATE;
    std::vector<float> samples(frames * CHANNELS, 0.0f);
    // Place clicks at 0.1s, 0.3s, 0.6s, 0.9s
    uint32_t clickFrames[] = {4800, 14400, 28800, 43200};
    for (auto cf : clickFrames) {
        if (cf < frames) {
            // Short impulse burst (50 samples)
            for (uint32_t j = 0; j < 50 && cf + j < frames; j++) {
                float v = 0.8f * (1.0f - static_cast<float>(j) / 50.0f);
                samples[(cf + j) * CHANNELS] = v;
                samples[(cf + j) * CHANNELS + 1] = v;
            }
        }
    }
    auto result = vivid::analyzeAudioBuffer(samples.data(), frames, CHANNELS, SAMPLE_RATE);

    // Should detect some onsets (exact count depends on threshold)
    CHECK(result.onsetCount >= 2);
    CHECK(result.onsetCount <= 6);
    CHECK(result.onsetDensity > 1.0f);
}

TEST_CASE("AudioAnalysis: Constant sine has zero onsets", "[audio_analysis]") {
    auto sine = generateSine(440.0f, SAMPLE_RATE, 0.5f);
    auto result = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    CHECK(result.onsetCount == 0);
    CHECK(result.onsetDensity == 0.0f);
}

TEST_CASE("AudioAnalysis: Short buffer has zero onsets", "[audio_analysis]") {
    auto sine = generateSine(440.0f, 512, 0.5f);
    auto result = vivid::analyzeAudioBuffer(sine.data(), 512, CHANNELS, SAMPLE_RATE);

    CHECK(result.onsetCount == 0);
}

// =============================================================================
// LUFS Loudness
// =============================================================================

TEST_CASE("AudioAnalysis: LUFS of 1kHz sine at -23 dBFS", "[audio_analysis]") {
    // -23 dBFS = amplitude of 10^(-23/20) ≈ 0.0708
    // For K-weighted 1kHz sine, integrated LUFS should be close to -23
    float amplitude = std::pow(10.0f, -23.0f / 20.0f);
    auto sine = generateSine(1000.0f, SAMPLE_RATE * 2, amplitude);  // 2 seconds
    auto result = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE * 2, CHANNELS, SAMPLE_RATE);

    CHECK(std::isfinite(result.integratedLUFS));
    CHECK(result.integratedLUFS > -26.0f);
    CHECK(result.integratedLUFS < -20.0f);
}

TEST_CASE("AudioAnalysis: LUFS of silence is -inf", "[audio_analysis]") {
    auto silence = generateSilence(SAMPLE_RATE);
    auto result = vivid::analyzeAudioBuffer(silence.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    CHECK(!std::isfinite(result.integratedLUFS));
    CHECK(!std::isfinite(result.momentaryLUFS));
}

TEST_CASE("AudioAnalysis: True peak for 0.9 amp sine >= 0.9", "[audio_analysis]") {
    auto sine = generateSine(1000.0f, SAMPLE_RATE, 0.9f);
    auto result = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    CHECK(result.truePeak >= 0.9f);
}

TEST_CASE("AudioAnalysis: LUFS JSON serializes -inf as null", "[audio_analysis]") {
    auto silence = generateSilence(SAMPLE_RATE);
    auto result = vivid::analyzeAudioBuffer(silence.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    std::string json = result.toJSON();
    CHECK(json.find("\"integratedLUFS\":null") != std::string::npos);
    CHECK(json.find("\"momentaryLUFS\":null") != std::string::npos);
}

TEST_CASE("AudioAnalysis: Short buffer skips LUFS", "[audio_analysis]") {
    // Less than sampleRate/10 frames
    auto sine = generateSine(440.0f, 1000, 0.5f);
    auto result = vivid::analyzeAudioBuffer(sine.data(), 1000, CHANNELS, SAMPLE_RATE);

    CHECK(!std::isfinite(result.integratedLUFS));
}

// =============================================================================
// Pitch Detection (YIN)
// =============================================================================

TEST_CASE("AudioAnalysis: A4 sine pitch detection", "[audio_analysis]") {
    auto sine = generateSine(440.0f, SAMPLE_RATE, 0.5f);
    auto result = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    CHECK(result.pitchHz > 430.0f);
    CHECK(result.pitchHz < 450.0f);
    CHECK(result.pitchConfidence > 0.8f);
    CHECK(result.pitchNote == "A4");
}

TEST_CASE("AudioAnalysis: Noise has low pitch confidence", "[audio_analysis]") {
    auto noise = generateNoise(SAMPLE_RATE, 0.3f);
    auto result = vivid::analyzeAudioBuffer(noise.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    CHECK(result.pitchConfidence < 0.5f);
}

TEST_CASE("AudioAnalysis: HNR sine vs noise", "[audio_analysis]") {
    auto sine = generateSine(440.0f, SAMPLE_RATE, 0.5f);
    auto noise = generateNoise(SAMPLE_RATE, 0.3f);
    auto sinResult = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);
    auto noiseResult = vivid::analyzeAudioBuffer(noise.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    // Sine has high HNR, noise has low HNR
    CHECK(sinResult.harmonicToNoiseRatio > 15.0f);
    // Noise may have 0 HNR if pitch confidence is too low
    CHECK(noiseResult.harmonicToNoiseRatio < 10.0f);
}

// =============================================================================
// Dynamic Range
// =============================================================================

TEST_CASE("AudioAnalysis: Constant sine has low dynamic range", "[audio_analysis]") {
    auto sine = generateSine(440.0f, SAMPLE_RATE, 0.5f);
    auto result = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE, CHANNELS, SAMPLE_RATE);

    CHECK(result.dynamicRangeDB < 1.0f);
    CHECK(result.dynamicRangeCoeffVar < 0.1f);
}

TEST_CASE("AudioAnalysis: AM signal has higher dynamic range", "[audio_analysis]") {
    // Amplitude-modulated sine: carrier 440Hz, modulator 2Hz
    uint32_t frames = SAMPLE_RATE * 2;  // 2 seconds
    std::vector<float> am(frames * CHANNELS);
    for (uint32_t i = 0; i < frames; i++) {
        float mod = 0.5f + 0.5f * std::sin(2.0f * M_PI * 2.0f * i / SAMPLE_RATE);
        float s = mod * 0.5f * std::sin(2.0f * M_PI * 440.0f * i / SAMPLE_RATE);
        am[i * CHANNELS] = s;
        am[i * CHANNELS + 1] = s;
    }
    auto result = vivid::analyzeAudioBuffer(am.data(), frames, CHANNELS, SAMPLE_RATE);

    CHECK(result.dynamicRangeDB > 6.0f);
}

// =============================================================================
// Edge Cases
// =============================================================================

TEST_CASE("AudioAnalysis: Null input", "[audio_analysis]") {
    auto result = vivid::analyzeAudioBuffer(nullptr, 0, 0, 0);
    CHECK(result.isSilent == true);
    CHECK(result.rmsLevel == 0.0f);
    CHECK(result.duration == 0.0f);
}

TEST_CASE("AudioAnalysis: Mono signal", "[audio_analysis]") {
    // Generate mono sine
    uint32_t frames = SAMPLE_RATE;
    std::vector<float> mono(frames);
    for (uint32_t i = 0; i < frames; i++) {
        mono[i] = 0.5f * std::sin(2.0f * M_PI * 440.0f * i / SAMPLE_RATE);
    }

    auto result = vivid::analyzeAudioBuffer(mono.data(), frames, 1, SAMPLE_RATE);
    CHECK(result.isSilent == false);
    // Mono: left and right should be the same
    CHECK_THAT(result.rmsLeft, WithinAbs(result.rmsRight, 0.001f));
}

// =============================================================================
// JSON Serialization
// =============================================================================

TEST_CASE("AudioAnalysis: toJSON", "[audio_analysis]") {
    auto sine = generateSine(440.0f, SAMPLE_RATE / 10, 0.5f);
    auto result = vivid::analyzeAudioBuffer(sine.data(), SAMPLE_RATE / 10, CHANNELS, SAMPLE_RATE);

    std::string json = result.toJSON();
    CHECK(json.find("\"rmsLevel\"") != std::string::npos);
    CHECK(json.find("\"peakLevel\"") != std::string::npos);
    CHECK(json.find("\"isSilent\"") != std::string::npos);
    CHECK(json.find("\"spectrum\"") != std::string::npos);
    CHECK(json.find("\"subBass\"") != std::string::npos);
    CHECK(json.find("\"duration\"") != std::string::npos);
    CHECK(json.find("\"dcOffset\"") != std::string::npos);
    CHECK(json.find("\"clippedSampleCount\"") != std::string::npos);
    CHECK(json.find("\"clippedSamplePct\"") != std::string::npos);
    CHECK(json.find("\"zeroCrossingRate\"") != std::string::npos);
    CHECK(json.find("\"stereoCorrelation\"") != std::string::npos);
    CHECK(json.find("\"stereoWidth\"") != std::string::npos);
    CHECK(json.find("\"spectralCentroid\"") != std::string::npos);
    CHECK(json.find("\"spectralFlatness\"") != std::string::npos);
    CHECK(json.find("\"onsetCount\"") != std::string::npos);
    CHECK(json.find("\"integratedLUFS\"") != std::string::npos);
    CHECK(json.find("\"truePeak\"") != std::string::npos);
    CHECK(json.find("\"loudnessRange\"") != std::string::npos);
    CHECK(json.find("\"pitchHz\"") != std::string::npos);
    CHECK(json.find("\"pitchConfidence\"") != std::string::npos);
    CHECK(json.find("\"pitchNote\"") != std::string::npos);
    CHECK(json.find("\"harmonicToNoiseRatio\"") != std::string::npos);
    CHECK(json.find("\"dynamicRangeDB\"") != std::string::npos);
    CHECK(json.find("\"dynamicRangeCoeffVar\"") != std::string::npos);
}

// =============================================================================
// WAV Round-trip
// =============================================================================

TEST_CASE("WAV: Write and read round-trip", "[wav]") {
    auto sine = generateSine(440.0f, SAMPLE_RATE / 2, 0.5f);  // 0.5 seconds
    uint32_t frames = SAMPLE_RATE / 2;

    std::string path = "/tmp/vivid_test_roundtrip.wav";

    REQUIRE(vivid::writeWAV(path, sine.data(), frames, CHANNELS, SAMPLE_RATE));

    std::vector<float> readSamples;
    uint32_t readFrames, readChannels, readSampleRate;
    REQUIRE(vivid::readWAV(path, readSamples, readFrames, readChannels, readSampleRate));

    CHECK(readFrames == frames);
    CHECK(readChannels == CHANNELS);
    CHECK(readSampleRate == SAMPLE_RATE);

    // Samples should match (float precision)
    for (uint32_t i = 0; i < std::min((uint32_t)100, frames * CHANNELS); i++) {
        CHECK_THAT(readSamples[i], WithinAbs(sine[i], 0.0001f));
    }

    // Clean up
    std::filesystem::remove(path);
}
