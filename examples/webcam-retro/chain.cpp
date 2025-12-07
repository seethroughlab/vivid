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

static Chain* chain = nullptr;
static bool ditherEnabled = true;
static bool scanlinesEnabled = true;
static bool crtEnabled = true;
static int ditherPattern = 1;  // 0=Bayer2x2, 1=Bayer4x4, 2=Bayer8x8

void setup(Context& ctx) {
    delete chain;
    chain = new Chain();

    // Webcam input
    auto& webcam = chain->add<Webcam>("webcam");

    // Downsample for that chunky pixel look
    auto& downsample = chain->add<Downsample>("downsample");

    // Dither for limited color palette feel
    auto& dither = chain->add<Dither>("dither");

    // Scanlines for CRT monitor effect
    auto& scanlines = chain->add<Scanlines>("scanlines");

    // CRT curvature and vignette
    auto& crt = chain->add<CRTEffect>("crt");

    // Final output
    auto& output = chain->add<Output>("output");

    // Configure webcam - 720p is plenty for retro look
    webcam.resolution(1280, 720)
          .frameRate(30);

    // Downsample to chunky resolution (will be upscaled with nearest neighbor)
    downsample.input(&webcam)
              .resolution(320, 180);  // Very chunky!

    // Ordered dithering for that 8-bit palette feel
    dither.input(&downsample)
          .pattern(DitherPattern::Bayer4x4)
          .levels(8)
          .strength(0.8f);

    // CRT scanlines
    scanlines.input(&dither)
             .spacing(3)
             .thickness(0.4f)
             .intensity(0.3f);

    // CRT monitor effect
    crt.input(&scanlines)
       .curvature(0.15f)
       .vignette(0.3f);

    output.input(&crt);
    chain->setOutput("output");
    chain->init(ctx);

    if (chain->hasError()) {
        ctx.setError(chain->error());
    }

    std::cout << "\n[Webcam Retro] Controls:" << std::endl;
    std::cout << "  D = Toggle dither" << std::endl;
    std::cout << "  S = Toggle scanlines" << std::endl;
    std::cout << "  C = Toggle CRT effect" << std::endl;
    std::cout << "  1/2/3 = Dither pattern (Bayer 2x2/4x4/8x8)" << std::endl;
    std::cout << "  Mouse X = Downsample resolution" << std::endl;
    std::cout << "  Mouse Y = Dither levels\n" << std::endl;
}

void update(Context& ctx) {
    if (!chain) return;

    auto& downsample = chain->get<Downsample>("downsample");
    auto& dither = chain->get<Dither>("dither");
    auto& scanlines = chain->get<Scanlines>("scanlines");
    auto& crt = chain->get<CRTEffect>("crt");
    auto& output = chain->get<Output>("output");

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
        ditherPattern = 0;
        dither.pattern(DitherPattern::Bayer2x2);
        std::cout << "[Webcam Retro] Dither: Bayer 2x2" << std::endl;
    }
    if (ctx.key(GLFW_KEY_2).pressed) {
        ditherPattern = 1;
        dither.pattern(DitherPattern::Bayer4x4);
        std::cout << "[Webcam Retro] Dither: Bayer 4x4" << std::endl;
    }
    if (ctx.key(GLFW_KEY_3).pressed) {
        ditherPattern = 2;
        dither.pattern(DitherPattern::Bayer8x8);
        std::cout << "[Webcam Retro] Dither: Bayer 8x8" << std::endl;
    }

    // Mouse X controls downsample resolution (160-640 width)
    int resWidth = 160 + static_cast<int>(ctx.mouseNorm().x * 480);
    int resHeight = resWidth * 9 / 16;  // Maintain 16:9 aspect
    downsample.resolution(resWidth, resHeight);

    // Mouse Y controls dither levels (4-32)
    int levels = 4 + static_cast<int>((1.0f - ctx.mouseNorm().y) * 28);
    dither.levels(levels);

    // Rebuild chain based on enabled effects
    TextureOperator* lastOp = &downsample;

    if (ditherEnabled) {
        dither.input(lastOp);
        lastOp = &dither;
    }

    if (scanlinesEnabled) {
        scanlines.input(lastOp);
        lastOp = &scanlines;
    }

    if (crtEnabled) {
        crt.input(lastOp);
        lastOp = &crt;
    }

    output.input(lastOp);

    chain->process(ctx);
}

VIVID_CHAIN(setup, update)
