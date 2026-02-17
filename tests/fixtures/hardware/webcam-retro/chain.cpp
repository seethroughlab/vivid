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

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Webcam input
    auto& webcam = chain.add<Webcam>("webcam");
    webcam.setResolution(1280, 720);
    webcam.setFrameRate(30.0f);

    // Downsample for that chunky pixel look
    auto& downsample = chain.add<Downsample>("downsample");
    downsample.input("webcam");
    downsample.targetW = 320;
    downsample.targetH = 180;

    // Dither for limited color palette feel
    auto& dither = chain.add<Dither>("dither");
    dither.input("downsample");
    dither.pattern(DitherPattern::Bayer4x4);
    dither.levels = 8;
    dither.strength = 0.8f;

    // Scanlines for CRT monitor effect
    auto& scanlines = chain.add<Scanlines>("scanlines");
    scanlines.input("dither");
    scanlines.spacing = 3;
    scanlines.thickness = 0.4f;
    scanlines.intensity = 0.3f;

    // CRT curvature and vignette
    auto& crt = chain.add<CRTEffect>("crt");
    crt.input("scanlines");
    crt.curvature = 0.15f;
    crt.vignette = 0.3f;

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

    // Get operators by name
    auto& downsample = chain.get<Downsample>("downsample");
    auto& dither = chain.get<Dither>("dither");
    auto& scanlines = chain.get<Scanlines>("scanlines");
    auto& crt = chain.get<CRTEffect>("crt");

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
        dither.pattern(DitherPattern::Bayer2x2);
        std::cout << "[Webcam Retro] Dither: Bayer 2x2" << std::endl;
    }
    if (ctx.key(GLFW_KEY_2).pressed) {
        dither.pattern(DitherPattern::Bayer4x4);
        std::cout << "[Webcam Retro] Dither: Bayer 4x4" << std::endl;
    }
    if (ctx.key(GLFW_KEY_3).pressed) {
        dither.pattern(DitherPattern::Bayer8x8);
        std::cout << "[Webcam Retro] Dither: Bayer 8x8" << std::endl;
    }

    // Mouse X controls downsample resolution (160-640 width)
    int resWidth = 160 + static_cast<int>(ctx.mouseNorm().x * 480);
    int resHeight = resWidth * 9 / 16;  // Maintain 16:9 aspect
    downsample.targetW = resWidth;
    downsample.targetH = resHeight;

    // Mouse Y controls dither levels (4-32)
    int levels = 4 + static_cast<int>((1.0f - ctx.mouseNorm().y) * 28);
    dither.levels = levels;

    // Rebuild chain based on enabled effects
    std::string lastOutput = "downsample";

    if (ditherEnabled) {
        dither.input(lastOutput);
        lastOutput = "dither";
    }

    if (scanlinesEnabled) {
        scanlines.input(lastOutput);
        lastOutput = "scanlines";
    }

    if (crtEnabled) {
        crt.input(lastOutput);
        lastOutput = "crt";
    }

    chain.output(lastOutput);
}

VIVID_CHAIN(setup, update)
