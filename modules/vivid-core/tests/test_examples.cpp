// vivid-core Example Smoke Tests
// Verifies all vivid-core examples run without crashing using snapshot mode.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <string>
#include <array>

namespace fs = std::filesystem;

static std::string getVividPath() { return VIVID_BINARY_PATH; }
static std::string getSourceDir() { return VIVID_SOURCE_DIR; }

static fs::path getTempOutputPath(const std::string& testName) {
    fs::path tempDir = fs::temp_directory_path();
    return tempDir / ("vivid_core_example_" + testName + ".png");
}

static std::string sanitizeName(const std::string& name) {
    std::string result = name;
    for (char& c : result) {
        if (c == '/' || c == '\\' || c == ' ' || c == '-') c = '_';
    }
    return result;
}

static int runSnapshot(const std::string& examplePath, const fs::path& outputPath, int frame = 15) {
    std::string cmd = "\"" + getVividPath() + "\" "
                    + "\"" + examplePath + "\" "
                    + "--snapshot \"" + outputPath.string() + "\" "
                    + "--snapshot-frame " + std::to_string(frame);
#ifdef _WIN32
    cmd = "cmd /c \"" + cmd + "\"";
#endif
    return std::system(cmd.c_str());
}

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

TEST_CASE("vivid-core examples run without crash", "[smoke][examples][core]") {
    auto example = GENERATE(
        "hello-noise",
        "feedback",
        "canvas-drawing",
        "particles",
        "particle-forces",
        "retro-crt",
        "generators",
        "image-pipeline",
        "color-grading",
        "blur-bloom",
        "distortion",
        "creative-effects",
        "compositing"
    );

    DYNAMIC_SECTION("Example: " << example) {
        fs::path outputPath = getTempOutputPath(sanitizeName(example));
        fs::remove(outputPath);

        // Examples are copied to build/modules/vivid-core/examples/
        std::string examplePath = std::string(VIVID_BINARY_PATH);
        // Remove the binary name to get bin directory, then go to modules/vivid-core/examples
        fs::path binDir = fs::path(examplePath).parent_path();
        fs::path modulesDir = binDir.parent_path() / "modules" / "vivid-core" / "examples" / example;

        // Fallback to source directory if build examples not found
        if (!fs::exists(modulesDir)) {
            modulesDir = fs::path(getSourceDir()) / "src" / "vivid-core" / "examples" / example;
        }

        if (!fs::exists(modulesDir)) {
            WARN("Skipping missing example: " << modulesDir.string());
            SUCCEED();
            return;
        }

        int result = runSnapshot(modulesDir.string(), outputPath, 15);

        INFO("Example: " << example);
        INFO("Path: " << modulesDir.string());
        INFO("Exit code: " << result);
        INFO("Output path: " << outputPath.string());

        REQUIRE(fs::exists(outputPath));
        REQUIRE(isValidPng(outputPath));

        fs::remove(outputPath);
    }
}
