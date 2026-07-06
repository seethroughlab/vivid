#pragma once
// UI-3: a read-only per-track AUDIO NODE GRAPH view. Reads the authoritative topology the RT
// engine runs (the session_track_audio_graph_* introspection API) and draws it as a node graph —
// the audio peer of the visuals node graph, but hosted as a detail-region deep view (ADR-0013),
// not a permanent pane. Auto-lays-out left->right by depth and auto-fits the given region, so a
// track's instrument -> FX -> output chain reads at a glance. Editing (rewiring) is a later step
// (the graph edit C-API); today this visualizes the live graph.
#include "ui/renderer_2d.h"

namespace vivid::session { struct Session; }

namespace vivid::ui {

class AudioNodeGraph {
public:
    // Bind to a track's graph + the region to draw into (both set per frame before draw()).
    void set_source(vivid::session::Session* s, int track) { s_ = s; track_ = track; }
    void set_bounds(float x0, float y0, float x1, float y1) { x0_ = x0; y0_ = y0; x1_ = x1; y1_ = y1; }

    // Read the introspection API + render nodes + edges, fitted to the bounds. If the track is not
    // on the native audio-graph path (VST3 / audio track), draws an explanatory message instead.
    void draw(Renderer2D& r) const;

private:
    vivid::session::Session* s_ = nullptr;
    int   track_ = -1;
    float x0_ = 0, y0_ = 0, x1_ = 0, y1_ = 0;
};

}  // namespace vivid::ui
