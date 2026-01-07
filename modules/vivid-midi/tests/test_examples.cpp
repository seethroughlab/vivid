// vivid-midi Example Smoke Tests
// MIDI examples require physical hardware - we only verify examples exist.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static std::string getSourceDir() { return VIVID_SOURCE_DIR; }

// MIDI examples require physical MIDI hardware
// We only verify the example directories exist and have chain.cpp files
TEST_CASE("vivid-midi examples exist", "[smoke][examples][midi]") {
    auto example = GENERATE(
        "midi-input"
    );

    DYNAMIC_SECTION("Example exists: " << example) {
        fs::path examplePath = fs::path(getSourceDir()) / "modules" / "vivid-midi" / "examples" / example;
        fs::path chainPath = examplePath / "chain.cpp";

        INFO("Example path: " << examplePath.string());
        INFO("Chain file: " << chainPath.string());

        REQUIRE(fs::exists(examplePath));
        REQUIRE(fs::exists(chainPath));
    }
}
