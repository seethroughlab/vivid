// vivid-render3d Example Smoke Tests
// Verifies all vivid-render3d examples run without crashing using snapshot mode.

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
    return tempDir / ("vivid_render3d_example_" + testName + ".png");
}

static std::string sanitizeName(const std::string& name) {
    std::string result = name;
    for (char& c : result) {
        if (c == '/' || c == '\\' || c == ' ' || c == '-') c = '_';
    }
    return result;
}

static int runSnapshot(const std::string& examplePath, const fs::path& outputPath, int frame = 20) {
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

// Check if example has required user-provided assets
static bool hasRequiredAssets(const fs::path& examplePath, const std::string& example) {
    if (example == "gltf-loader") {
        // Requires models in assets/models/
        fs::path modelsDir = examplePath / "assets" / "models";
        if (!fs::exists(modelsDir)) return false;
        for (const auto& entry : fs::directory_iterator(modelsDir)) {
            auto ext = entry.path().extension();
            if (ext == ".glb" || ext == ".gltf") return true;
        }
        return false;  // No models found
    }
    return true;  // No special assets required
}

TEST_CASE("vivid-render3d examples run without crash", "[smoke][examples][render3d]") {
    auto example = GENERATE(
        "3d-basics",
        "gltf-loader",
        "instancing"
    );

    DYNAMIC_SECTION("Example: " << example) {
        fs::path outputPath = getTempOutputPath(sanitizeName(example));
        fs::remove(outputPath);

        fs::path examplePath = fs::path(getSourceDir()) / "modules" / "vivid-render3d" / "examples" / example;

        if (!fs::exists(examplePath)) {
            WARN("Skipping missing example: " << examplePath.string());
            SUCCEED();
            return;
        }

        // Skip examples that require user-provided assets
        if (!hasRequiredAssets(examplePath, example)) {
            WARN("Skipping " << example << ": requires user-provided assets (place .glb files in assets/models/)");
            SUCCEED();
            return;
        }

        int result = runSnapshot(examplePath.string(), outputPath, 20);

        INFO("Example: " << example);
        INFO("Path: " << examplePath.string());
        INFO("Exit code: " << result);
        INFO("Output path: " << outputPath.string());

        REQUIRE(fs::exists(outputPath));
        REQUIRE(isValidPng(outputPath));

        fs::remove(outputPath);
    }
}
