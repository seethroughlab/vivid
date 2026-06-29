#include "app/shell.h"

#include "gpu/video_player.h"

#include <cstdio>

// Definitions of the shell globals declared extern in app/shell.h.
int   g_win_w = 1280, g_win_h = 800;
int   g_fb_w  = 1280, g_fb_h  = 800;
float g_dpi   = 1.0f;
float g_split_x = 512.f;
float g_dock_h  = 210.f;
int   g_visual_source = 0;
bool  g_show_mappings = false;
vivid::VisualGraph* g_vgraph = nullptr;
VideoPlayer*        g_video  = nullptr;
std::vector<std::string> g_video_paths;
int   g_video_idx = -1;

void load_video_at(int i) {
    if (g_video_paths.empty()) return;
    const int n = static_cast<int>(g_video_paths.size());
    g_video_idx = ((i % n) + n) % n;
    if (g_video) { video_close(g_video); g_video = nullptr; }
    g_video = video_open(g_video_paths[g_video_idx].c_str());
    if (g_video) {
        video_play(g_video, g_visual_source == 1);
        std::fprintf(stderr, "[vivid] video [%d/%d]: %s\n", g_video_idx + 1, n, g_video_paths[g_video_idx].c_str());
    }
}
