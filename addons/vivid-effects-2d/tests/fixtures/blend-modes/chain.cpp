// Blend Modes Test - Tests all Composite blend modes
// Tests: Over, Add, Multiply, Screen, Overlay, Difference modes

#include <vivid/vivid.h>
#include <vivid/effects/noise.h>
#include <vivid/effects/gradient.h>
#include <vivid/effects/hsv.h>
#include <vivid/effects/solid_color.h>
#include <vivid/effects/shape.h>
#include <vivid/effects/composite.h>
#include <vivid/effects/transform.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Base layer A: Animated noise with warm colors
    chain.add<Noise>("noise_a")
        .scale(2.0f)
        .speed(0.2f)
        .octaves(3);

    chain.add<HSV>("warm")
        .input("noise_a")
        .hue(0.08f)
        .saturation(0.9f)
        .value(0.8f);

    // Base layer B: Radial gradient with cool colors
    chain.add<Gradient>("gradient_b")
        .type(Gradient::Type::Radial)
        .startColor(0.2f, 0.5f, 1.0f, 1.0f)
        .endColor(0.8f, 0.2f, 0.6f, 1.0f);

    // Create 6 composite nodes with different blend modes
    // Row 1
    chain.add<Composite>("blend_over")
        .input(0, "warm")
        .input(1, "gradient_b")
        .mode(Composite::Mode::Over)
        .opacity(0.6f);

    chain.add<Composite>("blend_add")
        .input(0, "warm")
        .input(1, "gradient_b")
        .mode(Composite::Mode::Add);

    chain.add<Composite>("blend_multiply")
        .input(0, "warm")
        .input(1, "gradient_b")
        .mode(Composite::Mode::Multiply);

    // Row 2
    chain.add<Composite>("blend_screen")
        .input(0, "warm")
        .input(1, "gradient_b")
        .mode(Composite::Mode::Screen);

    chain.add<Composite>("blend_overlay")
        .input(0, "warm")
        .input(1, "gradient_b")
        .mode(Composite::Mode::Overlay);

    chain.add<Composite>("blend_diff")
        .input(0, "warm")
        .input(1, "gradient_b")
        .mode(Composite::Mode::Difference);

    // Labels using shapes
    chain.add<SolidColor>("black").color(0.0f, 0.0f, 0.0f, 0.6f);

    // Transform each blend result into a grid cell
    // Row 1: Over, Add, Multiply
    chain.add<Transform>("t_over").input("blend_over").scale(0.33f).translate(-0.67f, 0.5f);
    chain.add<Transform>("t_add").input("blend_add").scale(0.33f).translate(0.0f, 0.5f);
    chain.add<Transform>("t_multiply").input("blend_multiply").scale(0.33f).translate(0.67f, 0.5f);

    // Row 2: Screen, Overlay, Difference
    chain.add<Transform>("t_screen").input("blend_screen").scale(0.33f).translate(-0.67f, -0.5f);
    chain.add<Transform>("t_overlay").input("blend_overlay").scale(0.33f).translate(0.0f, -0.5f);
    chain.add<Transform>("t_diff").input("blend_diff").scale(0.33f).translate(0.67f, -0.5f);

    // Final composite showing all blend modes
    chain.add<Composite>("final")
        .input(0, "black")
        .input(1, "t_over")
        .input(2, "t_add")
        .input(3, "t_multiply")
        .input(4, "t_screen")
        .input(5, "t_overlay")
        .input(6, "t_diff")
        .mode(Composite::Mode::Over);

    chain.output("final");
}

void update(Context& ctx) {
    // Animation from noise
}

VIVID_CHAIN(setup, update)
