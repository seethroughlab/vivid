// vivid-network Example Smoke Tests
// Verifies all vivid-network examples run without crashing using snapshot mode.
// Note: Network examples bind to ports - marked as hidden tests.

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
    return tempDir / ("vivid_network_example_" + testName + ".png");
}

static std::string sanitizeName(const std::string& name) {
    std::string result = name;
    for (char& c : result) {
        if (c == '/' || c == '\\' || c == ' ' || c == '-') c = '_';
    }
    return result;
}

static int runSnapshot(const std::string& examplePath, const fs::path& outputPath, int frame = 10) {
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

// Hidden test (.) - network examples bind to ports which may conflict in CI
TEST_CASE("vivid-network examples run without crash", "[smoke][examples][network][.]") {
    auto example = GENERATE(
        "osc-control",
        "udp-receiver",
        "web-control"
    );

    DYNAMIC_SECTION("Example: " << example) {
        fs::path outputPath = getTempOutputPath(sanitizeName(example));
        fs::remove(outputPath);

        fs::path examplePath = fs::path(getSourceDir()) / "modules" / "vivid-network" / "examples" / example;

        if (!fs::exists(examplePath)) {
            WARN("Skipping missing example: " << examplePath.string());
            SUCCEED();
            return;
        }

        int result = runSnapshot(examplePath.string(), outputPath, 10);

        INFO("Example: " << example);
        INFO("Path: " << examplePath.string());
        INFO("Exit code: " << result);
        INFO("Output path: " << outputPath.string());

        REQUIRE(fs::exists(outputPath));
        REQUIRE(isValidPng(outputPath));

        fs::remove(outputPath);
    }
}
