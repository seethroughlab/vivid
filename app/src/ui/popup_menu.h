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
    bool        checked = false;   // param-curation menus: draw a checkmark when this param is shown
};

// A visuals op-node menu's single action (resolved at open time from the node's shader/source tier).
enum class NodeAction { None, OpenSource, ForkEdit, CloneEdit };

struct PopupMenu {
    // What the menu acts on (grows as menus migrate); drives the click dispatch.
    // NodeParamPin  = the "curate which params to show" checklist (click toggles a pin).
    // NodeParamConnect = same list opened by a dropped wire (click pins the param AND connects the wire).
    // For both, `a` = node index, `b` = pending wire source (data-node index; connect only), and each
    // PopupItem.id = the real param index. `data` holds "audio:<track>" when the menu targets an audio node.
    enum class Kind { None, TrackChars, MapSource, AudioNode, VisualNode, NodeParamPin, NodeParamConnect };
    enum Accent { Teal = 0, Gold = 1, Gpu = 2 };       // panel/item accent (resolved to a Style colour)

    bool  open = false;
    float x = 0.f, y = 0.f;
    std::string            header;
    std::vector<PopupItem> items;
    float  width = 176.f, row_h = 26.f;
    Accent accent = Teal;
    Kind   kind = Kind::None;
    int    a = -1, b = -1;      // kind-specific int payload (track index / audio node id / (track,node))
    std::string data;           // kind-specific string payload (VisualNode: the source path to edit)

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
// The "→ visuals" menu on an audio-graph node: RMS/FFT (or a modulator's control) shortcuts. `is_mod`
// picks the catalog; `type_name` is the node's op type (header + spawned-node label). a = track, b = node;
// each item's id is its index into the chosen catalog (kAudioNodeChars / kModNodeChars).
inline PopupMenu popup_audio_node(float x, float y, int track, int node_id, bool is_mod, const char* type_name) {
    PopupMenu m;
    m.open = true; m.x = x; m.y = y; m.width = 184.f; m.row_h = 26.f; m.accent = PopupMenu::Teal;
    m.kind = PopupMenu::Kind::AudioNode; m.a = track; m.b = node_id;
    char hdr[96]; std::snprintf(hdr, sizeof hdr, "%s  \xE2\x86\x92  visuals", type_name && *type_name ? type_name : "node");
    m.header = hdr;
    const AudioNodeChar* items = is_mod ? kModNodeChars : kAudioNodeChars;
    const int nitems = is_mod ? kNumModNodeChars : kNumAudioNodeChars;
    for (int j = 0; j < nitems; ++j) m.items.push_back({ items[j].label, j, true });
    return m;
}
// The right-click op-node menu (open/fork/clone its editable source). One item whose label + enabled +
// dispatch come from the resolved `action`; `target` is the source path the action acts on (in `data`);
// `header` is the node's op type. a = the visual node id.
inline PopupMenu popup_visual_node(float x, float y, int node_id, NodeAction action,
                                   const char* label, const char* header, const std::string& target) {
    PopupMenu m;
    m.open = true; m.x = x; m.y = y; m.width = 172.f; m.row_h = 22.f; m.accent = PopupMenu::Gpu;
    m.kind = PopupMenu::Kind::VisualNode; m.a = node_id; m.data = target;
    m.header = header ? header : "node";
    m.items.push_back({ label ? label : "", static_cast<int>(action), action != NodeAction::None });
    return m;
}

// The param-curation menu (Gesture A: click node edge to toggle shown params; Gesture B: a dropped wire
// picks a param to reveal+connect). Items are assembled by the caller (visual reads NodeGraph accessors,
// audio reads the session C-API), each PopupItem.id = the real param index, .checked = currently shown.
// `connect_mode` switches dispatch from toggle-pin to pin-and-connect; `pending_src` carries the wire
// source; `audio_track` >= 0 tags an audio-graph node (else it's a visual node).
inline PopupMenu popup_param_curate(float x, float y, int node_idx, int audio_track, bool connect_mode,
                                    int pending_src, const char* header, std::vector<PopupItem> items) {
    PopupMenu m;
    m.open = true; m.x = x; m.y = y; m.width = 204.f; m.row_h = 22.f; m.accent = PopupMenu::Gpu;
    m.kind = connect_mode ? PopupMenu::Kind::NodeParamConnect : PopupMenu::Kind::NodeParamPin;
    m.a = node_idx; m.b = pending_src;
    m.header = header && *header ? header : "show params";
    if (audio_track >= 0) m.data = "audio:" + std::to_string(audio_track);
    m.items = std::move(items);
    return m;
}

}  // namespace vivid::ui
