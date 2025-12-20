// Time Machine - Vivid Showcase
// Temporal displacement effect inspired by TouchDesigner's TimeMachine
//
// Uses webcam (or video/noise fallback) feeding into a frame cache.
// A grayscale displacement map selects which cached frame to show at each
// pixel, creating slit-scan, time-echo, and temporal smearing effects.
//
// Controls:
//   Mouse X: Depth (how far back in time to reach)
//   Mouse Y: Displacement offset
//   1-5: Displacement pattern presets (slit-scan, radial, diagonal, noise)
//   SPACE: Reset frame cache
//   I: Invert displacement direction

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/video/video.h>
#include <cmath>
#include <iostream>

using namespace vivid;
using namespace vivid::effects;

// Global state
static int displacementPreset = 0;
static bool invertDisplacement = false;

// Displacement pattern presets
static const char* presetNames[] = {
    "Vertical Slit-Scan",
    "Horizontal Slit-Scan",
    "Radial Time Tunnel",
    "Diagonal Wipe",
    "Turbulent Noise"
};
static const int numPresets = 5;

void printControls() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Time Machine - Temporal Displacement" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  Mouse X: Time depth" << std::endl;
    std::cout << "  Mouse Y: Offset bias" << std::endl;
    std::cout << "  1-5: Displacement patterns" << std::endl;
    std::cout << "  I: Invert displacement" << std::endl;
    std::cout << "  SPACE: Reset cache" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void setup(Context& ctx) {
    ctx.fullscreen(true);  // Start in fullscreen mode

    auto& chain = ctx.chain();

    // =========================================================================
    // Video Source (or animated fallback)
    // =========================================================================

    // Use webcam as the source
    auto& webcam = chain.add<vivid::video::Webcam>("webcam");
    webcam.setResolution(1280, 720);
    webcam.setFrameRate(30.0f);

    TextureOperator* source = &webcam;
    std::cout << "[Source: Webcam]" << std::endl;


    // =========================================================================
    // Frame Cache - Stores N frames for temporal sampling
    // =========================================================================

    auto& cache = chain.add<FrameCache>("cache");
    cache.input(source);
    cache.frameCount = 64;  // ~2 seconds at 30fps

    // =========================================================================
    // Displacement Maps - Different patterns for different effects
    // =========================================================================

    // Vertical gradient (classic slit-scan) - angle=π/2 for vertical
    auto& vertGrad = chain.add<Gradient>("vertGrad");
    vertGrad.mode(GradientMode::Linear);
    vertGrad.angle = 1.5708f;  // π/2 radians = 90 degrees (vertical)
    vertGrad.colorA.set(0.0f, 0.0f, 0.0f, 1.0f);  // Black
    vertGrad.colorB.set(1.0f, 1.0f, 1.0f, 1.0f);  // White

    // Horizontal gradient (horizontal slit-scan) - angle=0
    auto& horzGrad = chain.add<Gradient>("horzGrad");
    horzGrad.mode(GradientMode::Linear);
    horzGrad.angle = 0.0f;  // Horizontal
    horzGrad.colorA.set(0.0f, 0.0f, 0.0f, 1.0f);
    horzGrad.colorB.set(1.0f, 1.0f, 1.0f, 1.0f);

    // Radial gradient (time tunnel effect)
    auto& radialGrad = chain.add<Gradient>("radialGrad");
    radialGrad.mode(GradientMode::Radial);
    radialGrad.colorA.set(0.0f, 0.0f, 0.0f, 1.0f);
    radialGrad.colorB.set(1.0f, 1.0f, 1.0f, 1.0f);

    // Diagonal gradient - angle=π/4
    auto& diagGrad = chain.add<Gradient>("diagGrad");
    diagGrad.mode(GradientMode::Linear);
    diagGrad.angle = 0.7854f;  // π/4 radians = 45 degrees (diagonal)
    diagGrad.colorA.set(0.0f, 0.0f, 0.0f, 1.0f);
    diagGrad.colorB.set(1.0f, 1.0f, 1.0f, 1.0f);

    // Animated noise (turbulent organic displacement)
    auto& dispNoise = chain.add<Noise>("dispNoise");
    dispNoise.scale = 3.0f;
    dispNoise.speed = 0.0f;  // We'll animate via offset.z in update()
    dispNoise.octaves = 3;

    // =========================================================================
    // Time Machine - Temporal displacement
    // =========================================================================

    auto& timeMachine = chain.add<TimeMachine>("timeMachine");
    timeMachine.cache(&cache);
    timeMachine.displacementMap(&vertGrad);  // Start with vertical slit-scan
    timeMachine.depth = 1.0f;
    timeMachine.offset = 0.0f;
    timeMachine.invert = false;

    // =========================================================================
    // Post-processing - Light bloom for polish
    // =========================================================================

    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input(&timeMachine);
    bloom.threshold = 0.9f;   // Higher threshold for webcam
    bloom.intensity = 0.2f;
    bloom.radius = 0.003f;

    chain.output("bloom");

    printControls();
    std::cout << "[Pattern: " << presetNames[displacementPreset] << "]" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Get operators
    auto& timeMachine = chain.get<TimeMachine>("timeMachine");
    auto& vertGrad = chain.get<Gradient>("vertGrad");
    auto& horzGrad = chain.get<Gradient>("horzGrad");
    auto& radialGrad = chain.get<Gradient>("radialGrad");
    auto& diagGrad = chain.get<Gradient>("diagGrad");
    auto& dispNoise = chain.get<Noise>("dispNoise");

    // =========================================================================
    // Input Handling
    // =========================================================================

    // Pattern preset selection (1-5)
    for (int i = 0; i < numPresets; i++) {
        if (ctx.key(GLFW_KEY_1 + i).pressed) {
            displacementPreset = i;
            std::cout << "\r[Pattern: " << presetNames[displacementPreset] << "]          " << std::flush;

            // Set displacement map based on preset
            switch (i) {
                case 0: timeMachine.displacementMap(&vertGrad); break;
                case 1: timeMachine.displacementMap(&horzGrad); break;
                case 2: timeMachine.displacementMap(&radialGrad); break;
                case 3: timeMachine.displacementMap(&diagGrad); break;
                case 4: timeMachine.displacementMap(&dispNoise); break;
            }
        }
    }

    // Invert displacement (I key)
    if (ctx.key(GLFW_KEY_I).pressed) {
        invertDisplacement = !invertDisplacement;
        timeMachine.invert = invertDisplacement;
        std::cout << "\r[Invert: " << (invertDisplacement ? "ON" : "OFF") << "]          " << std::flush;
    }

    // Reset cache (SPACE) - just reinitializes the time machine state
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        std::cout << "\r[Cache warming up...]          " << std::flush;
    }

    // =========================================================================
    // Mouse Controls
    // =========================================================================

    glm::vec2 mouse = ctx.mouseNorm();

    // X axis: Time depth (0.2 minimum so effect is always visible)
    float depth = 0.2f + (mouse.x * 0.5f + 0.5f) * 0.8f;  // Map [-1,1] to [0.2,1.0]
    timeMachine.depth = depth;

    // Y axis: Offset bias
    float offset = (mouse.y * 0.5f + 0.5f) * 0.5f;  // Map to [0, 0.5]
    timeMachine.offset = offset;

    // =========================================================================
    // Animated Displacement (subtle motion)
    // =========================================================================

    // Add subtle animation to the radial gradient center
    float cx = 0.5f + 0.1f * std::sin(time * 0.3f);
    float cy = 0.5f + 0.1f * std::cos(time * 0.4f);
    radialGrad.center.set(cx, cy);

    // Animate noise displacement along Z axis for smooth evolution
    dispNoise.offset.set(0.0f, 0.0f, time * 0.3f);
}

VIVID_CHAIN(setup, update)
