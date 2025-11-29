// Multi-Composite Demo
// Demonstrates compositing multiple layers with a single Composite operator
//
// This example creates 4 animated circles (using Shape operators) and
// composites them all together with ONE Composite operator call.

#include <vivid/vivid.h>
#include <cmath>

using namespace vivid;

// Pointers to operators for animation updates
static Shape* circle1_ = nullptr;
static Shape* circle2_ = nullptr;
static Shape* circle3_ = nullptr;
static Shape* circle4_ = nullptr;

void setup(Chain& chain) {
    // Create 4 circle shape operators
    circle1_ = &chain.add<Shape>("circle1");
    circle2_ = &chain.add<Shape>("circle2");
    circle3_ = &chain.add<Shape>("circle3");
    circle4_ = &chain.add<Shape>("circle4");

    // Single composite operator that takes all 4 inputs
    chain.add<Composite>("comp")
         .inputs({"circle1", "circle2", "circle3", "circle4"})
         .opacity(1.0f);

    chain.setOutput("comp");
}

void update(Chain& chain, Context& ctx) {
    float t = ctx.time();

    // Animate circles in different patterns
    float r1 = 0.3f + 0.1f * std::sin(t * 2.0f);
    float r2 = 0.2f + 0.05f * std::sin(t * 3.0f);
    float r3 = 0.15f + 0.05f * std::cos(t * 2.5f);
    float r4 = 0.1f + 0.03f * std::sin(t * 4.0f);

    // Circular motion for each
    float angle1 = t * 0.5f;
    float angle2 = t * 0.7f + 1.0f;
    float angle3 = t * 0.9f + 2.0f;
    float angle4 = t * 1.1f + 3.0f;

    float cx1 = 0.5f + 0.2f * std::cos(angle1);
    float cy1 = 0.5f + 0.2f * std::sin(angle1);
    float cx2 = 0.5f + 0.15f * std::cos(angle2);
    float cy2 = 0.5f + 0.15f * std::sin(angle2);
    float cx3 = 0.5f + 0.25f * std::cos(angle3);
    float cy3 = 0.5f + 0.25f * std::sin(angle3);
    float cx4 = 0.5f + 0.1f * std::cos(angle4);
    float cy4 = 0.5f + 0.1f * std::sin(angle4);

    // Configure circles with different colors
    circle1_->type(Shape::Circle).center({cx1, cy1}).radius(r1)
             .color({1.0f, 0.3f, 0.3f}).softness(0.01f);

    circle2_->type(Shape::Circle).center({cx2, cy2}).radius(r2)
             .color({0.3f, 1.0f, 0.3f}).softness(0.01f);

    circle3_->type(Shape::Circle).center({cx3, cy3}).radius(r3)
             .color({0.3f, 0.3f, 1.0f}).softness(0.01f);

    circle4_->type(Shape::Circle).center({cx4, cy4}).radius(r4)
             .color({1.0f, 1.0f, 0.3f}).softness(0.01f);
}

VIVID_CHAIN(setup, update)
