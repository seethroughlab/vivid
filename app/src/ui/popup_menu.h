#pragma once
// ADR-0027: one popup-menu component. Replaces the hand-rolled per-menu structs (CtxMenu/NodeMenu/…)
// and their parallel draw + hit-test pairs. A PopupMenu owns its open/position state, its item list, and
// — through row_rect/hit_row — its geometry, so the draw (draw_popup, session_view.cpp) and the click
// hit-test compute rows from the SAME function and can't drift. The typed `kind` + `a`/`b` payload
// replaces the overloaded `CtxMenu.src` (which meant a track index for one menu, a node id for another).
#include "ui/layout.h"   // Rect, hit, and the item catalogs (kChars / kMapSources)

#include <cstdio>
#include <string>
#include <vector>

namespace vivid::ui {

// One row. `id` is a caller-defined dispatch key (here: an index into the source catalog); `label` is
// what's drawn; a disabled row is shown dimmed and does nothing on click.
struct PopupItem {
    std::string label;
    int         id = 0;
    bool        enabled = true;
};

struct PopupMenu {
    enum class Kind { None, TrackChars, MapSource };   // what the menu acts on (grows per migrated menu)
    enum Accent { Teal = 0, Gold = 1, Gpu = 2 };       // panel/item accent (resolved to a Style colour)

    bool  open = false;
    float x = 0.f, y = 0.f;
    std::string            header;
    std::vector<PopupItem> items;
    float  width = 176.f, row_h = 26.f;
    Accent accent = Teal;
    Kind   kind = Kind::None;
    int    a = -1, b = -1;   // kind-specific payload (e.g. a = track index, or an audio-graph node id)

    Rect row_rect(int j) const { return { x, y + static_cast<float>(j) * row_h, width, row_h }; }
    int  hit_row(float mx, float my) const {   // the row under the cursor, or -1 (used by the click path)
        if (!open) return -1;
        for (int j = 0; j < static_cast<int>(items.size()); ++j)
            if (vivid::ui::hit(row_rect(j), mx, my)) return j;
        return -1;
    }
    void close() { open = false; }
};

// --- builders (open-site helpers) -----------------------------------------------------------------
// The per-track "characteristics → visuals" menu. `track_index` < 0 = the master bus (no note sources,
// so only the 5 audio kinds); `name` is the header label. Each item's id is its index into kChars.
inline PopupMenu popup_track_chars(float x, float y, int track_index, const char* name) {
    PopupMenu m;
    m.open = true; m.x = x; m.y = y; m.width = 184.f; m.row_h = 26.f; m.accent = PopupMenu::Teal;
    m.kind = PopupMenu::Kind::TrackChars; m.a = track_index;
    char hdr[96]; std::snprintf(hdr, sizeof hdr, "%s  \xE2\x86\x92  visuals", name && *name ? name : "track");
    m.header = hdr;
    const int nc = track_index < 0 ? kNumCharsMaster : kNumChars;
    for (int j = 0; j < nc; ++j) m.items.push_back({ kChars[j].label, j, true });
    return m;
}
// The "map a param from:" return-path picker, opened from an audio-graph node. `node_id` is the node the
// mapped param lives on. Each item's id is its index into kMapSources.
inline PopupMenu popup_map_sources(float x, float y, int node_id) {
    PopupMenu m;
    m.open = true; m.x = x; m.y = y; m.width = 168.f; m.row_h = 24.f; m.accent = PopupMenu::Gold;
    m.kind = PopupMenu::Kind::MapSource; m.a = node_id;
    m.header = "map param from:";
    for (int j = 0; j < kNumMapSources; ++j) m.items.push_back({ kMapSources[j].label, j, true });
    return m;
}

}  // namespace vivid::ui
