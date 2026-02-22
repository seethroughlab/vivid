/**
 * @file test_av_assertions.cpp
 * @brief Tests for av.* assertion path resolution and evaluation
 *
 * Verifies that the assertion system can resolve av.* paths to
 * AudioVisualAnalysis fields on ChainInspection.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vivid/assertion.h>
#include <vivid/chain.h>
#include <vivid/av_analysis.h>

using Catch::Matchers::WithinAbs;

// Build a ChainInspection with known AV analysis values
static vivid::ChainInspection makeTestInspectionWithAV() {
    vivid::ChainInspection insp;
    insp.frame = 10;
    insp.time = 0.333f;
    insp.outputAnalysis.meanBrightness = 0.5f;
    insp.audioAnalysis.rmsLevel = 0.35f;

    vivid::AudioVisualAnalysis av;
    av.avCorrelation = 0.65f;
    av.avCorrelationBrightness = 0.58f;
    av.avCorrelationMotion = -0.62f;

    av.bandCorrelations[0] = {0.12f, "brightness", 0.12f};  // subBass
    av.bandCorrelations[1] = {0.71f, "brightness", 0.71f};  // bass
    av.bandCorrelations[2] = {0.35f, "motion", 0.35f};      // lowMid
    av.bandCorrelations[3] = {0.23f, "motion", -0.23f};     // mid
    av.bandCorrelations[4] = {0.15f, "contrast", 0.15f};    // highMid
    av.bandCorrelations[5] = {0.08f, "contrast", -0.08f};   // high

    av.reactivityLatencyFrames = 2;
    av.reactivityLatencyMs = 33.3f;
    av.reactivityPeakCorrelation = 0.68f;

    av.onsetResponseRate = 0.75f;
    av.onsetResponseCount = 6;
    av.totalOnsetsEvaluated = 8;
    av.responseMagnitude = 0.035f;
    av.responseMagnitudeRatio = 2.1f;
    av.avgPostOnsetDelta = 0.07f;
    av.avgBaselineDelta = 0.033f;

    av.avMutualInformation = 0.38f;
    av.avMutualInformationRaw = 0.52f;

    av.sampleCount = 90;
    av.durationSeconds = 3.0f;
    av.valid = true;

    insp.avAnalysis = av;
    return insp;
}

// =============================================================================
// resolvePath: av.* scalar fields
// =============================================================================

TEST_CASE("resolvePath: av.correlation", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.correlation", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.65, 0.001));
}

TEST_CASE("resolvePath: av.correlationBrightness", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.correlationBrightness", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.58, 0.001));
}

TEST_CASE("resolvePath: av.correlationMotion", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.correlationMotion", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(-0.62, 0.001));
}

TEST_CASE("resolvePath: av.reactivityLatencyFrames", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.reactivityLatencyFrames", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(2.0, 0.001));
}

TEST_CASE("resolvePath: av.reactivityLatencyMs", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.reactivityLatencyMs", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(33.3, 0.1));
}

TEST_CASE("resolvePath: av.reactivityPeakCorrelation", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.reactivityPeakCorrelation", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.68, 0.001));
}

TEST_CASE("resolvePath: av.onsetResponseRate", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.onsetResponseRate", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.75, 0.001));
}

TEST_CASE("resolvePath: av.onsetResponseCount", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.onsetResponseCount", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(6.0, 0.001));
}

TEST_CASE("resolvePath: av.totalOnsetsEvaluated", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.totalOnsetsEvaluated", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(8.0, 0.001));
}

TEST_CASE("resolvePath: av.responseMagnitude", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.responseMagnitude", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.035, 0.001));
}

TEST_CASE("resolvePath: av.responseMagnitudeRatio", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.responseMagnitudeRatio", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(2.1, 0.001));
}

TEST_CASE("resolvePath: av.avgPostOnsetDelta", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.avgPostOnsetDelta", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.07, 0.001));
}

TEST_CASE("resolvePath: av.avgBaselineDelta", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.avgBaselineDelta", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.033, 0.001));
}

TEST_CASE("resolvePath: av.mutualInformation", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.mutualInformation", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.38, 0.001));
}

TEST_CASE("resolvePath: av.mutualInformationRaw", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.mutualInformationRaw", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.52, 0.001));
}

TEST_CASE("resolvePath: av.sampleCount", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.sampleCount", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(90.0, 0.001));
}

TEST_CASE("resolvePath: av.durationSeconds", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.durationSeconds", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(3.0, 0.001));
}

TEST_CASE("resolvePath: av.valid", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.valid", insp);
    REQUIRE(found);
    CHECK(val == 1.0);
}

// =============================================================================
// resolvePath: av.bandCorrelation.* (per-band)
// =============================================================================

TEST_CASE("resolvePath: av.bandCorrelation.subBass", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.bandCorrelation.subBass", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.12, 0.001));
}

TEST_CASE("resolvePath: av.bandCorrelation.bass", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.bandCorrelation.bass", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.71, 0.001));
}

TEST_CASE("resolvePath: av.bandCorrelation.lowMid", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.bandCorrelation.lowMid", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.35, 0.001));
}

TEST_CASE("resolvePath: av.bandCorrelation.mid", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.bandCorrelation.mid", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.23, 0.001));
}

TEST_CASE("resolvePath: av.bandCorrelation.highMid", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.bandCorrelation.highMid", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.15, 0.001));
}

TEST_CASE("resolvePath: av.bandCorrelation.high", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.bandCorrelation.high", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.08, 0.001));
}

// =============================================================================
// resolvePath: invalid av paths
// =============================================================================

TEST_CASE("resolvePath: av.nonexistent returns false", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.nonexistent", insp);
    CHECK(!found);
}

TEST_CASE("resolvePath: av.bandCorrelation.nonexistent returns false", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av.bandCorrelation.nonexistent", insp);
    CHECK(!found);
}

TEST_CASE("resolvePath: av alone returns false", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();
    auto [found, val] = vivid::resolvePath("av", insp);
    CHECK(!found);
}

TEST_CASE("resolvePath: av.* returns false when avAnalysis is absent", "[assertions][av]") {
    vivid::ChainInspection insp;
    // No avAnalysis set
    auto [found, val] = vivid::resolvePath("av.correlation", insp);
    CHECK(!found);
}

// =============================================================================
// evaluateAssertions: av.* assertions
// =============================================================================

TEST_CASE("evaluateAssertions: av.correlation > 0.3 passes", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();

    vivid::Assertion a;
    a.path = "av.correlation";
    a.op = ">";
    a.value = 0.3;
    a.message = "Visuals respond to audio";

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
    CHECK(report.allPassed);
}

TEST_CASE("evaluateAssertions: av.onsetResponseRate > 0.5 passes", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();

    vivid::Assertion a;
    a.path = "av.onsetResponseRate";
    a.op = ">";
    a.value = 0.5;
    a.message = "Most beats produce visual change";

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
}

TEST_CASE("evaluateAssertions: av.reactivityLatencyMs < 100 passes", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();

    vivid::Assertion a;
    a.path = "av.reactivityLatencyMs";
    a.op = "<";
    a.value = 100.0;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
}

TEST_CASE("evaluateAssertions: av.bandCorrelation.bass > 0.5 passes", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();

    vivid::Assertion a;
    a.path = "av.bandCorrelation.bass";
    a.op = ">";
    a.value = 0.5;
    a.message = "Bass drives visual change";

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
    CHECK_THAT(report.results[0].actual, WithinAbs(0.71, 0.001));
}

TEST_CASE("evaluateAssertions: av.mutualInformation between range", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();

    vivid::Assertion a;
    a.path = "av.mutualInformation";
    a.op = "between";
    a.value = 0.2;
    a.valueHigh = 0.5;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
}

TEST_CASE("evaluateAssertions: mixed av + audio + visual assertions", "[assertions][av]") {
    auto insp = makeTestInspectionWithAV();

    std::vector<vivid::Assertion> assertions;

    vivid::Assertion visual;
    visual.path = "output.meanBrightness";
    visual.op = ">";
    visual.value = 0.1;
    assertions.push_back(visual);

    vivid::Assertion audio;
    audio.path = "audio.rmsLevel";
    audio.op = ">";
    audio.value = 0.01;
    assertions.push_back(audio);

    vivid::Assertion av;
    av.path = "av.correlation";
    av.op = ">";
    av.value = 0.3;
    assertions.push_back(av);

    auto report = vivid::evaluateAssertions(assertions, insp, 10);
    REQUIRE(report.results.size() == 3);
    CHECK(report.allPassed);
}
