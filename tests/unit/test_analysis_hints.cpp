/**
 * @file test_analysis_hints.cpp
 * @brief Unit tests for MCP analysis hint generation
 *
 * Tests that actionable hints are generated when metrics fall outside
 * healthy ranges, and that no hints appear for well-balanced outputs.
 */

#include <catch2/catch_test_macros.hpp>
#include <vivid/analysis_hints.h>
#include <nlohmann/json.hpp>

using json = nlohmann::json;

// =============================================================================
// Visual Hints
// =============================================================================

TEST_CASE("VisualHints: healthy output produces no hints", "[analysis_hints]") {
    json output = {
        {"meanBrightness", 0.5},
        {"contrast", 0.22},
        {"clipBlackPct", 0.01},
        {"clipWhitePct", 0.01},
        {"textureEntropy", 0.6},
        {"sharpness", 0.08},
        {"colorTemperature", 0.5},
        {"edgeDensity", 0.15}
    };
    auto hints = vivid::generateVisualHints(output);
    CHECK(hints.empty());
}

TEST_CASE("VisualHints: too dark triggers brightness hint", "[analysis_hints]") {
    json output = {{"meanBrightness", 0.05}};
    auto hints = vivid::generateVisualHints(output);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "meanBrightness");
    CHECK(hints[0].status == "low");
    CHECK(hints[0].suggestion.find("Brightness") != std::string::npos);
}

TEST_CASE("VisualHints: too bright triggers brightness hint", "[analysis_hints]") {
    json output = {{"meanBrightness", 0.95}};
    auto hints = vivid::generateVisualHints(output);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "meanBrightness");
    CHECK(hints[0].status == "high");
}

TEST_CASE("VisualHints: low contrast", "[analysis_hints]") {
    json output = {{"contrast", 0.02}};
    auto hints = vivid::generateVisualHints(output);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "contrast");
    CHECK(hints[0].status == "low");
}

TEST_CASE("VisualHints: high contrast", "[analysis_hints]") {
    json output = {{"contrast", 0.5}};
    auto hints = vivid::generateVisualHints(output);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "contrast");
    CHECK(hints[0].status == "high");
}

TEST_CASE("VisualHints: crushed blacks", "[analysis_hints]") {
    json output = {{"clipBlackPct", 0.2}};
    auto hints = vivid::generateVisualHints(output);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "clipBlackPct");
}

TEST_CASE("VisualHints: blown highlights", "[analysis_hints]") {
    json output = {{"clipWhitePct", 0.3}};
    auto hints = vivid::generateVisualHints(output);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "clipWhitePct");
}

TEST_CASE("VisualHints: low entropy", "[analysis_hints]") {
    json output = {{"textureEntropy", 0.05}};
    auto hints = vivid::generateVisualHints(output);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "textureEntropy");
}

TEST_CASE("VisualHints: low sharpness", "[analysis_hints]") {
    json output = {{"sharpness", 0.001}};
    auto hints = vivid::generateVisualHints(output);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "sharpness");
}

TEST_CASE("VisualHints: cold color temperature", "[analysis_hints]") {
    json output = {{"colorTemperature", 0.1}};
    auto hints = vivid::generateVisualHints(output);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "colorTemperature");
    CHECK(hints[0].status == "low");
}

TEST_CASE("VisualHints: warm color temperature", "[analysis_hints]") {
    json output = {{"colorTemperature", 0.9}};
    auto hints = vivid::generateVisualHints(output);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "colorTemperature");
    CHECK(hints[0].status == "high");
}

TEST_CASE("VisualHints: no edges", "[analysis_hints]") {
    json output = {{"edgeDensity", 0.005}};
    auto hints = vivid::generateVisualHints(output);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "edgeDensity");
}

TEST_CASE("VisualHints: null input returns empty", "[analysis_hints]") {
    auto hints = vivid::generateVisualHints(json());
    CHECK(hints.empty());
}

// =============================================================================
// Audio Hints
// =============================================================================

TEST_CASE("AudioHints: healthy audio produces no hints", "[analysis_hints]") {
    json audio = {
        {"isSilent", false},
        {"rmsLevel", 0.3},
        {"clippedSamplePct", 0.0},
        {"dcOffset", 0.001},
        {"integratedLUFS", -18.0}
    };
    auto hints = vivid::generateAudioHints(audio);
    CHECK(hints.empty());
}

TEST_CASE("AudioHints: silence detected", "[analysis_hints]") {
    json audio = {{"isSilent", true}, {"rmsLevel", 0.0}};
    auto hints = vivid::generateAudioHints(audio);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "isSilent");
    CHECK(hints[0].severity == 0);
    CHECK(hints[0].suggestion.find("AudioOutput") != std::string::npos);
}

TEST_CASE("AudioHints: silence short-circuits other checks", "[analysis_hints]") {
    json audio = {
        {"isSilent", true},
        {"rmsLevel", 0.0},
        {"clippedSamplePct", 0.5},  // Would normally trigger
        {"integratedLUFS", -60.0}   // Would normally trigger
    };
    auto hints = vivid::generateAudioHints(audio);
    // Only isSilent hint, not the others
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "isSilent");
}

TEST_CASE("AudioHints: near silence (low RMS)", "[analysis_hints]") {
    json audio = {{"isSilent", false}, {"rmsLevel", 0.005}};
    auto hints = vivid::generateAudioHints(audio);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "rmsLevel");
}

TEST_CASE("AudioHints: clipping", "[analysis_hints]") {
    json audio = {{"isSilent", false}, {"clippedSamplePct", 0.05}};
    auto hints = vivid::generateAudioHints(audio);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "clippedSamplePct");
    CHECK(hints[0].suggestion.find("Limiter") != std::string::npos);
}

TEST_CASE("AudioHints: DC offset", "[analysis_hints]") {
    json audio = {{"isSilent", false}, {"dcOffset", 0.1}};
    auto hints = vivid::generateAudioHints(audio);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "dcOffset");
}

TEST_CASE("AudioHints: very quiet LUFS", "[analysis_hints]") {
    json audio = {{"isSilent", false}, {"integratedLUFS", -50.0}};
    auto hints = vivid::generateAudioHints(audio);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "integratedLUFS");
}

// =============================================================================
// Temporal Hints
// =============================================================================

TEST_CASE("TemporalHints: healthy temporal produces no hints", "[analysis_hints]") {
    json temporal = {{"isFrozen", false}, {"flickerScore", 0.1}};
    auto hints = vivid::generateTemporalHints(temporal);
    CHECK(hints.empty());
}

TEST_CASE("TemporalHints: frozen output", "[analysis_hints]") {
    json temporal = {{"isFrozen", true}};
    auto hints = vivid::generateTemporalHints(temporal);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "isFrozen");
    CHECK(hints[0].severity == 0);
}

TEST_CASE("TemporalHints: excessive flicker", "[analysis_hints]") {
    json temporal = {{"isFrozen", false}, {"flickerScore", 0.8}};
    auto hints = vivid::generateTemporalHints(temporal);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "flickerScore");
}

// =============================================================================
// AV Reactivity Hints
// =============================================================================

TEST_CASE("AVHints: healthy AV produces no hints", "[analysis_hints]") {
    json av = {
        {"correlation", 0.5},
        {"onsetResponseRate", 0.7},
        {"reactivityLatencyMs", 50.0}
    };
    auto hints = vivid::generateAVHints(av);
    CHECK(hints.empty());
}

TEST_CASE("AVHints: low correlation", "[analysis_hints]") {
    json av = {{"correlation", 0.02}};
    auto hints = vivid::generateAVHints(av);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "avCorrelation");
}

TEST_CASE("AVHints: low onset response", "[analysis_hints]") {
    json av = {{"onsetResponseRate", 0.05}};
    auto hints = vivid::generateAVHints(av);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "onsetResponseRate");
}

TEST_CASE("AVHints: high latency", "[analysis_hints]") {
    json av = {{"reactivityLatencyMs", 350.0}};
    auto hints = vivid::generateAVHints(av);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "reactivityLatencyMs");
}

TEST_CASE("AVHints: invalid analysis skipped", "[analysis_hints]") {
    json av = {{"invalidReason", "no audio chain"}, {"correlation", 0.0}};
    auto hints = vivid::generateAVHints(av);
    CHECK(hints.empty());
}

TEST_CASE("AVHints: isValid false skipped", "[analysis_hints]") {
    json av = {{"isValid", false}, {"correlation", 0.0}};
    auto hints = vivid::generateAVHints(av);
    CHECK(hints.empty());
}

TEST_CASE("AVHints: avCorrelation field name also works", "[analysis_hints]") {
    json av = {{"avCorrelation", 0.02}};
    auto hints = vivid::generateAVHints(av);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0].metric == "avCorrelation");
}

// =============================================================================
// collectHints — integration
// =============================================================================

TEST_CASE("collectHints: empty response gives empty array", "[analysis_hints]") {
    json response = {{"inspection", json::object()}};
    auto hints = vivid::collectHints(response);
    CHECK(hints.is_array());
    CHECK(hints.empty());
}

TEST_CASE("collectHints: healthy inspection gives empty hints", "[analysis_hints]") {
    json response = {
        {"inspection", {
            {"output", {
                {"meanBrightness", 0.5},
                {"contrast", 0.22}
            }}
        }}
    };
    auto hints = vivid::collectHints(response);
    CHECK(hints.empty());
}

TEST_CASE("collectHints: bad metrics produce hints with correct JSON structure", "[analysis_hints]") {
    json response = {
        {"inspection", {
            {"output", {
                {"meanBrightness", 0.05},
                {"contrast", 0.02}
            }}
        }}
    };
    auto hints = vivid::collectHints(response);
    REQUIRE(hints.size() == 2);

    // Check JSON structure
    CHECK(hints[0].contains("metric"));
    CHECK(hints[0].contains("value"));
    CHECK(hints[0].contains("status"));
    CHECK(hints[0].contains("range"));
    CHECK(hints[0].contains("suggestion"));
    CHECK(hints[0]["range"].is_array());
    CHECK(hints[0]["range"].size() == 2);
}

TEST_CASE("collectHints: max 5 hints enforced", "[analysis_hints]") {
    // Create a response with many bad metrics
    json response = {
        {"inspection", {
            {"output", {
                {"meanBrightness", 0.05},
                {"contrast", 0.02},
                {"clipBlackPct", 0.3},
                {"clipWhitePct", 0.3},
                {"textureEntropy", 0.01},
                {"sharpness", 0.001},
                {"colorTemperature", 0.05},
                {"edgeDensity", 0.001}
            }},
            {"temporal", {
                {"isFrozen", true},
                {"flickerScore", 0.9}
            }}
        }}
    };
    auto hints = vivid::collectHints(response);
    CHECK(hints.size() <= 5);
}

TEST_CASE("collectHints: severity ordering - critical first", "[analysis_hints]") {
    json response = {
        {"inspection", {
            {"output", {
                {"sharpness", 0.001},          // severity 2 (minor)
                {"meanBrightness", 0.05}        // severity 1 (important)
            }},
            {"temporal", {
                {"isFrozen", true}              // severity 0 (critical)
            }}
        }}
    };
    auto hints = vivid::collectHints(response);
    REQUIRE(hints.size() == 3);
    // isFrozen (severity 0) should come first
    CHECK(hints[0]["metric"] == "isFrozen");
}

TEST_CASE("collectHints: audio from analysis key (capture_audio format)", "[analysis_hints]") {
    json response = {
        {"analysis", {
            {"isSilent", true},
            {"rmsLevel", 0.0}
        }}
    };
    auto hints = vivid::collectHints(response);
    REQUIRE(hints.size() >= 1);
    CHECK(hints[0]["metric"] == "isSilent");
}

TEST_CASE("collectHints: custom maxHints", "[analysis_hints]") {
    json response = {
        {"inspection", {
            {"output", {
                {"meanBrightness", 0.05},
                {"contrast", 0.02},
                {"clipBlackPct", 0.3}
            }}
        }}
    };
    auto hints = vivid::collectHints(response, 2);
    CHECK(hints.size() <= 2);
}

TEST_CASE("collectHints: inspect_chain multi-sample with temporal", "[analysis_hints]") {
    json response = {
        {"inspection", {
            {"output", {{"meanBrightness", 0.5}}},
            {"temporal", {{"isFrozen", true}}}
        }}
    };
    auto hints = vivid::collectHints(response);
    REQUIRE(hints.size() == 1);
    CHECK(hints[0]["metric"] == "isFrozen");
}
