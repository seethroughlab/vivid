// Texture Generators Test
// Tests: Shape, Gradient, SolidColor, Ramp, Noise (all types)

#include <vivid/vivid.h>
#include <vivid/effects/shape.h>
#include <vivid/effects/gradient.h>
#include <vivid/effects/solid_color.h>
#include <vivid/effects/ramp.h>
#include <vivid/effects/noise.h>
#include <vivid/effects/transform.h>
#include <vivid/effects/composite.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // === SHAPE TYPES ===
    chain.add<Shape>("circle")
        .type(Shape::Type::Circle)
        .size(0.8f)
        .color(1.0f, 0.4f, 0.2f, 1.0f);

    chain.add<Shape>("rect")
        .type(Shape::Type::Rectangle)
        .size(0.7f)
        .color(0.2f, 0.8f, 0.4f, 1.0f);

    chain.add<Shape>("triangle")
        .type(Shape::Type::Triangle)
        .size(0.75f)
        .color(0.3f, 0.5f, 1.0f, 1.0f);

    chain.add<Shape>("star")
        .type(Shape::Type::Star)
        .size(0.7f)
        .points(5)
        .color(1.0f, 0.9f, 0.2f, 1.0f);

    // === GRADIENT TYPES ===
    chain.add<Gradient>("grad_linear")
        .type(Gradient::Type::Linear)
        .startColor(1.0f, 0.0f, 0.0f, 1.0f)
        .endColor(0.0f, 0.0f, 1.0f, 1.0f)
        .angle(45.0f);

    chain.add<Gradient>("grad_radial")
        .type(Gradient::Type::Radial)
        .startColor(1.0f, 1.0f, 0.0f, 1.0f)
        .endColor(0.5f, 0.0f, 0.5f, 1.0f);

    chain.add<Gradient>("grad_angular")
        .type(Gradient::Type::Angular)
        .startColor(0.0f, 1.0f, 1.0f, 1.0f)
        .endColor(1.0f, 0.0f, 1.0f, 1.0f);

    chain.add<Gradient>("grad_diamond")
        .type(Gradient::Type::Diamond)
        .startColor(1.0f, 0.5f, 0.0f, 1.0f)
        .endColor(0.0f, 0.5f, 1.0f, 1.0f);

    // === NOISE TYPES ===
    chain.add<Noise>("noise_perlin")
        .type(Noise::Type::Perlin)
        .scale(3.0f)
        .speed(0.2f);

    chain.add<Noise>("noise_simplex")
        .type(Noise::Type::Simplex)
        .scale(3.0f)
        .speed(0.2f);

    chain.add<Noise>("noise_worley")
        .type(Noise::Type::Worley)
        .scale(3.0f)
        .speed(0.2f);

    chain.add<Noise>("noise_value")
        .type(Noise::Type::Value)
        .scale(3.0f)
        .speed(0.2f);

    // === RAMP (Color Cycle) ===
    chain.add<Ramp>("ramp")
        .speed(0.3f);

    // === SOLID COLOR ===
    chain.add<SolidColor>("solid")
        .color(0.2f, 0.2f, 0.25f, 1.0f);

    // === Grid Layout ===
    // Row 1: Shapes (4)
    chain.add<Transform>("t_circle").input("circle").scale(0.25f).translate(-0.75f, 0.75f);
    chain.add<Transform>("t_rect").input("rect").scale(0.25f).translate(-0.25f, 0.75f);
    chain.add<Transform>("t_triangle").input("triangle").scale(0.25f).translate(0.25f, 0.75f);
    chain.add<Transform>("t_star").input("star").scale(0.25f).translate(0.75f, 0.75f);

    // Row 2: Gradients (4)
    chain.add<Transform>("t_glin").input("grad_linear").scale(0.25f).translate(-0.75f, 0.25f);
    chain.add<Transform>("t_grad").input("grad_radial").scale(0.25f).translate(-0.25f, 0.25f);
    chain.add<Transform>("t_gang").input("grad_angular").scale(0.25f).translate(0.25f, 0.25f);
    chain.add<Transform>("t_gdia").input("grad_diamond").scale(0.25f).translate(0.75f, 0.25f);

    // Row 3: Noise types (4)
    chain.add<Transform>("t_nper").input("noise_perlin").scale(0.25f).translate(-0.75f, -0.25f);
    chain.add<Transform>("t_nsim").input("noise_simplex").scale(0.25f).translate(-0.25f, -0.25f);
    chain.add<Transform>("t_nwor").input("noise_worley").scale(0.25f).translate(0.25f, -0.25f);
    chain.add<Transform>("t_nval").input("noise_value").scale(0.25f).translate(0.75f, -0.25f);

    // Row 4: Ramp and solid (2) + padding
    chain.add<Transform>("t_ramp").input("ramp").scale(0.25f).translate(-0.5f, -0.75f);
    chain.add<Transform>("t_solid").input("solid").scale(0.25f).translate(0.5f, -0.75f);

    // Background
    chain.add<SolidColor>("bg").color(0.1f, 0.1f, 0.12f, 1.0f);

    // Composite all
    chain.add<Composite>("final")
        .input(0, "bg")
        .input(1, "t_circle")
        .input(2, "t_rect")
        .input(3, "t_triangle")
        .input(4, "t_star")
        .input(5, "t_glin")
        .input(6, "t_grad")
        .input(7, "t_gang")
        .mode(Composite::Mode::Over);

    // Add more with a second composite (max 8 inputs)
    chain.add<Composite>("final2")
        .input(0, "final")
        .input(1, "t_gdia")
        .input(2, "t_nper")
        .input(3, "t_nsim")
        .input(4, "t_nwor")
        .input(5, "t_nval")
        .input(6, "t_ramp")
        .input(7, "t_solid")
        .mode(Composite::Mode::Over);

    chain.output("final2");
}

void update(Context& ctx) {
    // Animations driven by noise speed and ramp
}

VIVID_CHAIN(setup, update)
