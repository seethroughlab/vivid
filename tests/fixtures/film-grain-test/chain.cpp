// FilmGrain test - verify grain overlay effect
#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Base image - gradient to show grain clearly
    auto& gradient = chain.add<Gradient>("gradient");
    gradient.colorA.set(Color::fromHex("#1a1a2e"));
    gradient.colorB.set(Color::fromHex("#4a4a6a"));
    gradient.angle = 45.0f;

    // Film grain overlay
    auto& grain = chain.add<FilmGrain>("grain");
    grain.input("gradient");
    grain.intensity = 0.25f;  // Noticeable grain
    grain.size = 1.5f;        // Medium grain size
    grain.speed = 24.0f;      // Film-like frame rate
    grain.colored = 0.2f;     // Slight color variation

    chain.output("grain");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    float time = ctx.time();

    // Animate intensity for testing
    chain.get<FilmGrain>("grain").intensity = 0.15f + 0.1f * std::sin(time * 0.5f);

    chain.process(ctx);
}

VIVID_CHAIN(setup, update)
