// Audio Evaluation CLI Tests for Vivid
// ======================================
// Tests the end-to-end audio evaluation pipeline:
//   1. `vivid check` with audio.* assertions against a real audio project
//   2. `vivid export --audio` produces a .audio-analysis.json sidecar
//
// These tests run vivid as a subprocess and require a GPU (headless).

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
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
using Catch::Matchers::WithinAbs;
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string getVividPath() { return VIVID_BINARY_PATH; }
static std::string getSourceDir() { return VIVID_SOURCE_DIR; }

// A project with audio output: drum synthesis showcase
static std::string audioProject() {
    return getSourceDir() + "/modules/vivid-audio/showcase/drum-synthesis";
}

static std::pair<std::string, int> runShell(const std::string& cmd) {
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return {"", -1};

    std::string output;
    std::array<char, 4096> buf;
    while (fgets(buf.data(), static_cast<int>(buf.size()), pipe) != nullptr)
        output += buf.data();

    int status = pclose(pipe);
#ifndef _WIN32
    if (WIFEXITED(status)) status = WEXITSTATUS(status);
#endif
    return {output, status};
}

// RAII temp file cleanup
struct TempFileGuard {
    fs::path path;
    ~TempFileGuard() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};

// RAII temp directory cleanup
struct TempDirGuard {
    fs::path path;
    ~TempDirGuard() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

// Write a temporary assertions JSON file
static fs::path writeTempAssertions(const json& assertions, int frame = 10) {
    json j;
    j["frame"] = frame;
    j["assertions"] = assertions;

    auto path = fs::temp_directory_path() / "vivid_test_audio_assertions.json";
    std::ofstream f(path);
    f << j.dump(2);
    return path;
}

// Run `vivid build` on a project and return exit code
static int runBuild(const std::string& project) {
    std::string cmd = "\"" + getVividPath() + "\" build \"" + project + "\" 2>/dev/null";
    auto [out, rc] = runShell(cmd);
    return rc;
}

// Run `vivid check` with a custom assertion file
static std::pair<std::string, int> runCheck(const std::string& project,
                                             const fs::path& assertionFile) {
    std::string cmd = "\"" + getVividPath() + "\" check \"" + project + "\""
        " --assertions \"" + assertionFile.string() + "\""
        " 2>/dev/null";
    return runShell(cmd);
}

// ---------------------------------------------------------------------------
// Tests: vivid check with audio.* assertions
// ---------------------------------------------------------------------------

TEST_CASE("check: audio.rmsLevel assertion passes on audio project", "[cli][audio_eval]") {
    // First verify the project builds
    int buildRc = runBuild(audioProject());
    REQUIRE(buildRc == 0);

    // Create assertions that should pass: audio should not be silent
    json assertions = json::array({
        {{"path", "audio.rmsLevel"}, {"op", ">"}, {"value", 0.001},
         {"message", "Audio should produce some output"}}
    });

    auto assertFile = writeTempAssertions(assertions, 30);
    TempFileGuard guard{assertFile};

    auto [output, rc] = runCheck(audioProject(), assertFile);
    CHECK(rc == 0);  // All assertions should pass
}

TEST_CASE("check: audio.peakLevel assertion passes", "[cli][audio_eval]") {
    int buildRc = runBuild(audioProject());
    REQUIRE(buildRc == 0);

    // Peak should be below clipping threshold
    json assertions = json::array({
        {{"path", "audio.peakLevel"}, {"op", "<"}, {"value", 1.0},
         {"message", "Audio should not clip"}}
    });

    auto assertFile = writeTempAssertions(assertions, 30);
    TempFileGuard guard{assertFile};

    auto [output, rc] = runCheck(audioProject(), assertFile);
    CHECK(rc == 0);
}

TEST_CASE("check: impossible audio assertion fails correctly", "[cli][audio_eval]") {
    int buildRc = runBuild(audioProject());
    REQUIRE(buildRc == 0);

    // This should fail: no audio has RMS > 999
    json assertions = json::array({
        {{"path", "audio.rmsLevel"}, {"op", ">"}, {"value", 999.0},
         {"message", "Impossible threshold"}}
    });

    auto assertFile = writeTempAssertions(assertions, 30);
    TempFileGuard guard{assertFile};

    auto [output, rc] = runCheck(audioProject(), assertFile);
    CHECK(rc == 1);  // Should fail
}

TEST_CASE("check: audio.spectrum band assertions", "[cli][audio_eval]") {
    int buildRc = runBuild(audioProject());
    REQUIRE(buildRc == 0);

    // Drum synthesis should have energy in bass/lowMid bands
    // Use very low thresholds to be robust across different frame timings
    json assertions = json::array({
        {{"path", "audio.spectrum.bass"}, {"op", ">="}, {"value", 0.0},
         {"message", "Bass band should resolve"}},
        {{"path", "audio.spectrum.mid"}, {"op", ">="}, {"value", 0.0},
         {"message", "Mid band should resolve"}},
        {{"path", "audio.spectrum.high"}, {"op", ">="}, {"value", 0.0},
         {"message", "High band should resolve"}}
    });

    auto assertFile = writeTempAssertions(assertions, 30);
    TempFileGuard guard{assertFile};

    auto [output, rc] = runCheck(audioProject(), assertFile);
    CHECK(rc == 0);
}

TEST_CASE("check: mixed visual + audio assertions", "[cli][audio_eval]") {
    int buildRc = runBuild(audioProject());
    REQUIRE(buildRc == 0);

    json assertions = json::array({
        // Visual: output should not be black
        {{"path", "output.meanBrightness"}, {"op", ">"}, {"value", 0.01},
         {"message", "Visual output not black"}},
        // Audio: should produce sound
        {{"path", "audio.peakLevel"}, {"op", "<"}, {"value", 1.0},
         {"message", "Audio not clipping"}}
    });

    auto assertFile = writeTempAssertions(assertions, 30);
    TempFileGuard guard{assertFile};

    auto [output, rc] = runCheck(audioProject(), assertFile);
    CHECK(rc == 0);
}

// ---------------------------------------------------------------------------
// Tests: export audio sidecar
// ---------------------------------------------------------------------------

TEST_CASE("export: --audio produces audio-analysis.json sidecar", "[cli][audio_eval][export]") {
    int buildRc = runBuild(audioProject());
    REQUIRE(buildRc == 0);

    auto outputPath = fs::temp_directory_path() / "vivid_test_audio_eval.mp4";
    auto sidecarPath = fs::temp_directory_path() / "vivid_test_audio_eval.audio-analysis.json";
    TempFileGuard guardMp4{outputPath};
    TempFileGuard guardJson{sidecarPath};

    // Remove any stale files
    std::error_code ec;
    fs::remove(outputPath, ec);
    fs::remove(sidecarPath, ec);

    std::string cmd = "\"" + getVividPath() + "\" export"
        " \"" + audioProject() + "\""
        " -o \"" + outputPath.string() + "\""
        " --duration 2 --quiet --audio"
        " 2>/dev/null";
    auto [out, rc] = runShell(cmd);
    REQUIRE(rc == 0);

    // Video file should exist
    REQUIRE(fs::exists(outputPath));

    // Sidecar JSON should exist
    REQUIRE(fs::exists(sidecarPath));

    // Parse and validate sidecar structure
    std::ifstream f(sidecarPath);
    REQUIRE(f.is_open());
    json sidecar = json::parse(f);

    // Must have duration, summary, timeSeries
    REQUIRE(sidecar.contains("duration"));
    REQUIRE(sidecar.contains("summary"));
    REQUIRE(sidecar.contains("timeSeries"));

    // Duration should be approximately 2 seconds
    float duration = sidecar["duration"].get<float>();
    CHECK(duration >= 1.5f);
    CHECK(duration <= 3.0f);

    // Summary should have all expected fields
    auto& summary = sidecar["summary"];
    REQUIRE(summary.contains("rmsLevel"));
    REQUIRE(summary.contains("peakLevel"));
    REQUIRE(summary.contains("rmsLeft"));
    REQUIRE(summary.contains("rmsRight"));
    REQUIRE(summary.contains("crestFactor"));
    REQUIRE(summary.contains("isSilent"));
    REQUIRE(summary.contains("spectrum"));

    // Spectrum should have all 6 bands
    auto& spectrum = summary["spectrum"];
    REQUIRE(spectrum.contains("subBass"));
    REQUIRE(spectrum.contains("bass"));
    REQUIRE(spectrum.contains("lowMid"));
    REQUIRE(spectrum.contains("mid"));
    REQUIRE(spectrum.contains("highMid"));
    REQUIRE(spectrum.contains("high"));

    // RMS should be non-negative
    float rms = summary["rmsLevel"].get<float>();
    CHECK(rms >= 0.0f);

    // Peak should be non-negative
    float peak = summary["peakLevel"].get<float>();
    CHECK(peak >= 0.0f);

    // TimeSeries should be a non-empty array
    REQUIRE(sidecar["timeSeries"].is_array());
    CHECK(sidecar["timeSeries"].size() >= 1);

    // Each time-series entry should have expected fields
    for (const auto& entry : sidecar["timeSeries"]) {
        REQUIRE(entry.contains("time"));
        REQUIRE(entry.contains("rmsLevel"));
        REQUIRE(entry.contains("peakLevel"));
        REQUIRE(entry.contains("spectrum"));
    }

    // First entry should start at time 0
    CHECK_THAT(sidecar["timeSeries"][0]["time"].get<double>(), WithinAbs(0.0, 0.01));
}

TEST_CASE("export: no sidecar without --audio flag", "[cli][audio_eval][export]") {
    int buildRc = runBuild(audioProject());
    REQUIRE(buildRc == 0);

    auto outputPath = fs::temp_directory_path() / "vivid_test_no_sidecar.mp4";
    auto sidecarPath = fs::temp_directory_path() / "vivid_test_no_sidecar.audio-analysis.json";
    TempFileGuard guardMp4{outputPath};
    TempFileGuard guardJson{sidecarPath};

    std::error_code ec;
    fs::remove(outputPath, ec);
    fs::remove(sidecarPath, ec);

    // Export WITHOUT --audio
    std::string cmd = "\"" + getVividPath() + "\" export"
        " \"" + audioProject() + "\""
        " -o \"" + outputPath.string() + "\""
        " --duration 1 --quiet"
        " 2>/dev/null";
    auto [out, rc] = runShell(cmd);
    REQUIRE(rc == 0);
    REQUIRE(fs::exists(outputPath));

    // Sidecar should NOT exist (no --audio flag)
    CHECK(!fs::exists(sidecarPath));
}
