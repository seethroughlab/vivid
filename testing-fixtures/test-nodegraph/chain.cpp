/**
 * Test fixture for the new NodeGraph + OverlayCanvas system
 *
 * This tests the custom node graph visualization that replaces imnodes.
 * Features tested:
 * - OverlayCanvas rendering
 * - NodeGraph node/pin/link rendering
 * - Zoom (scroll wheel)
 * - Pan (Ctrl+drag)
 * - Node selection
 */

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    Chain& c = ctx.chain();

    auto& noise = c.add<Noise>("noise");
    noise.scale = 2.0f;
    noise.speed = 0.3f;

    auto& blur = c.add<Blur>("blur");
    blur.setInput(&noise);
    blur.radius = 5.0f;

    c.output("blur");
}

void update(Context& ctx) {
    ctx.chain().process(ctx);

    // The NodeGraph test will be rendered by modifying cli/app.cpp
    // For now, this fixture just provides a simple background chain
}

VIVID_CHAIN(setup, update)
