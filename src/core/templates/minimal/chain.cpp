#include <vivid/vivid.h>

using namespace vivid;

void setup(Context& ctx) {
    auto& chain = ctx.chain();
    // Add operators here
}

void update(Context& ctx) {
    // Update parameters here
}

VIVID_CHAIN(setup, update)
