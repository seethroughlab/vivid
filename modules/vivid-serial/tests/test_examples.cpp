// vivid-serial Example Smoke Tests
// Serial examples require physical hardware - we only verify examples exist.

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <filesystem>
#include <string>

namespace fs = std::filesystem;

static std::string getSourceDir() { return VIVID_SOURCE_DIR; }

// Serial/DMX examples require physical hardware (Arduino, Enttec DMX USB Pro)
// We only verify the example directories exist and have chain.cpp files
TEST_CASE("vivid-serial examples exist", "[smoke][examples][serial]") {
    auto example = GENERATE(
        "arduino-led",
        "dmx-control"
    );

    DYNAMIC_SECTION("Example exists: " << example) {
        fs::path examplePath = fs::path(getSourceDir()) / "modules" / "vivid-serial" / "examples" / example;
        fs::path chainPath = examplePath / "chain.cpp";

        INFO("Example path: " << examplePath.string());
        INFO("Chain file: " << chainPath.string());

        REQUIRE(fs::exists(examplePath));
        REQUIRE(fs::exists(chainPath));
    }
}
