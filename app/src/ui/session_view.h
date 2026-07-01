#pragma once
#include "ui/layout.h"   // Rect
#include <string>
#include <vector>

namespace vivid { struct Window; struct CtxMenu; }
namespace vivid_poc { struct Session; }

namespace vivid::ui {
class Renderer2D;

// The Session view (transport, tracks×scenes clip grid, mixer), the bottom
// device dock, the clip-cell previews, and the small context menus. State comes
// from the Window (its metrics + selection, and the shared App behind win.app).
void draw_clip_preview(Renderer2D& ui, vivid_poc::Session* s, int t, int sc,
                       const Rect& b, float ar, float ag, float ab, bool on);
void draw_device_dock(Renderer2D& ui, const Window& w, double mx, double my);
void draw_ui(Renderer2D& ui, const Window& w, double beats, double mx, double my);
void draw_fx_menu(Renderer2D& ui, const CtxMenu& m);
void draw_track_menu(Renderer2D& ui, const CtxMenu& m);   // "+ Track" instrument picker (+ Audio track)
// File dropdown: 4 fixed rows (New/Open/Save/Save As) then recent projects.
constexpr int kFileMenuFixed = 4;
void draw_file_menu(Renderer2D& ui, const CtxMenu& m, const std::vector<std::string>& recent);
void draw_map_menu(Renderer2D& ui, const CtxMenu& m);
void draw_menu(Renderer2D& ui, const CtxMenu& m, const char* track);

// Which visuals source is under (mx,my): -1 = master, >=0 = track, -2 = none.
int meter_hit(int tracks, int scenes, double mx, double my);

}  // namespace vivid::ui
