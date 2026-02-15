/**
 * @file test_audio_assertions.cpp
 * @brief Tests for audio.* assertion path resolution and evaluation
 *
 * Verifies that the assertion system can resolve audio.* paths to
 * AudioAnalysis fields on ChainInspection, and that evaluateAssertions()
 * correctly passes/fails audio-related assertions.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vivid/assertion.h>
#include <vivid/chain.h>
#include <vivid/audio_analysis.h>

using Catch::Matchers::WithinAbs;

// Build a ChainInspection with known audio values for testing
static vivid::ChainInspection makeTestInspection() {
    vivid::ChainInspection insp;
    insp.frame = 10;
    insp.time = 0.333f;

    // Audio analysis with known values
    auto& aa = insp.audioAnalysis;
    aa.rmsLevel = 0.35f;
    aa.peakLevel = 0.87f;
    aa.rmsLeft = 0.34f;
    aa.rmsRight = 0.36f;
    aa.isSilent = false;
    aa.crestFactor = 2.49f;
    aa.duration = 1.0f;
    aa.spectrum = {0.01f, 0.15f, 0.08f, 0.20f, 0.05f, 0.02f};
    // subBass=0.01, bass=0.15, lowMid=0.08, mid=0.20, highMid=0.05, high=0.02

    // Some visual data too (to verify audio paths don't interfere)
    insp.outputAnalysis.meanBrightness = 0.5f;
    insp.outputAnalysis.contrast = 0.2f;

    return insp;
}

// =============================================================================
// resolvePath: audio.* scalar fields
// =============================================================================

TEST_CASE("resolvePath: audio.rmsLevel", "[assertions][audio]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("audio.rmsLevel", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.35, 0.001));
}

TEST_CASE("resolvePath: audio.peakLevel", "[assertions][audio]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("audio.peakLevel", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.87, 0.001));
}

TEST_CASE("resolvePath: audio.rmsLeft", "[assertions][audio]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("audio.rmsLeft", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.34, 0.001));
}

TEST_CASE("resolvePath: audio.rmsRight", "[assertions][audio]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("audio.rmsRight", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.36, 0.001));
}

TEST_CASE("resolvePath: audio.crestFactor", "[assertions][audio]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("audio.crestFactor", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(2.49, 0.001));
}

TEST_CASE("resolvePath: audio.duration", "[assertions][audio]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("audio.duration", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(1.0, 0.001));
}

TEST_CASE("resolvePath: audio.isSilent (false)", "[assertions][audio]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("audio.isSilent", insp);
    REQUIRE(found);
    CHECK(val == 0.0);  // false -> 0.0
}

TEST_CASE("resolvePath: audio.isSilent (true)", "[assertions][audio]") {
    auto insp = makeTestInspection();
    insp.audioAnalysis.isSilent = true;
    auto [found, val] = vivid::resolvePath("audio.isSilent", insp);
    REQUIRE(found);
    CHECK(val == 1.0);  // true -> 1.0
}

// =============================================================================
// resolvePath: audio.spectrum.* bands
// =============================================================================

TEST_CASE("resolvePath: audio.spectrum.subBass", "[assertions][audio]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("audio.spectrum.subBass", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.01, 0.001));
}

TEST_CASE("resolvePath: audio.spectrum.bass", "[assertions][audio]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("audio.spectrum.bass", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.15, 0.001));
}

TEST_CASE("resolvePath: audio.spectrum.lowMid", "[assertions][audio]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("audio.spectrum.lowMid", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.08, 0.001));
}

TEST_CASE("resolvePath: audio.spectrum.mid", "[assertions][audio]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("audio.spectrum.mid", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.20, 0.001));
}

TEST_CASE("resolvePath: audio.spectrum.highMid", "[assertions][audio]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("audio.spectrum.highMid", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.05, 0.001));
}

TEST_CASE("resolvePath: audio.spectrum.high", "[assertions][audio]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("audio.spectrum.high", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.02, 0.001));
}

// =============================================================================
// resolvePath: invalid audio paths return false
// =============================================================================

TEST_CASE("resolvePath: audio.nonexistent returns false", "[assertions][audio]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("audio.nonexistent", insp);
    CHECK(!found);
}

TEST_CASE("resolvePath: audio.spectrum.nonexistent returns false", "[assertions][audio]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("audio.spectrum.nonexistent", insp);
    CHECK(!found);
}

TEST_CASE("resolvePath: audio alone returns false", "[assertions][audio]") {
    auto insp = makeTestInspection();
    auto [found, val] = vivid::resolvePath("audio", insp);
    CHECK(!found);
}

// =============================================================================
// resolvePath: audio paths don't break output paths
// =============================================================================

TEST_CASE("resolvePath: output paths still work alongside audio", "[assertions][audio]") {
    auto insp = makeTestInspection();

    auto [found1, val1] = vivid::resolvePath("output.meanBrightness", insp);
    REQUIRE(found1);
    CHECK_THAT(val1, WithinAbs(0.5, 0.001));

    auto [found2, val2] = vivid::resolvePath("audio.rmsLevel", insp);
    REQUIRE(found2);
    CHECK_THAT(val2, WithinAbs(0.35, 0.001));
}

// =============================================================================
// evaluateAssertions: audio assertions pass/fail correctly
// =============================================================================

TEST_CASE("evaluateAssertions: audio.rmsLevel > 0.01 passes", "[assertions][audio]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "audio.rmsLevel";
    a.op = ">";
    a.value = 0.01;
    a.message = "Audio not silent";

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
    CHECK(report.allPassed);
    CHECK_THAT(report.results[0].actual, WithinAbs(0.35, 0.001));
}

TEST_CASE("evaluateAssertions: audio.rmsLevel > 0.99 fails", "[assertions][audio]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "audio.rmsLevel";
    a.op = ">";
    a.value = 0.99;
    a.message = "Impossible threshold";

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(!report.results[0].passed);
    CHECK(!report.allPassed);
}

TEST_CASE("evaluateAssertions: audio.peakLevel < 0.95 passes (no clipping)", "[assertions][audio]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "audio.peakLevel";
    a.op = "<";
    a.value = 0.95;
    a.message = "No clipping";

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
}

TEST_CASE("evaluateAssertions: audio.spectrum.bass > threshold", "[assertions][audio]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "audio.spectrum.bass";
    a.op = ">";
    a.value = 0.05;
    a.message = "Should have bass";

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
    CHECK_THAT(report.results[0].actual, WithinAbs(0.15, 0.001));
}

TEST_CASE("evaluateAssertions: audio.isSilent == 0 passes (not silent)", "[assertions][audio]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "audio.isSilent";
    a.op = "==";
    a.value = 0.0;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
}

TEST_CASE("evaluateAssertions: mixed audio+visual assertions", "[assertions][audio]") {
    auto insp = makeTestInspection();

    std::vector<vivid::Assertion> assertions;

    // Visual assertion
    vivid::Assertion visual;
    visual.path = "output.meanBrightness";
    visual.op = ">";
    visual.value = 0.1;
    visual.message = "Not black";
    assertions.push_back(visual);

    // Audio assertion (pass)
    vivid::Assertion audioPass;
    audioPass.path = "audio.rmsLevel";
    audioPass.op = ">";
    audioPass.value = 0.01;
    audioPass.message = "Audio not silent";
    assertions.push_back(audioPass);

    // Audio spectrum assertion (pass)
    vivid::Assertion specPass;
    specPass.path = "audio.spectrum.mid";
    specPass.op = ">=";
    specPass.value = 0.10;
    specPass.message = "Mid content present";
    assertions.push_back(specPass);

    auto report = vivid::evaluateAssertions(assertions, insp, 10);
    REQUIRE(report.results.size() == 3);
    CHECK(report.allPassed);
    for (const auto& r : report.results) {
        CHECK(r.passed);
    }
}

TEST_CASE("evaluateAssertions: nonexistent audio path fails", "[assertions][audio]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "audio.nonexistent";
    a.op = ">";
    a.value = 0.0;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(!report.results[0].passed);
    CHECK(!report.allPassed);
}

// =============================================================================
// CheckReport output includes audio assertion results
// =============================================================================

TEST_CASE("CheckReport::toJSON includes audio assertion results", "[assertions][audio]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "audio.rmsLevel";
    a.op = ">";
    a.value = 0.01;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    std::string jsonStr = report.toJSON();

    CHECK(jsonStr.find("audio.rmsLevel") != std::string::npos);
    CHECK((jsonStr.find("\"passed\": true") != std::string::npos ||
           jsonStr.find("\"passed\":true") != std::string::npos));
}

TEST_CASE("CheckReport::toVerbose includes audio assertion results", "[assertions][audio]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "audio.spectrum.bass";
    a.op = ">";
    a.value = 0.05;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    std::string verbose = report.toVerbose();

    CHECK(verbose.find("PASS") != std::string::npos);
    CHECK(verbose.find("audio.spectrum.bass") != std::string::npos);
}

// =============================================================================
// Silence scenario: all audio assertions should detect silence
// =============================================================================

TEST_CASE("evaluateAssertions: silent audio scenario", "[assertions][audio]") {
    vivid::ChainInspection insp;
    // audioAnalysis is default-constructed: all zeros, isSilent=true
    insp.audioAnalysis.isSilent = true;

    // This should fail - audio IS silent
    vivid::Assertion notSilent;
    notSilent.path = "audio.rmsLevel";
    notSilent.op = ">";
    notSilent.value = 0.01;
    notSilent.message = "Audio should not be silent";

    // This should pass - isSilent is 1.0
    vivid::Assertion checkSilent;
    checkSilent.path = "audio.isSilent";
    checkSilent.op = "==";
    checkSilent.value = 1.0;
    checkSilent.message = "Detecting silence";

    auto report = vivid::evaluateAssertions({notSilent, checkSilent}, insp, 0);
    REQUIRE(report.results.size() == 2);
    CHECK(!report.results[0].passed);  // rmsLevel > 0.01 fails (rms is 0)
    CHECK(report.results[1].passed);   // isSilent == 1.0 passes
    CHECK(!report.allPassed);          // Overall fails because first assertion failed
}

// =============================================================================
// Named assertions
// =============================================================================

TEST_CASE("Named assertion: name propagates to result", "[assertions][named]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.name = "feedback-alive";
    a.path = "output.meanBrightness";
    a.op = ">";
    a.value = 0.05;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
    CHECK(report.results[0].name == "feedback-alive");
}

TEST_CASE("Named assertion: name appears in verbose output", "[assertions][named]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.name = "not-black";
    a.path = "output.meanBrightness";
    a.op = ">";
    a.value = 0.05;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    std::string verbose = report.toVerbose();
    CHECK(verbose.find("not-black") != std::string::npos);
}

TEST_CASE("Named assertion: name appears in JSON output", "[assertions][named]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.name = "has-audio";
    a.path = "audio.rmsLevel";
    a.op = ">";
    a.value = 0.01;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    std::string jsonStr = report.toJSON();
    CHECK(jsonStr.find("has-audio") != std::string::npos);
}

TEST_CASE("Unnamed assertion: no name in result", "[assertions][named]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "output.contrast";
    a.op = ">";
    a.value = 0.0;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].name.empty());
}

// =============================================================================
// "between" operator
// =============================================================================

TEST_CASE("between: value in range passes", "[assertions][between]") {
    auto insp = makeTestInspection();
    // meanBrightness = 0.5

    vivid::Assertion a;
    a.path = "output.meanBrightness";
    a.op = "between";
    a.value = 0.2;
    a.valueHigh = 0.8;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
    CHECK_THAT(report.results[0].actual, WithinAbs(0.5, 0.001));
}

TEST_CASE("between: value below range fails", "[assertions][between]") {
    auto insp = makeTestInspection();
    // meanBrightness = 0.5

    vivid::Assertion a;
    a.path = "output.meanBrightness";
    a.op = "between";
    a.value = 0.6;
    a.valueHigh = 0.9;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(!report.results[0].passed);
}

TEST_CASE("between: value above range fails", "[assertions][between]") {
    auto insp = makeTestInspection();
    // meanBrightness = 0.5

    vivid::Assertion a;
    a.path = "output.meanBrightness";
    a.op = "between";
    a.value = 0.1;
    a.valueHigh = 0.4;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(!report.results[0].passed);
}

TEST_CASE("between: inclusive on low boundary", "[assertions][between]") {
    auto insp = makeTestInspection();
    // meanBrightness = 0.5

    vivid::Assertion a;
    a.path = "output.meanBrightness";
    a.op = "between";
    a.value = 0.5;     // exactly at low bound
    a.valueHigh = 0.8;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
}

TEST_CASE("between: inclusive on high boundary", "[assertions][between]") {
    auto insp = makeTestInspection();
    // meanBrightness = 0.5

    vivid::Assertion a;
    a.path = "output.meanBrightness";
    a.op = "between";
    a.value = 0.2;
    a.valueHigh = 0.5;  // exactly at high bound

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
}

TEST_CASE("between: expectedHigh in JSON output", "[assertions][between]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "output.meanBrightness";
    a.op = "between";
    a.value = 0.2;
    a.valueHigh = 0.8;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    std::string jsonStr = report.toJSON();
    CHECK(jsonStr.find("expectedHigh") != std::string::npos);
}

TEST_CASE("between: verbose output shows range", "[assertions][between]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "output.meanBrightness";
    a.op = "between";
    a.value = 0.2;
    a.valueHigh = 0.8;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    std::string verbose = report.toVerbose();
    CHECK(verbose.find("between") != std::string::npos);
    CHECK(verbose.find("0.2") != std::string::npos);
    CHECK(verbose.find("0.8") != std::string::npos);
}

TEST_CASE("between: failure message shows range", "[assertions][between]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "output.meanBrightness";
    a.op = "between";
    a.value = 0.6;
    a.valueHigh = 0.9;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(!report.results[0].passed);
    CHECK(report.results[0].message.find("between") != std::string::npos);
}

// =============================================================================
// Per-operator textureAnalysis paths
// =============================================================================

static vivid::ChainInspection makeInspectionWithTextureAnalysis() {
    auto insp = makeTestInspection();

    // Add an operator "bloom" with textureAnalysis
    vivid::InspectData bloomData;
    bloomData.set("threshold", 0.6f);
    vivid::FrameAnalysis bloomTex;
    bloomTex.meanBrightness = 0.15f;
    bloomTex.contrast = 0.08f;
    bloomTex.dominantHue = 120.0f;
    bloomTex.saturationAvg = 0.3f;
    bloomTex.dominantColor[0] = 0.1f;
    bloomTex.dominantColor[1] = 0.8f;
    bloomTex.dominantColor[2] = 0.2f;
    bloomTex.regionBrightness = {0.1f, 0.2f, 0.1f, 0.15f, 0.3f, 0.15f, 0.1f, 0.2f, 0.1f};
    bloomTex.histogram = {10, 30, 50, 40, 20, 5, 2, 1};
    bloomData.textureAnalysis = bloomTex;
    insp.operators.push_back({"bloom", bloomData});

    // Add an operator "noise" without textureAnalysis
    vivid::InspectData noiseData;
    noiseData.set("scale", 4.0f);
    insp.operators.push_back({"noise", noiseData});

    return insp;
}

TEST_CASE("resolvePath: operators.bloom.textureAnalysis.meanBrightness", "[assertions][textureAnalysis]") {
    auto insp = makeInspectionWithTextureAnalysis();
    auto [found, val] = vivid::resolvePath("operators.bloom.textureAnalysis.meanBrightness", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.15, 0.001));
}

TEST_CASE("resolvePath: operators.bloom.textureAnalysis.contrast", "[assertions][textureAnalysis]") {
    auto insp = makeInspectionWithTextureAnalysis();
    auto [found, val] = vivid::resolvePath("operators.bloom.textureAnalysis.contrast", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.08, 0.001));
}

TEST_CASE("resolvePath: operators.bloom.textureAnalysis.dominantHue", "[assertions][textureAnalysis]") {
    auto insp = makeInspectionWithTextureAnalysis();
    auto [found, val] = vivid::resolvePath("operators.bloom.textureAnalysis.dominantHue", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(120.0, 0.001));
}

TEST_CASE("resolvePath: operators.bloom.textureAnalysis.saturationAvg", "[assertions][textureAnalysis]") {
    auto insp = makeInspectionWithTextureAnalysis();
    auto [found, val] = vivid::resolvePath("operators.bloom.textureAnalysis.saturationAvg", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.3, 0.001));
}

TEST_CASE("resolvePath: operators.bloom.textureAnalysis.dominantColor.1", "[assertions][textureAnalysis]") {
    auto insp = makeInspectionWithTextureAnalysis();
    auto [found, val] = vivid::resolvePath("operators.bloom.textureAnalysis.dominantColor.1", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.8, 0.001));
}

TEST_CASE("resolvePath: operators.bloom.textureAnalysis.regionBrightness.4", "[assertions][textureAnalysis]") {
    auto insp = makeInspectionWithTextureAnalysis();
    auto [found, val] = vivid::resolvePath("operators.bloom.textureAnalysis.regionBrightness.4", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.3, 0.001));
}

TEST_CASE("resolvePath: operators.bloom.textureAnalysis.histogram.2", "[assertions][textureAnalysis]") {
    auto insp = makeInspectionWithTextureAnalysis();
    auto [found, val] = vivid::resolvePath("operators.bloom.textureAnalysis.histogram.2", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(50.0, 0.001));
}

TEST_CASE("resolvePath: operators.noise.textureAnalysis returns false (no analysis)", "[assertions][textureAnalysis]") {
    auto insp = makeInspectionWithTextureAnalysis();
    auto [found, val] = vivid::resolvePath("operators.noise.textureAnalysis.meanBrightness", insp);
    CHECK(!found);
}

TEST_CASE("resolvePath: operators.missing.textureAnalysis returns false", "[assertions][textureAnalysis]") {
    auto insp = makeInspectionWithTextureAnalysis();
    auto [found, val] = vivid::resolvePath("operators.missing.textureAnalysis.meanBrightness", insp);
    CHECK(!found);
}

TEST_CASE("evaluateAssertions: textureAnalysis assertion passes", "[assertions][textureAnalysis]") {
    auto insp = makeInspectionWithTextureAnalysis();

    vivid::Assertion a;
    a.name = "bloom-not-dark";
    a.path = "operators.bloom.textureAnalysis.meanBrightness";
    a.op = ">";
    a.value = 0.1;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
    CHECK(report.results[0].name == "bloom-not-dark");
}

TEST_CASE("evaluateAssertions: textureAnalysis assertion fails", "[assertions][textureAnalysis]") {
    auto insp = makeInspectionWithTextureAnalysis();

    vivid::Assertion a;
    a.path = "operators.bloom.textureAnalysis.meanBrightness";
    a.op = ">";
    a.value = 0.5;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(!report.results[0].passed);
}

// Verify metrics path still works alongside textureAnalysis
TEST_CASE("resolvePath: operators.bloom.metrics still works", "[assertions][textureAnalysis]") {
    auto insp = makeInspectionWithTextureAnalysis();
    auto [found, val] = vivid::resolvePath("operators.bloom.metrics.threshold", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.6, 0.001));
}

// =============================================================================
// exists / not_exists operators
// =============================================================================

TEST_CASE("exists: present path passes", "[assertions][exists]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "output.meanBrightness";
    a.op = "exists";

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
}

TEST_CASE("exists: absent path fails", "[assertions][exists]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "output.nonexistent";
    a.op = "exists";

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(!report.results[0].passed);
}

TEST_CASE("not_exists: absent path passes", "[assertions][exists]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "output.nonexistent";
    a.op = "not_exists";

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
}

TEST_CASE("not_exists: present path fails", "[assertions][exists]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "output.meanBrightness";
    a.op = "not_exists";

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(!report.results[0].passed);
}

TEST_CASE("exists: textureAnalysis path", "[assertions][exists][textureAnalysis]") {
    auto insp = makeInspectionWithTextureAnalysis();

    vivid::Assertion a;
    a.path = "operators.bloom.textureAnalysis.meanBrightness";
    a.op = "exists";
    a.message = "Bloom produces output";

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
}

TEST_CASE("exists: missing operator textureAnalysis fails", "[assertions][exists][textureAnalysis]") {
    auto insp = makeInspectionWithTextureAnalysis();

    vivid::Assertion a;
    a.path = "operators.noise.textureAnalysis.meanBrightness";
    a.op = "exists";

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(!report.results[0].passed);
}

TEST_CASE("exists: verbose output omits expected value", "[assertions][exists]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "output.meanBrightness";
    a.op = "exists";

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    std::string verbose = report.toVerbose();
    CHECK(verbose.find("PASS") != std::string::npos);
    CHECK(verbose.find("exists") != std::string::npos);
}

TEST_CASE("exists: failure message is descriptive", "[assertions][exists]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "output.nonexistent";
    a.op = "exists";

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(!report.results[0].passed);
    CHECK(report.results[0].message.find("Path not found") != std::string::npos);
}

TEST_CASE("not_exists: failure message is descriptive", "[assertions][exists]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "output.meanBrightness";
    a.op = "not_exists";

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(!report.results[0].passed);
    CHECK(report.results[0].message.find("Path exists") != std::string::npos);
}

// =============================================================================
// Conditional assertions: after_frame
// =============================================================================

TEST_CASE("after_frame: skipped when frame < threshold", "[assertions][conditional]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "output.meanBrightness";
    a.op = ">";
    a.value = 0.1;
    a.afterFrame = 30;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].skipped);
    CHECK(!report.results[0].skipReason.empty());
    CHECK(report.allPassed);  // Skipped assertions don't affect allPassed
}

TEST_CASE("after_frame: evaluated when frame >= threshold", "[assertions][conditional]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "output.meanBrightness";
    a.op = ">";
    a.value = 0.1;
    a.afterFrame = 10;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(!report.results[0].skipped);
    CHECK(report.results[0].passed);
}

TEST_CASE("after_frame: evaluated when frame > threshold", "[assertions][conditional]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "output.meanBrightness";
    a.op = ">";
    a.value = 0.1;
    a.afterFrame = 5;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(!report.results[0].skipped);
    CHECK(report.results[0].passed);
}

TEST_CASE("after_frame: -1 means no guard (default)", "[assertions][conditional]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "output.meanBrightness";
    a.op = ">";
    a.value = 0.1;
    // afterFrame defaults to -1

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(!report.results[0].skipped);
    CHECK(report.results[0].passed);
}

// =============================================================================
// Conditional assertions: when_path / when_check / when_value
// =============================================================================

TEST_CASE("when_*: evaluated when guard condition met", "[assertions][conditional]") {
    auto insp = makeTestInspection();
    // meanBrightness = 0.5

    vivid::Assertion a;
    a.path = "audio.rmsLevel";
    a.op = ">";
    a.value = 0.01;
    a.whenPath = "output.meanBrightness";
    a.whenCheck = ">";
    a.whenValue = 0.1;  // 0.5 > 0.1 is true, so guard passes

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(!report.results[0].skipped);
    CHECK(report.results[0].passed);
}

TEST_CASE("when_*: skipped when guard condition not met", "[assertions][conditional]") {
    auto insp = makeTestInspection();
    // meanBrightness = 0.5

    vivid::Assertion a;
    a.path = "audio.rmsLevel";
    a.op = ">";
    a.value = 0.01;
    a.whenPath = "output.meanBrightness";
    a.whenCheck = ">";
    a.whenValue = 0.9;  // 0.5 > 0.9 is false, so guard fails

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].skipped);
    CHECK(report.allPassed);  // Skipped doesn't affect allPassed
}

TEST_CASE("when_*: skipped when guard path not found", "[assertions][conditional]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "audio.rmsLevel";
    a.op = ">";
    a.value = 0.01;
    a.whenPath = "operators.nonexistent.metrics.foo";
    a.whenCheck = "==";
    a.whenValue = 1.0;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].skipped);
    CHECK(report.results[0].skipReason.find("not found") != std::string::npos);
}

TEST_CASE("when_*: uses == operator correctly", "[assertions][conditional]") {
    auto insp = makeTestInspection();
    // rmsLevel = 0.35

    vivid::Assertion a;
    a.path = "output.meanBrightness";
    a.op = ">";
    a.value = 0.1;
    a.whenPath = "audio.isSilent";
    a.whenCheck = "==";
    a.whenValue = 0.0;  // isSilent is false (0.0), so guard passes

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(!report.results[0].skipped);
    CHECK(report.results[0].passed);
}

// =============================================================================
// Combined: after_frame + when_*
// =============================================================================

TEST_CASE("Combined guards: after_frame checked first", "[assertions][conditional]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "output.meanBrightness";
    a.op = ">";
    a.value = 0.1;
    a.afterFrame = 30;  // frame 10 < 30 → skip
    a.whenPath = "output.meanBrightness";
    a.whenCheck = ">";
    a.whenValue = 0.1;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].skipped);
    CHECK(report.results[0].skipReason.find("after_frame") != std::string::npos);
}

// =============================================================================
// Verbose / JSON output for skipped assertions
// =============================================================================

TEST_CASE("Skipped assertion: SKIP in verbose output", "[assertions][conditional]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.name = "warmup-check";
    a.path = "output.contrast";
    a.op = ">";
    a.value = 0.15;
    a.afterFrame = 30;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    std::string verbose = report.toVerbose();
    CHECK(verbose.find("SKIP") != std::string::npos);
    CHECK(verbose.find("warmup-check") != std::string::npos);
    CHECK(verbose.find("0 failed") != std::string::npos);
    CHECK(verbose.find("1 skipped") != std::string::npos);
}

TEST_CASE("Skipped assertion: JSON output has skipped field", "[assertions][conditional]") {
    auto insp = makeTestInspection();

    vivid::Assertion a;
    a.path = "output.contrast";
    a.op = ">";
    a.value = 0.15;
    a.afterFrame = 30;

    auto report = vivid::evaluateAssertions({a}, insp, 10);
    std::string jsonStr = report.toJSON();
    CHECK(jsonStr.find("\"skipped\"") != std::string::npos);
    CHECK(jsonStr.find("skipReason") != std::string::npos);
}

TEST_CASE("Mixed skipped and evaluated assertions", "[assertions][conditional]") {
    auto insp = makeTestInspection();

    std::vector<vivid::Assertion> assertions;

    // Should be evaluated (passes)
    vivid::Assertion a1;
    a1.path = "output.meanBrightness";
    a1.op = ">";
    a1.value = 0.1;
    assertions.push_back(a1);

    // Should be skipped (after_frame)
    vivid::Assertion a2;
    a2.path = "output.contrast";
    a2.op = ">";
    a2.value = 0.15;
    a2.afterFrame = 30;
    assertions.push_back(a2);

    // Should be evaluated (fails)
    vivid::Assertion a3;
    a3.path = "output.meanBrightness";
    a3.op = ">";
    a3.value = 0.99;
    assertions.push_back(a3);

    auto report = vivid::evaluateAssertions(assertions, insp, 10);
    REQUIRE(report.results.size() == 3);
    CHECK(report.results[0].passed);
    CHECK(!report.results[0].skipped);
    CHECK(report.results[1].skipped);
    CHECK(!report.results[2].passed);
    CHECK(!report.results[2].skipped);
    CHECK(!report.allPassed);  // a3 failed
}
