// Vivid Hello Example
// A minimal chain that just prints to console

#include <iostream>

// Forward declarations (we don't include vivid.h as it references WebGPU)
namespace vivid {
    class Context;
}

void setup(vivid::Context& ctx) {
    std::cout << "Hello from chain.cpp setup!" << std::endl;
}

void update(vivid::Context& ctx) {
    // Called every frame - don't print here or it floods the console
}

// Export entry points
extern "C" {
    void vivid_setup(vivid::Context& ctx) { setup(ctx); }
    void vivid_update(vivid::Context& ctx) { update(ctx); }
}
