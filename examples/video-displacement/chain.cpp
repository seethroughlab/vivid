// Video Displacement Example
// Demonstrates video playback with noise displacement
// Uses: VideoFile → Noise → Displacement pipeline

#include <vivid/vivid.h>
#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;
using namespace vivid;

// Track video files and current index
static std::vector<std::string> videoPaths;
static size_t currentIndex = 0;

std::vector<std::string> findVideoFiles(const std::string& directory) {
    std::vector<std::string> videos;
    static const std::vector<std::string> videoExtensions = {
        ".mp4", ".mov", ".m4v", ".avi", ".mkv", ".webm", ".MP4", ".MOV"
    };

    try {
        for (const auto& entry : fs::recursive_directory_iterator(directory)) {
            if (!entry.is_regular_file()) continue;
            std::string ext = entry.path().extension().string();
            for (const auto& videoExt : videoExtensions) {
                if (ext == videoExt) {
                    videos.push_back(entry.path().string());
                    break;
                }
            }
        }
        std::sort(videos.begin(), videos.end());
    } catch (const std::exception& e) {
        std::cerr << "[VideoDisplacement] Error scanning directory: " << e.what() << "\n";
    }
    return videos;
}

void setup(Chain& chain) {
    // Find all video files (try current assets, then video-playback assets)
    videoPaths = findVideoFiles("assets");
    if (videoPaths.empty()) {
        videoPaths = findVideoFiles("../video-playback/assets");
    }

    if (videoPaths.empty()) {
        std::cerr << "[VideoDisplacement] No video files found!\n";
        std::cerr << "[VideoDisplacement] Place .mp4/.mov files in assets/\n";
        return;
    }

    std::cout << "[VideoDisplacement] Found " << videoPaths.size() << " video(s):\n";
    for (const auto& path : videoPaths) {
        std::cout << "  - " << path << "\n";
    }

    // Video player
    chain.add<VideoFile>("video")
        .path(videoPaths[0])
        .loop(true)
        .play();

    // Noise for displacement map
    chain.add<Noise>("noise")
        .scale(3.0f)
        .speed(0.3f)
        .octaves(2);

    // Apply displacement to video
    chain.add<Displacement>("displaced")
        .input("video")
        .map("noise")
        .amount(0.04f);

    chain.setOutput("displaced");
}

void update(Chain& chain, Context& ctx) {
    if (videoPaths.empty()) return;

    // SPACE: Switch to next video
    if (ctx.wasKeyPressed(Key::Space)) {
        currentIndex = (currentIndex + 1) % videoPaths.size();
        chain.get<VideoFile>("video")
            .path(videoPaths[currentIndex])
            .play();
        std::cout << "[VideoDisplacement] Now playing: " << videoPaths[currentIndex] << "\n";
    }

    // P: Toggle pause
    if (ctx.wasKeyPressed(Key::P)) {
        chain.get<VideoFile>("video").toggle();
    }

    // UP/DOWN: Adjust displacement amount
    static float amount = 0.04f;
    if (ctx.isKeyDown(Key::Up)) {
        amount = std::min(amount + 0.001f, 0.2f);
        chain.get<Displacement>("displaced").amount(amount);
    }
    if (ctx.isKeyDown(Key::Down)) {
        amount = std::max(amount - 0.001f, 0.0f);
        chain.get<Displacement>("displaced").amount(amount);
    }
}

VIVID_CHAIN(setup, update)
