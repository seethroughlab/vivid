// FM Synth Preset Test - verifies preset load/save functionality
#include <vivid/vivid.h>
#include <vivid/effects/noise.h>
#include <vivid/audio/fm_synth.h>
#include <iostream>

using namespace vivid;
using namespace vivid::audio;

FMSynth* fm = nullptr;
bool testedPresets = false;

void setup(Context& ctx) {
    auto& chain = ctx.chain();
    fm = &chain.add<FMSynth>("fm");
    fm->volume = 0.3f;

    // Add a noise background for visual feedback
    auto& bg = chain.add<vivid::effects::Noise>("noise");
    bg.scale = 4.0f;
    chain.output("noise");
}

void update(Context& ctx) {
    ctx.chain().process(ctx);

    // Run preset tests once
    if (!testedPresets) {
        testedPresets = true;

        std::cerr << "\n=== FMSynth Preset Test ===\n\n";

        // List factory presets
        std::cerr << "Available factory presets:\n";
        auto presets = PresetCapable::listPresets("FMSynth");
        for (const auto& p : presets) {
            std::cerr << "  - " << p.name << " (" << p.category << ")"
                      << (p.isFactory ? " [factory]" : " [user]") << "\n";
        }
        std::cerr << "\n";

        // Load a preset
        if (!presets.empty()) {
            std::cerr << "Loading preset: " << presets[0].name << "...\n";
            if (fm->loadPresetFile(presets[0].path)) {
                std::cerr << "  Loaded successfully!\n";
                std::cerr << "  ratio1=" << static_cast<float>(fm->ratio1)
                          << " level1=" << static_cast<float>(fm->level1)
                          << " feedback=" << static_cast<float>(fm->feedback) << "\n";
            } else {
                std::cerr << "  FAILED to load!\n";
            }
        }

        // Save a custom preset
        std::cerr << "\nSaving custom preset...\n";
        fm->ratio1 = 2.0f;
        fm->ratio2 = 5.0f;
        fm->feedback = 0.5f;

        auto userDir = PresetCapable::userPresetDir() / "FMSynth";
        std::string customPath = (userDir / "TestPreset.json").string();

        if (fm->savePreset(customPath, "Test Preset", "Claude", "Test")) {
            std::cerr << "  Saved to: " << customPath << "\n";

            // Load it back
            std::cerr << "\nReloading custom preset...\n";
            fm->ratio1 = 1.0f;  // Reset
            if (fm->loadPresetFile(customPath)) {
                std::cerr << "  Loaded successfully!\n";
                std::cerr << "  ratio1=" << static_cast<float>(fm->ratio1)
                          << " (expected 2.0)\n";
            }
        } else {
            std::cerr << "  FAILED to save!\n";
        }

        std::cerr << "\n=== Test Complete ===\n\n";

        // Play a note to hear the preset
        fm->noteOn(440.0f);
    }

    // Release note after 2 seconds
    static int frame = 0;
    if (frame == 120) {
        fm->noteOff(440.0f);
    }
    frame++;
}

VIVID_CHAIN(setup, update)
