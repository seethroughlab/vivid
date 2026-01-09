// Webcam Feedback with Time Echo - Vivid Example
// Demonstrates feedback trails + time-delayed echo overlay
//
// Core feedback creates spiraling trails, while FrameCache adds
// a ghostly time-echo that follows you with adjustable delay.

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/video/video.h>

using namespace vivid;
using namespace vivid::effects;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Live webcam input
    auto& cam = chain.add<vivid::video::Webcam>("cam");

    // Core feedback loop (instant, 1-frame delay)
    auto& feedback = chain.add<Feedback>("feedback");
    feedback.input("cam");
    feedback.decay = 0.96f;
    feedback.mix = 0.2f;
    feedback.zoom = 1.01f;
    feedback.rotate = 0.015f;

    // Cache the feedback output for time echo
    auto& cache = chain.add<FrameCache>("cache");
    cache.input("feedback");
    cache.frameCount = 45;  // 1.5 seconds of history

    // Uniform delay map
    auto& white = chain.add<SolidColor>("white");
    white.color.set(1.0f, 1.0f, 1.0f, 1.0f);

    // Get delayed echo from cache
    auto& echo = chain.add<TimeMachine>("echo");
    echo.cache(&cache);
    echo.displacementMap(&white);
    echo.depth = 1.0f;
    echo.offset = 0.5f;  // 50% into cache = ~22 frame delay

    // Blend feedback with its delayed echo
    auto& blend = chain.add<Composite>("blend");
    blend.inputA("feedback");  // Current feedback
    blend.inputB("echo");      // Delayed ghost
    blend.mode = BlendMode::Screen;  // Brightens overlaps
    blend.opacity = 0.4f;      // Subtle echo

    chain.output("blend");
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& feedback = chain.get<Feedback>("feedback");
    auto& echo = chain.get<TimeMachine>("echo");

    // Mouse X: rotation (-0.05 to 0.05)
    float rotation = (ctx.mouseNorm().x - 0.5f) * 0.1f;
    feedback.rotate = rotation;

    // Mouse Y: echo delay (0.1 to 0.9 = 5 to 40 frame delay)
    float delayAmount = 0.1f + ctx.mouseNorm().y * 0.8f;
    echo.offset = delayAmount;

    ctx.debug("rotation", rotation);
    ctx.debug("delay", delayAmount);
}

VIVID_CHAIN(setup, update)
