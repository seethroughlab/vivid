#include "app/app.h"

#include "gpu/video_player.h"

#include <cstdio>

namespace vivid {

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
