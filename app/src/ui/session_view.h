#pragma once
#include "ui/layout.h"   // Rect

struct AudioState;
struct CtxMenu;
namespace vivid_poc { struct Session; }

namespace vivid::ui {
class Renderer2D;

// The Session view (transport, tracks×scenes clip grid, mixer), the bottom
// device dock, the clip-cell previews, and the small context menus. Window dims
// are passed explicitly (no globals) so these are reusable / testable.
void draw_clip_preview(Renderer2D& ui, vivid_poc::Session* s, int t, int sc,
                       const Rect& b, float ar, float ag, float ab, bool on);
void draw_device_dock(Renderer2D& ui, const AudioState& st, double mx, double my,
                      int win_w, int win_h, float dock_h);
void draw_ui(Renderer2D& ui, const AudioState& st, double beats, double mx, double my,
             int win_w, int win_h, float split_x, float dock_h, int visual_source);
void draw_fx_menu(Renderer2D& ui, const CtxMenu& m);
void draw_map_menu(Renderer2D& ui, const CtxMenu& m);
void draw_menu(Renderer2D& ui, const CtxMenu& m, const char* track);

// Which visuals source is under (mx,my): -1 = master, >=0 = track, -2 = none.
int meter_hit(int tracks, int scenes, double mx, double my);

}  // namespace vivid::ui
