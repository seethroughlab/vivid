// Hello Noise - Vivid V3 Example
// Demonstrates the basic Noise → Output chain

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>

using namespace vivid;
using namespace vivid::effects;

// Operators (persistent across hot-reloads)
static Noise* noise = nullptr;
static Output* output = nullptr;

void setup(Context& ctx) {
    // Create operators
    noise = new Noise();
    output = new Output();

    // Configure noise
    noise->scale(4.0f)
         .speed(0.5f)
         .octaves(4)
         .lacunarity(2.0f)
         .persistence(0.5f);

    // Connect chain: Noise → Output
    output->input(noise);
}

void update(Context& ctx) {
    // Process the chain
    noise->process(ctx);
    output->process(ctx);
}

// Export entry points
extern "C" {
    void vivid_setup(Context& ctx) {
        // Clean up previous operators if hot-reloading
        delete noise;
        delete output;
        noise = nullptr;
        output = nullptr;

        setup(ctx);
    }

    void vivid_update(Context& ctx) {
        update(ctx);
    }
}
