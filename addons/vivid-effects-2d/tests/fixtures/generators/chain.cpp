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
    auto& circle = chain.add<Shape>("circle");
    circle.type(ShapeType::Circle).size(0.8f).color(1.0f, 0.4f, 0.2f, 1.0f);

    auto& rect = chain.add<Shape>("rect");
    rect.type(ShapeType::Rectangle).size(0.7f).color(0.2f, 0.8f, 0.4f, 1.0f);

    auto& triangle = chain.add<Shape>("triangle");
    triangle.type(ShapeType::Triangle).size(0.75f).color(0.3f, 0.5f, 1.0f, 1.0f);

    auto& star = chain.add<Shape>("star");
    star.type(ShapeType::Star).size(0.7f).sides(5).color(1.0f, 0.9f, 0.2f, 1.0f);

    // === GRADIENT TYPES ===
    auto& grad_linear = chain.add<Gradient>("grad_linear");
    grad_linear.mode(GradientMode::Linear)
        .colorA(1.0f, 0.0f, 0.0f, 1.0f)
        .colorB(0.0f, 0.0f, 1.0f, 1.0f)
        .angle(0.785f);  // 45 degrees

    auto& grad_radial = chain.add<Gradient>("grad_radial");
    grad_radial.mode(GradientMode::Radial)
        .colorA(1.0f, 1.0f, 0.0f, 1.0f)
        .colorB(0.5f, 0.0f, 0.5f, 1.0f);

    auto& grad_angular = chain.add<Gradient>("grad_angular");
    grad_angular.mode(GradientMode::Angular)
        .colorA(0.0f, 1.0f, 1.0f, 1.0f)
        .colorB(1.0f, 0.0f, 1.0f, 1.0f);

    auto& grad_diamond = chain.add<Gradient>("grad_diamond");
    grad_diamond.mode(GradientMode::Diamond)
        .colorA(1.0f, 0.5f, 0.0f, 1.0f)
        .colorB(0.0f, 0.5f, 1.0f, 1.0f);

    // === NOISE TYPES ===
    auto& noise_perlin = chain.add<Noise>("noise_perlin");
    noise_perlin.type(NoiseType::Perlin);
    noise_perlin.scale = 3.0f;
    noise_perlin.speed = 0.2f;

    auto& noise_simplex = chain.add<Noise>("noise_simplex");
    noise_simplex.type(NoiseType::Simplex);
    noise_simplex.scale = 3.0f;
    noise_simplex.speed = 0.2f;

    auto& noise_worley = chain.add<Noise>("noise_worley");
    noise_worley.type(NoiseType::Worley);
    noise_worley.scale = 3.0f;
    noise_worley.speed = 0.2f;

    auto& noise_value = chain.add<Noise>("noise_value");
    noise_value.type(NoiseType::Value);
    noise_value.scale = 3.0f;
    noise_value.speed = 0.2f;

    // === RAMP (Color Cycle) ===
    auto& ramp = chain.add<Ramp>("ramp");
    ramp.hueSpeed(0.3f);

    // === SOLID COLOR ===
    auto& solid = chain.add<SolidColor>("solid");
    solid.color(0.2f, 0.2f, 0.25f, 1.0f);

    // === Grid Layout ===
    // Row 1: Shapes (4)
    chain.add<Transform>("t_circle").input(&circle).scale(0.25f).translate(-0.75f, 0.75f);
    chain.add<Transform>("t_rect").input(&rect).scale(0.25f).translate(-0.25f, 0.75f);
    chain.add<Transform>("t_triangle").input(&triangle).scale(0.25f).translate(0.25f, 0.75f);
    chain.add<Transform>("t_star").input(&star).scale(0.25f).translate(0.75f, 0.75f);

    // Row 2: Gradients (4)
    chain.add<Transform>("t_glin").input(&grad_linear).scale(0.25f).translate(-0.75f, 0.25f);
    chain.add<Transform>("t_grad").input(&grad_radial).scale(0.25f).translate(-0.25f, 0.25f);
    chain.add<Transform>("t_gang").input(&grad_angular).scale(0.25f).translate(0.25f, 0.25f);
    chain.add<Transform>("t_gdia").input(&grad_diamond).scale(0.25f).translate(0.75f, 0.25f);

    // Row 3: Noise types (4)
    chain.add<Transform>("t_nper").input(&noise_perlin).scale(0.25f).translate(-0.75f, -0.25f);
    chain.add<Transform>("t_nsim").input(&noise_simplex).scale(0.25f).translate(-0.25f, -0.25f);
    chain.add<Transform>("t_nwor").input(&noise_worley).scale(0.25f).translate(0.25f, -0.25f);
    chain.add<Transform>("t_nval").input(&noise_value).scale(0.25f).translate(0.75f, -0.25f);

    // Row 4: Ramp and solid (2) + padding
    chain.add<Transform>("t_ramp").input(&ramp).scale(0.25f).translate(-0.5f, -0.75f);
    chain.add<Transform>("t_solid").input(&solid).scale(0.25f).translate(0.5f, -0.75f);

    // Background
    auto& bg = chain.add<SolidColor>("bg");
    bg.color(0.1f, 0.1f, 0.12f, 1.0f);

    // Composite all - get references to transforms
    auto& t_circle_ref = chain.get<Transform>("t_circle");
    auto& t_rect_ref = chain.get<Transform>("t_rect");
    auto& t_triangle_ref = chain.get<Transform>("t_triangle");
    auto& t_star_ref = chain.get<Transform>("t_star");
    auto& t_glin_ref = chain.get<Transform>("t_glin");
    auto& t_grad_ref = chain.get<Transform>("t_grad");
    auto& t_gang_ref = chain.get<Transform>("t_gang");

    chain.add<Composite>("final")
        .input(0, &bg)
        .input(1, &t_circle_ref)
        .input(2, &t_rect_ref)
        .input(3, &t_triangle_ref)
        .input(4, &t_star_ref)
        .input(5, &t_glin_ref)
        .input(6, &t_grad_ref)
        .input(7, &t_gang_ref)
        .mode(BlendMode::Over);

    // Add more with a second composite (max 8 inputs)
    auto& final_ref = chain.get<Composite>("final");
    auto& t_gdia_ref = chain.get<Transform>("t_gdia");
    auto& t_nper_ref = chain.get<Transform>("t_nper");
    auto& t_nsim_ref = chain.get<Transform>("t_nsim");
    auto& t_nwor_ref = chain.get<Transform>("t_nwor");
    auto& t_nval_ref = chain.get<Transform>("t_nval");
    auto& t_ramp_ref = chain.get<Transform>("t_ramp");
    auto& t_solid_ref = chain.get<Transform>("t_solid");

    chain.add<Composite>("final2")
        .input(0, &final_ref)
        .input(1, &t_gdia_ref)
        .input(2, &t_nper_ref)
        .input(3, &t_nsim_ref)
        .input(4, &t_nwor_ref)
        .input(5, &t_nval_ref)
        .input(6, &t_ramp_ref)
        .input(7, &t_solid_ref)
        .mode(BlendMode::Over);

    chain.output("final2");
}

void update(Context& ctx) {
    // Animations driven by noise speed and ramp
}

VIVID_CHAIN(setup, update)
