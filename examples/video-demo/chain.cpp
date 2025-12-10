// Video Demo - Vivid Example
// Demonstrates video playback with audio using the vivid-video addon
// Tests various video codecs: H.264, HEVC, ProRes, HAP, HAP-Q, HAP-Alpha, Motion JPEG
// Press number keys to switch videos, SPACE to pause/play, R to restart

#include <vivid/vivid.h>
#include <vivid/effects/effects.h>
#include <vivid/video/video.h>
#include <vivid/audio_output.h>
#include <iostream>
#include <vector>
#include <string>

using namespace vivid;
using namespace vivid::effects;
using namespace vivid::video;

static int currentVideoIndex = 0;
static bool hsvEnabled = true;

// Video codec description for display
struct VideoEntry {
    std::string path;
    std::string codec;
    std::string description;
};

static const std::vector<VideoEntry> videos = {
    {"assets/videos/sync-test-h264.mp4",       "H.264",      "A/V sync test - beep aligns with flash"},
    {"assets/videos/sync-test-hevc.mp4",       "HEVC/H.265", "High efficiency codec"},
    {"assets/videos/sync-test-prores.mov",     "ProRes",     "Apple professional codec"},
    {"assets/videos/sync-test-hap.mov",        "HAP",        "GPU-compressed DXT1"},
    {"assets/videos/sync-test-hapq.mov",       "HAP-Q",      "GPU-compressed, higher quality"},
    {"assets/videos/sync-test-hap-alpha.mov",  "HAP-Alpha",  "GPU-compressed with alpha"},
    {"assets/videos/sync-test-mjpeg.mov",      "MJPEG",      "Motion JPEG"},
    {"assets/videos/hap-1080p-audio.mov",      "HAP (long)", "617MB HAP with audio track"},
};

void printCurrentVideo() {
    const auto& v = videos[currentVideoIndex];
    std::cout << "\n========================================" << std::endl;
    std::cout << "NOW PLAYING [" << (currentVideoIndex + 1) << "/" << videos.size() << "]: " << v.codec << std::endl;
    std::cout << "File: " << v.path << std::endl;
    std::cout << "Info: " << v.description << std::endl;
    std::cout << "========================================\n" << std::endl;
}

void setup(Context& ctx) {
    auto& chain = ctx.chain();

    // Video player as source
    auto& video = chain.add<VideoPlayer>("video");
    auto& hsv = chain.add<HSV>("hsv");

    // Load first video with looping
    video.file(videos[currentVideoIndex].path)
         .loop(true);

    // Subtle color adjustment
    hsv.input(&video)
       .saturation(1.1f)
       .value(1.0f);

    // Set outputs
    chain.output("hsv");

    std::cout << "\n[VideoDemo] Codec Test Suite" << std::endl;
    std::cout << "Controls: 1-" << videos.size() << "=switch video, SPACE=pause/play, R=restart, H=toggle HSV\n" << std::endl;

    std::cout << "Available videos:" << std::endl;
    for (size_t i = 0; i < videos.size(); i++) {
        std::cout << "  " << (i + 1) << ": " << videos[i].codec << " - " << videos[i].description << std::endl;
    }

    printCurrentVideo();
}

void update(Context& ctx) {
    auto& chain = ctx.chain();
    auto& video = chain.get<VideoPlayer>("video");
    auto& hsv = chain.get<HSV>("hsv");

    // Number keys 1-9 - switch videos
    for (int i = 0; i < std::min((int)videos.size(), 9); i++) {
        if (ctx.key(GLFW_KEY_1 + i).pressed && i != currentVideoIndex) {
            currentVideoIndex = i;
            video.file(videos[currentVideoIndex].path);
            printCurrentVideo();
        }
    }

    // Left/Right arrows - prev/next video
    if (ctx.key(GLFW_KEY_LEFT).pressed && currentVideoIndex > 0) {
        currentVideoIndex--;
        video.file(videos[currentVideoIndex].path);
        printCurrentVideo();
    }
    if (ctx.key(GLFW_KEY_RIGHT).pressed && currentVideoIndex < (int)videos.size() - 1) {
        currentVideoIndex++;
        video.file(videos[currentVideoIndex].path);
        printCurrentVideo();
    }

    // Space bar - toggle pause/play
    if (ctx.key(GLFW_KEY_SPACE).pressed) {
        if (video.isPlaying()) {
            video.pause();
            std::cout << "[VideoDemo] PAUSED at " << video.currentTime() << "s" << std::endl;
        } else {
            video.play();
            std::cout << "[VideoDemo] PLAYING" << std::endl;
        }
    }

    // R key - restart
    if (ctx.key(GLFW_KEY_R).pressed) {
        video.restart();
        std::cout << "[VideoDemo] RESTARTED" << std::endl;
    }

    // H key - toggle HSV effect
    if (ctx.key(GLFW_KEY_H).pressed) {
        hsvEnabled = !hsvEnabled;
        if (hsvEnabled) {
            chain.output("hsv");
            std::cout << "[VideoDemo] HSV effect ON" << std::endl;
        } else {
            chain.output("video");
            std::cout << "[VideoDemo] HSV effect OFF (direct video)" << std::endl;
        }
    }

    // Mouse X controls hue shift when HSV is enabled
    if (hsvEnabled) {
        float hue = ctx.mouseNorm().x * 0.2f;
        hsv.hueShift(hue);
    }
}

VIVID_CHAIN(setup, update)
