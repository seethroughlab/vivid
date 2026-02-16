// GUI Visual Regression Tests for Vivid
// =======================================
// Compares rendered snapshots of the devtools UI against reference images.
// Uses --snapshot-ui to capture the composite texture (chain + UI overlay).

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
    return tempDir / ("vivid_gui_visual_" + testName + ".png");
}

// Run vivid with --snapshot-ui to capture composite (chain + devtools UI)
static int runGUISnapshot(const std::string& projectPath, const fs::path& outputPath, int frame = 30) {
    std::string cmd = "\"" + getVividPath() + "\" "
                    + "\"" + projectPath + "\" "
                    + "--snapshot-ui "
                    + "--snapshot \"" + outputPath.string() + "\" "
                    + "--snapshot-frame " + std::to_string(frame);

#ifdef _WIN32
    cmd = "cmd /c \"" + cmd + "\"";
#endif

    return std::system(cmd.c_str());
}

// Run vivid with --snapshot-ui + --script for puppeteered interaction tests
static int runGUISnapshotWithScript(const std::string& projectPath,
                                     const fs::path& outputPath,
                                     const std::string& scriptPath,
                                     int frame = 35) {
    std::string cmd = "\"" + getVividPath() + "\" "
                    + "\"" + projectPath + "\" "
                    + "--snapshot-ui "
                    + "--snapshot \"" + outputPath.string() + "\" "
                    + "--snapshot-frame " + std::to_string(frame) + " "
                    + "--script \"" + scriptPath + "\"";

#ifdef _WIN32
    cmd = "cmd /c \"" + cmd + "\"";
#endif

    return std::system(cmd.c_str());
}

// -----------------------------------------------------------------------------
// Image Comparison (same approach as test_visual_regression.cpp)
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
// GUI Visual Regression Test Cases
// -----------------------------------------------------------------------------

struct GUIVisualTestCase {
    const char* name;
    const char* fixturePath;       // Relative to source dir
    const char* referencePath;     // Relative to source dir
    int snapshotFrame;
    double tolerance;              // RMSE tolerance (looser than content tests due to text/AA)
};

static const GUIVisualTestCase GUI_VISUAL_TESTS[] = {
    // Default devtools UI: node graph + status bar
    {"gui-default", "tests/fixtures/gui-visual-test",
     "tests/fixtures/reference-images/gui/gui-default.png", 30, 0.08},
};

TEST_CASE("GUI visual regression tests", "[visual][gui]") {
    for (const auto& test : GUI_VISUAL_TESTS) {
        DYNAMIC_SECTION("GUI Visual: " << test.name) {
            std::string referencePath = getSourceDir() + "/" + test.referencePath;

            // Skip if reference image doesn't exist
            if (!fs::exists(referencePath)) {
                WARN("Skipping - reference image missing: " << referencePath);
                WARN("Run 'cmake --build build --target generate-gui-reference-images' to generate");
                SUCCEED();
                continue;
            }

            std::string fixturePath = getSourceDir() + "/" + test.fixturePath;

            // Skip if fixture doesn't exist
            if (!fs::exists(fixturePath)) {
                WARN("Skipping - fixture missing: " << fixturePath);
                SUCCEED();
                continue;
            }

            // Generate snapshot with devtools UI
            fs::path actualPath = getTempOutputPath(test.name);
            fs::remove(actualPath);

            int exitCode = runGUISnapshot(fixturePath, actualPath, test.snapshotFrame);
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
// Sanity Check: --snapshot-ui produces output
// -----------------------------------------------------------------------------

TEST_CASE("snapshot-ui produces a valid image", "[visual][gui]") {
    std::string fixturePath = getSourceDir() + "/tests/fixtures/gui-visual-test";

    if (!fs::exists(fixturePath)) {
        WARN("Skipping - fixture missing: " << fixturePath);
        SUCCEED();
        return;
    }

    fs::path outputPath = getTempOutputPath("sanity-check");
    fs::remove(outputPath);

    int exitCode = runGUISnapshot(fixturePath, outputPath, 30);
    INFO("Exit code: " << exitCode);

    REQUIRE(fs::exists(outputPath));

    ImageData img = loadImage(outputPath);
    REQUIRE(img.isValid());

    // Should be at least default window size (1280x720, or 2x on Retina)
    CHECK(img.width >= 1280);
    CHECK(img.height >= 720);

    // Should not be all black (UI should have rendered something)
    double totalBrightness = 0.0;
    size_t pixelCount = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
    for (size_t i = 0; i < pixelCount; ++i) {
        size_t idx = i * 4;
        totalBrightness += (img.pixels[idx] + img.pixels[idx + 1] + img.pixels[idx + 2]) / (3.0 * 255.0);
    }
    double avgBrightness = totalBrightness / static_cast<double>(pixelCount);
    INFO("Average brightness: " << avgBrightness);
    CHECK(avgBrightness > 0.01);  // Not completely black

    fs::remove(outputPath);
}

// -----------------------------------------------------------------------------
// Reference Image Generation Verification
// -----------------------------------------------------------------------------

TEST_CASE("GUI reference images exist", "[visual][gui][reference]") {
    for (const auto& test : GUI_VISUAL_TESTS) {
        DYNAMIC_SECTION("Reference: " << test.name) {
            std::string referencePath = getSourceDir() + "/" + test.referencePath;

            if (!fs::exists(referencePath)) {
                WARN("Missing reference image: " << test.referencePath);
                WARN("Run 'cmake --build build --target generate-gui-reference-images' to generate");
            }

            // Don't fail - just warn about missing references
            SUCCEED();
        }
    }
}

// =============================================================================
// Puppeteered GUI Interaction Tests
// =============================================================================
// These tests inject scripted input (clicks, scrolls) via --script and verify
// the resulting UI state visually using --snapshot-ui.

struct GUIInteractionTestCase {
    const char* name;
    const char* fixturePath;       // Relative to source dir
    const char* scriptPath;        // Relative to source dir (JSON event script)
    const char* referencePath;     // Relative to source dir
    int snapshotFrame;
    double tolerance;
};

static const GUIInteractionTestCase GUI_INTERACTION_TESTS[] = {
    // Click on a node in the graph — should open the inspector panel
    {"gui-click-node", "tests/fixtures/gui-visual-test",
     "tests/fixtures/gui-scripts/click-node.json",
     "tests/fixtures/reference-images/gui/gui-click-node.png", 35, 0.08},

    // Scroll to zoom out on the node graph — nodes render smaller
    {"gui-zoom-out", "tests/fixtures/gui-visual-test",
     "tests/fixtures/gui-scripts/zoom-out.json",
     "tests/fixtures/reference-images/gui/gui-zoom-out.png", 35, 0.08},
};

TEST_CASE("GUI puppeteered interaction tests", "[visual][gui][interaction]") {
    for (const auto& test : GUI_INTERACTION_TESTS) {
        DYNAMIC_SECTION("GUI Interaction: " << test.name) {
            std::string referencePath = getSourceDir() + "/" + test.referencePath;

            // Skip if reference image doesn't exist
            if (!fs::exists(referencePath)) {
                WARN("Skipping - reference image missing: " << referencePath);
                WARN("Run 'cmake --build build --target generate-gui-reference-images' to generate");
                SUCCEED();
                continue;
            }

            std::string fixturePath = getSourceDir() + "/" + test.fixturePath;
            std::string scriptPath = getSourceDir() + "/" + test.scriptPath;

            // Skip if fixture or script doesn't exist
            if (!fs::exists(fixturePath)) {
                WARN("Skipping - fixture missing: " << fixturePath);
                SUCCEED();
                continue;
            }
            if (!fs::exists(scriptPath)) {
                WARN("Skipping - script missing: " << scriptPath);
                SUCCEED();
                continue;
            }

            // Generate snapshot with devtools UI + scripted input
            fs::path actualPath = getTempOutputPath(test.name);
            fs::remove(actualPath);

            int exitCode = runGUISnapshotWithScript(fixturePath, actualPath, scriptPath, test.snapshotFrame);
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

// Sanity check: --script + --snapshot-ui produces output without crashing
TEST_CASE("snapshot-ui with script produces a valid image", "[visual][gui][interaction]") {
    std::string fixturePath = getSourceDir() + "/tests/fixtures/gui-visual-test";
    std::string scriptPath = getSourceDir() + "/tests/fixtures/gui-scripts/click-node.json";

    if (!fs::exists(fixturePath) || !fs::exists(scriptPath)) {
        WARN("Skipping - fixture or script missing");
        SUCCEED();
        return;
    }

    fs::path outputPath = getTempOutputPath("script-sanity-check");
    fs::remove(outputPath);

    int exitCode = runGUISnapshotWithScript(fixturePath, outputPath, scriptPath, 35);
    INFO("Exit code: " << exitCode);

    REQUIRE(fs::exists(outputPath));

    ImageData img = loadImage(outputPath);
    REQUIRE(img.isValid());

    CHECK(img.width >= 1280);
    CHECK(img.height >= 720);

    // Should not be all black
    double totalBrightness = 0.0;
    size_t pixelCount = static_cast<size_t>(img.width) * static_cast<size_t>(img.height);
    for (size_t i = 0; i < pixelCount; ++i) {
        size_t idx = i * 4;
        totalBrightness += (img.pixels[idx] + img.pixels[idx + 1] + img.pixels[idx + 2]) / (3.0 * 255.0);
    }
    double avgBrightness = totalBrightness / static_cast<double>(pixelCount);
    INFO("Average brightness: " << avgBrightness);
    CHECK(avgBrightness > 0.01);

    fs::remove(outputPath);
}
