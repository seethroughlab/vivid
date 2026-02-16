// Inspect CLI Tests for Vivid
// ============================
// Tests the JSON output of `vivid inspect` in single-shot, multi-sample,
// per-operator, --out, and audio modes. Each test runs vivid as a subprocess
// via popen(), captures stdout, and validates JSON structure.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <cstdio>
#include <array>
#include <filesystem>
#include <fstream>

#ifdef _WIN32
#define popen _popen
#define pclose _pclose
#endif

using json = nlohmann::json;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string getVividPath() { return VIVID_BINARY_PATH; }
static std::string getSourceDir() { return VIVID_SOURCE_DIR; }

static std::string feedbackProject() {
    return getSourceDir() + "/tests/fixtures/feedback-effects";
}

static std::string audioProject() {
    return getSourceDir() + "/modules/vivid-audio/showcase/drum-synthesis";
}

static std::string brokenProject() {
    return getSourceDir() + "/tests/fixtures/broken-chain";
}

// Run a shell command and capture stdout + exit code.
static std::pair<std::string, int> runCommand(const std::string& args) {
    std::string cmd = "\"" + getVividPath() + "\" " + args;
#ifdef _WIN32
    cmd += " 2>NUL";
#else
    cmd += " 2>/dev/null";
#endif

    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {"", -1};

    std::string output;
    std::array<char, 4096> buffer;
    while (fgets(buffer.data(), static_cast<int>(buffer.size()), pipe) != nullptr)
        output += buffer.data();

    int status = pclose(pipe);
#ifndef _WIN32
    if (WIFEXITED(status)) status = WEXITSTATUS(status);
#endif
    return {output, status};
}

// Extract JSON from output that may contain log lines before/after.
static std::string extractJson(const std::string& output) {
    std::vector<size_t> candidates;
    for (size_t i = 0; i < output.size(); ++i) {
        if ((output[i] == '{' || output[i] == '[') &&
            (i == 0 || output[i - 1] == '\n')) {
            candidates.push_back(i);
        }
    }

    for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
        size_t start = *it;
        std::string sub = output.substr(start);
        if (json::accept(sub)) {
            return sub;
        }
        char opener = output[start];
        char closer = (opener == '{') ? '}' : ']';
        int depth = 0;
        bool inString = false;
        for (size_t j = start; j < output.size(); ++j) {
            char c = output[j];
            if (c == '"' && (j == start || output[j - 1] != '\\')) {
                inString = !inString;
            }
            if (inString) continue;
            if (c == opener) depth++;
            else if (c == closer) depth--;
            if (depth == 0) {
                std::string candidate = output.substr(start, j - start + 1);
                if (json::accept(candidate)) {
                    return candidate;
                }
                break;
            }
        }
    }
    return "";
}

// Parse stdout as JSON, failing the test with context on parse error
static json parseJson(const std::string& output) {
    std::string jsonStr = extractJson(output);
    INFO("Raw output (last 500 chars): " << output.substr(output.size() > 500 ? output.size() - 500 : 0));
    INFO("Extracted JSON: " << jsonStr);
    REQUIRE(!jsonStr.empty());
    json j;
    try {
        j = json::parse(jsonStr);
    } catch (const json::parse_error& e) {
        INFO("JSON parse error: " << e.what());
        FAIL("Failed to parse JSON from command output");
    }
    return j;
}

// RAII temp directory cleanup
struct TempDirGuard {
    fs::path path;
    ~TempDirGuard() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

// Verify a file is a valid PNG (check magic bytes)
static bool isValidPng(const fs::path& path) {
    if (!fs::exists(path)) return false;

    std::ifstream file(path, std::ios::binary);
    if (!file) return false;

    std::array<unsigned char, 8> magic{};
    file.read(reinterpret_cast<char*>(magic.data()), 8);
    if (file.gcount() != 8) return false;

    return magic[0] == 0x89 && magic[1] == 0x50 && magic[2] == 0x4E && magic[3] == 0x47
        && magic[4] == 0x0D && magic[5] == 0x0A && magic[6] == 0x1A && magic[7] == 0x0A;
}

// Validate FrameAnalysis fields in a JSON object
static void validateFrameAnalysis(const json& fa) {
    REQUIRE(fa.contains("meanBrightness"));
    REQUIRE(fa.contains("contrast"));
    REQUIRE(fa.contains("dominantColor"));
    REQUIRE(fa.contains("dominantHue"));
    REQUIRE(fa.contains("saturationAvg"));
    REQUIRE(fa.contains("histogram"));
    REQUIRE(fa.contains("regionBrightness"));

    REQUIRE(fa["dominantColor"].is_array());
    REQUIRE(fa["dominantColor"].size() == 3);

    REQUIRE(fa["histogram"].is_array());
    REQUIRE(fa["histogram"].size() == 8);

    REQUIRE(fa["regionBrightness"].is_array());
    REQUIRE(fa["regionBrightness"].size() == 9);

    float brightness = fa["meanBrightness"].get<float>();
    CHECK(brightness >= 0.0f);
    CHECK(brightness <= 1.0f);
}

// Validate a single inspection sample (either standalone or within samples array)
static void validateInspectionSample(const json& j) {
    REQUIRE(j.contains("frame"));
    REQUIRE(j.contains("time"));
    REQUIRE(j.contains("operators"));
    REQUIRE(j.contains("outputAnalysis"));

    REQUIRE(j["operators"].is_object());
    validateFrameAnalysis(j["outputAnalysis"]);
}

// =============================================================================
// Single-shot inspect
// =============================================================================

TEST_CASE("inspect: single-shot returns valid JSON structure", "[cli][inspect]") {
    auto [output, exitCode] = runCommand("inspect \"" + feedbackProject() + "\"");

    INFO("Exit code: " << exitCode);
    REQUIRE(exitCode == 0);

    json j = parseJson(output);
    validateInspectionSample(j);

    // Default frame is 10
    REQUIRE(j["frame"].get<int>() == 10);
    REQUIRE(j["time"].get<float>() > 0.0f);

    // feedback-effects has 4 operators: noise, feedback, ramp, comp
    REQUIRE(j["operators"].contains("noise"));
    REQUIRE(j["operators"].contains("feedback"));
    REQUIRE(j["operators"].contains("ramp"));
    REQUIRE(j["operators"].contains("comp"));

    // Each operator should have metadata with type and output_kind
    for (auto& [name, data] : j["operators"].items()) {
        REQUIRE(data.contains("metadata"));
        REQUIRE(data["metadata"].contains("type"));
        REQUIRE(data["metadata"]["type"].is_string());
        REQUIRE(data["metadata"].contains("output_kind"));
        REQUIRE(data["metadata"]["output_kind"].is_string());
    }
}

TEST_CASE("inspect: non-audio project reports silent audio", "[cli][inspect]") {
    auto [output, exitCode] = runCommand("inspect \"" + feedbackProject() + "\"");
    REQUIRE(exitCode == 0);

    json j = parseJson(output);
    REQUIRE(j.contains("audioAnalysis"));
    REQUIRE(j["audioAnalysis"]["isSilent"].get<bool>() == true);
}

// =============================================================================
// Custom frame
// =============================================================================

TEST_CASE("inspect: custom --frame selects correct frame", "[cli][inspect]") {
    auto [output, exitCode] = runCommand("inspect \"" + feedbackProject() + "\" --frame 30");
    REQUIRE(exitCode == 0);

    json j = parseJson(output);
    REQUIRE(j["frame"].get<int>() == 30);
}

// =============================================================================
// Multi-sample inspect
// =============================================================================

TEST_CASE("inspect: multi-sample produces envelope with samples", "[cli][inspect]") {
    auto [output, exitCode] = runCommand("inspect \"" + feedbackProject() + "\" --duration 1 --samples 3");

    INFO("Exit code: " << exitCode);
    REQUIRE(exitCode == 0);

    json j = parseJson(output);

    // Envelope fields
    REQUIRE(j.contains("project"));
    REQUIRE(j.contains("duration"));
    REQUIRE(j.contains("sampleCount"));
    REQUIRE(j.contains("samples"));

    REQUIRE(j["duration"].get<float>() == Catch::Approx(1.0f));
    REQUIRE(j["sampleCount"].get<int>() == 3);

    REQUIRE(j["samples"].is_array());
    REQUIRE(j["samples"].size() == 3);

    // Each sample is a valid inspection
    for (const auto& sample : j["samples"]) {
        validateInspectionSample(sample);
    }

    // Frame numbers should be monotonically increasing
    for (size_t i = 1; i < j["samples"].size(); i++) {
        int prevFrame = j["samples"][i - 1]["frame"].get<int>();
        int currFrame = j["samples"][i]["frame"].get<int>();
        CHECK(currFrame > prevFrame);
    }

    // Time values should be monotonically increasing
    for (size_t i = 1; i < j["samples"].size(); i++) {
        float prevTime = j["samples"][i - 1]["time"].get<float>();
        float currTime = j["samples"][i]["time"].get<float>();
        CHECK(currTime > prevTime);
    }
}

// =============================================================================
// Per-operator analysis
// =============================================================================

TEST_CASE("inspect: per-operator includes textureAnalysis", "[cli][inspect]") {
    auto [output, exitCode] = runCommand("inspect \"" + feedbackProject() + "\" --per-operator");

    INFO("Exit code: " << exitCode);
    REQUIRE(exitCode == 0);

    json j = parseJson(output);
    REQUIRE(j.contains("operators"));

    // Each texture operator should have textureAnalysis
    for (auto& [name, data] : j["operators"].items()) {
        if (data.contains("metadata") &&
            data["metadata"].value("output_kind", "") == "Texture") {
            REQUIRE(data.contains("textureAnalysis"));
            validateFrameAnalysis(data["textureAnalysis"]);
        }
    }
}

// =============================================================================
// Output directory
// =============================================================================

TEST_CASE("inspect: --out creates inspection.json and snapshot.png", "[cli][inspect]") {
    auto outDir = fs::temp_directory_path() / "vivid_test_inspect_out";
    TempDirGuard guard{outDir};

    // Remove any stale directory
    std::error_code ec;
    fs::remove_all(outDir, ec);

    auto [output, exitCode] = runCommand(
        "inspect \"" + feedbackProject() + "\" --out \"" + outDir.string() + "\"");

    INFO("Exit code: " << exitCode);
    REQUIRE(exitCode == 0);

    // inspection.json should exist
    auto jsonPath = outDir / "inspection.json";
    REQUIRE(fs::exists(jsonPath));

    // snapshot.png should exist and be valid
    auto pngPath = outDir / "snapshot.png";
    REQUIRE(fs::exists(pngPath));
    REQUIRE(isValidPng(pngPath));

    // inspection.json content should match stdout
    std::ifstream f(jsonPath);
    REQUIRE(f.is_open());
    std::string fileContent((std::istreambuf_iterator<char>(f)),
                             std::istreambuf_iterator<char>());
    json fileJson = json::parse(fileContent);
    json stdoutJson = parseJson(output);
    CHECK(fileJson == stdoutJson);

    // Non-audio project should NOT have waveform.png
    CHECK(!fs::exists(outDir / "waveform.png"));
}

// =============================================================================
// Audio project inspect
// =============================================================================

TEST_CASE("inspect: audio project reports non-silent audio", "[cli][inspect]") {
    auto [output, exitCode] = runCommand("inspect \"" + audioProject() + "\" --frame 30");

    INFO("Exit code: " << exitCode);
    REQUIRE(exitCode == 0);

    json j = parseJson(output);
    REQUIRE(j.contains("audioAnalysis"));

    auto& audio = j["audioAnalysis"];
    CHECK(audio["isSilent"].get<bool>() == false);
    CHECK(audio["rmsLevel"].get<float>() > 0.0f);
    CHECK(audio["peakLevel"].get<float>() > 0.0f);

    // Spectrum should have 6 named bands
    REQUIRE(audio.contains("spectrum"));
    auto& spectrum = audio["spectrum"];
    REQUIRE(spectrum.contains("subBass"));
    REQUIRE(spectrum.contains("bass"));
    REQUIRE(spectrum.contains("lowMid"));
    REQUIRE(spectrum.contains("mid"));
    REQUIRE(spectrum.contains("highMid"));
    REQUIRE(spectrum.contains("high"));
}

TEST_CASE("inspect: audio project --out creates waveform.png", "[cli][inspect]") {
    auto outDir = fs::temp_directory_path() / "vivid_test_inspect_audio_out";
    TempDirGuard guard{outDir};

    std::error_code ec;
    fs::remove_all(outDir, ec);

    auto [output, exitCode] = runCommand(
        "inspect \"" + audioProject() + "\" --frame 30 --out \"" + outDir.string() + "\"");

    INFO("Exit code: " << exitCode);
    REQUIRE(exitCode == 0);

    REQUIRE(fs::exists(outDir / "inspection.json"));
    REQUIRE(fs::exists(outDir / "snapshot.png"));
    REQUIRE(isValidPng(outDir / "snapshot.png"));

    // Audio project should produce waveform.png
    REQUIRE(fs::exists(outDir / "waveform.png"));
    REQUIRE(isValidPng(outDir / "waveform.png"));
}

// =============================================================================
// Error case
// =============================================================================

TEST_CASE("inspect: broken chain returns error", "[cli][inspect]") {
    auto [output, exitCode] = runCommand("inspect \"" + brokenProject() + "\"");

    INFO("Exit code: " << exitCode);
    REQUIRE(exitCode != 0);
}
