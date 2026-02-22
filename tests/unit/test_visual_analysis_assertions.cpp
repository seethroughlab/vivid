/**
 * @file test_visual_analysis_assertions.cpp
 * @brief Tests for harmony.*, symmetry.*, balance.* assertion path resolution
 *
 * Verifies that the assertion system can resolve visual analysis paths
 * to ColorHarmonyAnalysis, SymmetryAnalysis, and SpatialBalanceAnalysis
 * fields on ChainInspection.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <vivid/assertion.h>
#include <vivid/chain.h>
#include <vivid/visual_analysis.h>

using Catch::Matchers::WithinAbs;

// Build a ChainInspection with known visual analysis values
static vivid::ChainInspection makeTestInspectionWithVisual() {
    vivid::ChainInspection insp;
    insp.frame = 5;
    insp.time = 0.167f;
    insp.outputAnalysis.meanBrightness = 0.5f;

    vivid::VisualAnalysis va;

    // Color harmony
    va.harmony.harmonyScore = 0.82f;
    va.harmony.harmonyType = "complementary";
    va.harmony.paletteContrast = 0.65f;
    va.harmony.palette = {"#ff0000", "#00ff00", "#0000ff"};

    // Symmetry
    va.symmetry.horizontalSymmetry = 0.91f;
    va.symmetry.verticalSymmetry = 0.78f;
    va.symmetry.radialSymmetry = 0.64f;

    // Spatial balance
    va.balance.thirdsScore = 0.55f;
    va.balance.horizontalBias = -0.12f;
    va.balance.verticalBias = 0.08f;
    va.balance.balanceScore = 0.73f;

    insp.visualAnalysis = va;
    return insp;
}

// =============================================================================
// resolvePath: harmony.* fields
// =============================================================================

TEST_CASE("resolvePath: harmony.harmonyScore", "[assertions][visual]") {
    auto insp = makeTestInspectionWithVisual();
    auto [found, val] = vivid::resolvePath("harmony.harmonyScore", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.82, 0.001));
}

TEST_CASE("resolvePath: harmony.paletteContrast", "[assertions][visual]") {
    auto insp = makeTestInspectionWithVisual();
    auto [found, val] = vivid::resolvePath("harmony.paletteContrast", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.65, 0.001));
}

// =============================================================================
// resolvePath: symmetry.* fields
// =============================================================================

TEST_CASE("resolvePath: symmetry.horizontalSymmetry", "[assertions][visual]") {
    auto insp = makeTestInspectionWithVisual();
    auto [found, val] = vivid::resolvePath("symmetry.horizontalSymmetry", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.91, 0.001));
}

TEST_CASE("resolvePath: symmetry.verticalSymmetry", "[assertions][visual]") {
    auto insp = makeTestInspectionWithVisual();
    auto [found, val] = vivid::resolvePath("symmetry.verticalSymmetry", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.78, 0.001));
}

TEST_CASE("resolvePath: symmetry.radialSymmetry", "[assertions][visual]") {
    auto insp = makeTestInspectionWithVisual();
    auto [found, val] = vivid::resolvePath("symmetry.radialSymmetry", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.64, 0.001));
}

// =============================================================================
// resolvePath: balance.* fields
// =============================================================================

TEST_CASE("resolvePath: balance.thirdsScore", "[assertions][visual]") {
    auto insp = makeTestInspectionWithVisual();
    auto [found, val] = vivid::resolvePath("balance.thirdsScore", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.55, 0.001));
}

TEST_CASE("resolvePath: balance.horizontalBias", "[assertions][visual]") {
    auto insp = makeTestInspectionWithVisual();
    auto [found, val] = vivid::resolvePath("balance.horizontalBias", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(-0.12, 0.001));
}

TEST_CASE("resolvePath: balance.verticalBias", "[assertions][visual]") {
    auto insp = makeTestInspectionWithVisual();
    auto [found, val] = vivid::resolvePath("balance.verticalBias", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.08, 0.001));
}

TEST_CASE("resolvePath: balance.balanceScore", "[assertions][visual]") {
    auto insp = makeTestInspectionWithVisual();
    auto [found, val] = vivid::resolvePath("balance.balanceScore", insp);
    REQUIRE(found);
    CHECK_THAT(val, WithinAbs(0.73, 0.001));
}

// =============================================================================
// resolvePath: invalid visual paths
// =============================================================================

TEST_CASE("resolvePath: harmony.nonexistent returns false", "[assertions][visual]") {
    auto insp = makeTestInspectionWithVisual();
    auto [found, val] = vivid::resolvePath("harmony.nonexistent", insp);
    CHECK(!found);
}

TEST_CASE("resolvePath: symmetry.nonexistent returns false", "[assertions][visual]") {
    auto insp = makeTestInspectionWithVisual();
    auto [found, val] = vivid::resolvePath("symmetry.nonexistent", insp);
    CHECK(!found);
}

TEST_CASE("resolvePath: balance.nonexistent returns false", "[assertions][visual]") {
    auto insp = makeTestInspectionWithVisual();
    auto [found, val] = vivid::resolvePath("balance.nonexistent", insp);
    CHECK(!found);
}

TEST_CASE("resolvePath: harmony alone returns false", "[assertions][visual]") {
    auto insp = makeTestInspectionWithVisual();
    auto [found, val] = vivid::resolvePath("harmony", insp);
    CHECK(!found);
}

TEST_CASE("resolvePath: harmony.* returns false when visualAnalysis absent", "[assertions][visual]") {
    vivid::ChainInspection insp;
    auto [found, val] = vivid::resolvePath("harmony.harmonyScore", insp);
    CHECK(!found);
}

TEST_CASE("resolvePath: symmetry.* returns false when visualAnalysis absent", "[assertions][visual]") {
    vivid::ChainInspection insp;
    auto [found, val] = vivid::resolvePath("symmetry.horizontalSymmetry", insp);
    CHECK(!found);
}

TEST_CASE("resolvePath: balance.* returns false when visualAnalysis absent", "[assertions][visual]") {
    vivid::ChainInspection insp;
    auto [found, val] = vivid::resolvePath("balance.balanceScore", insp);
    CHECK(!found);
}

// =============================================================================
// evaluateAssertions: harmony string comparison (harmonyType)
// =============================================================================

TEST_CASE("evaluateAssertions: harmony.harmonyType == complementary passes", "[assertions][visual]") {
    auto insp = makeTestInspectionWithVisual();

    vivid::Assertion a;
    a.path = "harmony.harmonyType";
    a.op = "==";
    a.strValue = "complementary";

    auto report = vivid::evaluateAssertions({a}, insp, 5);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
    CHECK(report.allPassed);
}

TEST_CASE("evaluateAssertions: harmony.harmonyType != analogous passes", "[assertions][visual]") {
    auto insp = makeTestInspectionWithVisual();

    vivid::Assertion a;
    a.path = "harmony.harmonyType";
    a.op = "!=";
    a.strValue = "analogous";

    auto report = vivid::evaluateAssertions({a}, insp, 5);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
}

TEST_CASE("evaluateAssertions: harmony.harmonyType == wrong value fails", "[assertions][visual]") {
    auto insp = makeTestInspectionWithVisual();

    vivid::Assertion a;
    a.path = "harmony.harmonyType";
    a.op = "==";
    a.strValue = "triadic";

    auto report = vivid::evaluateAssertions({a}, insp, 5);
    REQUIRE(report.results.size() == 1);
    CHECK(!report.results[0].passed);
    CHECK(!report.allPassed);
}

// =============================================================================
// evaluateAssertions: numeric visual assertions
// =============================================================================

TEST_CASE("evaluateAssertions: harmony.harmonyScore > 0.5 passes", "[assertions][visual]") {
    auto insp = makeTestInspectionWithVisual();

    vivid::Assertion a;
    a.path = "harmony.harmonyScore";
    a.op = ">";
    a.value = 0.5;
    a.message = "Good color harmony";

    auto report = vivid::evaluateAssertions({a}, insp, 5);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
    CHECK(report.allPassed);
}

TEST_CASE("evaluateAssertions: symmetry.radialSymmetry > 0.5 passes", "[assertions][visual]") {
    auto insp = makeTestInspectionWithVisual();

    vivid::Assertion a;
    a.path = "symmetry.radialSymmetry";
    a.op = ">";
    a.value = 0.5;

    auto report = vivid::evaluateAssertions({a}, insp, 5);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
}

TEST_CASE("evaluateAssertions: balance.balanceScore between range", "[assertions][visual]") {
    auto insp = makeTestInspectionWithVisual();

    vivid::Assertion a;
    a.path = "balance.balanceScore";
    a.op = "between";
    a.value = 0.5;
    a.valueHigh = 0.9;

    auto report = vivid::evaluateAssertions({a}, insp, 5);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
}

TEST_CASE("evaluateAssertions: balance.horizontalBias between centered range", "[assertions][visual]") {
    auto insp = makeTestInspectionWithVisual();

    vivid::Assertion a;
    a.path = "balance.horizontalBias";
    a.op = "between";
    a.value = -0.3;
    a.valueHigh = 0.3;

    auto report = vivid::evaluateAssertions({a}, insp, 5);
    REQUIRE(report.results.size() == 1);
    CHECK(report.results[0].passed);
}

TEST_CASE("evaluateAssertions: mixed visual + output assertions", "[assertions][visual]") {
    auto insp = makeTestInspectionWithVisual();

    std::vector<vivid::Assertion> assertions;

    vivid::Assertion visual;
    visual.path = "output.meanBrightness";
    visual.op = ">";
    visual.value = 0.1;
    assertions.push_back(visual);

    vivid::Assertion harmony;
    harmony.path = "harmony.harmonyScore";
    harmony.op = ">";
    harmony.value = 0.5;
    assertions.push_back(harmony);

    vivid::Assertion symmetry;
    symmetry.path = "symmetry.horizontalSymmetry";
    symmetry.op = ">";
    symmetry.value = 0.8;
    assertions.push_back(symmetry);

    vivid::Assertion balance;
    balance.path = "balance.balanceScore";
    balance.op = ">";
    balance.value = 0.6;
    assertions.push_back(balance);

    auto report = vivid::evaluateAssertions(assertions, insp, 5);
    REQUIRE(report.results.size() == 4);
    CHECK(report.allPassed);
}
