/**
 * @file test_frame_analysis_assertions.cpp
 * @brief Tests for new FrameAnalysis Tier 1 field assertion path resolution
 *
 * Verifies that the assertion system can resolve all new output.* paths
 * (textureEntropy, edgeDensity, clipping, sharpness, visual center, color
 * temperature, hue histogram, alpha stats) from FrameAnalysis structs.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vivid/assertion.h>
#include <vivid/chain.h>

using Catch::Matchers::WithinAbs;

// Build a ChainInspection with known FrameAnalysis values for all new fields
static vivid::ChainInspection makeTestInspection() {
    vivid::ChainInspection insp;
    insp.frame = 10;
    insp.time = 0.333f;

    auto& fa = insp.outputAnalysis;
    fa.meanBrightness = 0.5f;
    fa.contrast = 0.2f;

    // Tier 1 fields
    fa.textureEntropy = 0.72f;
    fa.edgeDensity = 0.15f;
    fa.avgGradientMag = 0.08f;
    fa.clipBlackPct = 0.02f;
    fa.clipWhitePct = 0.01f;
    fa.headroom = 0.05f;
    fa.rangeSpan = 0.93f;
    fa.sharpness = 0.012f;
    fa.noiseLevel = 0.008f;
    fa.visualCenterX = 0.48f;
    fa.visualCenterY = 0.52f;
    fa.colorTemperature = 0.65f;
    fa.hueHistogram = {0.3f, 0.1f, 0.05f, 0.02f, 0.01f, 0.01f,
                       0.01f, 0.02f, 0.05f, 0.1f, 0.2f, 0.13f};
    fa.uniqueHueCount = 5;
    fa.hueEntropy = 0.68f;
    fa.alphaOpaquePct = 0.85f;
    fa.alphaTransparentPct = 0.05f;
    fa.alphaPartialPct = 0.10f;
    fa.alphaMean = 0.92f;

    return insp;
}

// =============================================================================
// resolvePath: output.* scalar fields (Tier 1)
// =============================================================================

TEST_CASE("resolvePath: output.textureEntropy", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.textureEntropy", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.72, 0.001));
}

TEST_CASE("resolvePath: output.edgeDensity", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.edgeDensity", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.15, 0.001));
}

TEST_CASE("resolvePath: output.avgGradientMag", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.avgGradientMag", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.08, 0.001));
}

TEST_CASE("resolvePath: output.clipBlackPct", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.clipBlackPct", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.02, 0.001));
}

TEST_CASE("resolvePath: output.clipWhitePct", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.clipWhitePct", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.01, 0.001));
}

TEST_CASE("resolvePath: output.headroom", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.headroom", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.05, 0.001));
}

TEST_CASE("resolvePath: output.rangeSpan", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.rangeSpan", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.93, 0.001));
}

TEST_CASE("resolvePath: output.sharpness", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.sharpness", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.012, 0.001));
}

TEST_CASE("resolvePath: output.noiseLevel", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.noiseLevel", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.008, 0.001));
}

TEST_CASE("resolvePath: output.visualCenterX", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.visualCenterX", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.48, 0.001));
}

TEST_CASE("resolvePath: output.visualCenterY", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.visualCenterY", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.52, 0.001));
}

TEST_CASE("resolvePath: output.colorTemperature", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.colorTemperature", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.65, 0.001));
}

TEST_CASE("resolvePath: output.uniqueHueCount", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.uniqueHueCount", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(5.0, 0.001));
}

TEST_CASE("resolvePath: output.hueEntropy", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.hueEntropy", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.68, 0.001));
}

TEST_CASE("resolvePath: output.alphaOpaquePct", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.alphaOpaquePct", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.85, 0.001));
}

TEST_CASE("resolvePath: output.alphaTransparentPct", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.alphaTransparentPct", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.05, 0.001));
}

TEST_CASE("resolvePath: output.alphaPartialPct", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.alphaPartialPct", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.10, 0.001));
}

TEST_CASE("resolvePath: output.alphaMean", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.alphaMean", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.92, 0.001));
}

// =============================================================================
// resolvePath: output.hueHistogram.N indexed access
// =============================================================================

TEST_CASE("resolvePath: output.hueHistogram.0", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.hueHistogram.0", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.3, 0.001));
}

TEST_CASE("resolvePath: output.hueHistogram.5", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.hueHistogram.5", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.01, 0.001));
}

TEST_CASE("resolvePath: output.hueHistogram.11", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.hueHistogram.11", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.13, 0.001));
}

TEST_CASE("resolvePath: output.hueHistogram.12 out of range", "[assertions][tier1]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("output.hueHistogram.12", insp);
    CHECK(!found);
}

// =============================================================================
// Per-operator textureAnalysis with new Tier 1 fields
// =============================================================================

static vivid::ChainInspection makeInspectionWithTier1TextureAnalysis() {
    auto insp = makeTestInspection();

    vivid::InspectData bloomData;
    bloomData.set("threshold", 0.6f);
    vivid::FrameAnalysis bloomTex;
    bloomTex.meanBrightness = 0.15f;
    bloomTex.textureEntropy = 0.45f;
    bloomTex.edgeDensity = 0.08f;
    bloomTex.sharpness = 0.005f;
    bloomTex.colorTemperature = 0.55f;
    bloomTex.hueHistogram = {0.5f, 0.2f, 0.1f, 0.05f, 0.05f, 0.02f,
                             0.02f, 0.01f, 0.01f, 0.01f, 0.01f, 0.02f};
    bloomData.textureAnalysis = bloomTex;
    insp.operators.push_back({"bloom", bloomData});

    return insp;
}

TEST_CASE("resolvePath: per-operator textureAnalysis.textureEntropy", "[assertions][tier1][textureAnalysis]") {
    auto insp = makeInspectionWithTier1TextureAnalysis();
    auto [found, val] = vivid::resolvePath("operators.bloom.textureAnalysis.textureEntropy", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.45, 0.001));
}

TEST_CASE("resolvePath: per-operator textureAnalysis.edgeDensity", "[assertions][tier1][textureAnalysis]") {
    auto insp = makeInspectionWithTier1TextureAnalysis();
    auto [found, val] = vivid::resolvePath("operators.bloom.textureAnalysis.edgeDensity", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.08, 0.001));
}

TEST_CASE("resolvePath: per-operator textureAnalysis.sharpness", "[assertions][tier1][textureAnalysis]") {
    auto insp = makeInspectionWithTier1TextureAnalysis();
    auto [found, val] = vivid::resolvePath("operators.bloom.textureAnalysis.sharpness", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.005, 0.001));
}

TEST_CASE("resolvePath: per-operator textureAnalysis.colorTemperature", "[assertions][tier1][textureAnalysis]") {
    auto insp = makeInspectionWithTier1TextureAnalysis();
    auto [found, val] = vivid::resolvePath("operators.bloom.textureAnalysis.colorTemperature", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.55, 0.001));
}

TEST_CASE("resolvePath: per-operator textureAnalysis.hueHistogram.0", "[assertions][tier1][textureAnalysis]") {
    auto insp = makeInspectionWithTier1TextureAnalysis();
    auto [found, val] = vivid::resolvePath("operators.bloom.textureAnalysis.hueHistogram.0", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.5, 0.001));
}

// =============================================================================
// resolvePath: temporal.* paths
// =============================================================================

static vivid::ChainInspection makeInspectionWithTemporal() {
    auto insp = makeTestInspection();

    vivid::TemporalAnalysis ta;
    ta.flickerScore = 0.15f;
    ta.flickerFrequency = 7.5f;
    ta.frameDelta = 0.03f;
    ta.convergenceScore = 0.8f;
    ta.isConverged = true;
    ta.motionMagnitude = 0.05f;
    ta.regionMotion = {0.01f, 0.02f, 0.01f, 0.03f, 0.08f, 0.03f, 0.01f, 0.02f, 0.01f};
    ta.frameDiversity = 0.0005f;
    ta.isFrozen = false;
    ta.isLooping = true;
    ta.loopPeriodSeconds = 2.1f;
    ta.loopPeriodFrames = 126;
    ta.loopConfidence = 0.85f;
    ta.noveltyScore = 0.07f;
    ta.noveltyTrend = 0.6f;
    ta.keyframeCount = 4;
    insp.temporalAnalysis = ta;

    return insp;
}

TEST_CASE("resolvePath: temporal.flickerScore", "[assertions][temporal]") {
    auto insp = makeInspectionWithTemporal();
    auto [found, val] = vivid::resolvePath("temporal.flickerScore", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.15, 0.001));
}

TEST_CASE("resolvePath: temporal.flickerFrequency", "[assertions][temporal]") {
    auto insp = makeInspectionWithTemporal();
    auto [found, val] = vivid::resolvePath("temporal.flickerFrequency", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(7.5, 0.001));
}

TEST_CASE("resolvePath: temporal.frameDelta", "[assertions][temporal]") {
    auto insp = makeInspectionWithTemporal();
    auto [found, val] = vivid::resolvePath("temporal.frameDelta", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.03, 0.001));
}

TEST_CASE("resolvePath: temporal.convergenceScore", "[assertions][temporal]") {
    auto insp = makeInspectionWithTemporal();
    auto [found, val] = vivid::resolvePath("temporal.convergenceScore", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.8, 0.001));
}

TEST_CASE("resolvePath: temporal.isConverged (true)", "[assertions][temporal]") {
    auto insp = makeInspectionWithTemporal();
    auto [found, val] = vivid::resolvePath("temporal.isConverged", insp);
    REQUIRE(found);
    CHECK(val == 1.0);
}

TEST_CASE("resolvePath: temporal.motionMagnitude", "[assertions][temporal]") {
    auto insp = makeInspectionWithTemporal();
    auto [found, val] = vivid::resolvePath("temporal.motionMagnitude", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.05, 0.001));
}

TEST_CASE("resolvePath: temporal.regionMotion.4", "[assertions][temporal]") {
    auto insp = makeInspectionWithTemporal();
    auto [found, val] = vivid::resolvePath("temporal.regionMotion.4", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.08, 0.001));
}

TEST_CASE("resolvePath: temporal.regionMotion.9 out of range", "[assertions][temporal]") {
    auto insp = makeInspectionWithTemporal();
    auto [found, val] = vivid::resolvePath("temporal.regionMotion.9", insp);
    CHECK(!found);
}

TEST_CASE("resolvePath: temporal.frameDiversity", "[assertions][temporal]") {
    auto insp = makeInspectionWithTemporal();
    auto [found, val] = vivid::resolvePath("temporal.frameDiversity", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.0005, 0.0001));
}

TEST_CASE("resolvePath: temporal.isFrozen (false)", "[assertions][temporal]") {
    auto insp = makeInspectionWithTemporal();
    auto [found, val] = vivid::resolvePath("temporal.isFrozen", insp);
    REQUIRE(found);
    CHECK(val == 0.0);
}

TEST_CASE("resolvePath: temporal.isLooping (true)", "[assertions][temporal]") {
    auto insp = makeInspectionWithTemporal();
    auto [found, val] = vivid::resolvePath("temporal.isLooping", insp);
    REQUIRE(found);
    CHECK(val == 1.0);
}

TEST_CASE("resolvePath: temporal.loopPeriodSeconds", "[assertions][temporal]") {
    auto insp = makeInspectionWithTemporal();
    auto [found, val] = vivid::resolvePath("temporal.loopPeriodSeconds", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(2.1, 0.001));
}

TEST_CASE("resolvePath: temporal.loopPeriodFrames", "[assertions][temporal]") {
    auto insp = makeInspectionWithTemporal();
    auto [found, val] = vivid::resolvePath("temporal.loopPeriodFrames", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(126.0, 0.001));
}

TEST_CASE("resolvePath: temporal.loopConfidence", "[assertions][temporal]") {
    auto insp = makeInspectionWithTemporal();
    auto [found, val] = vivid::resolvePath("temporal.loopConfidence", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.85, 0.001));
}

TEST_CASE("resolvePath: temporal.noveltyScore", "[assertions][temporal]") {
    auto insp = makeInspectionWithTemporal();
    auto [found, val] = vivid::resolvePath("temporal.noveltyScore", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.07, 0.001));
}

TEST_CASE("resolvePath: temporal.noveltyTrend", "[assertions][temporal]") {
    auto insp = makeInspectionWithTemporal();
    auto [found, val] = vivid::resolvePath("temporal.noveltyTrend", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.6, 0.001));
}

TEST_CASE("resolvePath: temporal.keyframeCount", "[assertions][temporal]") {
    auto insp = makeInspectionWithTemporal();
    auto [found, val] = vivid::resolvePath("temporal.keyframeCount", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(4.0, 0.001));
}

// temporal paths return false when no temporal analysis present
TEST_CASE("resolvePath: temporal.* without temporal data returns false", "[assertions][temporal]") {
    auto insp = makeTestInspection(); // no temporalAnalysis set
    auto [found, val] = vivid::resolvePath("temporal.flickerScore", insp);
    CHECK(!found);
}

TEST_CASE("resolvePath: temporal.nonexistent returns false", "[assertions][temporal]") {
    auto insp = makeInspectionWithTemporal();
    auto [found, val] = vivid::resolvePath("temporal.nonexistent", insp);
    CHECK(!found);
}

// =============================================================================
// evaluateAssertions with new Tier 1 fields
// =============================================================================

TEST_CASE("evaluateAssertions: output.textureEntropy > threshold passes", "[assertions][tier1]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "output.textureEntropy";
    a.op = ">";
    a.value = 0.3;
    a.message = "Has visual complexity";

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
    CHECK_THAT(report.results[0].actual, WithinAbs(0.72, 0.001));
}

TEST_CASE("evaluateAssertions: output.clipBlackPct < threshold passes", "[assertions][tier1]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "output.clipBlackPct";
    a.op = "<";
    a.value = 0.5;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
}

TEST_CASE("evaluateAssertions: output.visualCenterX between passes", "[assertions][tier1]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "output.visualCenterX";
    a.op = "between";
    a.value = 0.4;
    a.valueHigh = 0.6;
    a.message = "Content is horizontally centered";

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
}

TEST_CASE("evaluateAssertions: temporal.isLooping == 1 passes", "[assertions][temporal]") {
    auto insp = makeInspectionWithTemporal();

    vivid::Assertion a;
    a.path = "temporal.isLooping";
    a.op = "==";
    a.value = 1.0;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
}
