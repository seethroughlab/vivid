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
