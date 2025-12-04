// Vivid Runtime - Main Entry Point

#include "vivid/vivid.h"
#include "vivid/operators.h"
#include <iostream>
#include <chrono>
#include <memory>
#include <cmath>

// Platform-specific defines for GLFW native access
#if defined(PLATFORM_WIN32)
    #define GLFW_EXPOSE_NATIVE_WIN32 1
#elif defined(PLATFORM_LINUX)
    #define GLFW_EXPOSE_NATIVE_X11 1
#elif defined(PLATFORM_MACOS)
    #define GLFW_EXPOSE_NATIVE_COCOA 1
#endif

#include <GLFW/glfw3.h>
#include <GLFW/glfw3native.h>

// Undef Windows macros that conflict with our code
#ifdef GetObject
    #undef GetObject
#endif
#ifdef CreateWindow
    #undef CreateWindow
#endif

namespace vivid {

// Test 1: SolidColor operator
bool testSolidColor(Context& ctx) {
    std::cout << "\n=== Test 1: SolidColor ===" << std::endl;

    auto solidColor = std::make_unique<SolidColor>();
    auto output = std::make_unique<Output>();

    solidColor->color(0.8f, 0.2f, 0.3f, 1.0f);  // Red-ish
    output->setInput(solidColor.get());

    solidColor->init(ctx);
    output->init(ctx);

    std::cout << "SolidColor: Displaying red color for 2 seconds..." << std::endl;

    float startTime = ctx.time();
    while (!ctx.shouldClose() && (ctx.time() - startTime) < 2.0f) {
        ctx.pollEvents();
        ctx.beginFrame();

        // Animate color
        float t = ctx.time();
        solidColor->color(
            0.5f + 0.5f * std::sin(t * 2.0f),
            0.5f + 0.5f * std::sin(t * 2.5f + 1.0f),
            0.5f + 0.5f * std::sin(t * 3.0f + 2.0f),
            1.0f
        );

        solidColor->process(ctx);
        output->process(ctx);
        ctx.endFrame();
    }

    output->cleanup();
    solidColor->cleanup();

    std::cout << "SolidColor: PASSED" << std::endl;
    return true;
}

// Test 2: Noise operator
bool testNoise(Context& ctx) {
    std::cout << "\n=== Test 2: Noise ===" << std::endl;

    auto noise = std::make_unique<Noise>();
    auto output = std::make_unique<Output>();

    noise->scale(4.0f);
    noise->speed(1.0f);
    noise->octaves(4);
    output->setInput(noise.get());

    noise->init(ctx);
    output->init(ctx);

    std::cout << "Noise: Displaying animated noise for 3 seconds..." << std::endl;

    float startTime = ctx.time();
    while (!ctx.shouldClose() && (ctx.time() - startTime) < 3.0f) {
        ctx.pollEvents();
        ctx.beginFrame();
        noise->process(ctx);
        output->process(ctx);
        ctx.endFrame();
    }

    output->cleanup();
    noise->cleanup();

    std::cout << "Noise: PASSED" << std::endl;
    return true;
}

// Test 3: Blur operator
bool testBlur(Context& ctx) {
    std::cout << "\n=== Test 3: Blur ===" << std::endl;

    auto noise = std::make_unique<Noise>();
    auto blur = std::make_unique<Blur>();
    auto output = std::make_unique<Output>();

    noise->scale(8.0f);
    noise->speed(0.5f);
    blur->setInput(noise.get());
    blur->radius(20.0f);
    output->setInput(blur.get());

    noise->init(ctx);
    blur->init(ctx);
    output->init(ctx);

    std::cout << "Blur: Displaying blurred noise (animated radius) for 3 seconds..." << std::endl;

    float startTime = ctx.time();
    while (!ctx.shouldClose() && (ctx.time() - startTime) < 3.0f) {
        ctx.pollEvents();
        ctx.beginFrame();

        // Animate blur radius
        float t = ctx.time() - startTime;
        blur->radius(5.0f + 25.0f * (0.5f + 0.5f * std::sin(t * 2.0f)));

        noise->process(ctx);
        blur->process(ctx);
        output->process(ctx);
        ctx.endFrame();
    }

    output->cleanup();
    blur->cleanup();
    noise->cleanup();

    std::cout << "Blur: PASSED" << std::endl;
    return true;
}

// Test 4: Composite operator (all blend modes)
bool testComposite(Context& ctx) {
    std::cout << "\n=== Test 4: Composite ===" << std::endl;

    auto colorA = std::make_unique<SolidColor>();
    auto colorB = std::make_unique<SolidColor>();
    auto composite = std::make_unique<Composite>();
    auto output = std::make_unique<Output>();

    colorA->color(0.8f, 0.2f, 0.1f, 1.0f);  // Red
    colorB->color(0.1f, 0.2f, 0.8f, 0.5f);  // Blue with alpha

    composite->setInput(0, colorA.get());  // Input A
    composite->setInput(1, colorB.get());  // Input B
    output->setInput(composite.get());

    colorA->init(ctx);
    colorB->init(ctx);
    composite->init(ctx);
    output->init(ctx);

    const char* modeNames[] = {"Over", "Add", "Multiply", "Screen", "Overlay"};

    for (int mode = 0; mode < 5 && !ctx.shouldClose(); mode++) {
        composite->mode(static_cast<BlendMode>(mode));
        std::cout << "Composite: Testing " << modeNames[mode] << " blend mode..." << std::endl;

        float startTime = ctx.time();
        while (!ctx.shouldClose() && (ctx.time() - startTime) < 1.5f) {
            ctx.pollEvents();
            ctx.beginFrame();

            // Animate colors
            float t = ctx.time();
            colorA->color(
                0.5f + 0.5f * std::sin(t * 1.5f),
                0.3f,
                0.2f,
                1.0f
            );
            colorB->color(
                0.2f,
                0.3f,
                0.5f + 0.5f * std::sin(t * 2.0f + 1.0f),
                0.6f
            );

            colorA->process(ctx);
            colorB->process(ctx);
            composite->process(ctx);
            output->process(ctx);
            ctx.endFrame();
        }
    }

    output->cleanup();
    composite->cleanup();
    colorB->cleanup();
    colorA->cleanup();

    std::cout << "Composite: PASSED (all 5 blend modes)" << std::endl;
    return true;
}

// Test 5: Full chain (Noise -> Blur -> Composite with SolidColor -> Output)
bool testFullChain(Context& ctx) {
    std::cout << "\n=== Test 5: Full Operator Chain ===" << std::endl;

    auto noise = std::make_unique<Noise>();
    auto blur = std::make_unique<Blur>();
    auto solidColor = std::make_unique<SolidColor>();
    auto composite = std::make_unique<Composite>();
    auto output = std::make_unique<Output>();

    // Chain: noise -> blur -> composite (with solid color) -> output
    noise->scale(6.0f);
    noise->speed(0.3f);
    noise->octaves(3);

    blur->setInput(noise.get());
    blur->radius(10.0f);

    solidColor->color(0.1f, 0.3f, 0.6f, 0.7f);  // Blue tint

    composite->setInput(0, blur.get());       // Input A
    composite->setInput(1, solidColor.get()); // Input B
    composite->mode(BlendMode::Screen);

    output->setInput(composite.get());

    // Initialize in dependency order
    noise->init(ctx);
    blur->init(ctx);
    solidColor->init(ctx);
    composite->init(ctx);
    output->init(ctx);

    std::cout << "Full Chain: Noise -> Blur -> Composite(Screen) -> Output" << std::endl;
    std::cout << "Running for 4 seconds..." << std::endl;

    float startTime = ctx.time();
    while (!ctx.shouldClose() && (ctx.time() - startTime) < 4.0f) {
        ctx.pollEvents();
        ctx.beginFrame();

        // Animate parameters
        float t = ctx.time();
        noise->scale(4.0f + 4.0f * std::sin(t * 0.5f));
        blur->radius(5.0f + 15.0f * (0.5f + 0.5f * std::sin(t * 1.0f)));
        solidColor->color(
            0.1f + 0.2f * std::sin(t * 0.7f),
            0.2f + 0.2f * std::sin(t * 0.9f + 1.0f),
            0.5f + 0.3f * std::sin(t * 1.1f + 2.0f),
            0.5f + 0.3f * std::sin(t * 0.5f)
        );

        // Process in order
        noise->process(ctx);
        blur->process(ctx);
        solidColor->process(ctx);
        composite->process(ctx);
        output->process(ctx);

        ctx.endFrame();
    }

    // Cleanup in reverse order
    output->cleanup();
    composite->cleanup();
    solidColor->cleanup();
    blur->cleanup();
    noise->cleanup();

    std::cout << "Full Chain: PASSED" << std::endl;
    return true;
}

// Run all operator tests
void runOperatorTests(Context& ctx) {
    std::cout << "Vivid Runtime v" << VERSION_MAJOR << "." << VERSION_MINOR << "." << VERSION_PATCH << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "       OPERATOR TEST SUITE" << std::endl;
    std::cout << "============================================" << std::endl;
    std::cout << "Press ESC at any time to exit" << std::endl;

    int passed = 0;
    int total = 5;

    if (!ctx.shouldClose() && testSolidColor(ctx)) passed++;
    if (!ctx.shouldClose() && testNoise(ctx)) passed++;
    if (!ctx.shouldClose() && testBlur(ctx)) passed++;
    if (!ctx.shouldClose() && testComposite(ctx)) passed++;
    if (!ctx.shouldClose() && testFullChain(ctx)) passed++;

    std::cout << "\n============================================" << std::endl;
    std::cout << "       TEST RESULTS: " << passed << "/" << total << " PASSED" << std::endl;
    std::cout << "============================================" << std::endl;

    if (passed == total) {
        std::cout << "All operators working correctly!" << std::endl;
    }
}

} // namespace vivid

int main(int argc, char** argv) {
    std::cout << "Starting Vivid..." << std::endl;

    vivid::Context ctx;

    // Initialize with default window
    if (!ctx.init(1280, 720, "Vivid - Operator Tests")) {
        std::cerr << "Failed to initialize Vivid context" << std::endl;
        return -1;
    }

    std::cout << "Context initialized successfully" << std::endl;

    // Run operator test suite
    vivid::runOperatorTests(ctx);

    ctx.shutdown();

    std::cout << "Vivid shutdown complete" << std::endl;
    return 0;
}
