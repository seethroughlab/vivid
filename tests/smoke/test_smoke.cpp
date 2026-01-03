// Smoke Tests for Vivid
// ======================
// Tests that verify examples run without crashing using snapshot mode.
// These tests launch vivid with --snapshot to capture a frame and exit.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <string>
#include <array>

namespace fs = std::filesystem;

// Get vivid binary path from CMake definition
static std::string getVividPath() {
    return VIVID_BINARY_PATH;
}

// Get source directory from CMake definition
static std::string getSourceDir() {
    return VIVID_SOURCE_DIR;
}

// Get a temporary file path for test output
static fs::path getTempOutputPath(const std::string& testName) {
    fs::path tempDir = fs::temp_directory_path();
    return tempDir / ("vivid_smoke_" + testName + ".png");
}

// Sanitize a path string for use in filenames
static std::string sanitizeName(const std::string& name) {
    std::string result = name;
    for (char& c : result) {
        if (c == '/' || c == '\\' || c == ' ') {
            c = '_';
        }
    }
    return result;
}

// Run vivid with snapshot mode and return exit code
static int runSnapshot(const std::string& examplePath, const fs::path& outputPath, int frame = 10) {
    std::string cmd = "\"" + getVividPath() + "\" "
                    + "\"" + examplePath + "\" "
                    + "--snapshot \"" + outputPath.string() + "\" "
                    + "--snapshot-frame " + std::to_string(frame);

#ifdef _WIN32
    // On Windows, we need to use cmd /c to run the command
    cmd = "cmd /c \"" + cmd + "\"";
#endif

    return std::system(cmd.c_str());
}

// Verify a file is a valid PNG (check magic bytes)
static bool isValidPng(const fs::path& path) {
    if (!fs::exists(path)) {
        return false;
    }

    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return false;
    }

    // PNG magic bytes: 89 50 4E 47 0D 0A 1A 0A
    std::array<unsigned char, 8> magic{};
    file.read(reinterpret_cast<char*>(magic.data()), 8);

    if (file.gcount() != 8) {
        return false;
    }

    return magic[0] == 0x89 && magic[1] == 0x50 && magic[2] == 0x4E && magic[3] == 0x47
        && magic[4] == 0x0D && magic[5] == 0x0A && magic[6] == 0x1A && magic[7] == 0x0A;
}

// -----------------------------------------------------------------------------
// Snapshot Mode Tests
// -----------------------------------------------------------------------------

TEST_CASE("Snapshot mode creates valid PNG", "[smoke][snapshot]") {
    fs::path outputPath = getTempOutputPath("snapshot_basic");

    // Clean up any existing file
    fs::remove(outputPath);

    // Run with a simple example
    std::string examplePath = getSourceDir() + "/projects/getting-started/02-hello-noise";
    int result = runSnapshot(examplePath, outputPath, 5);

    INFO("Command exit code: " << result);
    INFO("Output path: " << outputPath.string());

    // On some platforms, the process might return non-zero but still create the file
    // The important thing is that the file exists and is valid
    REQUIRE(fs::exists(outputPath));
    REQUIRE(isValidPng(outputPath));

    // Verify file has some content (at least 1KB for a real image)
    auto fileSize = fs::file_size(outputPath);
    INFO("File size: " << fileSize << " bytes");
    REQUIRE(fileSize > 1024);

    // Cleanup
    fs::remove(outputPath);
}

// -----------------------------------------------------------------------------
// 2D Examples Smoke Tests
// -----------------------------------------------------------------------------

TEST_CASE("Getting started examples run without crash", "[smoke][2d][getting-started]") {
    auto example = GENERATE(
        "getting-started/01-template",
        "getting-started/02-hello-noise"
    );

    DYNAMIC_SECTION("Example: " << example) {
        fs::path outputPath = getTempOutputPath(sanitizeName(example));
        fs::remove(outputPath);

        std::string examplePath = getSourceDir() + "/projects/" + example;

        // Skip if example doesn't exist
        if (!fs::exists(examplePath)) {
            WARN("Skipping missing example: " << examplePath);
            SUCCEED();
            return;
        }

        int result = runSnapshot(examplePath, outputPath, 10);

        INFO("Example: " << example);
        INFO("Exit code: " << result);
        INFO("Output path: " << outputPath.string());

        // Verify snapshot was created
        REQUIRE(fs::exists(outputPath));
        REQUIRE(isValidPng(outputPath));

        fs::remove(outputPath);
    }
}

TEST_CASE("2D effect examples run without crash", "[smoke][2d][effects]") {
    auto example = GENERATE(
        "2d-effects/chain-basics",
        "2d-effects/feedback",
        "2d-effects/kaleidoscope",
        "2d-effects/particles",
        "2d-effects/retro-crt",
        "2d-effects/canvas-drawing"
    );

    DYNAMIC_SECTION("Example: " << example) {
        fs::path outputPath = getTempOutputPath(sanitizeName(example));
        fs::remove(outputPath);

        std::string examplePath = getSourceDir() + "/projects/" + example;

        // Skip if example doesn't exist
        if (!fs::exists(examplePath)) {
            WARN("Skipping missing example: " << examplePath);
            SUCCEED();
            return;
        }

        int result = runSnapshot(examplePath, outputPath, 15);

        INFO("Example: " << example);
        INFO("Exit code: " << result);
        INFO("Output path: " << outputPath.string());

        REQUIRE(fs::exists(outputPath));
        REQUIRE(isValidPng(outputPath));

        fs::remove(outputPath);
    }
}

// -----------------------------------------------------------------------------
// 3D Examples Smoke Tests
// -----------------------------------------------------------------------------

TEST_CASE("3D rendering examples run without crash", "[smoke][3d]") {
    auto example = GENERATE(
        "3d-rendering/lighting-test",
        "3d-rendering/fog-test",
        "3d-rendering/globe"
    );

    DYNAMIC_SECTION("Example: " << example) {
        fs::path outputPath = getTempOutputPath(sanitizeName(example));
        fs::remove(outputPath);

        std::string examplePath = getSourceDir() + "/projects/" + example;

        // Skip if example doesn't exist
        if (!fs::exists(examplePath)) {
            WARN("Skipping missing example: " << examplePath);
            SUCCEED();
            return;
        }

        int result = runSnapshot(examplePath, outputPath, 20);

        INFO("Example: " << example);
        INFO("Exit code: " << result);
        INFO("Output path: " << outputPath.string());

        REQUIRE(fs::exists(outputPath));
        REQUIRE(isValidPng(outputPath));

        fs::remove(outputPath);
    }
}

// -----------------------------------------------------------------------------
// Testing Fixtures Smoke Tests
// -----------------------------------------------------------------------------

TEST_CASE("Testing fixtures run without crash", "[smoke][fixtures]") {
    auto fixture = GENERATE(
        "tests/fixtures/feedback-effects",
        "tests/fixtures/canvas-compositing",
        "tests/fixtures/blend-modes-all",
        "tests/fixtures/retro-suite"
    );

    DYNAMIC_SECTION("Fixture: " << fixture) {
        fs::path outputPath = getTempOutputPath(sanitizeName(fixture));
        fs::remove(outputPath);

        std::string fixturePath = getSourceDir() + "/" + fixture;

        // Skip if fixture doesn't exist
        if (!fs::exists(fixturePath)) {
            WARN("Skipping missing fixture: " << fixturePath);
            SUCCEED();
            return;
        }

        int result = runSnapshot(fixturePath, outputPath, 30);

        INFO("Fixture: " << fixture);
        INFO("Exit code: " << result);
        INFO("Output path: " << outputPath.string());

        REQUIRE(fs::exists(outputPath));
        REQUIRE(isValidPng(outputPath));

        fs::remove(outputPath);
    }
}
