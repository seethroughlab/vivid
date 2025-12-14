// Math and Logic Operators Test
// Tests: LFO, Math, Logic operators controlling visual parameters

#include <vivid/vivid.h>
#include <vivid/effects/noise.h>
#include <vivid/effects/hsv.h>
#include <vivid/effects/lfo.h>
#include <vivid/effects/math_op.h>
#include <vivid/effects/logic_op.h>
#include <vivid/effects/blur.h>
#include <vivid/effects/brightness.h>
#include <vivid/effects/shape.h>
#include <vivid/effects/composite.h>

using namespace vivid;
using namespace vivid::effects;

// Pointers for update access
LFO* lfoSlow = nullptr;
LFO* lfoFast = nullptr;
Math* mathRemap = nullptr;
Math* mathAbs = nullptr;
Logic* logicPositive = nullptr;
HSV* noiseColor = nullptr;
Blur* blur = nullptr;
Shape* shape = nullptr;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // LFO oscillators at different rates
    lfoSlow = &chain.add<LFO>("lfo_slow");
    lfoSlow->frequency = 0.2f;
    lfoSlow->waveform(LFOWaveform::Sine);

    lfoFast = &chain.add<LFO>("lfo_fast");
    lfoFast->frequency = 1.5f;
    lfoFast->waveform(LFOWaveform::Triangle);

    // Math: Remap slow LFO from [-1,1] to [0,1]
    mathRemap = &chain.add<Math>("math_remap");
    mathRemap->operation(MathOperation::Remap);
    mathRemap->inMin = -1.0f;
    mathRemap->inMax = 1.0f;
    mathRemap->outMin = 0.0f;
    mathRemap->outMax = 1.0f;

    // Math: Absolute value of fast LFO
    mathAbs = &chain.add<Math>("math_abs");
    mathAbs->operation(MathOperation::Abs);

    // Logic: Check if slow LFO > 0
    logicPositive = &chain.add<Logic>("logic_positive");
    logicPositive->operation(LogicOperation::GreaterThan);
    logicPositive->inputB = 0.0f;

    // Visual elements modulated by value operators

    // Background noise
    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;
    noise.speed = 0.5f;

    // Color modulated by remapped LFO
    noiseColor = &chain.add<HSV>("noise_color");
    noiseColor->input(&noise);
    noiseColor->saturation = 0.8f;

    // Blur amount controlled by absolute LFO
    blur = &chain.add<Blur>("blur");
    blur->input(noiseColor);

    // Brightness
    auto& brightness = chain.add<Brightness>("brightness");
    brightness.input(blur);
    brightness.brightness = 0.1f;
    brightness.contrast = 1.2f;

    // Pulsing shape based on logic result
    shape = &chain.add<Shape>("shape");
    shape->type(ShapeType::Circle);
    shape->size.set(0.2f, 0.2f);
    shape->color.set(1.0f, 0.5f, 0.2f, 1.0f);

    // Final composite
    auto& final = chain.add<Composite>("final");
    final.input(0, &brightness);
    final.input(1, shape);
    final.mode(BlendMode::Add);

    chain.output("final");
}

void update(Context& ctx) {
    // Update math inputs from LFO outputs
    mathRemap->inputA = lfoSlow->outputValue();
    mathAbs->inputA = lfoFast->outputValue();
    logicPositive->inputA = lfoSlow->outputValue();

    // Use math/logic results to modulate visuals
    noiseColor->hueShift = mathRemap->outputValue();  // Hue cycles 0-1
    blur->radius = mathAbs->outputValue() * 15.0f;  // Blur 0-15
    float s = logicPositive->result() ? 0.3f : 0.1f;
    shape->size.set(s, s);  // Size toggles
}

VIVID_CHAIN(setup, update)
