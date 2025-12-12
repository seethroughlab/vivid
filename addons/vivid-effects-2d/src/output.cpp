// Vivid Effects 2D - Output Operator Implementation

#include <vivid/effects/output.h>
#include <vivid/context.h>

namespace vivid::effects {

void Output::process(Context& ctx) {
    checkResize(ctx);

    // Get the input texture view
    WGPUTextureView view = inputView(0);

    // Register with context for display
    if (view) {
        ctx.setOutputTexture(view);
    }

    didCook();
}

} // namespace vivid::effects
