// See SPEC.md for project description
#include <vivid/vivid.h>
using namespace vivid;

// === GLOBALS ===
// GOAL: Define scene objects here

void setup(Chain& chain) {
    chain.setOutput("out");
}

void update(Chain& chain, Context& ctx) {
    // === SETUP (first frame) ===
    // GOAL: Create meshes, materials, camera

    // === UPDATE ===
    // GOAL: Animation and interaction logic

    // === RENDER ===
    // GOAL: Render the scene
}

VIVID_CHAIN(setup, update)
