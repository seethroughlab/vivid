/**
 * @file test_temporal_analysis.cpp
 * @brief Unit tests for TemporalAnalyzer
 *
 * Tests temporal metrics with synthetic pixel sequences.
 * All CPU-based — no GPU needed.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vivid/temporal_analysis.h>
#include <cmath>
#include <vector>
#include <cstdint>

using Catch::Matchers::WithinAbs;

static constexpr int W = 64;
static constexpr int H = 64;
static constexpr int PIXEL_COUNT = W * H;
static constexpr int BYTE_COUNT = PIXEL_COUNT * 4;

// Generate a uniform gray frame (RGBA8)
static std::vector<uint8_t> makeGrayFrame(uint8_t gray, uint8_t alpha = 255) {
    std::vector<uint8_t> frame(BYTE_COUNT);
    for (int i = 0; i < PIXEL_COUNT; ++i) {
        frame[i * 4 + 0] = gray;
        frame[i * 4 + 1] = gray;
        frame[i * 4 + 2] = gray;
        frame[i * 4 + 3] = alpha;
    }
    return frame;
}

// Generate a horizontal gradient frame (brightness increases left to right)
static std::vector<uint8_t> makeGradientFrame(float offset = 0.0f) {
    std::vector<uint8_t> frame(BYTE_COUNT);
    for (int y = 0; y < H; ++y) {
        for (int x = 0; x < W; ++x) {
            float val = std::fmod(static_cast<float>(x) / W + offset, 1.0f);
            uint8_t v = static_cast<uint8_t>(val * 255.0f);
            int idx = (y * W + x) * 4;
            frame[idx + 0] = v;
            frame[idx + 1] = v;
            frame[idx + 2] = v;
            frame[idx + 3] = 255;
        }
    }
    return frame;
}

// =============================================================================
// Frozen detection: identical frames → isFrozen = true, motionMagnitude ≈ 0
// =============================================================================

TEST_CASE("TemporalAnalyzer: frozen frames", "[temporal]") {
    vivid::TemporalAnalyzer::Config config;
    config.windowSize = 16;
    config.downsampleWidth = W;
    config.downsampleHeight = H;
    vivid::TemporalAnalyzer analyzer(config);

    auto frame = makeGrayFrame(128);
    for (int i = 0; i < 16; ++i) {
        analyzer.pushFrame(frame.data(), W, H);
    }

    auto result = analyzer.analyze();
    CHECK(result.isFrozen);
    CHECK(result.motionMagnitude < 0.001f);
    CHECK(result.frameDelta < 0.001f);
}

// =============================================================================
// Steady motion: scrolling gradient → isFrozen = false, motionMagnitude > 0
// =============================================================================

TEST_CASE("TemporalAnalyzer: steady motion (scrolling gradient)", "[temporal]") {
    vivid::TemporalAnalyzer::Config config;
    config.windowSize = 16;
    config.downsampleWidth = W;
    config.downsampleHeight = H;
    vivid::TemporalAnalyzer analyzer(config);

    for (int i = 0; i < 16; ++i) {
        auto frame = makeGradientFrame(static_cast<float>(i) / 16.0f);
        analyzer.pushFrame(frame.data(), W, H);
    }

    auto result = analyzer.analyze();
    CHECK(!result.isFrozen);
    CHECK(result.motionMagnitude > 0.0f);
}

// =============================================================================
// Flicker: alternating bright/dark → flickerScore > 0.8
// =============================================================================

TEST_CASE("TemporalAnalyzer: flicker detection", "[temporal]") {
    vivid::TemporalAnalyzer::Config config;
    config.windowSize = 16;
    config.downsampleWidth = W;
    config.downsampleHeight = H;
    vivid::TemporalAnalyzer analyzer(config);

    auto bright = makeGrayFrame(220);
    auto dark = makeGrayFrame(30);

    for (int i = 0; i < 16; ++i) {
        if (i % 2 == 0) {
            analyzer.pushFrame(bright.data(), W, H);
        } else {
            analyzer.pushFrame(dark.data(), W, H);
        }
    }

    auto result = analyzer.analyze();
    CHECK(result.flickerScore > 0.8f);
}

// =============================================================================
// Convergence: gradually stabilizing → isConverged = true
// =============================================================================

TEST_CASE("TemporalAnalyzer: convergence detection", "[temporal]") {
    vivid::TemporalAnalyzer::Config config;
    config.windowSize = 32;
    config.brightnessHistorySize = 60;
    config.downsampleWidth = W;
    config.downsampleHeight = H;
    vivid::TemporalAnalyzer analyzer(config);

    // Start with large changes, then converge to stable
    for (int i = 0; i < 10; ++i) {
        // Varying brightness early on
        uint8_t gray = static_cast<uint8_t>(128 + (10 - i) * 10);
        auto frame = makeGrayFrame(gray);
        analyzer.pushFrame(frame.data(), W, H);
    }
    // Then many identical frames (need enough for EMA to decay below threshold + 8 consecutive)
    auto stableFrame = makeGrayFrame(128);
    for (int i = 0; i < 35; ++i) {
        analyzer.pushFrame(stableFrame.data(), W, H);
    }

    auto result = analyzer.analyze();
    CHECK(result.isConverged);
    CHECK(result.convergenceScore > 0.7f);
}

// =============================================================================
// Loop detection: repeating brightness pattern → isLooping = true
// =============================================================================

TEST_CASE("TemporalAnalyzer: loop detection (30-frame cycle)", "[temporal]") {
    vivid::TemporalAnalyzer::Config config;
    config.windowSize = 16;
    config.brightnessHistorySize = 120;
    config.downsampleWidth = W;
    config.downsampleHeight = H;
    config.fps = 60.0f;
    vivid::TemporalAnalyzer analyzer(config);

    // Generate 120 frames with a 30-frame brightness cycle
    for (int i = 0; i < 120; ++i) {
        float t = static_cast<float>(i % 30) / 30.0f;
        uint8_t gray = static_cast<uint8_t>(128 + 100 * std::sin(2.0f * 3.14159f * t));
        auto frame = makeGrayFrame(gray);
        analyzer.pushFrame(frame.data(), W, H);
    }

    auto result = analyzer.analyze();
    CHECK(result.isLooping);
    CHECK(result.loopPeriodFrames >= 28);
    CHECK(result.loopPeriodFrames <= 32);
    CHECK(result.loopConfidence > 0.7f);
}

// =============================================================================
// Novelty — static: identical frames → noveltyScore ≈ 0
// =============================================================================

TEST_CASE("TemporalAnalyzer: novelty - static frames", "[temporal]") {
    vivid::TemporalAnalyzer::Config config;
    config.windowSize = 16;
    config.brightnessHistorySize = 120;
    config.downsampleWidth = W;
    config.downsampleHeight = H;
    config.keyframeInterval = 10;
    config.keyframeBufferSize = 8;
    vivid::TemporalAnalyzer analyzer(config);

    auto frame = makeGrayFrame(128);
    for (int i = 0; i < 60; ++i) {
        analyzer.pushFrame(frame.data(), W, H);
    }

    auto result = analyzer.analyze();
    CHECK(result.noveltyScore < 0.01f);
    CHECK(result.keyframeCount > 0);
}

// =============================================================================
// Novelty — evolving: gradually changing → noveltyScore > 0.05
// =============================================================================

TEST_CASE("TemporalAnalyzer: novelty - evolving content", "[temporal]") {
    vivid::TemporalAnalyzer::Config config;
    config.windowSize = 16;
    config.brightnessHistorySize = 120;
    config.downsampleWidth = W;
    config.downsampleHeight = H;
    config.keyframeInterval = 10;
    config.keyframeBufferSize = 8;
    vivid::TemporalAnalyzer analyzer(config);

    // Generate frames that gradually change brightness
    for (int i = 0; i < 60; ++i) {
        uint8_t gray = static_cast<uint8_t>(30 + i * 3);  // 30 → 207 over 60 frames
        auto frame = makeGrayFrame(gray);
        analyzer.pushFrame(frame.data(), W, H);
    }

    auto result = analyzer.analyze();
    CHECK(result.noveltyScore > 0.05f);
}

// =============================================================================
// Sparse mode: flicker invalid, motion valid
// =============================================================================

TEST_CASE("TemporalAnalyzer: sparse mode disables flicker", "[temporal]") {
    vivid::TemporalAnalyzer::Config config;
    config.windowSize = 16;
    config.downsampleWidth = W;
    config.downsampleHeight = H;
    vivid::TemporalAnalyzer analyzer(config);
    analyzer.setSparseMode(true);

    auto bright = makeGrayFrame(220);
    auto dark = makeGrayFrame(30);

    for (int i = 0; i < 10; ++i) {
        if (i % 2 == 0) {
            analyzer.pushFrame(bright.data(), W, H);
        } else {
            analyzer.pushFrame(dark.data(), W, H);
        }
    }

    auto result = analyzer.analyze();
    // In sparse mode, flicker should be 0 (invalid)
    CHECK(result.flickerScore == 0.0f);
    // But motion should still be detected
    CHECK(result.motionMagnitude > 0.0f);
}

// =============================================================================
// Reset: old history cleared
// =============================================================================

TEST_CASE("TemporalAnalyzer: reset clears state", "[temporal]") {
    vivid::TemporalAnalyzer::Config config;
    config.windowSize = 16;
    config.downsampleWidth = W;
    config.downsampleHeight = H;
    vivid::TemporalAnalyzer analyzer(config);

    // Push some frames
    auto frame1 = makeGrayFrame(200);
    for (int i = 0; i < 16; ++i) {
        analyzer.pushFrame(frame1.data(), W, H);
    }
    CHECK(analyzer.frameCount() == 16);

    // Reset
    analyzer.reset();
    CHECK(analyzer.frameCount() == 0);

    // Push 2 new frames
    auto frame2 = makeGrayFrame(100);
    analyzer.pushFrame(frame2.data(), W, H);
    analyzer.pushFrame(frame2.data(), W, H);
    CHECK(analyzer.frameCount() == 2);

    auto result = analyzer.analyze();
    CHECK(result.isFrozen);  // Both frames identical
}

// =============================================================================
// toJSON: serialization works
// =============================================================================

TEST_CASE("TemporalAnalysis::toJSON produces valid output", "[temporal]") {
    vivid::TemporalAnalysis ta;
    ta.flickerScore = 0.5f;
    ta.isLooping = true;
    ta.loopPeriodFrames = 30;

    std::string json = ta.toJSON();
    CHECK(json.find("flickerScore") != std::string::npos);
    CHECK(json.find("isLooping") != std::string::npos);
    CHECK(json.find("true") != std::string::npos);
    CHECK(json.find("loopPeriodFrames") != std::string::npos);
}
