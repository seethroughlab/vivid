#pragma once
#include <algorithm>
#include <string>

// Shared UI geometry + layout constants for the PoC shell: the session grid
// (tracks × scenes + mixer), the visuals viewer/splitter, and the bottom device
// dock. Window-relative helpers take explicit size/split/dock args (no globals)
// so they're reusable across the draw + input + frame modules. Pure + header-only.
namespace vivid::ui {

struct Rect { float x, y, w, h; };
inline bool hit(const Rect& r, double mx, double my) {
    return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
}

// --- Session grid: columns = tracks, rows = scenes; a mixer strip below. ---
constexpr float kSceneColX = 14.f, kSceneColW = 58.f;
constexpr float kTrackX0 = 78.f, kTrackW = 102.f, kTrackGap = 4.f;
constexpr float kHeaderY = 56.f, kHeaderH = 30.f;
constexpr float kGridTopY = 92.f;
constexpr float kRowH = 56.f, kRowGap = 4.f;
inline float track_x(int t) { return kTrackX0 + t * (kTrackW + kTrackGap); }
inline Rect clip_cell_rect(int track, int scene) { return { track_x(track), kGridTopY + scene * (kRowH + kRowGap), kTrackW, kRowH }; }
// Top transport bar affordances (header y=0..40): a play/pause button after the beat dots.
inline Rect transport_play_rect() { return { 452.f, 11.f, 18.f, 18.f }; }
inline Rect track_header_rect(int t) { return { track_x(t), kHeaderY, kTrackW, kHeaderH }; }
inline Rect track_add_rect(int tracks) { return { track_x(tracks), kHeaderY, kTrackW, kHeaderH }; }  // "+ Track" header
inline Rect track_header_x_rect(int t) { return { track_x(t) + kTrackW - 15.f, kHeaderY + 3.f, 12.f, 12.f }; }  // remove ×
inline Rect scene_launch_rect(int scene) { return { kSceneColX, kGridTopY + scene * (kRowH + kRowGap), kSceneColW, kRowH }; }
inline float mixer_y(int scenes) { return kGridTopY + scenes * (kRowH + kRowGap) + 18.f; }
inline Rect track_meter_rect(int t, int scenes) { return { track_x(t) + 8.f, mixer_y(scenes) + 16.f, kTrackW - 16.f, 8.f }; }
inline Rect track_gain_rect(int t, int scenes)  { return { track_x(t) + 8.f, mixer_y(scenes) + 30.f, kTrackW - 16.f, 12.f }; }
inline Rect master_meter_rect(int scenes) { return { kSceneColX, mixer_y(scenes) + 16.f, kSceneColW, 26.f }; }
// Explicit "send this source to the visuals graph" buttons (the bridge entry point).
inline Rect track_viz_rect(int t, int scenes)  { return { track_x(t) + 8.f, mixer_y(scenes) + 48.f, kTrackW - 16.f, 18.f }; }
inline Rect master_viz_rect(int scenes) { return { kSceneColX, mixer_y(scenes) + 48.f, kSceneColW, 18.f }; }

// Sources offered when mapping an audio param (the return path): audio characteristics + visuals state.
struct MapSrc { const char* label; const char* id; };
constexpr MapSrc kMapSources[] = {
    { "Master Level", "master.level" }, { "Master Transient", "master.transient" },
    { "Master Low", "master.low" }, { "Master Mid", "master.mid" }, { "Master High", "master.high" },
    { "Viz Warp", "viz.warp" }, { "Viz Glow", "viz.glow" }, { "Viz Feedback", "viz.feedback" },
    { "\xE2\x80\x94 clear \xE2\x80\x94", "" } };
constexpr int kNumMapSources = 9;
inline std::string param_dest(int track, int device, int i) {
    return "param:" + std::to_string(track) + ":" + std::to_string(device) + ":" + std::to_string(i);
}
inline void track_accent(int t, float& r, float& g, float& b) {
    static const float P[3][3] = { {0.94f,0.63f,0.19f}, {0.88f,0.39f,0.23f}, {0.35f,0.66f,0.90f} };
    r = P[t%3][0]; g = P[t%3][1]; b = P[t%3][2];
}
// Right/left-click the MASTER meter opens a menu of audio characteristics (the bridge).
struct CharItem { const char* label; int id; };
constexpr CharItem kChars[] = {
    { "Level (RMS)", 0 }, { "Transient", 1 }, { "Low band", 2 }, { "Mid band", 3 }, { "High band", 4 } };
constexpr int kNumChars = 5;
// Characteristic id encoding: master uses kind (0..4); track t uses 100 + t*8 + kind.
inline int char_id_for(int src, int kind) { return src < 0 ? kind : 100 + src * 8 + kind; }

// Visuals FBO internal resolution (fixed; the on-screen viewer scales to it).
constexpr float kViewW = 720.f, kViewH = 300.f;

// --- Window-relative geometry (explicit args; no globals). ---
inline float dock_top(int win_h, float dock_h) { return win_h - dock_h; }   // y where the dock begins
inline Rect viewer_rect(int win_w, float split_x) { return { split_x + 8.f, 100.f, static_cast<float>(win_w) - split_x - 16.f, 330.f }; }
inline Rect splitter_rect(int win_h, float dock_h, float split_x) { return { split_x - 3.f, 44.f, 6.f, dock_top(win_h, dock_h) - 44.f }; }
inline Rect dock_resize_rect(int win_w, int win_h, float dock_h) { return { 0.f, dock_top(win_h, dock_h) - 3.f, static_cast<float>(win_w), 7.f }; }

// Bottom device-view dock: device chips (instrument + FX + "+FX") + a knob grid
// of the selected device's params. Geometry shared by draw + hit-test.
struct DockGeom { float y0, gridY0, cellW, cellH, knobOff; int cols, maxRows; };
inline DockGeom dock_geom(int win_w, int win_h, float dock_h) {
    DockGeom d; d.y0 = dock_top(win_h, dock_h);
    d.gridY0 = d.y0 + 62.f; d.cellW = 64.f; d.cellH = 58.f; d.knobOff = 20.f;
    d.cols = std::max(1, static_cast<int>((win_w - 24.f) / d.cellW));
    d.maxRows = std::max(1, static_cast<int>((d.y0 + dock_h - 6.f - d.gridY0) / d.cellH));
    return d;
}
inline Rect dock_chip(int i, int win_h, float dock_h)   { return { 12.f + i * 128.f, dock_top(win_h, dock_h) + 22.f, 120.f, 32.f }; }
inline Rect dock_chip_x(int i, int win_h, float dock_h) { Rect b = dock_chip(i, win_h, dock_h); return { b.x + b.w - 16.f, b.y + 3.f, 13.f, 13.f }; }
inline void dock_knob(int i, const DockGeom& d, float& cx, float& cy) {
    cx = 12.f + (i % d.cols) * d.cellW + d.cellW * 0.5f;
    cy = d.gridY0 + (i / d.cols) * d.cellH + d.knobOff;
}
inline Rect dock_knob_map(int i, const DockGeom& d) { float cx, cy; dock_knob(i, d, cx, cy); return { cx + 13.f, cy - 24.f, 9.f, 9.f }; }

}  // namespace vivid::ui
