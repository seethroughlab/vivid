#pragma once
#include <string>
#include <vector>
#include "ui/layout.h"   // vivid::ui::Rect / DockGeom + window-relative geometry

namespace vivid { class VisualGraph; }
struct VideoPlayer;

// Shell state shared by main, the frame loop, and the input handlers: live window
// metrics, the DAW|visuals splitter + device-dock heights, the visuals generator
// source, the mapping-overview toggle, and the video player + playlist.
//
// These are extern globals — an intermediate step in the main.cpp decomposition;
// Stage F folds them into an `App` struct. (UI/main thread only.)
extern int   g_win_w, g_win_h;   // logical (point) size — all UI layout
extern int   g_fb_w,  g_fb_h;    // framebuffer (physical) size — the surface
extern float g_dpi;              // g_fb_w / g_win_w (2.0 on retina)
extern float g_split_x;          // DAW|visuals splitter x
extern float g_dock_h;           // bottom device-view dock height
extern int   g_visual_source;    // 0 = plasma shader, 1 = texture (image/video)
extern bool  g_show_mappings;    // P28: the mapping-overview overlay (toggle: M)
extern vivid::VisualGraph* g_vgraph;          // source of truth for the generator
extern VideoPlayer*        g_video;           // current video clip (or null)
extern std::vector<std::string> g_video_paths;// the scanned playlist
extern int   g_video_idx;        // index into g_video_paths

// Open clip i (wraps) into g_video and start it if the source is video.
void load_video_at(int i);

// Window-relative geometry bound to the live shell globals, so call sites stay
// parameterless. (DockGeom, dock_knob, dock_knob_map need no window state and are
// used directly from ui/layout.h.)
inline float dock_top()                  { return vivid::ui::dock_top(g_win_h, g_dock_h); }
inline vivid::ui::Rect viewer_rect()     { return vivid::ui::viewer_rect(g_win_w, g_split_x); }
inline vivid::ui::Rect splitter_rect()   { return vivid::ui::splitter_rect(g_win_h, g_dock_h, g_split_x); }
inline vivid::ui::Rect dock_resize_rect(){ return vivid::ui::dock_resize_rect(g_win_w, g_win_h, g_dock_h); }
inline vivid::ui::DockGeom dock_geom()   { return vivid::ui::dock_geom(g_win_w, g_win_h, g_dock_h); }
inline vivid::ui::Rect dock_chip(int i)  { return vivid::ui::dock_chip(i, g_win_h, g_dock_h); }
inline vivid::ui::Rect dock_chip_x(int i){ return vivid::ui::dock_chip_x(i, g_win_h, g_dock_h); }
