// Flow Field - Vivid Showcase
// Generative particle art with noise-driven movement and GPU plexus network
//
// Controls:
//   Mouse X: Turbulence intensity
//   Mouse Y: Trail length (feedback decay)
//   SPACE: Reset particles
//   1-4: Color presets
//   R: Randomize attractor positions

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <cmath>
#include <iostream>

using namespace vivid;
using namespace vivid::effects;

// Global state
static int colorPreset = 0;
static float attractorPhase = 0.0f;

// Color presets for different moods
struct ColorPreset {
    const char* name;
    Color color1;
    Color color2;
    Color color3;
    Color plexusNode;
    Color plexusLine;
};

static const ColorPreset presets[] = {
    {"Cyber",  Color::fromHex("#00CCFF"), Color::fromHex("#0080CC"), Color::fromHex("#334D80"),
               Color::fromHex("#00E6FF").withAlpha(0.9f), Color::fromHex("#0099E6").withAlpha(0.35f)},
    {"Matrix", Color::Lime, Color::fromHex("#00B333"), Color::fromHex("#00661A"),
               Color::fromHex("#00FF66").withAlpha(0.9f), Color::fromHex("#00CC33").withAlpha(0.35f)},
    {"Ember",  Color::OrangeRed, Color::fromHex("#CC330D"), Color::fromHex("#661A0D"),
               Color::Coral.withAlpha(0.9f), Color::fromHex("#FF4D1A").withAlpha(0.35f)},
    {"Void",   Color::LightSlateGray, Color::fromHex("#4D4D66"), Color::fromHex("#262633"),
               Color::fromHex("#B3B3CC").withAlpha(0.9f), Color::fromHex("#808099").withAlpha(0.3f)},
};
static const int numPresets = 4;

void printControls() {
    std::cout << "\n========================================" << std::endl;
    std::cout << "Flow Field - Generative Art" << std::endl;
    std::cout << "========================================" << std::endl;
    std::cout << "Controls:" << std::endl;
    std::cout << "  Mouse X: Turbulence intensity" << std::endl;
    std::cout << "  Mouse Y: Trail length" << std::endl;
    std::cout << "  SPACE: Reset particles" << std::endl;
    std::cout << "  1-4: Color presets" << std::endl;
    std::cout << "  R: Randomize flow" << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // =========================================================================
    // Particle Systems - Three layers with different behaviors
    // =========================================================================

    // Layer 1: Dense field of particles - slow, flowing
    auto& flow1 = chain.add<Particles>("flow1");
    flow1.emitter(EmitterShape::Disc)
        .position(0.5f, 0.5f)
        .emitterSize(0.7f)
        .emitRate(600.0f)
        .maxParticles(30000)
        .velocity(0.0f, 0.0f)
        .radialVelocity(0.015f)
        .turbulence(0.12f)
        .drag(0.6f)
        .life(5.0f)
        .lifeVariation(0.5f)
        .size(0.002f, 0.0005f)
        .color(presets[0].color1)
        .colorEnd(presets[0].color1.withAlpha(0.0f))
        .fadeOut(true)
        .clearColor(Color::Black);

    // Layer 2: Swirling particles around attractors
    auto& flow2 = chain.add<Particles>("flow2");
    flow2.emitter(EmitterShape::Ring)
        .position(0.5f, 0.5f)
        .emitterSize(0.35f)
        .emitRate(400.0f)
        .maxParticles(20000)
        .velocity(0.0f, 0.0f)
        .turbulence(0.18f)
        .attractor(0.5f, 0.5f, 0.25f)
        .drag(0.4f)
        .life(4.0f)
        .lifeVariation(0.4f)
        .size(0.0015f, 0.0003f)
        .color(presets[0].color2)
        .colorEnd(presets[0].color2.withAlpha(0.0f))
        .fadeOut(true)
        .clearColor(Color::Transparent);

    // Layer 3: Fast accent particles
    auto& flow3 = chain.add<Particles>("flow3");
    flow3.emitter(EmitterShape::Disc)
        .position(0.5f, 0.5f)
        .emitterSize(0.5f)
        .emitRate(200.0f)
        .maxParticles(10000)
        .velocity(0.0f, 0.0f)
        .turbulence(0.22f)
        .drag(0.25f)
        .life(3.5f)
        .lifeVariation(0.5f)
        .size(0.001f, 0.0002f)
        .color(presets[0].color3)
        .colorEnd(presets[0].color3.withAlpha(0.0f))
        .fadeOut(true)
        .clearColor(Color::Transparent);

    // =========================================================================
    // GPU Plexus Network - Nodes connected by proximity lines
    // =========================================================================

    auto& plexus = chain.add<Plexus>("plexus");
    plexus.nodeCount(350)
          .nodeSize(0.003f)
          .nodeColor(presets[0].plexusNode)
          .connectionDistance(0.09f)
          .lineWidth(1.0f)
          .lineColor(presets[0].plexusLine)
          .turbulence(0.06f)
          .drag(0.6f)
          .centerAttraction(0.08f)
          .spread(0.65f)
          .clearColor(Color::Transparent);

    // =========================================================================
    // Compositing - Layer everything together
    // =========================================================================

    auto& particleComp = chain.add<Composite>("particleComp");
    particleComp.input(0, &flow1)
                .input(1, &flow2)
                .input(2, &flow3)
                .input(3, &plexus)
                .mode(BlendMode::Add);

    // =========================================================================
    // Feedback - Creates trailing effect
    // =========================================================================

    auto& feedback = chain.add<Feedback>("feedback");
    feedback.input(&particleComp)
        .decay(0.96f)
        .mix(0.4f)
        .zoom(1.001f)
        .rotate(0.001f);

    // =========================================================================
    // Post-processing - Bloom for ethereal glow
    // =========================================================================

    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input(&feedback)
        .threshold(0.3f)
        .intensity(0.4f)
        .radius(0.01f);

    chain.output("bloom");

    printControls();
    std::cout << "[Preset: " << presets[colorPreset].name << "]" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = static_cast<float>(ctx.time());

    // Get operators
    auto& flow1 = chain.get<Particles>("flow1");
    auto& flow2 = chain.get<Particles>("flow2");
    auto& flow3 = chain.get<Particles>("flow3");
    auto& plexus = chain.get<Plexus>("plexus");
    auto& feedback = chain.get<Feedback>("feedback");

    // =========================================================================
    // Input Handling
    // =========================================================================

    // Color preset selection (1-4)
    for (int i = 0; i < numPresets; i++) {
        if (ctx.key(GLFW_KEY_1 + i).pressed) {
            colorPreset = i;
            std::cout << "\r[Preset: " << presets[colorPreset].name << "]          " << std::flush;

            const auto& p = presets[colorPreset];
            flow1.color(p.color1).colorEnd(p.color1.withAlpha(0.0f));
            flow2.color(p.color2).colorEnd(p.color2.withAlpha(0.0f));
            flow3.color(p.color3).colorEnd(p.color3.withAlpha(0.0f));
            plexus.nodeColor(p.plexusNode).lineColor(p.plexusLine);
        }
    }

    // Reset particles (SPACE)
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        flow1.burst(0);
        flow2.burst(0);
        flow3.burst(0);
    }

    // Randomize attractor phase (R)
    if (ctx.key(GLFW_KEY_R).pressed) {
        attractorPhase += 3.14159f;
        std::cout << "\r[Randomized flow]          " << std::flush;
    }

    // =========================================================================
    // Mouse Controls
    // =========================================================================

    glm::vec2 mouse = ctx.mouseNorm();

    float turbulence = 0.05f + (mouse.x * 0.5f + 0.5f) * 0.35f;
    flow1.turbulence(turbulence);
    flow2.turbulence(turbulence * 1.2f);
    flow3.turbulence(turbulence * 1.5f);
    plexus.turbulence(turbulence * 0.3f);

    float decay = 0.9f + (mouse.y * 0.5f + 0.5f) * 0.09f;
    feedback.decay(decay);

    // =========================================================================
    // Animated Attractors
    // =========================================================================

    float a1x = 0.5f + 0.25f * std::cos(time * 0.3f + attractorPhase);
    float a1y = 0.5f + 0.25f * std::sin(time * 0.4f + attractorPhase);
    flow2.attractor(a1x, a1y, 0.25f);

    float e2x = 0.5f + 0.15f * std::cos(time * 0.2f);
    float e2y = 0.5f + 0.15f * std::sin(time * 0.25f);
    flow2.position(e2x, e2y);

    float breathe = 1.0f + 0.08f * std::sin(time * 0.5f);
    flow1.emitterSize(0.7f * breathe);

    float rotation = 0.002f * std::sin(time * 0.3f);
    feedback.rotate(rotation);
}

VIVID_CHAIN(setup, update)
