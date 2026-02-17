// Lesson 09: Custom Operators
// Understanding how to extend Vivid with your own operators
//
// Run: ./build/bin/vivid projects/getting-started/09-custom-operators
//
// This lesson demonstrates the PATTERN for custom operators.
// For full custom operators, see docs/CREATING-OPERATORS.md

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

// ============================================================================
// CUSTOM OPERATOR PATTERN
// ============================================================================
//
// A custom operator looks like this:
//
// class MyEffect : public TextureOperator {
// public:
//     // Parameters - appear as sliders in visualizer
//     Param<float> intensity{"intensity", 1.0f, 0.0f, 2.0f};
//
//     MyEffect() {
//         registerParam(intensity);
//     }
//
//     void init(Context& ctx) override {
//         // Called once - create GPU resources (textures, pipelines)
//     }
//
//     void process(Context& ctx) override {
//         // Called every frame - run your shader
//         if (!needsCook()) return;
//         ctx.runShader("shaders/my_effect.wgsl", input_, output_);
//         didCook();
//     }
//
//     void cleanup() override {
//         // Release GPU resources
//     }
//
//     std::string name() const override { return "MyEffect"; }
// };
//
// ============================================================================

// For this demo, we'll use built-in operators to achieve a "custom" effect.
// This pattern shows how you'd structure the logic before moving to a real
// custom operator.

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Generate source content
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 8.0f;
    noise.speed = 0.3f;
    noise.octaves = 4;

    // =========================================
    // "Custom Effect" using built-ins
    // =========================================
    // This achieves a stylized threshold + glow effect
    // In a real custom operator, this would be a single shader

    // Step 1: Threshold (would be part of custom shader)
    auto& threshold = chain.add<Threshold>("threshold");
    threshold.input("noise");
    threshold.threshold = 0.5f;    // Cutoff point
    threshold.softness = 0.1f; // Edge softness

    // Step 2: Color it (would be in custom shader)
    auto& grad = chain.add<Gradient>("grad");
    grad.colorA.set(0.0f, 0.0f, 0.1f, 1.0f);   // Dark blue
    grad.colorB.set(0.2f, 0.8f, 1.0f, 1.0f);   // Cyan

    auto& colorize = chain.add<Lookup>("colorize");
    colorize.input("threshold");
    colorize.lut("grad");

    // Step 3: Add glow (would be in custom shader)
    auto& bloom = chain.add<Bloom>("bloom");
    bloom.input("colorize");
    bloom.threshold = 0.3f;
    bloom.intensity = 0.5f;
    bloom.radius = 15.0f;

    chain.output("bloom");

    // =========================================
    // If this were a REAL custom operator:
    // =========================================
    //
    // 1. Create header: include/my_module/stylized_threshold.h
    //    - Define class with Param<float> for level, softness, colors
    //
    // 2. Create implementation: src/stylized_threshold.cpp
    //    - init(): Create pipeline from shader
    //    - process(): Run shader with uniform parameters
    //    - cleanup(): Release pipeline
    //
    // 3. Create shader: shaders/stylized_threshold.wgsl
    //    - Sample input texture
    //    - Apply threshold with softness
    //    - Apply color gradient
    //    - Add bloom in single pass (more efficient!)
    //
    // See docs/CREATING-OPERATORS.md for complete examples
}

void update(Context& ctx) {
    // Animate the threshold to show the effect
    auto& threshold = ctx.chain().get<Threshold>("threshold");
    float t = ctx.time();
    threshold.threshold = 0.4f + sin(t * 0.5f) * 0.2f;
}

VIVID_CHAIN(setup, update)
