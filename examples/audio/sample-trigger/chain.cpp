// Sample Trigger Demo - Vivid Example
// Demonstrates SampleBank and SamplePlayer for loading and triggering audio samples
// Controls:
//   1-8: Trigger samples by index
//   Q-I: Trigger samples with pitch variation
//   UP/DOWN: Master volume
//   TAB: Open parameter controls

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/audio/audio.h>
#include <vivid/audio_output.h>
#include <iostream>
#include <cmath>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::audio;

// Visual hit indicators
static float hitDecay[8] = {0};
static int lastTriggered = -1;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Sample Bank - Load samples from folder
    // =========================================================================
    // Place your .wav files in assets/audio/samples/
    // Or change the path below to your sample folder

    auto& bank = chain.add<SampleBank>("bank");
    bank.folder("assets/audio/samples");

    // Alternative: Load individual files
    // bank.file("assets/audio/kick.wav")
    //     .file("assets/audio/snare.wav")
    //     .file("assets/audio/hihat.wav");

    // =========================================================================
    // Sample Player - Polyphonic sample playback
    // =========================================================================

    auto& player = chain.add<SamplePlayer>("player");
    player.bank("bank")
          .voices(16)    // Max 16 simultaneous voices
          .volume(0.8f);

    // =========================================================================
    // Effects Chain
    // =========================================================================

    // Add reverb for ambience
    auto& reverb = chain.add<Reverb>("reverb");
    reverb.input("player")
          .roomSize(0.4f)
          .damping(0.5f)
          .mix(0.2f);

    // Master gain control
    auto& gain = chain.add<AudioGain>("gain");
    gain.gain(1.0f).input("reverb");

    // =========================================================================
    // Audio Output
    // =========================================================================

    auto& audioOut = chain.add<AudioOutput>("audioOut");
    audioOut.input("gain").volume(1.0f);
    chain.audioOutput("audioOut");

    // =========================================================================
    // Visual Feedback
    // =========================================================================

    // Dark background
    auto& bg = chain.add<SolidColor>("bg");
    bg.color(0.08f, 0.06f, 0.1f);

    // Create 8 pad visualizers in a 4x2 grid
    for (int i = 0; i < 8; ++i) {
        std::string name = "pad" + std::to_string(i);
        auto& pad = chain.add<Shape>(name);

        float x = 0.2f + (i % 4) * 0.2f;  // 4 columns
        float y = 0.4f + (i / 4) * 0.3f;  // 2 rows

        // Color palette - different color per pad
        float hue = static_cast<float>(i) / 8.0f;
        float r = 0.5f + 0.5f * std::sin(hue * 6.28f);
        float g = 0.5f + 0.5f * std::sin(hue * 6.28f + 2.09f);
        float b = 0.5f + 0.5f * std::sin(hue * 6.28f + 4.19f);

        pad.type(ShapeType::Rectangle)
           .position(x, y)
           .size(0.12f, 0.18f)
           .color(r, g, b, 0.3f)
           .cornerRadius(0.02f);
    }

    // Composite all layers
    auto& comp = chain.add<Composite>("comp");
    comp.input(0, &bg);
    for (int i = 0; i < 8; ++i) {
        auto& pad = chain.get<Shape>("pad" + std::to_string(i));
        comp.input(i + 1, &pad);
    }
    comp.mode(BlendMode::Add);

    chain.output("comp");

    // =========================================================================
    // Console Output
    // =========================================================================

    std::cout << "\n========================================" << std::endl;
    std::cout << "Sample Trigger Demo" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  1-8: Trigger samples (normal pitch)" << std::endl;
    std::cout << "  Q-I: Trigger samples (pitch up)" << std::endl;
    std::cout << "  A-K: Trigger samples (pitch down)" << std::endl;
    std::cout << "  UP/DOWN: Master volume" << std::endl;
    std::cout << "  TAB: Open parameter controls" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "\nPlace .wav files in assets/audio/samples/" << std::endl;
    std::cout << "Loaded samples will appear below:\n" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& player = chain.get<SamplePlayer>("player");
    auto& bank = chain.get<SampleBank>("bank");
    auto& gain = chain.get<AudioGain>("gain");

    // Print loaded samples on first frame
    static bool firstFrame = true;
    if (firstFrame) {
        firstFrame = false;
        auto names = bank.names();
        if (names.empty()) {
            std::cout << "[No samples found - add .wav files to assets/audio/samples/]" << std::endl;
        } else {
            std::cout << "Loaded " << names.size() << " samples:" << std::endl;
            for (size_t i = 0; i < names.size() && i < 8; ++i) {
                std::cout << "  " << (i + 1) << ": " << names[i] << std::endl;
            }
        }
        std::cout << std::endl;
    }

    // =========================================================================
    // Input Controls - Normal pitch (1-8)
    // =========================================================================

    const int numberKeys[] = {
        GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3, GLFW_KEY_4,
        GLFW_KEY_5, GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8
    };

    for (int i = 0; i < 8; ++i) {
        if (ctx.key(numberKeys[i]).pressed) {
            player.trigger(i, 1.0f, 0.0f, 1.0f);  // Normal pitch
            hitDecay[i] = 1.0f;
            lastTriggered = i;
        }
    }

    // =========================================================================
    // Input Controls - Pitch up (Q-I row)
    // =========================================================================

    const int qwertKeys[] = {
        GLFW_KEY_Q, GLFW_KEY_W, GLFW_KEY_E, GLFW_KEY_R,
        GLFW_KEY_T, GLFW_KEY_Y, GLFW_KEY_U, GLFW_KEY_I
    };

    for (int i = 0; i < 8; ++i) {
        if (ctx.key(qwertKeys[i]).pressed) {
            // Random pitch variation up (1.0 to 2.0)
            float pitch = 1.0f + (static_cast<float>(rand()) / RAND_MAX) * 1.0f;
            player.trigger(i, 0.8f, 0.0f, pitch);
            hitDecay[i] = 1.0f;
            lastTriggered = i;
        }
    }

    // =========================================================================
    // Input Controls - Pitch down (A-K row)
    // =========================================================================

    const int asdfKeys[] = {
        GLFW_KEY_A, GLFW_KEY_S, GLFW_KEY_D, GLFW_KEY_F,
        GLFW_KEY_G, GLFW_KEY_H, GLFW_KEY_J, GLFW_KEY_K
    };

    for (int i = 0; i < 8; ++i) {
        if (ctx.key(asdfKeys[i]).pressed) {
            // Random pitch variation down (0.5 to 1.0)
            float pitch = 0.5f + (static_cast<float>(rand()) / RAND_MAX) * 0.5f;
            player.trigger(i, 0.9f, 0.0f, pitch);
            hitDecay[i] = 1.0f;
            lastTriggered = i;
        }
    }

    // =========================================================================
    // Volume Control
    // =========================================================================

    float currentGain = 1.0f;
    float gainVal[4];
    if (gain.getParam("gain", gainVal)) {
        currentGain = gainVal[0];
    }

    if (ctx.key(GLFW_KEY_UP).pressed) {
        currentGain = std::min(currentGain + 0.1f, 2.0f);
        gain.gain(currentGain);
        std::cout << "\r[Volume: " << static_cast<int>(currentGain * 100) << "%]   " << std::flush;
    }
    if (ctx.key(GLFW_KEY_DOWN).pressed) {
        currentGain = std::max(currentGain - 0.1f, 0.0f);
        gain.gain(currentGain);
        std::cout << "\r[Volume: " << static_cast<int>(currentGain * 100) << "%]   " << std::flush;
    }

    // =========================================================================
    // Visual Feedback
    // =========================================================================

    float decayRate = 1.0f - ctx.dt() * 6.0f;

    for (int i = 0; i < 8; ++i) {
        hitDecay[i] *= decayRate;

        auto& pad = chain.get<Shape>("pad" + std::to_string(i));

        // Update size - pulse on hit
        float baseSize = 0.12f;
        float hitSize = baseSize + hitDecay[i] * 0.04f;
        pad.size(hitSize, hitSize * 1.5f);

        // Update color - brighten on hit
        float hue = static_cast<float>(i) / 8.0f;
        float r = 0.5f + 0.5f * std::sin(hue * 6.28f);
        float g = 0.5f + 0.5f * std::sin(hue * 6.28f + 2.09f);
        float b = 0.5f + 0.5f * std::sin(hue * 6.28f + 4.19f);

        float brightness = 0.3f + hitDecay[i] * 0.7f;
        pad.color(r, g, b, brightness);
    }
}

VIVID_CHAIN(setup, update)
