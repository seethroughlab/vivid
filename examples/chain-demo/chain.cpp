// Chain Demo - Vivid V3 Example
// Demonstrates the Chain API with multiple operators and dependencies

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

// Chain object (persistent across hot-reloads)
static Chain* chain = nullptr;

void setup(Context& ctx) {
    // Create chain
    chain = new Chain();

    // Add operators with names
    auto& noise1 = chain->add<Noise>("noise1");
    auto& noise2 = chain->add<Noise>("noise2");
    auto& color = chain->add<SolidColor>("color");
    auto& comp1 = chain->add<Composite>("comp1");
    auto& comp2 = chain->add<Composite>("comp2");
    auto& output = chain->add<Output>("output");

    // Configure operators
    noise1.scale(4.0f).speed(0.3f).octaves(4);
    noise2.scale(8.0f).speed(0.5f).octaves(2).offset(100.0f, 0.0f);
    color.color(0.2f, 0.5f, 0.8f, 1.0f);  // Blue tint

    // Build dependency graph:
    // noise1 + noise2 -> comp1 (multiply blend)
    // comp1 + color -> comp2 (overlay blend)
    // comp2 -> output
    comp1.inputA(&noise1).inputB(&noise2).mode(BlendMode::Multiply);
    comp2.inputA(&comp1).inputB(&color).mode(BlendMode::Overlay).opacity(0.5f);
    output.input(&comp2);

    // Set output operator
    chain->setOutput("output");

    // Initialize chain (computes execution order)
    chain->init(ctx);

    if (chain->hasError()) {
        ctx.setError(chain->error());
    }
}

void update(Context& ctx) {
    if (!chain) return;

    // You can access operators by name for live tweaking
    // Example: modify noise scale based on mouse position
    auto& noise1 = chain->get<Noise>("noise1");
    float scale = 2.0f + ctx.mouseNorm().x * 4.0f;  // 2-6 based on mouse X
    noise1.scale(scale);

    // Process entire chain
    chain->process(ctx);
}

// Export entry points
extern "C" {
    void vivid_setup(Context& ctx) {
        // Clean up previous chain if hot-reloading
        delete chain;
        chain = nullptr;

        setup(ctx);
    }

    void vivid_update(Context& ctx) {
        update(ctx);
    }
}
