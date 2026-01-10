// GUI Widgets Example
// Demonstrates the immediate-mode GUI widget system
//
// NOTE: This example shows the API but GUI rendering requires
// integration with the WebGPU render pass. The GUI system is
// currently used internally by the chain visualizer.
// User-facing GUI support is planned for a future release.

#include <vivid/vivid.h>

using namespace vivid;

// This example demonstrates a noise effect that would be
// controllable via GUI widgets in the future.

static float scale = 2.0f;
static float speed = 0.5f;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    auto& noise = chain.add<Noise>("noise");
    noise.scale = scale;
    noise.speed = speed;
    noise.octaves = 4;

    chain.setOutput(noise);
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // Future: GUI controls would go here
    // Gui gui(canvas, input);
    // gui.beginPanel("Controls", 10, 10, 200, 200);
    // gui.slider("Scale", &scale, 0.5f, 10.0f);
    // gui.slider("Speed", &speed, 0.0f, 2.0f);
    // gui.endPanel();

    // Update noise parameters (currently from code)
    if (auto* noise = chain.get<Noise>("noise")) {
        noise->scale = scale;
        noise->speed = speed;
    }

    chain.process();
}

// Good size for UI demonstrations
static vivid::ChainConfig config{
    .windowWidth = 1024,
    .windowHeight = 768,
    .resizable = true
};
VIVID_CHAIN_CONFIG(setup, update, config)
