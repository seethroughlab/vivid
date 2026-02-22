/**
 * @file test_av_analysis.cpp
 * @brief Unit tests for AudioVisualAnalyzer and AudioVisualAnalysis
 *
 * Tests correlation, band correlation, reactivity latency, onset response,
 * response magnitude, mutual information, and validity checks using
 * synthetic time-series data.
 */

#define _USE_MATH_DEFINES
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vivid/av_analysis.h>
#include <cmath>
#include <vector>

using Catch::Matchers::WithinAbs;

// =============================================================================
// Helpers
// =============================================================================

// Push N samples with custom audio/visual generators
static void pushSamples(vivid::AudioVisualAnalyzer& analyzer, int n,
                         std::function<float(int)> audioFn,
                         std::function<float(int)> brightnessFn,
                         std::function<float(int)> motionFn = nullptr,
                         std::function<bool(int)> onsetFn = nullptr) {
    for (int i = 0; i < n; i++) {
        vivid::AudioVisualAnalyzer::Sample s;
        s.audioRms = audioFn(i);
        s.brightness = brightnessFn(i);
        s.motionMagnitude = motionFn ? motionFn(i) : 0.0f;
        s.contrast = 0.2f;
        s.frameDelta = motionFn ? motionFn(i) : 0.0f;
        s.isOnset = onsetFn ? onsetFn(i) : false;
        // Fill spectrum with audioRms scaled to bands
        for (int b = 0; b < 6; b++) {
            s.audioSpectrum[b] = s.audioRms * (0.1f + 0.05f * b);
        }
        analyzer.pushSample(s);
    }
}

// Deterministic hash-based pseudo-random float in [0, 1]
// Different seeds produce independent sequences
static float pseudoRandom(int i, uint32_t seed = 12345) {
    // Murmur-style hash mixing for good independence
    uint32_t h = seed ^ (static_cast<uint32_t>(i) * 2654435761u);
    h ^= h >> 16;
    h *= 0x85ebca6bu;
    h ^= h >> 13;
    h *= 0xc2b2ae35u;
    h ^= h >> 16;
    return static_cast<float>(h) / static_cast<float>(UINT32_MAX);
}

// =============================================================================
// Tier 1: AV Correlation
// =============================================================================

TEST_CASE("AV: Perfect correlation (audioRms == brightness)", "[av_analysis]") {
    vivid::AudioVisualAnalyzer analyzer;
    auto fn = [](int i) { return 0.1f + 0.8f * std::sin(0.1f * i) * 0.5f + 0.5f; };
    pushSamples(analyzer, 60, fn, fn);

    auto result = analyzer.analyze();
    CHECK(result.valid);
    CHECK(result.avCorrelationBrightness > 0.95f);
    CHECK(result.avCorrelation > 0.95f);
}

TEST_CASE("AV: Inverse correlation (brightness = 1 - audioRms)", "[av_analysis]") {
    vivid::AudioVisualAnalyzer analyzer;
    auto audioFn = [](int i) { return 0.1f + 0.8f * (std::sin(0.1f * i) * 0.5f + 0.5f); };
    auto visFn = [](int i) { return 1.0f - (0.1f + 0.8f * (std::sin(0.1f * i) * 0.5f + 0.5f)); };
    pushSamples(analyzer, 60, audioFn, visFn);

    auto result = analyzer.analyze();
    CHECK(result.valid);
    CHECK(result.avCorrelationBrightness < -0.95f);
    CHECK(result.avCorrelation > 0.95f);  // Absolute value
}

TEST_CASE("AV: No correlation (random audio, random visual)", "[av_analysis]") {
    vivid::AudioVisualAnalyzer analyzer;
    auto audioFn = [](int i) { return pseudoRandom(i, 12345); };
    auto visFn = [](int i) { return pseudoRandom(i, 67890); };
    pushSamples(analyzer, 100, audioFn, visFn);

    auto result = analyzer.analyze();
    CHECK(result.valid);
    CHECK(result.avCorrelation < 0.3f);
}

TEST_CASE("AV: Motion correlation (audioRms drives motion)", "[av_analysis]") {
    vivid::AudioVisualAnalyzer analyzer;
    auto audioFn = [](int i) { return 0.5f * std::sin(0.15f * i) + 0.5f; };
    auto brightFn = [](int i) { return 0.5f; };  // Constant brightness
    auto motionFn = [](int i) { return 0.5f * std::sin(0.15f * i) + 0.5f; };  // Motion tracks audio
    pushSamples(analyzer, 60, audioFn, brightFn, motionFn);

    auto result = analyzer.analyze();
    CHECK(result.valid);
    CHECK(result.avCorrelationMotion > 0.95f);
    CHECK(result.avCorrelation > 0.95f);
}

// =============================================================================
// Validity checks
// =============================================================================

TEST_CASE("AV: Silent audio -> valid=false", "[av_analysis]") {
    vivid::AudioVisualAnalyzer analyzer;
    pushSamples(analyzer, 30,
        [](int) { return 0.0f; },
        [](int i) { return 0.5f + 0.3f * std::sin(0.1f * i); });

    auto result = analyzer.analyze();
    CHECK(!result.valid);
    CHECK(result.invalidReason.find("audio") != std::string::npos);
}

TEST_CASE("AV: Static visual -> valid=false", "[av_analysis]") {
    vivid::AudioVisualAnalyzer analyzer;
    pushSamples(analyzer, 30,
        [](int i) { return 0.5f + 0.3f * std::sin(0.1f * i); },
        [](int) { return 0.5f; },
        [](int) { return 0.0f; });  // Zero motion too

    auto result = analyzer.analyze();
    CHECK(!result.valid);
    CHECK(result.invalidReason.find("visual") != std::string::npos);
}

TEST_CASE("AV: Insufficient samples (5) -> valid=false", "[av_analysis]") {
    vivid::AudioVisualAnalyzer analyzer;
    pushSamples(analyzer, 5,
        [](int i) { return 0.5f + 0.3f * std::sin(0.1f * i); },
        [](int i) { return 0.5f + 0.3f * std::sin(0.1f * i); });

    auto result = analyzer.analyze();
    CHECK(!result.valid);
    CHECK(result.invalidReason.find("< 10") != std::string::npos);
}

// =============================================================================
// Tier 1: Band Correlation
// =============================================================================

TEST_CASE("AV: Band-specific (bass drives brightness, others flat)", "[av_analysis]") {
    vivid::AudioVisualAnalyzer analyzer;
    int n = 60;

    for (int i = 0; i < n; i++) {
        vivid::AudioVisualAnalyzer::Sample s;
        float bassLevel = 0.5f * std::sin(0.12f * i) + 0.5f;
        // audioRms varies (needed for valid analysis) but independently of brightness
        s.audioRms = 0.3f + 0.1f * std::sin(0.07f * i);
        s.audioSpectrum = {0.1f, bassLevel, 0.1f, 0.1f, 0.1f, 0.1f};  // Only bass varies
        s.brightness = bassLevel;  // Brightness tracks bass
        s.motionMagnitude = 0.1f + 0.05f * std::sin(0.05f * i);
        s.contrast = 0.2f;
        s.frameDelta = 0.0f;
        analyzer.pushSample(s);
    }

    auto result = analyzer.analyze();
    CHECK(result.valid);
    CHECK(result.bandCorrelations[1].correlation > 0.9f);  // bass (index 1)
    CHECK(result.bandCorrelations[1].drivesMetric == "brightness");
    // Other bands should have low correlation (they're constant)
    CHECK(result.bandCorrelations[0].correlation < 0.2f);  // subBass
    CHECK(result.bandCorrelations[3].correlation < 0.2f);  // mid
}

// =============================================================================
// Tier 1: Reactivity Latency
// =============================================================================

TEST_CASE("AV: Latency 2 frames (shifted visual)", "[av_analysis]") {
    vivid::AudioVisualAnalyzer analyzer;
    int n = 60;
    int shift = 2;

    for (int i = 0; i < n; i++) {
        vivid::AudioVisualAnalyzer::Sample s;
        s.audioRms = 0.5f * std::sin(0.15f * i) + 0.5f;
        // Visual is same signal but shifted by 2 frames
        int shifted = std::max(0, i - shift);
        s.brightness = 0.5f * std::sin(0.15f * shifted) + 0.5f;
        s.motionMagnitude = 0.0f;
        s.contrast = 0.2f;
        s.frameDelta = 0.0f;
        for (int b = 0; b < 6; b++) s.audioSpectrum[b] = s.audioRms * 0.1f;
        analyzer.pushSample(s);
    }

    auto result = analyzer.analyze();
    CHECK(result.valid);
    CHECK(result.reactivityLatencyFrames == shift);
    CHECK(result.reactivityPeakCorrelation > 0.8f);
}

TEST_CASE("AV: Latency zero (simultaneous)", "[av_analysis]") {
    vivid::AudioVisualAnalyzer analyzer;
    auto fn = [](int i) { return 0.5f * std::sin(0.15f * i) + 0.5f; };
    pushSamples(analyzer, 60, fn, fn);

    auto result = analyzer.analyze();
    CHECK(result.valid);
    CHECK(result.reactivityLatencyFrames == 0);
}

// =============================================================================
// Tier 2: Onset Response
// =============================================================================

TEST_CASE("AV: Onset response - all onsets produce response", "[av_analysis]") {
    vivid::AudioVisualAnalyzer::Config config;
    config.responseWindowFrames = 3;
    config.responseThreshold = 0.01f;
    vivid::AudioVisualAnalyzer configuredAnalyzer(config);

    int n = 60;
    std::vector<int> onsets = {10, 25, 40};

    for (int i = 0; i < n; i++) {
        vivid::AudioVisualAnalyzer::Sample s;
        // Varying audio and brightness for validity
        s.audioRms = 0.3f + 0.2f * std::sin(0.1f * i);
        s.brightness = 0.5f + 0.2f * std::sin(0.08f * i);
        s.motionMagnitude = 0.05f;
        s.contrast = 0.2f;

        bool isOnset = false;
        for (int o : onsets) {
            if (i == o) isOnset = true;
        }
        s.isOnset = isOnset;

        s.frameDelta = 0.001f;
        for (int o : onsets) {
            if (i > o && i <= o + 3) {
                s.frameDelta = 0.1f;
            }
        }

        for (int b = 0; b < 6; b++) s.audioSpectrum[b] = s.audioRms * 0.1f;
        configuredAnalyzer.pushSample(s);
    }

    auto result = configuredAnalyzer.analyze();
    CHECK(result.valid);
    CHECK(result.totalOnsetsEvaluated == 3);
    CHECK(result.onsetResponseCount == 3);
    CHECK_THAT(result.onsetResponseRate, WithinAbs(1.0, 0.01));
}

TEST_CASE("AV: Onset response - no visual response to onsets", "[av_analysis]") {
    vivid::AudioVisualAnalyzer::Config config;
    config.responseThreshold = 0.05f;
    vivid::AudioVisualAnalyzer configuredAnalyzer(config);

    int n = 60;
    for (int i = 0; i < n; i++) {
        vivid::AudioVisualAnalyzer::Sample s;
        s.audioRms = 0.3f + 0.2f * std::sin(0.1f * i);
        s.brightness = 0.5f + 0.2f * std::sin(0.08f * i);
        s.motionMagnitude = 0.05f;
        s.contrast = 0.2f;
        s.frameDelta = 0.001f;  // Low baseline, no response
        s.isOnset = (i == 10 || i == 25 || i == 40);
        for (int b = 0; b < 6; b++) s.audioSpectrum[b] = s.audioRms * 0.1f;
        configuredAnalyzer.pushSample(s);
    }

    auto result = configuredAnalyzer.analyze();
    CHECK(result.valid);
    CHECK(result.totalOnsetsEvaluated == 3);
    CHECK(result.onsetResponseCount == 0);
    CHECK_THAT(result.onsetResponseRate, WithinAbs(0.0, 0.01));
}

TEST_CASE("AV: Onset response - partial (2 of 3 onsets)", "[av_analysis]") {
    vivid::AudioVisualAnalyzer::Config config;
    config.responseWindowFrames = 3;
    config.responseThreshold = 0.03f;
    vivid::AudioVisualAnalyzer configuredAnalyzer(config);

    int n = 60;
    for (int i = 0; i < n; i++) {
        vivid::AudioVisualAnalyzer::Sample s;
        s.audioRms = 0.3f + 0.2f * std::sin(0.1f * i);
        s.brightness = 0.5f + 0.2f * std::sin(0.08f * i);
        s.motionMagnitude = 0.05f;
        s.contrast = 0.2f;
        s.frameDelta = 0.001f;
        s.isOnset = (i == 10 || i == 25 || i == 40);

        // Respond only to onsets at 10 and 40
        if ((i > 10 && i <= 13) || (i > 40 && i <= 43)) {
            s.frameDelta = 0.1f;
        }

        for (int b = 0; b < 6; b++) s.audioSpectrum[b] = s.audioRms * 0.1f;
        configuredAnalyzer.pushSample(s);
    }

    auto result = configuredAnalyzer.analyze();
    CHECK(result.valid);
    CHECK(result.totalOnsetsEvaluated == 3);
    CHECK(result.onsetResponseCount == 2);
    CHECK_THAT(result.onsetResponseRate, WithinAbs(2.0f / 3.0f, 0.05));
}

// =============================================================================
// Tier 2: Response Magnitude
// =============================================================================

TEST_CASE("AV: Response magnitude (post-onset delta > baseline)", "[av_analysis]") {
    vivid::AudioVisualAnalyzer::Config config;
    config.responseWindowFrames = 3;
    config.responseThreshold = 0.01f;
    vivid::AudioVisualAnalyzer configuredAnalyzer(config);

    int n = 60;
    for (int i = 0; i < n; i++) {
        vivid::AudioVisualAnalyzer::Sample s;
        s.audioRms = 0.3f + 0.2f * std::sin(0.1f * i);
        s.brightness = 0.5f + 0.2f * std::sin(0.08f * i);
        s.motionMagnitude = 0.05f;
        s.contrast = 0.2f;
        s.isOnset = (i == 10 || i == 30 || i == 50);

        // Large delta after onsets, small baseline
        s.frameDelta = 0.005f;
        if ((i > 10 && i <= 13) || (i > 30 && i <= 33) || (i > 50 && i <= 53)) {
            s.frameDelta = 0.08f;
        }

        for (int b = 0; b < 6; b++) s.audioSpectrum[b] = s.audioRms * 0.1f;
        configuredAnalyzer.pushSample(s);
    }

    auto result = configuredAnalyzer.analyze();
    CHECK(result.valid);
    CHECK(result.responseMagnitude > 0.0f);
    CHECK(result.avgPostOnsetDelta > result.avgBaselineDelta);
    CHECK(result.responseMagnitudeRatio > 2.0f);
}

// =============================================================================
// Tier 3: Mutual Information
// =============================================================================

TEST_CASE("AV: MI dependent (quantized mapping -> MI > 0.3)", "[av_analysis]") {
    vivid::AudioVisualAnalyzer analyzer;
    int n = 100;

    for (int i = 0; i < n; i++) {
        vivid::AudioVisualAnalyzer::Sample s;
        float audio = pseudoRandom(i, 11111);
        s.audioRms = audio;
        // Non-linear deterministic mapping: quantize audio to 4 levels
        float quantized = std::floor(audio * 4.0f) / 4.0f;
        s.brightness = quantized;
        s.motionMagnitude = 0.0f;
        s.contrast = 0.2f;
        s.frameDelta = 0.0f;
        for (int b = 0; b < 6; b++) s.audioSpectrum[b] = audio * 0.1f;
        analyzer.pushSample(s);
    }

    auto result = analyzer.analyze();
    CHECK(result.valid);
    CHECK(result.avMutualInformation > 0.3f);
}

TEST_CASE("AV: MI independent (unrelated -> MI < 0.3)", "[av_analysis]") {
    vivid::AudioVisualAnalyzer analyzer;
    int n = 500;

    for (int i = 0; i < n; i++) {
        vivid::AudioVisualAnalyzer::Sample s;
        s.audioRms = pseudoRandom(i, 11111);
        s.brightness = pseudoRandom(i, 99999);
        s.motionMagnitude = pseudoRandom(i, 55555) * 0.1f;
        s.contrast = 0.2f;
        s.frameDelta = 0.0f;
        for (int b = 0; b < 6; b++) s.audioSpectrum[b] = s.audioRms * 0.1f;
        analyzer.pushSample(s);
    }

    auto result = analyzer.analyze();
    CHECK(result.valid);
    CHECK(result.avMutualInformation < 0.3f);
}

// =============================================================================
// Metadata
// =============================================================================

TEST_CASE("AV: sampleCount is correct", "[av_analysis]") {
    vivid::AudioVisualAnalyzer analyzer;
    pushSamples(analyzer, 42,
        [](int i) { return 0.5f + 0.3f * std::sin(0.1f * i); },
        [](int i) { return 0.5f + 0.3f * std::sin(0.1f * i); });

    auto result = analyzer.analyze();
    CHECK(result.sampleCount == 42);
}

TEST_CASE("AV: reset clears all state", "[av_analysis]") {
    vivid::AudioVisualAnalyzer analyzer;
    pushSamples(analyzer, 30,
        [](int i) { return 0.5f + 0.3f * std::sin(0.1f * i); },
        [](int i) { return 0.5f + 0.3f * std::sin(0.1f * i); });

    CHECK(analyzer.sampleCount() == 30);
    analyzer.reset();
    CHECK(analyzer.sampleCount() == 0);
}

// =============================================================================
// JSON serialization
// =============================================================================

TEST_CASE("AV: toJSON contains all expected fields", "[av_analysis]") {
    vivid::AudioVisualAnalyzer analyzer;
    auto fn = [](int i) { return 0.5f + 0.3f * std::sin(0.1f * i); };
    pushSamples(analyzer, 30, fn, fn);

    auto result = analyzer.analyze();
    std::string json = result.toJSON();

    CHECK(json.find("\"avCorrelation\"") != std::string::npos);
    CHECK(json.find("\"avCorrelationBrightness\"") != std::string::npos);
    CHECK(json.find("\"avCorrelationMotion\"") != std::string::npos);
    CHECK(json.find("\"bandCorrelations\"") != std::string::npos);
    CHECK(json.find("\"bass\"") != std::string::npos);
    CHECK(json.find("\"drivesMetric\"") != std::string::npos);
    CHECK(json.find("\"reactivityLatencyFrames\"") != std::string::npos);
    CHECK(json.find("\"reactivityLatencyMs\"") != std::string::npos);
    CHECK(json.find("\"reactivityPeakCorrelation\"") != std::string::npos);
    CHECK(json.find("\"onsetResponseRate\"") != std::string::npos);
    CHECK(json.find("\"onsetResponseCount\"") != std::string::npos);
    CHECK(json.find("\"totalOnsetsEvaluated\"") != std::string::npos);
    CHECK(json.find("\"responseMagnitude\"") != std::string::npos);
    CHECK(json.find("\"responseMagnitudeRatio\"") != std::string::npos);
    CHECK(json.find("\"avMutualInformation\"") != std::string::npos);
    CHECK(json.find("\"avMutualInformationRaw\"") != std::string::npos);
    CHECK(json.find("\"sampleCount\"") != std::string::npos);
    CHECK(json.find("\"valid\"") != std::string::npos);
}

TEST_CASE("AV: toJSON includes invalidReason when invalid", "[av_analysis]") {
    vivid::AudioVisualAnalyzer analyzer;
    pushSamples(analyzer, 5,
        [](int i) { return 0.5f + 0.3f * std::sin(0.1f * i); },
        [](int i) { return 0.5f + 0.3f * std::sin(0.1f * i); });

    auto result = analyzer.analyze();
    std::string json = result.toJSON();

    CHECK(json.find("\"invalidReason\"") != std::string::npos);
    CHECK(json.find("\"valid\":false") != std::string::npos);
}
