/**
 * Conditional Routing Example
 *
 * Demonstrates: Switch, Logic, Math
 *
 * Shows how to use conditional operators to route textures
 * based on logic comparisons and math transformations.
 */

#include <vivid/vivid.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Create three different visual options
    chain.add<Noise>("option_a")
        .scale(4.0f)
        .speed(0.5f);

    chain.add<Gradient>("option_b")
        .direction(GradientDirection::Radial);

    chain.add<Shape>("option_c")
        .type(ShapeType::Circle)
        .radius(0.3f)
        .feather(0.1f);

    // Math: Remap time to sawtooth wave (0-1 over 3 seconds)
    chain.add<Math>("time_remap")
        .operation(MathOperation::Fract);

    // Math: Scale remapped time to 0-3 range for index selection
    chain.add<Math>("index_calc")
        .operation(MathOperation::Multiply);

    // Logic: Check if we're in first third (index 0)
    chain.add<Logic>("check_a")
        .operation(LogicOperation::LessThan);

    // Logic: Check if we're in second third (index 1)
    chain.add<Logic>("check_b")
        .operation(LogicOperation::InRange);

    // Switch: Select between the three options
    chain.add<Switch>("selector");
    auto& sw = chain.get<Switch>("selector");
    sw.input(0, "option_a");
    sw.input(1, "option_b");
    sw.input(2, "option_c");
    sw.blend = 0.2f;  // Soft crossfade between options

    chain.output("selector");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float t = ctx.time();

    // Calculate which option to show based on time
    // Cycles through 0, 1, 2 every 3 seconds
    auto& timeRemap = chain.get<Math>("time_remap");
    timeRemap.inputA = t / 3.0f;  // 3-second cycle

    auto& indexCalc = chain.get<Math>("index_calc");
    indexCalc.inputA = timeRemap.value();
    indexCalc.inputB = 3.0f;  // Scale to 0-3

    // Use Logic to demonstrate comparison operations
    auto& checkA = chain.get<Logic>("check_a");
    checkA.inputA = indexCalc.value();
    checkA.inputB = 1.0f;

    auto& checkB = chain.get<Logic>("check_b");
    checkB.inputA = indexCalc.value();
    checkB.rangeMin = 1.0f;
    checkB.rangeMax = 2.0f;

    // Set switch index based on calculated value
    auto& sw = chain.get<Switch>("selector");
    sw.index = static_cast<int>(indexCalc.value());

    // Animate individual options
    chain.get<Noise>("option_a").offset.set(t * 0.1f, 0.0f);
    chain.get<Shape>("option_c").position.set(
        0.5f + 0.2f * std::sin(t * 2.0f),
        0.5f + 0.2f * std::cos(t * 2.0f)
    );

    chain.process();
}

VIVID_CHAIN(setup, update)
