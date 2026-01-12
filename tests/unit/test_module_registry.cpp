/**
 * @file test_module_registry.cpp
 * @brief Unit tests for ModuleRegistry dependency resolution
 *
 * Tests module discovery, dependency parsing, and transitive resolution.
 * Uses temporary directories with test fixtures to isolate tests.
 */

#include <catch2/catch_test_macros.hpp>
#include <vivid/module_registry.h>
#include <nlohmann/json.hpp>

#include <fstream>
#include <set>

using namespace vivid;
using json = nlohmann::json;

// =============================================================================
// Test Fixture Helper
// =============================================================================

class ModuleTestFixture {
    fs::path m_tempDir;
    static int s_counter;

public:
    ModuleTestFixture() {
        // Create unique temp directory for each test
        m_tempDir = fs::temp_directory_path() / ("vivid-test-" + std::to_string(++s_counter));
        fs::create_directories(m_tempDir / "modules");
    }

    ~ModuleTestFixture() {
        std::error_code ec;
        fs::remove_all(m_tempDir, ec);
    }

    fs::path tempDir() const { return m_tempDir; }

    // Create a module with optional dependencies
    void createModule(const std::string& name,
                      const std::vector<std::string>& deps = {},
                      const std::string& version = "1.0.0") {
        auto modDir = m_tempDir / "modules" / name;
        fs::create_directories(modDir / "include");

        json j;
        j["name"] = name;
        j["version"] = version;
        if (!deps.empty()) {
            j["dependencies"] = deps;
        }

        std::ofstream out(modDir / "module.json");
        out << j.dump(2);
    }

    // Create a module without module.json (uses defaults)
    void createModuleNoJson(const std::string& name) {
        auto modDir = m_tempDir / "modules" / name;
        fs::create_directories(modDir / "include");
    }

    // Write a chain.cpp file with specific content
    fs::path writeChain(const std::string& content) {
        auto chainPath = m_tempDir / "chain.cpp";
        std::ofstream out(chainPath);
        out << content;
        return chainPath;
    }
};

int ModuleTestFixture::s_counter = 0;

// Helper to get module names as a set
static std::set<std::string> getModuleNames(const std::vector<ModuleInfo>& modules) {
    std::set<std::string> names;
    for (const auto& m : modules) {
        names.insert(m.name);
    }
    return names;
}

// =============================================================================
// Basic Module Discovery Tests
// =============================================================================

TEST_CASE("ModuleRegistry basic discovery", "[unit][modules]") {
    ModuleTestFixture fixture;

    SECTION("discovers module from #include <vivid/audio/>") {
        fixture.createModule("vivid-audio");
        auto chainPath = fixture.writeChain(R"(
            #include <vivid/audio/audio_in.h>
        )");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        REQUIRE(modules.size() == 1);
        REQUIRE(modules[0].name == "vivid-audio");
    }

    SECTION("discovers module from #include <vivid/video/>") {
        fixture.createModule("vivid-video");
        auto chainPath = fixture.writeChain(R"(
            #include <vivid/video/player.h>
        )");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        REQUIRE(modules.size() == 1);
        REQUIRE(modules[0].name == "vivid-video");
    }

    SECTION("discovers vivid-render3d from #include <vivid/render3d/>") {
        fixture.createModule("vivid-render3d");
        auto chainPath = fixture.writeChain(R"(
            #include <vivid/render3d/scene.h>
        )");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        REQUIRE(modules.size() == 1);
        REQUIRE(modules[0].name == "vivid-render3d");
    }

    SECTION("discovers vivid-imgui from #include <vivid/gui/>") {
        fixture.createModule("vivid-imgui");
        auto chainPath = fixture.writeChain(R"(
            #include <vivid/gui/imgui_panel.h>
        )");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        REQUIRE(modules.size() == 1);
        REQUIRE(modules[0].name == "vivid-imgui");
    }

    SECTION("ignores core namespaces") {
        auto chainPath = fixture.writeChain(R"(
            #include <vivid/effects/noise.h>
            #include <vivid/context.h>
            #include <vivid/chain.h>
            #include <vivid/operator.h>
            #include <vivid/vivid.h>
        )");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        REQUIRE(modules.empty());
    }

    SECTION("returns empty for chain with no module includes") {
        auto chainPath = fixture.writeChain(R"(
            #include <iostream>
            #include <vector>
            int main() { return 0; }
        )");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        REQUIRE(modules.empty());
    }

    SECTION("discovers multiple different modules") {
        fixture.createModule("vivid-audio");
        fixture.createModule("vivid-video");
        fixture.createModule("vivid-network");

        auto chainPath = fixture.writeChain(R"(
            #include <vivid/audio/audio_in.h>
            #include <vivid/video/player.h>
            #include <vivid/network/osc.h>
        )");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        REQUIRE(modules.size() == 3);
        auto names = getModuleNames(modules);
        REQUIRE(names.count("vivid-audio") == 1);
        REQUIRE(names.count("vivid-video") == 1);
        REQUIRE(names.count("vivid-network") == 1);
    }

    SECTION("deduplicates multiple includes of same module") {
        fixture.createModule("vivid-audio");

        auto chainPath = fixture.writeChain(R"(
            #include <vivid/audio/audio_in.h>
            #include <vivid/audio/fft.h>
            #include <vivid/audio/oscillator.h>
        )");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        REQUIRE(modules.size() == 1);
        REQUIRE(modules[0].name == "vivid-audio");
    }
}

// =============================================================================
// Module Metadata Parsing Tests
// =============================================================================

TEST_CASE("ModuleRegistry metadata", "[unit][modules]") {
    ModuleTestFixture fixture;

    SECTION("parses name from module.json") {
        fixture.createModule("vivid-test", {}, "2.0.0");
        auto chainPath = fixture.writeChain(R"(#include <vivid/test/foo.h>)");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        REQUIRE(modules.size() == 1);
        REQUIRE(modules[0].name == "vivid-test");
    }

    SECTION("parses version from module.json") {
        fixture.createModule("vivid-test", {}, "2.5.0");
        auto chainPath = fixture.writeChain(R"(#include <vivid/test/foo.h>)");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        REQUIRE(modules.size() == 1);
        REQUIRE(modules[0].version == "2.5.0");
    }

    SECTION("parses dependencies array from module.json") {
        fixture.createModule("vivid-a", {"vivid-b", "vivid-c"});
        fixture.createModule("vivid-b");
        fixture.createModule("vivid-c");
        auto chainPath = fixture.writeChain(R"(#include <vivid/a/foo.h>)");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        // Should have all 3 modules (a + its 2 deps)
        REQUIRE(modules.size() == 3);
    }

    SECTION("uses defaults when module.json missing") {
        fixture.createModuleNoJson("vivid-test");
        auto chainPath = fixture.writeChain(R"(#include <vivid/test/foo.h>)");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        REQUIRE(modules.size() == 1);
        // Name defaults to directory name
        REQUIRE(modules[0].name == "vivid-test");
        REQUIRE(modules[0].version.empty());
        REQUIRE(modules[0].dependencies.empty());
    }

    SECTION("handles empty dependencies array") {
        // Create module with explicit empty array
        auto modDir = fixture.tempDir() / "modules" / "vivid-test";
        fs::create_directories(modDir / "include");
        json j;
        j["name"] = "vivid-test";
        j["dependencies"] = json::array();
        std::ofstream out(modDir / "module.json");
        out << j.dump();
        out.close();

        auto chainPath = fixture.writeChain(R"(#include <vivid/test/foo.h>)");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        REQUIRE(modules.size() == 1);
        REQUIRE(modules[0].dependencies.empty());
    }
}

// =============================================================================
// Transitive Dependency Resolution Tests (THE KEY TESTS)
// =============================================================================

TEST_CASE("ModuleRegistry transitive dependencies", "[unit][modules]") {
    ModuleTestFixture fixture;

    SECTION("resolves single module with no dependencies") {
        fixture.createModule("vivid-audio");
        auto chainPath = fixture.writeChain(R"(#include <vivid/audio/foo.h>)");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        REQUIRE(modules.size() == 1);
        REQUIRE(modules[0].name == "vivid-audio");
    }

    SECTION("resolves A -> B direct dependency") {
        fixture.createModule("vivid-a", {"vivid-b"});
        fixture.createModule("vivid-b");
        auto chainPath = fixture.writeChain(R"(#include <vivid/a/foo.h>)");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        REQUIRE(modules.size() == 2);
        auto names = getModuleNames(modules);
        REQUIRE(names.count("vivid-a") == 1);
        REQUIRE(names.count("vivid-b") == 1);
    }

    SECTION("resolves A -> B -> C transitive chain") {
        fixture.createModule("vivid-a", {"vivid-b"});
        fixture.createModule("vivid-b", {"vivid-c"});
        fixture.createModule("vivid-c");
        auto chainPath = fixture.writeChain(R"(#include <vivid/a/foo.h>)");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        REQUIRE(modules.size() == 3);
        auto names = getModuleNames(modules);
        REQUIRE(names.count("vivid-a") == 1);
        REQUIRE(names.count("vivid-b") == 1);
        REQUIRE(names.count("vivid-c") == 1);
    }

    SECTION("resolves deep chain A -> B -> C -> D") {
        fixture.createModule("vivid-a", {"vivid-b"});
        fixture.createModule("vivid-b", {"vivid-c"});
        fixture.createModule("vivid-c", {"vivid-d"});
        fixture.createModule("vivid-d");
        auto chainPath = fixture.writeChain(R"(#include <vivid/a/foo.h>)");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        REQUIRE(modules.size() == 4);
        auto names = getModuleNames(modules);
        REQUIRE(names.count("vivid-a") == 1);
        REQUIRE(names.count("vivid-b") == 1);
        REQUIRE(names.count("vivid-c") == 1);
        REQUIRE(names.count("vivid-d") == 1);
    }

    SECTION("resolves diamond: A -> B,C and B,C -> D") {
        fixture.createModule("vivid-a", {"vivid-b", "vivid-c"});
        fixture.createModule("vivid-b", {"vivid-d"});
        fixture.createModule("vivid-c", {"vivid-d"});
        fixture.createModule("vivid-d");
        auto chainPath = fixture.writeChain(R"(#include <vivid/a/foo.h>)");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        // Should have exactly 4 modules (no duplicates of D)
        REQUIRE(modules.size() == 4);
        auto names = getModuleNames(modules);
        REQUIRE(names.count("vivid-a") == 1);
        REQUIRE(names.count("vivid-b") == 1);
        REQUIRE(names.count("vivid-c") == 1);
        REQUIRE(names.count("vivid-d") == 1);
    }

    SECTION("deduplicates when multiple paths to same module") {
        // Both B and C depend on shared D
        fixture.createModule("vivid-a", {"vivid-b", "vivid-c"});
        fixture.createModule("vivid-b", {"vivid-shared"});
        fixture.createModule("vivid-c", {"vivid-shared"});
        fixture.createModule("vivid-shared");
        auto chainPath = fixture.writeChain(R"(#include <vivid/a/foo.h>)");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        // vivid-shared should only appear once
        int sharedCount = 0;
        for (const auto& m : modules) {
            if (m.name == "vivid-shared") sharedCount++;
        }
        REQUIRE(sharedCount == 1);
    }

    SECTION("handles circular A -> B -> A without hanging") {
        // Note: We use module names like vivid-cyclea because the include
        // namespace regex only matches \w+ (no hyphens in namespace)
        fixture.createModule("vivid-cyclea", {"vivid-cycleb"});
        fixture.createModule("vivid-cycleb", {"vivid-cyclea"});
        auto chainPath = fixture.writeChain(R"(#include <vivid/cyclea/foo.h>)");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());

        // This should complete without infinite loop
        auto modules = registry.discoverFromChain(chainPath);

        // Both modules should be present exactly once
        REQUIRE(modules.size() == 2);
        auto names = getModuleNames(modules);
        REQUIRE(names.count("vivid-cyclea") == 1);
        REQUIRE(names.count("vivid-cycleb") == 1);
    }

    SECTION("handles deeper cycle A -> B -> C -> A") {
        fixture.createModule("vivid-x", {"vivid-y"});
        fixture.createModule("vivid-y", {"vivid-z"});
        fixture.createModule("vivid-z", {"vivid-x"});
        auto chainPath = fixture.writeChain(R"(#include <vivid/x/foo.h>)");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());

        auto modules = registry.discoverFromChain(chainPath);

        REQUIRE(modules.size() == 3);
        auto names = getModuleNames(modules);
        REQUIRE(names.count("vivid-x") == 1);
        REQUIRE(names.count("vivid-y") == 1);
        REQUIRE(names.count("vivid-z") == 1);
    }

    SECTION("continues when transitive dependency not found") {
        // A depends on B, B depends on missing C
        fixture.createModule("vivid-a", {"vivid-b"});
        fixture.createModule("vivid-b", {"vivid-missing"});
        // Note: vivid-missing is NOT created
        auto chainPath = fixture.writeChain(R"(#include <vivid/a/foo.h>)");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        // A and B should still be found
        REQUIRE(modules.size() == 2);
        auto names = getModuleNames(modules);
        REQUIRE(names.count("vivid-a") == 1);
        REQUIRE(names.count("vivid-b") == 1);
    }

    SECTION("continues when direct module not found") {
        // No modules exist
        auto chainPath = fixture.writeChain(R"(#include <vivid/nonexistent/foo.h>)");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        REQUIRE(modules.empty());
    }
}

// =============================================================================
// Real-World Scenario Test (The Bug That Was Fixed)
// =============================================================================

TEST_CASE("ModuleRegistry vivid-midi scenario", "[unit][modules]") {
    ModuleTestFixture fixture;

    SECTION("vivid-midi resolves vivid-audio dependency") {
        // This tests the exact scenario that caused the original bug:
        // Chain uses vivid-midi, which depends on vivid-audio
        fixture.createModule("vivid-midi", {"vivid-audio"}, "0.1.0");
        fixture.createModule("vivid-audio", {}, "0.1.0");

        auto chainPath = fixture.writeChain(R"(
            #include <vivid/vivid.h>
            #include <vivid/effects/effects.h>
            #include <vivid/midi/midi_in.h>

            void setup(Context& ctx) {
                // Uses MIDI
            }
        )");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        auto modules = registry.discoverFromChain(chainPath);

        // Should have both vivid-midi AND vivid-audio
        REQUIRE(modules.size() == 2);
        auto names = getModuleNames(modules);
        REQUIRE(names.count("vivid-midi") == 1);
        REQUIRE(names.count("vivid-audio") == 1);
    }
}

// =============================================================================
// Search Path Tests
// =============================================================================

TEST_CASE("ModuleRegistry search paths", "[unit][modules]") {
    ModuleTestFixture fixture;

    SECTION("getSearchPaths returns configured paths") {
        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());

        auto paths = registry.getSearchPaths();

        // Should include the modules/ subdirectory
        REQUIRE(!paths.empty());
        bool found = false;
        for (const auto& p : paths) {
            if (p == fixture.tempDir() / "modules") {
                found = true;
                break;
            }
        }
        REQUIRE(found);
    }

    SECTION("getModule returns info for discovered module") {
        fixture.createModule("vivid-test", {}, "1.2.3");
        auto chainPath = fixture.writeChain(R"(#include <vivid/test/foo.h>)");

        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());
        registry.discoverFromChain(chainPath);

        auto info = registry.getModule("vivid-test");
        REQUIRE(info.has_value());
        REQUIRE(info->name == "vivid-test");
        REQUIRE(info->version == "1.2.3");
    }

    SECTION("getModule returns nullopt for unknown module") {
        ModuleRegistry registry;
        registry.setRootDir(fixture.tempDir());

        auto info = registry.getModule("vivid-nonexistent");
        REQUIRE_FALSE(info.has_value());
    }
}
