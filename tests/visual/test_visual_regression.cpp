// Visual Regression Tests for Vivid
// ==================================
// Compares rendered snapshots against reference images.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <filesystem>
#include <fstream>
#include <cstdlib>
#include <string>
#include <vector>
#include <cmath>

// stb_image for loading images
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#include "stb_image.h"

namespace fs = std::filesystem;
using Catch::Matchers::WithinAbs;

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
    return tempDir / ("vivid_visual_" + testName + ".png");
}

// Run vivid with snapshot mode
static int runSnapshot(const std::string& examplePath, const fs::path& outputPath, int frame = 30) {
    std::string cmd = "\"" + getVividPath() + "\" "
                    + "\"" + examplePath + "\" "
                    + "--snapshot \"" + outputPath.string() + "\" "
                    + "--snapshot-frame " + std::to_string(frame);

#ifdef _WIN32
    cmd = "cmd /c \"" + cmd + "\"";
#endif

    return std::system(cmd.c_str());
}

// -----------------------------------------------------------------------------
// Image Comparison
// -----------------------------------------------------------------------------

struct ImageData {
    std::vector<unsigned char> pixels;
    int width = 0;
    int height = 0;
    int channels = 0;
    std::string error;

    bool isValid() const { return !pixels.empty() && width > 0 && height > 0; }
};

static ImageData loadImage(const fs::path& path) {
    ImageData img;

    if (!fs::exists(path)) {
        img.error = "File does not exist: " + path.string();
        return img;
    }

    unsigned char* data = stbi_load(path.string().c_str(), &img.width, &img.height, &img.channels, 4);
    if (!data) {
        img.error = "Failed to load image: " + std::string(stbi_failure_reason());
        return img;
    }

    // Force 4 channels (RGBA)
    img.channels = 4;
    size_t size = static_cast<size_t>(img.width) * static_cast<size_t>(img.height) * 4;
    img.pixels.assign(data, data + size);
    stbi_image_free(data);

    return img;
}

struct CompareResult {
    double rmse = 1.0;           // Root Mean Square Error (0.0 = identical, 1.0 = max difference)
    int diffPixelCount = 0;      // Number of pixels that differ beyond threshold
    bool sizeMismatch = false;   // True if dimensions don't match
    std::string error;           // Error message if comparison failed
};

// Compare two images and return RMSE
static CompareResult compareImages(const ImageData& expected, const ImageData& actual, double pixelThreshold = 0.02) {
    CompareResult result;

    if (!expected.isValid()) {
        result.error = "Expected image invalid: " + expected.error;
        return result;
    }

    if (!actual.isValid()) {
        result.error = "Actual image invalid: " + actual.error;
        return result;
    }

    if (expected.width != actual.width || expected.height != actual.height) {
        result.sizeMismatch = true;
        result.error = "Size mismatch: expected " + std::to_string(expected.width) + "x" + std::to_string(expected.height)
                     + ", got " + std::to_string(actual.width) + "x" + std::to_string(actual.height);
        return result;
    }

    size_t pixelCount = static_cast<size_t>(expected.width) * static_cast<size_t>(expected.height);
    double sumSquaredError = 0.0;

    for (size_t i = 0; i < pixelCount; ++i) {
        size_t idx = i * 4;

        // Compare RGBA channels
        double pixelError = 0.0;
        for (int c = 0; c < 4; ++c) {
            double diff = (static_cast<double>(expected.pixels[idx + c]) - static_cast<double>(actual.pixels[idx + c])) / 255.0;
            pixelError += diff * diff;
        }
        pixelError = std::sqrt(pixelError / 4.0);  // Average per channel

        sumSquaredError += pixelError * pixelError;

        if (pixelError > pixelThreshold) {
            result.diffPixelCount++;
        }
    }

    result.rmse = std::sqrt(sumSquaredError / static_cast<double>(pixelCount));
    return result;
}

// -----------------------------------------------------------------------------
// Visual Regression Test Cases
// -----------------------------------------------------------------------------

struct VisualTestCase {
    const char* name;
    const char* examplePath;       // Relative to source dir
    const char* referencePath;     // Relative to source dir
    int snapshotFrame;
    double tolerance;              // RMSE tolerance (0.0 to 1.0)
};

// Define test cases with tolerances based on effect type
static const VisualTestCase VISUAL_TESTS[] = {
    // Static/deterministic effects - low tolerance
    {"chain-basics", "examples/2d-effects/chain-basics",
     "tests/fixtures/reference-images/2d-effects/chain-basics.png", 30, 0.05},

    {"kaleidoscope", "examples/2d-effects/kaleidoscope",
     "tests/fixtures/reference-images/2d-effects/kaleidoscope.png", 30, 0.05},

    {"retro-crt", "examples/2d-effects/retro-crt",
     "tests/fixtures/reference-images/2d-effects/retro-crt.png", 30, 0.05},

    // Test fixtures
    {"retro-suite", "tests/fixtures/retro-suite",
     "tests/fixtures/reference-images/retro-suite.png", 30, 0.05},

    {"blend-modes-all", "tests/fixtures/blend-modes-all",
     "tests/fixtures/reference-images/blend-modes-all.png", 30, 0.05},
};

TEST_CASE("Visual regression tests", "[visual]") {
    for (const auto& test : VISUAL_TESTS) {
        DYNAMIC_SECTION("Visual: " << test.name) {
            std::string referencePath = getSourceDir() + "/" + test.referencePath;

            // Skip if reference image doesn't exist
            if (!fs::exists(referencePath)) {
                WARN("Skipping - reference image missing: " << referencePath);
                WARN("Run 'cmake --build build --target generate-reference-images' to generate");
                SUCCEED();
                continue;
            }

            std::string examplePath = getSourceDir() + "/" + test.examplePath;

            // Skip if example doesn't exist
            if (!fs::exists(examplePath)) {
                WARN("Skipping - example missing: " << examplePath);
                SUCCEED();
                continue;
            }

            // Generate snapshot
            fs::path actualPath = getTempOutputPath(test.name);
            fs::remove(actualPath);

            int exitCode = runSnapshot(examplePath, actualPath, test.snapshotFrame);
            INFO("Exit code: " << exitCode);

            REQUIRE(fs::exists(actualPath));

            // Load images
            ImageData expected = loadImage(referencePath);
            ImageData actual = loadImage(actualPath);

            REQUIRE(expected.isValid());
            REQUIRE(actual.isValid());

            // Compare
            CompareResult result = compareImages(expected, actual);

            INFO("RMSE: " << result.rmse);
            INFO("Diff pixels: " << result.diffPixelCount);
            INFO("Tolerance: " << test.tolerance);

            if (!result.error.empty()) {
                FAIL(result.error);
            }

            REQUIRE_FALSE(result.sizeMismatch);
            REQUIRE_THAT(result.rmse, WithinAbs(0.0, test.tolerance));

            // Cleanup
            fs::remove(actualPath);
        }
    }
}

// -----------------------------------------------------------------------------
// Reference Image Generation Verification
// -----------------------------------------------------------------------------

TEST_CASE("Reference images exist", "[visual][reference]") {
    for (const auto& test : VISUAL_TESTS) {
        DYNAMIC_SECTION("Reference: " << test.name) {
            std::string referencePath = getSourceDir() + "/" + test.referencePath;

            if (!fs::exists(referencePath)) {
                WARN("Missing reference image: " << test.referencePath);
                WARN("Run 'cmake --build build --target generate-reference-images' to generate");
            }

            // Don't fail - just warn about missing references
            SUCCEED();
        }
    }
}
