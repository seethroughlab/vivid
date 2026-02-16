// Testing Fixture: GUI Visual Regression
// Minimal static chain for devtools UI screenshot testing.
// speed=0 ensures deterministic output every frame.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& solid = chain.add<SolidColor>("bg");
    solid.color.set(0.15f, 0.15f, 0.2f, 1.0f);

    auto& noise = chain.add<Noise>("noise");
    noise.scale = 4.0f;
    noise.speed = 0.0f;  // Static — no animation, fully deterministic
    noise.octaves = 3;

    auto& comp = chain.add<Composite>("output");
    comp.inputA("bg");
    comp.inputB("noise");
    comp.mode = BlendMode::Add;

    chain.output("output");
}

void update(Context& ctx) { ctx.chain().process(ctx); }
VIVID_CHAIN(setup, update)
