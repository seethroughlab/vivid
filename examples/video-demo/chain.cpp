// Video Demo Example
// Demonstrates video playback using the vivid-video addon
// Press SPACE to cycle through videos, R to restart current video

#include <vivid/vivid.h>
#include <vivid/video/video.h>
#include <iostream>
#include <vector>
#include <string>
#include <filesystem>

using namespace vivid;
using namespace vivid::video;
namespace fs = std::filesystem;

// Video display (handles both playback and rendering)
static std::unique_ptr<VideoDisplay> videoDisplay;
static std::vector<std::string> videoFiles;
static int currentVideoIndex = 0;
static bool initialized = false;

// Find all video files in a directory
std::vector<std::string> findVideoFiles(const std::string& directory) {
    std::vector<std::string> files;

    if (!fs::exists(directory)) {
        return files;
    }

    for (const auto& entry : fs::directory_iterator(directory)) {
        if (!entry.is_regular_file()) continue;

        std::string ext = entry.path().extension().string();
        // Convert to lowercase for comparison
        for (auto& c : ext) c = std::tolower(c);

        if (ext == ".mp4" || ext == ".mov" || ext == ".mkv" ||
            ext == ".avi" || ext == ".m4v" || ext == ".webm" || ext == ".ts") {
            files.push_back(entry.path().string());
        }
    }

    // Sort for consistent ordering
    std::sort(files.begin(), files.end());
    return files;
}

void loadVideo(Context& ctx, int index) {
    if (videoFiles.empty()) return;

    currentVideoIndex = index % videoFiles.size();
    const std::string& path = videoFiles[currentVideoIndex];

    std::cout << "\n[VideoDemo] Loading video " << (currentVideoIndex + 1)
              << "/" << videoFiles.size() << std::endl;

    videoDisplay->close();

    if (videoDisplay->open(ctx, path, true)) {  // Loop enabled
        auto& player = videoDisplay->player();
        std::cout << "[VideoDemo] File: " << fs::path(path).filename().string() << std::endl;
        std::cout << "[VideoDemo] Size: " << player.width() << "x" << player.height() << std::endl;
        std::cout << "[VideoDemo] Duration: " << player.duration() << "s @ "
                  << player.frameRate() << " fps" << std::endl;
    } else {
        std::cerr << "[VideoDemo] Failed to open: " << path << std::endl;
    }
}

void setup(Context& ctx) {
    std::cout << "[VideoDemo] Scanning for video files..." << std::endl;

    videoDisplay = std::make_unique<VideoDisplay>();

    // Find all video files in assets directory
    videoFiles = findVideoFiles("examples/video-demo/assets");

    if (videoFiles.empty()) {
        std::cerr << "[VideoDemo] No video files found in examples/video-demo/assets/" << std::endl;
        std::cerr << "[VideoDemo] Supported formats: .mp4, .mov, .mkv, .avi, .m4v, .webm" << std::endl;
        initialized = false;
        return;
    }

    std::cout << "[VideoDemo] Found " << videoFiles.size() << " video(s)" << std::endl;
    std::cout << "[VideoDemo] Controls: SPACE=next video, R=restart" << std::endl;

    // Load first video
    loadVideo(ctx, 0);
    initialized = true;
}

void update(Context& ctx) {
    if (!initialized || videoFiles.empty()) {
        return;
    }

    // Update video (decode and render)
    videoDisplay->update(ctx);

    // Print status occasionally
    static int frameCount = 0;
    if (++frameCount % 180 == 0) {
        std::cout << "[VideoDemo] " << fs::path(videoFiles[currentVideoIndex]).filename().string()
                  << " - " << videoDisplay->currentTime() << "s / "
                  << videoDisplay->duration() << "s" << std::endl;
    }

    // Space bar - next video
    if (ctx.wasKeyPressed(32)) {
        loadVideo(ctx, currentVideoIndex + 1);
    }

    // R key - restart current video
    if (ctx.wasKeyPressed(82)) {
        videoDisplay->seek(0.0f);
        std::cout << "[VideoDemo] Restarted" << std::endl;
    }
}

VIVID_CHAIN(setup, update)
