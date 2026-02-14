// Testing Fixture: Broken Chain
// Intentionally broken chain for testing vivid build error reporting.
// This file should NOT compile - it references a non-existent member.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();
    auto& noise = chain.add<Noise>("noise");
    noise.undeclared_member = 1.0f;  // Deliberate error: no such member
    chain.output("noise");
}

void update(Context& ctx) {}

VIVID_CHAIN(setup, update)
