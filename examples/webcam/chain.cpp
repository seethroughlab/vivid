// Webcam Glitch Example
// Demonstrates live camera capture with glitch effects using Chain API:
// Webcam -> ChromaticAberration -> Pixelate -> Scanlines

#include <vivid/vivid.h>
#include <iostream>

using namespace vivid;

void setup(Chain& chain) {
    // Input: Live webcam feed
    chain.add<Webcam>("webcam")
        .resolution(1280, 720)
        .frameRate(30.0f);

    // Effect 1: Chromatic Aberration - RGB channel separation
    chain.add<ChromaticAberration>("chroma")
        .input("webcam")
        .amount(0.012f)
        .mode(1);  // radial mode

    // Effect 2: Pixelate - subtle blockiness for retro feel
    chain.add<Pixelate>("pixel")
        .input("chroma")
        .size(3.0f);

    // Effect 3: Scanlines - CRT monitor effect
    chain.add<Scanlines>("scanlines")
        .input("pixel")
        .density(400.0f)
        .intensity(0.25f)
        .mode(2);  // RGB sub-pixel mode

    chain.setOutput("scanlines");
}

void update(Chain& chain, Context& ctx) {
    // Uncomment to print available camera modes:
    // static bool once = false;
    // if (!once) { ctx.printCameraModes(); once = true; }

    // Rotate chromatic aberration angle for dynamic effect
    chain.get<ChromaticAberration>("chroma").angle(ctx.time() * 0.3f);

    // Slow scroll the scanlines
    chain.get<Scanlines>("scanlines").scrollSpeed(ctx.time() * 20.0f);

    // Mouse X controls chromatic aberration amount
    float chroma = ctx.mouseNormX() * 0.03f;
    chain.get<ChromaticAberration>("chroma").amount(chroma);

    // Mouse Y controls pixel size
    float pixelSize = 1.0f + ctx.mouseNormY() * 8.0f;
    chain.get<Pixelate>("pixel").size(pixelSize);
}

VIVID_CHAIN(setup, update)
