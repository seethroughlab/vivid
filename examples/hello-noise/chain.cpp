// Vivid Example: Hello Noise
// A simple example demonstrating hot reload
// Edit this file while vivid is running to see live updates!

#include <vivid/vivid.h>
#include <iostream>

using namespace vivid;

// Called once when the chain is loaded (or reloaded)
void setup(Context& ctx) {
    std::cout << "[Chain] Setup called - chain loaded!" << std::endl;
}

// Called every frame
void update(Context& ctx) {
    // Simple frame counter - try changing this message!
    static int frameCount = 0;
    if (frameCount % 60 == 0) {
        std::cout << "[Chain] Frame " << frameCount << " - time: " << ctx.time() << "s" << std::endl;
    }
    frameCount++;
}

// Export entry points for hot reload
VIVID_CHAIN(setup, update)
