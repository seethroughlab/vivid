#pragma once
#include "ui/layout.h"   // Rect
#include <string>
#include <vector>

namespace vivid { struct Window; struct CtxMenu; struct ModEditor; }
namespace vivid::session { struct Session; }

namespace vivid::ui {
class Renderer2D;

// The Session view (transport, tracks×scenes clip grid, mixer), the bottom
// device dock, the clip-cell previews, and the small context menus. State comes
// from the Window (its metrics + selection, and the shared App behind win.app).
void draw_clip_preview(Renderer2D& ui, vivid::session::Session* s, int t, int sc,
                       const Rect& b, float ar, float ag, float ab, bool on);
void draw_device_dock(Renderer2D& ui, const Window& w, double beats, double mx, double my);
void draw_ui(Renderer2D& ui, const Window& w, double beats, double mx, double my);
// ADR-0014: the floating OUTPUT preview's chrome (frame + header + pop-out/close + resize grip).
// Drawn in the overlay pass, AFTER the output FBO has been blitted into its body, so the frame
// sits above both the graph canvas and the rendered output.
void draw_output_preview(Renderer2D& ui, const Window& w, double mx, double my);
void draw_map_menu(Renderer2D& ui, const CtxMenu& m);
void draw_mod_editor(Renderer2D& ui, const ModEditor& m, vivid::session::Session* s, int track);
void draw_menu(Renderer2D& ui, const CtxMenu& m, const char* track);
void draw_node_menu(Renderer2D& ui, const Window& w);   // right-click op-node menu (open source / clone)
void draw_audio_node_menu(Renderer2D& ui, const Window& w);   // right-click audio-node "→ visuals" menu

// Which visuals source is under (mx,my): -1 = master, >=0 = track, -2 = none.
int meter_hit(int tracks, int scenes, double mx, double my);

// --- Unified device chain (VST3 + native audio operators) ---
// The device dock lays chips left-to-right: slot 0 = instrument, then the VST3 FX
// chain, then the native audio-op FX chain, then a "+ FX" tile. `DevSlot` resolves a
// chip slot to the underlying device so draw + input + drag-apply all agree on which
// engine API (VST3 vs native) a slot addresses. Native params are normalized to 0..1
// for the knob using their Param<> range; VST3 params are already normalized.
struct DevSlot {
    bool valid = false;
    bool native = false;         // native audio operator vs VST3
    bool is_instrument = false;  // slot 0
    int  api_index = 0;          // native: -1 = instrument, >=0 = effect; VST3: device index
};
int     dock_device_count(vivid::session::Session* s, int track);          // chips excluding "+ FX"
DevSlot dock_resolve(vivid::session::Session* s, int track, int slot);
int     dock_param_count(vivid::session::Session* s, int track, const DevSlot& d);
const char* dock_param_name(vivid::session::Session* s, int track, const DevSlot& d, int i);
float   dock_param_norm(vivid::session::Session* s, int track, const DevSlot& d, int i);       // 0..1
void    dock_param_set_norm(vivid::session::Session* s, int track, const DevSlot& d, int i, float norm);
std::string dock_param_dest(int track, const DevSlot& d, int i);           // mapping dest string

}  // namespace vivid::ui
