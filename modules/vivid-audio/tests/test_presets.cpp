/**
 * @file test_presets.cpp
 * @brief Unit tests for PresetCapable via FMSynth concrete implementation
 *
 * FMSynth default-constructs without audio context.
 */

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <vivid/audio/fm_synth.h>
#include <vivid/audio/preset.h>
#include <nlohmann/json.hpp>
#include <fstream>
#include <cstdio>
#include <filesystem>

using namespace vivid::audio;
using Catch::Matchers::WithinAbs;
using json = nlohmann::json;

namespace fs = std::filesystem;

static void removeFile(const std::string& path) {
    std::remove(path.c_str());
}

// ---------------------------------------------------------------------------
// Save
// ---------------------------------------------------------------------------

TEST_CASE("FMSynth preset save", "[unit][preset]") {
    const std::string path = (fs::temp_directory_path() / "vivid_test_preset.json").string();

    SECTION("savePreset returns true and creates file") {
        FMSynth synth;
        REQUIRE(synth.savePreset(path, "Test Preset", "Tester", "Bass"));
        REQUIRE(fs::exists(path));
        removeFile(path);
    }

    SECTION("JSON contains metadata") {
        FMSynth synth;
        synth.savePreset(path, "My Sound", "Jeff", "Pads");

        std::ifstream f(path);
        auto j = json::parse(f);

        REQUIRE(j["synth"] == "FMSynth");
        REQUIRE(j["name"] == "My Sound");
        REQUIRE(j["author"] == "Jeff");
        REQUIRE(j["category"] == "Pads");
        REQUIRE(j.contains("params"));
        REQUIRE(j["params"].is_object());

        removeFile(path);
    }

    SECTION("param values are written correctly") {
        FMSynth synth;
        synth.ratio1 = 2.5f;
        synth.feedback = 0.7f;
        synth.volume = 0.8f;
        synth.savePreset(path, "Custom");

        std::ifstream f(path);
        auto j = json::parse(f);

        REQUIRE_THAT(j["params"]["ratio1"].get<double>(), WithinAbs(2.5, 0.001));
        REQUIRE_THAT(j["params"]["feedback"].get<double>(), WithinAbs(0.7, 0.001));
        REQUIRE_THAT(j["params"]["volume"].get<double>(), WithinAbs(0.8, 0.001));

        removeFile(path);
    }

    SECTION("algorithm written via serializeExtra") {
        FMSynth synth;
        synth.setAlgorithm(FMAlgorithm::Pairs);
        synth.savePreset(path, "Algo Test");

        std::ifstream f(path);
        auto j = json::parse(f);

        REQUIRE(j["algorithm"] == "Pairs");

        removeFile(path);
    }

    SECTION("returns false for invalid path") {
        FMSynth synth;
        // Use a file as a directory component — guaranteed to fail on all platforms
        auto blocker = fs::temp_directory_path() / "vivid_test_blocker";
        { std::ofstream f(blocker); f << "x"; }
        auto badPath = blocker / "sub" / "preset.json";
        REQUIRE_FALSE(synth.savePreset(badPath.string()));
        fs::remove(blocker);
    }
}

// ---------------------------------------------------------------------------
// Load
// ---------------------------------------------------------------------------

TEST_CASE("FMSynth preset load", "[unit][preset]") {
    const std::string path = (fs::temp_directory_path() / "vivid_test_preset_load.json").string();

    SECTION("save + load round-trip preserves all params and algorithm") {
        FMSynth original;
        original.ratio1 = 3.0f;
        original.ratio2 = 5.0f;
        original.level1 = 0.6f;
        original.feedback = 0.4f;
        original.volume = 0.9f;
        original.setAlgorithm(FMAlgorithm::Diamond);

        REQUIRE(original.savePreset(path, "RoundTrip"));

        FMSynth loaded;
        REQUIRE(loaded.loadPresetFile(path));

        REQUIRE_THAT(static_cast<double>(static_cast<float>(loaded.ratio1)), WithinAbs(3.0, 0.001));
        REQUIRE_THAT(static_cast<double>(static_cast<float>(loaded.ratio2)), WithinAbs(5.0, 0.001));
        REQUIRE_THAT(static_cast<double>(static_cast<float>(loaded.level1)), WithinAbs(0.6, 0.001));
        REQUIRE_THAT(static_cast<double>(static_cast<float>(loaded.feedback)), WithinAbs(0.4, 0.001));
        REQUIRE_THAT(static_cast<double>(static_cast<float>(loaded.volume)), WithinAbs(0.9, 0.001));
        REQUIRE(loaded.algorithm() == FMAlgorithm::Diamond);

        removeFile(path);
    }

    SECTION("returns false for nonexistent file") {
        FMSynth synth;
        REQUIRE_FALSE(synth.loadPresetFile((fs::temp_directory_path() / "vivid_nonexistent_preset.json").string()));
    }

    SECTION("handles partial params gracefully") {
        // Write a preset with only some params
        json j;
        j["synth"] = "FMSynth";
        j["name"] = "Partial";
        j["params"]["ratio1"] = 7.0f;
        // Omit ratio2, feedback, volume, etc.

        {
            std::ofstream f(path);
            f << j.dump(2);
        }

        FMSynth synth;
        float defaultRatio2 = static_cast<float>(synth.ratio2);
        float defaultVolume = static_cast<float>(synth.volume);

        REQUIRE(synth.loadPresetFile(path));

        // ratio1 should be updated
        REQUIRE_THAT(static_cast<double>(static_cast<float>(synth.ratio1)), WithinAbs(7.0, 0.001));

        // Others should remain at defaults
        REQUIRE_THAT(static_cast<double>(static_cast<float>(synth.ratio2)),
                     WithinAbs(static_cast<double>(defaultRatio2), 0.001));
        REQUIRE_THAT(static_cast<double>(static_cast<float>(synth.volume)),
                     WithinAbs(static_cast<double>(defaultVolume), 0.001));

        removeFile(path);
    }
}

// ---------------------------------------------------------------------------
// Directory helpers
// ---------------------------------------------------------------------------

TEST_CASE("PresetCapable directory helpers", "[unit][preset]") {
    SECTION("userPresetDir path contains .vivid and presets") {
        auto dir = PresetCapable::userPresetDir();
        auto dirStr = dir.string();
        REQUIRE(dirStr.find(".vivid") != std::string::npos);
        REQUIRE(dirStr.find("presets") != std::string::npos);
    }

    SECTION("factoryPresetDir path contains presets") {
        auto dir = PresetCapable::factoryPresetDir();
        auto dirStr = dir.string();
        REQUIRE(dirStr.find("presets") != std::string::npos);
    }

    SECTION("listPresets returns empty for nonexistent synth type") {
        auto presets = PresetCapable::listPresets("NonExistentSynthType12345");
        REQUIRE(presets.empty());
    }
}
