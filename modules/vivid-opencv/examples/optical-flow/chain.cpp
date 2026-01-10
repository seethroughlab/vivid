/**
 * @file chain.cpp
 * @brief Optical flow motion detection example
 *
 * Demonstrates dense optical flow using webcam input.
 * Shows motion vectors between consecutive frames.
 */

#include <vivid/vivid.h>
#include <vivid/video/video.h>
#include <vivid/opencv/opencv.h>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::video;

void setup(Context& ctx) {
    auto& chain = ctx.chain();
    chain.setResolution(1280, 720);

    // Webcam input (provides CPU pixels for OpenCV)
    auto& cam = chain.add<Webcam>("cam");

    // Optical flow - detects motion between frames
    auto& flow = chain.add<opencv::OpticalFlow>("flow");
    flow.input("cam");
    flow.sensitivity = 2.0f;    // Amplify motion visualization
    flow.vizMode = 0;           // 0=HSV color wheel, 1=arrows, 2=magnitude

    chain.output("flow");
}

void update(Context& ctx) {
    ctx.chain().process(ctx);
}

VIVID_CHAIN(setup, update)
