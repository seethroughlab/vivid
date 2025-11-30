// Video Playback Example
// Demonstrates the VideoFile operator with Chain API
// Press SPACE to switch between videos
// Press LEFT/RIGHT to seek
// Press P to pause/play

#include <vivid/vivid.h>
#include <filesystem>
#include <algorithm>
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
        std::cerr << "[VideoPlayback] Error scanning directory: " << e.what() << "\n";
    }
    return videos;
}

void setup(Chain& chain) {
    // Find all video files (relative to project folder)
    videoPaths = findVideoFiles("assets");

    if (videoPaths.empty()) {
        std::cerr << "[VideoPlayback] No video files found in assets folder\n";
        std::cerr << "[VideoPlayback] Place .mp4/.mov files in assets/\n";
        return;
    }

    std::cout << "[VideoPlayback] Found " << videoPaths.size() << " video(s):\n";
    for (const auto& path : videoPaths) {
        std::cout << "  - " << path << "\n";
    }

    // Create video player with first video
    chain.add<VideoFile>("video")
        .path(videoPaths[0])
        .loop(true)
        .speed(1.0f)
        .play();

    // Add some color enhancement
    chain.add<HSV>("color")
        .input("video")
        .saturation(1.1f)
        .brightness(1.0f);

    chain.setOutput("color");
}

void update(Chain& chain, Context& ctx) {
    if (videoPaths.empty()) return;

    // SPACE: Switch to next video
    if (ctx.wasKeyPressed(Key::Space)) {
        currentIndex = (currentIndex + 1) % videoPaths.size();
        chain.get<VideoFile>("video")
            .path(videoPaths[currentIndex])
            .play();
        std::cout << "[VideoPlayback] Now playing: " << videoPaths[currentIndex] << "\n";
    }

    // P: Toggle pause
    if (ctx.wasKeyPressed(Key::P)) {
        chain.get<VideoFile>("video").toggle();
    }

    // LEFT/RIGHT: Seek
    if (ctx.wasKeyPressed(Key::Left)) {
        chain.get<VideoFile>("video").seek(0.0f);  // Back to start
    }
    if (ctx.wasKeyPressed(Key::Right)) {
        chain.get<VideoFile>("video").seek(0.5f);  // Jump to middle
    }
}

VIVID_CHAIN(setup, update)
