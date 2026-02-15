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
