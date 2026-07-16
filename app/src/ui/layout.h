#pragma once
#include <algorithm>
#include <string>

// Shared UI geometry + layout constants for the Vivid shell: the session grid
// (tracks × scenes + mixer), the visuals viewer/splitter, and the bottom device
// dock. Window-relative helpers take explicit size/split/dock args (no globals)
// so they're reusable across the draw + input + frame modules. Pure + header-only.
namespace vivid::ui {

struct Rect { float x, y, w, h; };
inline bool hit(const Rect& r, double mx, double my) {
    return mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h;
}
inline float dock_top(int win_h, float dock_h);   // fwd (defined in the window-relative section)

// --- Pane region chrome: bounded panels on a margin/gutter grid. ---
constexpr float kTopBarH    = 40.f;    // transport bar height
constexpr float kPaneMargin = 12.f;    // gap from pane edge to a region panel
constexpr float kSidebarW   = 244.f;   // left browser column width when open (0 = collapsed)
constexpr float kPanelHdH   = 22.f;    // region header strip (matches Style.panel_hd_h)
constexpr float kPanePad    = 8.f;     // inner padding inside a region (matches Style.s4)

// --- Session grid: columns = tracks, rows = scenes; a mixer strip below. ---
// Anchored inside the Session panel: pane margin + header + inner padding.
constexpr float kSceneColX = kPaneMargin + kPanePad;              // scene-launch column x
constexpr float kSceneColW = 40.f;
constexpr float kColGap    = 6.f;                                 // gutter between columns
constexpr float kTrackX0   = kSceneColX + kSceneColW + kColGap;   // first track column x
constexpr float kTrackW    = 88.f, kTrackGap = kColGap;           // smaller, defined clip columns
constexpr float kHeaderY   = kTopBarH + kPaneMargin + kPanelHdH + kPanePad;  // track-header row
constexpr float kHeaderH   = 24.f;
constexpr float kHdrGap    = 8.f;                                 // gap under headers before scenes
constexpr float kGridTopY  = kHeaderY + kHeaderH + kHdrGap;       // first scene row
constexpr float kRowH      = 44.f, kRowGap = 6.f;                 // smaller clip cells
inline float track_x(int t) { return kTrackX0 + t * (kTrackW + kTrackGap); }
inline Rect clip_cell_rect(int track, int scene) { return { track_x(track), kGridTopY + scene * (kRowH + kRowGap), kTrackW, kRowH }; }
// Top transport bar affordances: a browser toggle + a play/pause button. (File is a native menu.)
inline Rect sidebar_toggle_rect() { return { 96.f, 11.f, 20.f, 18.f }; }
inline Rect transport_play_rect() { return { 300.f, 11.f, 18.f, 18.f }; }
inline Rect transport_record_rect() { return { 500.f, 11.f, 18.f, 18.f }; }   // record toggle
inline Rect transport_metro_rect()  { return { 524.f, 11.f, 18.f, 18.f }; }   // metronome toggle
// ADR-0019 health rollup: a status dot right-aligned in the transport bar; click opens diagnostics.
inline Rect health_dot_rect(int win_w) { return { static_cast<float>(win_w) - 26.f, 14.f, 12.f, 12.f }; }
inline Rect track_header_rect(int t) { return { track_x(t), kHeaderY, kTrackW, kHeaderH }; }
inline Rect track_add_rect(int tracks) { return { track_x(tracks), kHeaderY, kTrackW, kHeaderH }; }  // "+ Track" header
inline Rect track_header_x_rect(int t) { return { track_x(t) + kTrackW - 15.f, kHeaderY + 3.f, 12.f, 12.f }; }  // remove ×
inline Rect scene_launch_rect(int scene) { return { kSceneColX, kGridTopY + scene * (kRowH + kRowGap), kSceneColW, kRowH }; }
inline float mixer_y(int scenes) { return kGridTopY + scenes * (kRowH + kRowGap) + kPanePad; }
inline Rect track_meter_rect(int t, int scenes) { return { track_x(t) + 2.f, mixer_y(scenes) + 20.f, kTrackW - 4.f, 6.f }; }
inline Rect track_gain_rect(int t, int scenes)  { return { track_x(t) + 2.f, mixer_y(scenes) + 32.f, kTrackW - 4.f, 10.f }; }
inline Rect master_meter_rect(int scenes) { return { kSceneColX, mixer_y(scenes) + 20.f, kSceneColW, 22.f }; }
// Explicit "send this source to the visuals graph" buttons (the bridge entry point).
// The mixer's bottom button row is split: ARM (left half) | VIZ (right half).
inline Rect track_arm_rect(int t, int scenes)  { return { track_x(t) + 2.f, mixer_y(scenes) + 48.f, (kTrackW - 6.f) * 0.5f, 16.f }; }
inline Rect track_viz_rect(int t, int scenes)  { const float hw = (kTrackW - 6.f) * 0.5f; return { track_x(t) + 4.f + hw, mixer_y(scenes) + 48.f, hw, 16.f }; }
inline Rect master_viz_rect(int scenes) { return { kSceneColX, mixer_y(scenes) + 48.f, kSceneColW, 16.f }; }
// The Session region panel: the full left column (track grid + mixer + room below).
inline Rect session_panel(float split_x, int win_h, float dock_h) {
    const float top = kTopBarH + kPaneMargin;
    return { kPaneMargin, top, split_x - 2.f * kPaneMargin, dock_top(win_h, dock_h) - kPaneMargin - top };
}
inline float mixer_divider_y(int scenes) { return mixer_y(scenes) - 6.f; }  // rule between grid and mixer

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
// A per-track AUDIO-GRAPH node param (the bridge return path onto the node graph). Addressed by the
// node's STABLE id (not a chain index, which can't name a node in a rewired DAG); i = the param index.
inline std::string gnode_param_dest(int track, int node_id, int i) {
    return "gnode:" + std::to_string(track) + ":" + std::to_string(node_id) + ":" + std::to_string(i);
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

// (The visuals render-target size is NOT here any more: the Output node owns it — see
// gpu/output_format.h + ADR-0014. It used to be a fixed 720x300 FBO stretched to the panel, which
// is why several ops hard-coded a /1.7778 aspect correction.)

// --- Window-relative geometry (explicit args; no globals). ---
inline float dock_top(int win_h, float dock_h) { return win_h - dock_h; }   // y where the dock begins
// Left browser sidebar: a bounded panel over [0..sidebar_w] between the transport and dock.
inline Rect sidebar_panel(float sidebar_w, int win_h, float dock_h) {
    const float top = kTopBarH + kPaneMargin;
    return { kPaneMargin, top, sidebar_w - 2.f * kPaneMargin, dock_top(win_h, dock_h) - kPaneMargin - top };
}
// The sidebar is the CLIPS pool. (The PLUGINS browser panel is gone: adding a node is Tab in the
// graph, over the unified catalog — a browser that could only ever list plugins, never native ops,
// was half a catalog behind a second add gesture.)
inline Rect sidebar_clips_panel(float sidebar_w, int win_h, float dock_h) {
    return sidebar_panel(sidebar_w, win_h, dock_h);
}

// Clip-pool items — a vertical list inside the sidebar's CLIPS panel.
constexpr float kPoolItemH = 44.f, kPoolItemGap = 6.f;
inline float sidebar_content_x()   { return kPaneMargin + kPanePad; }
inline float sidebar_content_top() { return kTopBarH + kPaneMargin + kPanelHdH + kPanePad; }
inline Rect pool_item_rect(int i, float sidebar_w) {
    return { sidebar_content_x(), sidebar_content_top() + i * (kPoolItemH + kPoolItemGap),
             sidebar_w - 2.f * (kPaneMargin + kPanePad), kPoolItemH };
}
// A pool item is only live while it fits within the CLIPS panel body (no scroll yet).
inline bool pool_item_visible(int i, float sidebar_w, int win_h, float dock_h) {
    Rect c = sidebar_clips_panel(sidebar_w, win_h, dock_h);
    return pool_item_rect(i, sidebar_w).y + kPoolItemH <= c.y + c.h - kPanePad;
}

inline Rect pool_item_x_rect(int i, float sidebar_w) { Rect r = pool_item_rect(i, sidebar_w); return { r.x + r.w - 15.f, r.y + 3.f, 13.f, 13.f }; }
inline int pool_item_at(float sidebar_w, int count, double mx, double my) {
    for (int i = 0; i < count; ++i) if (hit(pool_item_rect(i, sidebar_w), mx, my)) return i;
    return -1;
}
// The pool drop-zone = the whole sidebar column (drop a grid clip anywhere in it to stash).
inline bool in_sidebar(float sidebar_w, int win_h, float dock_h, double mx, double my) {
    return sidebar_w > 0.f && mx >= 0 && mx < sidebar_w && my >= kTopBarH && my < dock_top(win_h, dock_h);
}
// --- The visuals zone (ADR-0014): the node graph IS the zone; the output floats over it. ---
// The graph owns the whole right column (transport -> dock). No enclosing panel frame: the
// graph canvas is the surface, so structure comes from the column itself, not a nested box.
inline Rect visuals_panel(int win_w, int win_h, float split_x, float dock_h) {
    const float top = kTopBarH + kPaneMargin;
    return { split_x + kPaneMargin, top,
             static_cast<float>(win_w) - split_x - 2.f * kPaneMargin,
             dock_top(win_h, dock_h) - kPaneMargin - top };
}

// The floating OUTPUT preview: a movable/resizable panel drawn over the graph. Its position +
// width are per-window state; its HEIGHT is derived from the output's aspect ratio, so the
// preview always shows the true shape of the output (Output-node params own that aspect).
constexpr float kPreviewMinW = 160.f, kPreviewMaxW = 1600.f;
inline float preview_body_h(float w, float aspect) { return w / (aspect > 0.01f ? aspect : 1.f); }
inline Rect preview_panel(float px, float py, float pw, float aspect) {
    return { px, py, pw, kPanelHdH + preview_body_h(pw, aspect) };
}
// The viewer = the panel body under the header. This is the rect the VisualGraph blits into,
// so it matches the output's aspect exactly (Fit/Fill only matter inside it once the surface
// aspect and the output aspect can differ — e.g. the pop-out window).
inline Rect preview_viewer_rect(float px, float py, float pw, float aspect) {
    return { px, py + kPanelHdH, pw, preview_body_h(pw, aspect) };
}
inline Rect preview_header_rect(float px, float py, float pw) { return { px, py, pw, kPanelHdH }; }
inline Rect preview_close_rect(float px, float py, float pw)  { return { px + pw - 16.f, py + 4.f, 13.f, 13.f }; }
// Pop the output out to its own window (second display / performance screen).
inline Rect preview_popout_rect(float px, float py, float pw) { return { px + pw - 84.f, py + 4.f, 64.f, kPanelHdH - 7.f }; }
// Bottom-right resize grip (drag to scale the preview; the aspect drives the height).
inline Rect preview_grip_rect(float px, float py, float pw, float aspect) {
    const Rect p = preview_panel(px, py, pw, aspect);
    return { p.x + p.w - 14.f, p.y + p.h - 14.f, 14.f, 14.f };
}
// Graph chrome, pinned to the visuals column's top-right corner (screen space, not canvas space).
inline Rect graph_relayout_rect(int win_w, int win_h, float split_x, float dock_h) {
    const Rect g = visuals_panel(win_w, win_h, split_x, dock_h);
    return { g.x + g.w - 78.f, g.y + 4.f, 74.f, kPanelHdH - 6.f };
}
// The DAW|visuals splitter: a full-height grab strip running from the transport bar down to the
// dock (no gap at the top — a divider that stops short reads as an artifact, not a handle).
inline Rect splitter_rect(int win_h, float dock_h, float split_x) { return { split_x - 3.f, kTopBarH, 6.f, dock_top(win_h, dock_h) - kTopBarH }; }
// The grip: a short run of rules at the strip's vertical midpoint — the "you can drag me" mark.
inline Rect splitter_grip_rect(int win_h, float dock_h, float split_x) {
    const Rect s = splitter_rect(win_h, dock_h, split_x);
    return { s.x, s.y + s.h * 0.5f - 14.f, s.w, 28.f };
}
inline Rect dock_resize_rect(int win_w, int win_h, float dock_h) { return { 0.f, dock_top(win_h, dock_h) - 3.f, static_cast<float>(win_w), 7.f }; }
// A close (x) in the detail-region header strip — exits the current focus back to the
// session default (the device view). Sits just left of the domain badge on the right edge.
inline Rect dock_close_rect(int win_w, int win_h, float dock_h) { return { win_w - 78.f, dock_top(win_h, dock_h) + 4.f, 13.f, 13.f }; }

// UI-4b: the "Editor" button in the visual-node inspector header — drills into the op's custom
// editor (only drawn/hit when the selected op exports one). Sits left of the close ×.
inline Rect dock_op_editor_button_rect(int win_w, int win_h, float dock_h) { return { win_w - 148.f, dock_top(win_h, dock_h) + 3.f, 60.f, 15.f }; }
// The audio-graph dock header "Editor" button — opens the selected VST3 node's own native plugin
// window (the full param surface). Only drawn when the selected node exposes an IEditController.
inline Rect dock_audio_editor_button_rect(int win_w, int win_h, float dock_h) { return { win_w - 148.f, dock_top(win_h, dock_h) + 3.f, 60.f, 15.f }; }
// ADR-0021/P4: the "Presets" button in the visual-node inspector header (left of Editor/close).
inline Rect dock_node_presets_button_rect(int win_w, int win_h, float dock_h) { return { win_w - 224.f, dock_top(win_h, dock_h) + 3.f, 68.f, 15.f }; }

// UI-5: the "Float" button on the OpEditor header — pops the editor out into its own OS window.
// Sits left of the close ×.
inline Rect dock_op_float_button_rect(int win_w, int win_h, float dock_h) { return { win_w - 140.f, dock_top(win_h, dock_h) + 3.f, 52.f, 15.f }; }

// UI-3: the "Graph" toggle in the device-dock header — drills the detail region into the selected
// track's audio node graph (deep view). Sits left of the close × / domain badge on the right edge.
inline Rect audio_graph_button_rect(int win_w, int win_h, float dock_h) { return { win_w - 150.f, dock_top(win_h, dock_h) + 3.f, 56.f, 15.f }; }

// Bottom device-view dock: a title strip, a bounded CHAIN rack (the selected
// track's instrument + FX chips) and a PARAMS knob grid below it. The rack is a
// defined section that only appears in track mode (hidden when a visual node is
// inspected). Geometry shared by draw + hit-test.
constexpr float kDockHdH    = 20.f;   // title strip height
constexpr float kDockChainY = 25.f;   // chain-rack top (below the title strip)
constexpr float kDockChainH = 42.f;   // chain-rack height (a chip + inset padding)
constexpr float kDockChipY  = 30.f;   // chips sit inside the rack (kDockChainY + pad)
inline Rect dock_chain_rect(int win_w, int win_h, float dock_h) {
    return { 8.f, dock_top(win_h, dock_h) + kDockChainY, static_cast<float>(win_w) - 16.f, kDockChainH };
}
// UI-3: the region the audio node graph deep view draws into — the dock interior below the title.
inline Rect audio_graph_panel(int win_w, int win_h, float dock_h) {
    return { 8.f, dock_top(win_h, dock_h) + kDockHdH + 6.f, static_cast<float>(win_w) - 16.f, dock_h - kDockHdH - 12.f };
}
struct DockGeom { float y0, gridY0, cellW, cellH, knobOff; int cols, maxRows; };
inline DockGeom dock_geom_at(int win_w, int win_h, float dock_h, float grid_top) {
    DockGeom d; d.y0 = dock_top(win_h, dock_h);
    d.gridY0 = grid_top;
    d.cellW = 64.f; d.cellH = 58.f; d.knobOff = 20.f;
    d.cols = std::max(1, static_cast<int>((win_w - 24.f) / d.cellW));
    d.maxRows = std::max(1, static_cast<int>((d.y0 + dock_h - 6.f - d.gridY0) / d.cellH));
    return d;
}
// Track mode: knobs sit below the CHAIN rack (+ a gap that clears the knob labels).
inline DockGeom dock_geom(int win_w, int win_h, float dock_h) {
    return dock_geom_at(win_w, win_h, dock_h, dock_top(win_h, dock_h) + kDockChainY + kDockChainH + 18.f);
}
// Node-inspector mode: no rack, so the knobs start just under the title strip.
inline DockGeom dock_geom_node(int win_w, int win_h, float dock_h) {
    return dock_geom_at(win_w, win_h, dock_h, dock_top(win_h, dock_h) + kDockHdH + 20.f);
}
inline void dock_knob(int i, const DockGeom& d, float& cx, float& cy) {
    cx = 12.f + (i % d.cols) * d.cellW + d.cellW * 0.5f;
    cy = d.gridY0 + (i / d.cols) * d.cellH + d.knobOff;
}
inline Rect dock_knob_map(int i, const DockGeom& d) { float cx, cy; dock_knob(i, d, cx, cy); return { cx + 13.f, cy - 24.f, 9.f, 9.f }; }

// Node-inspector param rows: one row per param (label column + a widget), laid out in
// columns that wrap when the dock is short. Shared by draw + hit-test.
constexpr float kNodeRowH = 26.f, kNodeRowGap = 4.f, kNodeColW = 300.f, kNodeLabelW = 88.f;
inline int node_rows_per_col(int win_h, float dock_h) {
    const float avail = dock_h - kDockHdH - 16.f;
    return std::max(1, static_cast<int>(avail / (kNodeRowH + kNodeRowGap)));
}
inline Rect node_param_row(int i, int win_w, int win_h, float dock_h) {
    const int rpc = node_rows_per_col(win_h, dock_h);
    const int col = i / rpc, row = i % rpc;
    const float top = dock_top(win_h, dock_h) + kDockHdH + 8.f;
    const float w = std::min(kNodeColW, static_cast<float>(win_w) - 24.f);
    return { 12.f + col * (kNodeColW + 12.f), top + row * (kNodeRowH + kNodeRowGap), w, kNodeRowH };
}
// The interactive widget sub-rect (right of the label column) for param i.
inline Rect node_param_widget_rect(int i, int win_w, int win_h, float dock_h) {
    Rect r = node_param_row(i, win_w, win_h, dock_h);
    return { r.x + kNodeLabelW, r.y + 2.f, r.w - kNodeLabelW, r.h - 4.f };
}
inline Rect node_param_map_rect(int i, int win_w, int win_h, float dock_h) {  // small wire affordance
    Rect r = node_param_row(i, win_w, win_h, dock_h);
    return { r.x + kNodeLabelW - 14.f, r.y + (kNodeRowH - 9.f) * 0.5f, 9.f, 9.f };
}
// UI-4a: a compound widget (XY-pad/color/ADSR/…) claims `span` consecutive param rows; its rect is
// the widget column spanning rows i..i+span-1 (assumes the group stays in one column — true when
// the group starts a column, which is how operators declare them). Shared by draw + hit-test.
inline Rect node_param_compound_rect(int i, int span, int win_w, int win_h, float dock_h) {
    const Rect a = node_param_widget_rect(i, win_w, win_h, dock_h);
    const Rect z = node_param_widget_rect(i + (span > 1 ? span - 1 : 0), win_w, win_h, dock_h);
    return { a.x, a.y, a.w, (z.y + z.h) - a.y };
}

}  // namespace vivid::ui
