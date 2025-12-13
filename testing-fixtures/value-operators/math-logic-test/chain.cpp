// Math and Logic Operators Test
// Tests: LFO, Math, Logic operators controlling visual parameters

#include <vivid/vivid.h>
#include <vivid/effects/noise.h>
#include <vivid/effects/hsv.h>
#include <vivid/effects/lfo.h>
#include <vivid/effects/math.h>
#include <vivid/effects/logic.h>
#include <vivid/effects/blur.h>
#include <vivid/effects/brightness.h>
#include <vivid/effects/shape.h>
#include <vivid/effects/composite.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // LFO oscillators at different rates
    chain.add<LFO>("lfo_slow")
        .frequency(0.2f)
        .waveform(LFO::Waveform::Sine);

    chain.add<LFO>("lfo_fast")
        .frequency(1.5f)
        .waveform(LFO::Waveform::Triangle);

    chain.add<LFO>("lfo_square")
        .frequency(0.5f)
        .waveform(LFO::Waveform::Square);

    // Math operations on LFO values
    // Remap slow LFO from [-1,1] to [0,1]
    chain.add<Math>("math_remap")
        .inputA("lfo_slow")
        .operation(Math::Operation::Remap)
        .param1(-1.0f)   // input min
        .param2(1.0f)    // input max
        .param3(0.0f)    // output min
        .param4(1.0f);   // output max

    // Absolute value of fast LFO
    chain.add<Math>("math_abs")
        .inputA("lfo_fast")
        .operation(Math::Operation::Abs);

    // Multiply two LFOs
    chain.add<Math>("math_multiply")
        .inputA("lfo_slow")
        .inputB("lfo_fast")
        .operation(Math::Operation::Multiply);

    // Logic operations
    // Check if slow LFO > 0
    chain.add<Logic>("logic_positive")
        .inputA("lfo_slow")
        .operation(Logic::Operation::GreaterThan)
        .threshold(0.0f);

    // Check if fast LFO is in range [-0.5, 0.5]
    chain.add<Logic>("logic_range")
        .inputA("lfo_fast")
        .operation(Logic::Operation::InRange)
        .rangeMin(-0.5f)
        .rangeMax(0.5f);

    // Visual elements modulated by value operators

    // Background noise modulated by slow LFO
    chain.add<Noise>("noise")
        .scale(4.0f)
        .speed(0.5f);

    chain.add<HSV>("noise_color")
        .input("noise")
        .hueInput("math_remap")  // Color cycles with LFO
        .saturation(0.8f);

    // Blur amount controlled by absolute LFO
    chain.add<Blur>("blur")
        .input("noise_color")
        .radiusInput("math_abs")
        .radiusScale(20.0f);  // Scale abs value to reasonable blur

    // Brightness pulses with multiplied LFOs
    chain.add<Brightness>("brightness")
        .input("blur")
        .brightnessInput("math_multiply")
        .contrastInput("math_abs");

    // Pulsing shape based on logic
    chain.add<Shape>("shape")
        .type(Shape::Type::Circle)
        .sizeInput("logic_positive")  // Size toggles with positive check
        .sizeScale(0.3f)
        .color(1.0f, 0.5f, 0.2f, 1.0f);

    // Final composite
    chain.add<Composite>("final")
        .input(0, "brightness")
        .input(1, "shape")
        .mode(Composite::Mode::Add);

    chain.output("final");
}

void update(Context& ctx) {
    // All animation driven by LFOs
}

VIVID_CHAIN(setup, update)
