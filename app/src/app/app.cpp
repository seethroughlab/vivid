#include "app/app.h"

#include "gpu/video_player.h"

#include <cstdio>
#include <filesystem>

namespace vivid {

void App::remember_project_path(const std::string& path) {
    project.remember_project_path(path);
}

void App::set_media_root(const std::string& root) {
    namespace fs = std::filesystem;
    project.media_root = root;
    video_paths.clear();
    project.missing_media.clear();
    video_idx = -1;
    if (video) { video_close(video); video = nullptr; }
    if (root.empty()) return;
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        project.missing_media.push_back(root);
        std::fprintf(stderr, "[vivid] media root unavailable: %s\n", root.c_str());
        return;
    }
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        const std::string ext = entry.path().extension().string();
        if (ext == ".mp4" || ext == ".mov" || ext == ".m4v")
            video_paths.push_back(entry.path().string());
    }
    std::sort(video_paths.begin(), video_paths.end());
    if (!video_paths.empty()) load_video_at(0);
    else project.missing_media.push_back(root);
    std::fprintf(stderr, "[vivid] media root %s: %zu video clips found\n", root.c_str(), video_paths.size());
}

void App::load_video_at(int i) {
    if (video_paths.empty()) return;
    const int n = static_cast<int>(video_paths.size());
    video_idx = ((i % n) + n) % n;
    if (video) { video_close(video); video = nullptr; }
    video = video_open(video_paths[video_idx].c_str());
    if (video) {
        video_play(video, visual_source == 1);
        std::fprintf(stderr, "[vivid] video [%d/%d]: %s\n", video_idx + 1, n, video_paths[video_idx].c_str());
    }
}

}  // namespace vivid
