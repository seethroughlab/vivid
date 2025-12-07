// Video Demo - Vivid Example
// Demonstrates video playback using the vivid-video addon
// Press 1/2/3 to switch videos, SPACE to pause/play, R to restart

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/video/video.h>
#include <iostream>
#include <vector>
#include <string>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::video;

static int currentVideoIndex = 0;
static bool hsvEnabled = true;

static const std::vector<std::string> videos = {
    "assets/videos/hap-1080p-audio.mov",
    "assets/videos/h264-1080p.mp4",
    "assets/videos/mpeg2-1080p.ts"
};

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Video player as source
    auto& video = chain.add<VideoPlayer>("video");
    auto& hsv = chain.add<HSV>("hsv");

    // Load first video with looping
    video.file(videos[currentVideoIndex])
         .loop(true);

    // Subtle color adjustment
    hsv.input(&video)
       .saturation(1.1f)
       .value(1.0f);

    // Default to HSV output
    chain.output("hsv");

    std::cout << "[VideoDemo] Controls: 1/2/3=switch video, SPACE=pause/play, R=restart, H=toggle HSV" << std::endl;
    std::cout << "[VideoDemo] Videos:" << std::endl;
    std::cout << "  1: hap-1080p-audio.mov (HAP - GPU compressed)" << std::endl;
    std::cout << "  2: h264-1080p.mp4 (H.264)" << std::endl;
    std::cout << "  3: mpeg2-1080p.ts (MPEG2)" << std::endl;
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& video = chain.get<VideoPlayer>("video");
    auto& hsv = chain.get<HSV>("hsv");

    // Number keys - switch videos
    for (int i = 0; i < (int)videos.size(); i++) {
        if (ctx.key(GLFW_KEY_1 + i).pressed && i != currentVideoIndex) {
            currentVideoIndex = i;
            video.file(videos[currentVideoIndex]);
            std::cout << "[VideoDemo] Switched to: " << videos[currentVideoIndex] << std::endl;
        }
    }

    // Space bar - toggle pause/play
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        if (video.isPlaying()) {
            video.pause();
            std::cout << "[VideoDemo] Paused at " << video.currentTime() << "s" << std::endl;
        } else {
            video.play();
            std::cout << "[VideoDemo] Playing" << std::endl;
        }
    }

    // R key - restart
    if (ctx.key(GLFW_KEY_R).pressed) {
        video.restart();
        std::cout << "[VideoDemo] Restarted" << std::endl;
    }

    // H key - toggle HSV effect
    if (ctx.key(GLFW_KEY_H).pressed) {
        hsvEnabled = !hsvEnabled;
        if (hsvEnabled) {
            chain.output("hsv");
            std::cout << "[VideoDemo] HSV enabled" << std::endl;
        } else {
            chain.output("video");
            std::cout << "[VideoDemo] HSV disabled (direct video)" << std::endl;
        }
    }

    // Mouse X controls hue shift
    float hue = ctx.mouseNorm().x * 0.2f;
    hsv.hueShift(hue);
}

VIVID_CHAIN(setup, update)
