// Shapes Example
// Demonstrates SDF-based shape rendering
//
// This example shows:
// - Rendering SDF shapes (circle, rectangle, triangle, ring, star)
// - Animating shape parameters
// - Compositing multiple shapes with blending

#include <vivid/vivid.h>
#include <vivid/operators.h>
#include <cmath>
#include <iostream>

using namespace vivid;

// Operators
static std::unique_ptr<Shape> ring;
static std::unique_ptr<Shape> star;
static std::unique_ptr<Shape> circle;
static std::unique_ptr<Composite> comp1;
static std::unique_ptr<Composite> comp2;
static std::unique_ptr<Output> output;

static bool initialized = false;

void setup(Context& ctx) {
    std::cout << "[Shapes] Setting up SDF shape demo..." << std::endl;

    // Create operators
    ring = std::make_unique<Shape>();
    star = std::make_unique<Shape>();
    circle = std::make_unique<Shape>();
    comp1 = std::make_unique<Composite>();
    comp2 = std::make_unique<Composite>();
    output = std::make_unique<Output>();

    // Initialize all
    ring->init(ctx);
    star->init(ctx);
    circle->init(ctx);
    comp1->init(ctx);
    comp2->init(ctx);
    output->init(ctx);

    // Configure ring (animated)
    ring->type(ShapeType::Ring)
        .center(0.5f, 0.5f)
        .radius(0.25f)
        .innerRadius(0.18f)
        .softness(0.01f)
        .color(glm::vec3(0.2f, 0.8f, 1.0f))     // Cyan
        .backgroundColor(glm::vec4(0.0f, 0.0f, 0.05f, 1.0f)); // Dark blue bg

    // Configure star (animated, counter-rotating)
    star->type(ShapeType::Star)
        .center(0.5f, 0.5f)
        .radius(0.18f)
        .points(5)
        .softness(0.008f)
        .color(glm::vec3(1.0f, 0.8f, 0.2f))     // Gold
        .backgroundColor(glm::vec4(0.0f));       // Transparent

    // Configure small pulsing circle in center
    circle->type(ShapeType::Circle)
        .center(0.5f, 0.5f)
        .radius(0.05f)
        .softness(0.02f)
        .color(glm::vec3(1.0f, 0.4f, 0.8f))     // Pink
        .backgroundColor(glm::vec4(0.0f));       // Transparent

    // Connect operators: ring + star -> comp1, comp1 + circle -> comp2 -> output
    comp1->setInput(0, ring.get());
    comp1->setInput(1, star.get());
    comp1->mode(BlendMode::Add).opacity(1.0f);

    comp2->setInput(0, comp1.get());
    comp2->setInput(1, circle.get());
    comp2->mode(BlendMode::Add).opacity(1.0f);

    output->setInput(comp2.get());

    initialized = true;
    std::cout << "[Shapes] Demo initialized!" << std::endl;
    std::cout << "  - Ring (cyan, rotating)" << std::endl;
    std::cout << "  - Star (gold, counter-rotating)" << std::endl;
    std::cout << "  - Circle (pink, pulsing)" << std::endl;
}

void update(Context& ctx) {
    if (!initialized) return;

    float time = ctx.time();

    // Animate parameters
    float pulse = std::sin(time * 2.0f) * 0.5f + 0.5f;
    float rotation = time * 0.5f;

    // Update ring - pulsing size and rotation
    ring->radius(0.22f + pulse * 0.08f)
        .innerRadius(0.16f + pulse * 0.04f)
        .rotation(rotation);

    // Update star - counter-rotating
    star->rotation(-rotation * 1.5f);

    // Update circle - pulsing
    circle->radius(0.03f + pulse * 0.04f);

    // Process chain
    ring->process(ctx);
    star->process(ctx);
    circle->process(ctx);
    comp1->process(ctx);
    comp2->process(ctx);
    output->process(ctx);
}

VIVID_CHAIN(setup, update)
