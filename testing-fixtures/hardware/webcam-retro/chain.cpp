// Webcam Retro - Vivid Example
// Live webcam with retro post-processing effects
// Controls: D=toggle dither, S=toggle scanlines, C=toggle CRT, 1-3=dither patterns

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/video/video.h>
#include <iostream>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::video;

static bool ditherEnabled = true;
static bool scanlinesEnabled = true;
static bool crtEnabled = true;

// Pointers for update access
Webcam* webcam = nullptr;
Downsample* downsample = nullptr;
Dither* dither = nullptr;
Scanlines* scanlines = nullptr;
CRTEffect* crt = nullptr;

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Webcam input
    webcam = &chain.add<Webcam>("webcam");
    webcam->resolution(1280, 720).frameRate(30);

    // Downsample for that chunky pixel look
    downsample = &chain.add<Downsample>("downsample");
    downsample->input(webcam);
    downsample->targetW = 320;
    downsample->targetH = 180;

    // Dither for limited color palette feel
    dither = &chain.add<Dither>("dither");
    dither->input(downsample);
    dither->pattern(DitherPattern::Bayer4x4);
    dither->levels = 8;
    dither->strength = 0.8f;

    // Scanlines for CRT monitor effect
    scanlines = &chain.add<Scanlines>("scanlines");
    scanlines->input(dither);
    scanlines->spacing = 3;
    scanlines->thickness = 0.4f;
    scanlines->intensity = 0.3f;

    // CRT curvature and vignette
    crt = &chain.add<CRTEffect>("crt");
    crt->input(scanlines);
    crt->curvature = 0.15f;
    crt->vignette = 0.3f;

    chain.output("crt");

    std::cout << "\n[Webcam Retro] Controls:" << std::endl;
    std::cout << "  D = Toggle dither" << std::endl;
    std::cout << "  S = Toggle scanlines" << std::endl;
    std::cout << "  C = Toggle CRT effect" << std::endl;
    std::cout << "  1/2/3 = Dither pattern (Bayer 2x2/4x4/8x8)" << std::endl;
    std::cout << "  Mouse X = Downsample resolution" << std::endl;
    std::cout << "  Mouse Y = Dither levels\n" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();

    // D key - toggle dither
    if (ctx.key(GLFW_KEY_D).pressed) {
        ditherEnabled = !ditherEnabled;
        std::cout << "[Webcam Retro] Dither: " << (ditherEnabled ? "ON" : "OFF") << std::endl;
    }

    // S key - toggle scanlines
    if (ctx.key(GLFW_KEY_S).pressed) {
        scanlinesEnabled = !scanlinesEnabled;
        std::cout << "[Webcam Retro] Scanlines: " << (scanlinesEnabled ? "ON" : "OFF") << std::endl;
    }

    // C key - toggle CRT
    if (ctx.key(GLFW_KEY_C).pressed) {
        crtEnabled = !crtEnabled;
        std::cout << "[Webcam Retro] CRT: " << (crtEnabled ? "ON" : "OFF") << std::endl;
    }

    // Number keys - dither pattern
    if (ctx.key(GLFW_KEY_1).pressed) {
        dither->pattern(DitherPattern::Bayer2x2);
        std::cout << "[Webcam Retro] Dither: Bayer 2x2" << std::endl;
    }
    if (ctx.key(GLFW_KEY_2).pressed) {
        dither->pattern(DitherPattern::Bayer4x4);
        std::cout << "[Webcam Retro] Dither: Bayer 4x4" << std::endl;
    }
    if (ctx.key(GLFW_KEY_3).pressed) {
        dither->pattern(DitherPattern::Bayer8x8);
        std::cout << "[Webcam Retro] Dither: Bayer 8x8" << std::endl;
    }

    // Mouse X controls downsample resolution (160-640 width)
    int resWidth = 160 + static_cast<int>(ctx.mouseNorm().x * 480);
    int resHeight = resWidth * 9 / 16;  // Maintain 16:9 aspect
    downsample->targetW = resWidth;
    downsample->targetH = resHeight;

    // Mouse Y controls dither levels (4-32)
    int levels = 4 + static_cast<int>((1.0f - ctx.mouseNorm().y) * 28);
    dither->levels = levels;

    // Rebuild chain based on enabled effects
    TextureOperator* lastOp = downsample;

    if (ditherEnabled) {
        dither->input(lastOp);
        lastOp = dither;
    }

    if (scanlinesEnabled) {
        scanlines->input(lastOp);
        lastOp = scanlines;
    }

    if (crtEnabled) {
        crt->input(lastOp);
        lastOp = crt;
    }

    // Update output based on what's enabled
    if (crtEnabled) {
        chain.output("crt");
    } else if (scanlinesEnabled) {
        chain.output("scanlines");
    } else if (ditherEnabled) {
        chain.output("dither");
    } else {
        chain.output("downsample");
    }
}

VIVID_CHAIN(setup, update)
