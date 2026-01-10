/**
 * @file chain.cpp
 * @brief Blob detection and tracking example
 *
 * Demonstrates blob detection using webcam input.
 * Detects circular regions based on size, color, and shape criteria.
 *
 * Try pointing the camera at:
 * - Bright lights or LED indicators
 * - Colored balls or round objects
 * - Faces (with relaxed circularity)
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

    // Webcam input
    auto& cam = chain.add<Webcam>("cam");

    // Blob detection - finds circular regions
    auto& blobs = chain.add<opencv::BlobTrack>("blobs");
    blobs.input("cam");

    // Detection parameters - tune these for your use case:
    blobs.minArea = 200.0f;        // Minimum blob size (pixels^2)
    blobs.maxArea = 50000.0f;      // Maximum blob size
    blobs.minCircularity = 0.3f;   // How circular (0=any shape, 1=perfect circle)
    blobs.minConvexity = 0.5f;     // How convex (0=any, 1=fully convex)
    blobs.minInertia = 0.1f;       // Elongation filter

    // What to detect:
    blobs.detectBright = 1;        // Detect bright blobs (lights, white objects)
    blobs.detectDark = 1;          // Detect dark blobs (dark objects on light bg)
    blobs.threshold = 128.0f;      // Brightness threshold for detection

    chain.output("blobs");
}

void update(Context& ctx) {
    ctx.chain().process(ctx);
}

VIVID_CHAIN(setup, update)
