// Export CLI Tests for Vivid
// ==========================
// Tests that `vivid export --audio` produces a video file with a non-silent
// audio track whose duration matches the video track. Uses ffprobe/ffmpeg for
// verification, skipping gracefully when those tools are absent.

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <cstdio>
#include <cstdlib>
#include <array>
#include <filesystem>

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

static std::string drumSynthProject() {
    return getSourceDir() + "/modules/vivid-audio/showcase/drum-synthesis";
}

// Run a shell command and capture stdout + exit code.
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

// Check whether a tool (ffprobe / ffmpeg) is available on $PATH.
static bool toolAvailable(const std::string& tool) {
#ifdef _WIN32
    auto [out, rc] = runShell("where " + tool + " 2>NUL");
#else
    auto [out, rc] = runShell("which " + tool + " 2>/dev/null");
#endif
    return rc == 0;
}

// Build a unique temp path for an export file.
static fs::path tempExportPath(const std::string& tag) {
    auto dir = fs::temp_directory_path();
    return dir / ("vivid_test_export_" + tag + ".mp4");
}

// Run `vivid export` with the given extra args. Returns exit code.
static int runExport(const std::string& project, const fs::path& output,
                     float duration, const std::string& extraFlags = "") {
    std::string cmd = "\"" + getVividPath() + "\" export"
        + " \"" + project + "\""
        + " -o \"" + output.string() + "\""
        + " --duration " + std::to_string(duration)
        + " --quiet"
        + " " + extraFlags
        + " 2>/dev/null";
    auto [out, rc] = runShell(cmd);
    return rc;
}

// Run ffprobe and return parsed JSON with stream info.
static json ffprobeJson(const fs::path& file) {
    std::string cmd = "ffprobe -v error -print_format json"
        " -show_streams \"" + file.string() + "\" 2>/dev/null";
    auto [out, rc] = runShell(cmd);
    if (rc != 0 || out.empty()) return json{};
    return json::parse(out, nullptr, false);
}

// Return mean_volume (dB) reported by ffmpeg volumedetect. Returns 0 on error.
static double meanVolume(const fs::path& file) {
    // volumedetect writes to stderr
    std::string cmd = "ffmpeg -i \"" + file.string() + "\""
        " -af volumedetect -f null /dev/null 2>&1";
    auto [out, rc] = runShell(cmd);

    // Parse "mean_volume: -XX.X dB"
    auto pos = out.find("mean_volume:");
    if (pos == std::string::npos) return 0.0;
    pos += 12; // skip "mean_volume:"
    return std::stod(out.substr(pos));
}

// RAII guard that removes a file on destruction.
struct TempFileGuard {
    fs::path path;
    ~TempFileGuard() {
        std::error_code ec;
        fs::remove(path, ec);
    }
};

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

TEST_CASE("export: audio flag produces audio stream", "[cli][export]") {
    if (!toolAvailable("ffprobe")) SKIP("ffprobe not found");

    auto out = tempExportPath("audio_stream");
    TempFileGuard guard{out};

    int rc = runExport(drumSynthProject(), out, 3.0f, "--audio");
    INFO("vivid export exit code: " << rc);
    REQUIRE(rc == 0);
    REQUIRE(fs::exists(out));
    REQUIRE(fs::file_size(out) > 0);

    json probe = ffprobeJson(out);
    REQUIRE(!probe.is_discarded());
    REQUIRE(probe.contains("streams"));

    bool hasAudio = false;
    for (const auto& s : probe["streams"]) {
        if (s.value("codec_type", "") == "audio") {
            hasAudio = true;
            break;
        }
    }
    REQUIRE(hasAudio);
}

TEST_CASE("export: audio is not silent", "[cli][export]") {
    if (!toolAvailable("ffprobe")) SKIP("ffprobe not found");
    if (!toolAvailable("ffmpeg"))  SKIP("ffmpeg not found");

    auto out = tempExportPath("audio_level");
    TempFileGuard guard{out};

    int rc = runExport(drumSynthProject(), out, 3.0f, "--audio");
    REQUIRE(rc == 0);
    REQUIRE(fs::exists(out));

    double vol = meanVolume(out);
    INFO("mean_volume: " << vol << " dB");
    // Silence is typically -91 dB or worse. Drums should be well above -60 dB.
    REQUIRE(vol > -60.0);
}

TEST_CASE("export: audio and video durations match", "[cli][export]") {
    if (!toolAvailable("ffprobe")) SKIP("ffprobe not found");

    auto out = tempExportPath("av_sync");
    TempFileGuard guard{out};

    int rc = runExport(drumSynthProject(), out, 3.0f, "--audio");
    REQUIRE(rc == 0);
    REQUIRE(fs::exists(out));

    json probe = ffprobeJson(out);
    REQUIRE(!probe.is_discarded());

    double videoDur = -1, audioDur = -1;
    for (const auto& s : probe["streams"]) {
        std::string type = s.value("codec_type", "");
        if (s.contains("duration")) {
            double dur = std::stod(s["duration"].get<std::string>());
            if (type == "video") videoDur = dur;
            if (type == "audio") audioDur = dur;
        }
    }

    INFO("video duration: " << videoDur << "s, audio duration: " << audioDur << "s");
    REQUIRE(videoDur > 0);
    REQUIRE(audioDur > 0);
    REQUIRE(std::abs(videoDur - audioDur) < 0.5);
}

TEST_CASE("export: no audio flag produces video-only", "[cli][export]") {
    if (!toolAvailable("ffprobe")) SKIP("ffprobe not found");

    auto out = tempExportPath("video_only");
    TempFileGuard guard{out};

    // Export WITHOUT --audio
    int rc = runExport(drumSynthProject(), out, 2.0f);
    INFO("vivid export exit code: " << rc);
    REQUIRE(rc == 0);
    REQUIRE(fs::exists(out));

    json probe = ffprobeJson(out);
    REQUIRE(!probe.is_discarded());
    REQUIRE(probe.contains("streams"));

    bool hasAudio = false;
    for (const auto& s : probe["streams"]) {
        if (s.value("codec_type", "") == "audio") {
            hasAudio = true;
            break;
        }
    }
    REQUIRE_FALSE(hasAudio);
}
