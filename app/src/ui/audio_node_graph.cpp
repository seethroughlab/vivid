#include "ui/audio_node_graph.h"
#include "ui/ui_style.h"
#include "ui/node_canvas.h"       // shared node-editor drawing (node_card / node_wire / node_port)
#include "ui/node_graph.h"        // NodeGraph::source_of — the bridge mapped-state query
#include "ui/compound_widget.h"   // UI-4a: ADSR / LFO compound-widget previews
#include "ui/param_widget.h"      // Phase 2b: node_widget_kind (type/hint/enum -> widget)
#include "audio/vst3_host.h"
#include "audio/audio_graph.h"    // ADR-0022: control_resolve — the UI resolves the same combine

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace vivid::ui {

namespace {
namespace P = vivid::session;

// ADR-0022: the modulation state of one param, in the same normalized [0,1] space the widgets use.
// `wired` gates the overlay; lo01..hi01 is the reachable range (control_resolve at src 0 and 1),
// live01 is where it is right now. Computed entirely from the C API on the UI thread — the range is
// pure math, the live value reads the modulator's published output — so it never touches the audio
// thread.
struct ParamMod { bool wired = false; float lo01 = 0.f, hi01 = 0.f, live01 = 0.f; };
static ParamMod param_mod(vivid::session::Session* s, int track, int node, int param, float mn, float mx) {
    ParamMod m;
    if (!P::session_audio_graph_node_param_wired(s, track, node, param)) return m;
    m.wired = true;
    auto nrm = [&](float v) { return (mx > mn) ? std::clamp((v - mn) / (mx - mn), 0.f, 1.f) : 0.f; };
    const float base = P::session_audio_graph_node_param_get(s, track, node, param);   // BASE
    // Find the control edge driving this param and evaluate its reach at the endpoints. Single edge
    // is the common case; with several, the first sets the drawn band while the live dot (resolved)
    // still reflects all of them stacked.
    vivid::audio::ControlShape sh; bool found = false;
    for (int e = 0; e < P::session_track_audio_graph_edge_count(s, track) && !found; ++e) {
        if (P::session_track_audio_graph_edge_kind(s, track, e) != 2) continue;   // 2 = control
        if (P::session_track_audio_graph_edge_to(s, track, e) != node) continue;
        if (P::session_track_audio_graph_edge_dest_param(s, track, e) != param) continue;
        float amt = 1.f, cur = 0.f; int inv = 0, bip = 0;
        P::session_track_audio_graph_edge_control_shape(s, track, e, &amt, &cur, &inv, &bip);
        sh.amount = amt; sh.curve = cur; sh.invert = inv != 0; sh.bipolar = bip != 0;
        found = true;
    }
    if (found) {
        m.lo01 = nrm(vivid::audio::control_resolve(base, 0.f, sh, mn, mx));
        m.hi01 = nrm(vivid::audio::control_resolve(base, 1.f, sh, mn, mx));
    }
    m.live01 = nrm(P::session_audio_graph_node_param_resolved(s, track, node, param));
    return m;
}

constexpr float kCardW = 132.f, kGapX = 46.f, kGapY = 22.f, kPad = 12.f;   // wider: hosts param-name ports
// ADR-0022: the card now exposes its params as ports down the left edge (like the visuals graph),
// so its height is VARIABLE — a function of how many params it exposes. These are the row metrics
// (unscaled, like the other card widgets — remove_rect etc. — which also use unscaled offsets).
constexpr float kCardHeaderH   = 22.f;    // the title strip
constexpr float kPortRowH  = 15.f;    // one left-edge port row (signal-in, then one per exposed param)
constexpr float kCardPrevH = 30.f;    // the live-waveform preview well, below the port rows
constexpr float kAddRowH   = 15.f;    // the "+ param" row on a plugin card
constexpr float kParamBand = 60.f;      // bottom strip that hosts the selected node's params
constexpr float kParamBandTall = 118.f; // grown to host a compound-widget preview above the knobs
constexpr float kPreviewH = 46.f;       // the compound-preview strip at the top of a tall band
constexpr float kPCellW = 66.f, kPCellH = 42.f;   // param knob cell; the band wraps knobs into a grid
constexpr int   kPMaxRows = 3;          // cap the grid at 3 rows (a VST3 node's overflow lives in its plugin editor)
// Curated inspector (Phase 2b): the vertical pinned-param rows for a plugin node.
constexpr float kPinRowH   = 24.f;      // one pinned-param row
constexpr float kPinLabelW = 118.f;     // label column width (left of the widget)
constexpr float kPinValueW = 78.f;      // right-aligned value column
constexpr float kAddBtnH   = 18.f;      // the "+ Add param" button
constexpr int   kPinMaxVisible = 8;     // rows shown before overflow (scroll deferred; use the editor)
// LFO waveform names, indexed by the enum value (matches the SineSynth convention). The session
// API exposes the value but not the enum choice labels, so the widget names them by convention.
const char* kLfoWaveNames[4] = { "sine", "triangle", "square", "saw" };

// MIDI note number -> name (60 -> "C4"), for the key-range readout.
void note_name(int n, char* buf, size_t sz) {
    static const char* names[12] = { "C","C#","D","D#","E","F","F#","G","G#","A","A#","B" };
    std::snprintf(buf, sz, "%s%d", names[((n % 12) + 12) % 12], n / 12 - 1);
}

// Forward to the shared substrate bezier so both editors draw identical wires.
inline void wire(Renderer2D& r, float x0, float y0, float x1, float y1, const float* c) {
    node_wire(r, x0, y0, x1, y1, c[0], c[1], c[2]);
}
}  // namespace

// Does the selected node carry any compound-widget group (ADSR/LFO)? Drives the band height so
// the preview strip has room. Scans the selection's param hints (draw + input agree via sel_node_).
float AudioNodeGraph::param_band_h() const {
    if (!s_ || sel_node_ < 0) return kParamBand;
    if (is_plugin_node(sel_node_)) {   // curated vertical inspector: rows + the add button
        const int np = std::min(P::session_audio_graph_node_param_pinned_count(s_, track_, sel_node_), kPinMaxVisible);
        return std::max(kParamBand, 8.f + np * kPinRowH + 8.f + kAddBtnH + 8.f);
    }
    const int pc = P::session_audio_graph_node_param_count(s_, track_, sel_node_);
    bool compound = false; int knobs = 0;
    for (int i = 0; i < pc; ++i) {
        const int h = P::session_audio_graph_node_param_hint(s_, track_, sel_node_, i);
        if (is_compound_widget(h)) compound = true;
        if (h != VIVID_DISPLAY_LFO) ++knobs;
    }
    if (knobs == 0) return compound ? kParamBandTall : kParamBand;
    const int perRow = std::max(1, static_cast<int>(((x1_ - x0_) - 12.f) / kPCellW));
    const int rows = std::min(kPMaxRows, (knobs + perRow - 1) / perRow);
    const float top = compound ? kPreviewH : 8.f;
    return std::max(compound ? kParamBandTall : kParamBand, top + rows * kPCellH + 8.f);
}

// The compound-widget previews to draw for the selected node (ADSR/LFO), laid out left-to-right in
// the top preview strip. Shared by draw (render) and input (LFO click-to-cycle hit-test).
std::vector<AudioCompoundPreview> AudioNodeGraph::compound_previews() const {
    std::vector<AudioCompoundPreview> out;
    if (!s_ || sel_node_ < 0) return out;
    const Rect pr = param_region();
    const int pc = P::session_audio_graph_node_param_count(s_, track_, sel_node_);
    float px = pr.x + 6.f;
    for (int i = 0; i < pc; ++i) {
        const int hint = P::session_audio_graph_node_param_hint(s_, track_, sel_node_, i);
        if (!is_compound_widget(hint)) continue;
        const float w = (hint == VIVID_DISPLAY_ADSR) ? 190.f : 150.f;
        out.push_back({ hint, i, { px, pr.y + 3.f, w, kPreviewH - 4.f } });
        px += w + 10.f;
    }
    return out;
}

Rect AudioNodeGraph::param_region() const { const float b = param_band_h(); return { x0_, y1_ - b, x1_ - x0_, b }; }
Rect AudioNodeGraph::graph_region() const { const float b = param_band_h(); return { x0_, y0_, x1_ - x0_, (y1_ - b) - y0_ }; }

// A plugin node (VST3, i.e. it exposes an IEditController) uses the curated vertical inspector;
// native ops keep the compound/knob strip. CLAP is treated as native for now (no metadata yet).
bool AudioNodeGraph::is_plugin_node(int sel_node) const {
    return s_ && sel_node >= 0 && P::session_audio_graph_node_is_plugin(s_, track_, sel_node) != 0;
}

// The pinned params of a plugin node laid out as full-width vertical rows. Shared by draw + input.
std::vector<AudioPinRow> AudioNodeGraph::pinned_rows(int sel_node) const {
    std::vector<AudioPinRow> out;
    if (!is_plugin_node(sel_node)) return out;
    const Rect pr = param_region();
    const int np = std::min(P::session_audio_graph_node_param_pinned_count(s_, track_, sel_node), kPinMaxVisible);
    const float rx = pr.x + 8.f, rw = pr.w - 16.f;
    out.reserve(np);
    for (int i = 0; i < np; ++i) {
        const int idx = P::session_audio_graph_node_param_pinned_at(s_, track_, sel_node, i);
        if (idx < 0) continue;
        const int ptype = P::session_audio_graph_node_param_type(s_, track_, sel_node, idx);
        const int hint  = P::session_audio_graph_node_param_hint(s_, track_, sel_node, idx);
        const int cc    = P::session_audio_graph_node_param_choice_count(s_, track_, sel_node, idx);
        AudioPinRow r;
        r.index  = idx;
        r.widget = static_cast<int>(node_widget_kind(ptype, hint, cc));
        const float top = pr.y + 8.f + i * kPinRowH;
        r.row    = { rx, top, rw, kPinRowH - 4.f };
        r.remove = { rx + rw - 15.f, top + 3.f, 13.f, 13.f };
        r.label  = { rx, top, kPinLabelW - 16.f, kPinRowH - 4.f };
        r.mapdot = { rx + kPinLabelW - 13.f, top + 6.f, 9.f, 9.f };
        const float vx = r.remove.x - 6.f - kPinValueW;
        r.value  = { vx, top, kPinValueW, kPinRowH - 4.f };
        r.widget_rect = { rx + kPinLabelW, top + 4.f, std::max(20.f, vx - (rx + kPinLabelW) - 8.f), kPinRowH - 10.f };
        out.push_back(r);
    }
    return out;
}

Rect AudioNodeGraph::add_param_button_rect(int sel_node) const {
    if (!is_plugin_node(sel_node)) return { 0.f, 0.f, 0.f, 0.f };
    const Rect pr = param_region();
    const int np = std::min(P::session_audio_graph_node_param_pinned_count(s_, track_, sel_node), kPinMaxVisible);
    return { pr.x + 8.f, pr.y + 8.f + np * kPinRowH + 2.f, 120.f, kAddBtnH };
}

Rect AudioNodeGraph::add_button_rect() const {
    const Rect g = graph_region();
    return { g.x + g.w - 54.f, g.y + 2.f, 48.f, 16.f };
}
Rect AudioNodeGraph::source_add_button_rect() const {
    const Rect a = add_button_rect();
    return { a.x - 54.f, a.y, 48.f, 16.f };   // pinned left of "+ FX"
}

bool AudioNodeGraph::sel_is_source(int sel_node) const {
    if (!s_ || sel_node < 0) return false;
    const int n = P::session_track_audio_graph_node_count(s_, track_);
    for (int i = 0; i < n; ++i)
        if (P::session_track_audio_graph_node_id(s_, track_, i) == sel_node)
            return P::session_track_audio_graph_node_kind(s_, track_, i) == 0;
    return false;
}
// Two small drag handles at the bottom-right of the param strip (source nodes only). Empty
// otherwise. Draw + input share these so the hit-rects agree.
Rect AudioNodeGraph::key_lo_rect(int sel_node) const {
    if (!sel_is_source(sel_node)) return { 0.f, 0.f, 0.f, 0.f };
    const Rect pr = param_region();
    return { pr.x + pr.w - 150.f, pr.y + pr.h - 28.f, 66.f, 21.f };
}
Rect AudioNodeGraph::key_hi_rect(int sel_node) const {
    if (!sel_is_source(sel_node)) return { 0.f, 0.f, 0.f, 0.f };
    const Rect pr = param_region();
    return { pr.x + pr.w - 78.f, pr.y + pr.h - 28.f, 66.f, 21.f };
}

Rect AudioNodeGraph::remove_rect(const AudioNodeBox& b) const {
    return { b.x + b.w - 13.f, b.y + 3.f, 10.f, 10.f };
}

Rect AudioNodeGraph::out_port_rect(const AudioNodeBox& b) const {
    return { b.x + b.w - 6.f, b.y + b.h * 0.5f - 6.f, 12.f, 12.f };   // right-edge dot hit area
}

// A node has a SIGNAL input (drawn as row 0 of the left edge) iff it consumes a signal: an effect
// or the output take audio; a note effect takes notes. Instruments, MidiIn, and modulators are
// pure sources — no signal-in row, so their param ports start at row 0.
static bool kind_has_sig_in(int kind) { return kind == 1 || kind == 2 || kind == 4; }

// The shared card port-row layout for a node — the one place that knows a card's row composition
// (signal-in? + exposed params + plugin "+param" row). Draw + hit-test both read it, so a port's
// drawn centre and its hit-rect are the same formula. Metrics are the file's unscaled card widths.
CardPorts AudioNodeGraph::card_ports(int node_id, int kind) const {
    CardPorts cp;
    cp.header_h = kCardHeaderH; cp.row_h = kPortRowH; cp.tail_h = kCardPrevH; cp.tail_pad = 6.f;
    cp.lead_rows = kind_has_sig_in(kind) ? 1 : 0;   // audio: a single signal-in row (or none)
    cp.params   = static_cast<int>(exposed_params(node_id).size());
    cp.add_row  = s_ && P::session_audio_graph_node_is_plugin(s_, track_, node_id) != 0;
    return cp;
}

Rect AudioNodeGraph::in_port_rect(const AudioNodeBox& b) const {
    const CardPorts cp = card_ports(b.node_id, b.kind);
    if (cp.lead_rows == 0) return { 0.f, 0.f, 0.f, 0.f };             // no signal input
    return { b.x - 6.f, cp.row_cy(b.y, 0) - 6.f, 12.f, 12.f };        // top-left row
}

// A native op exposes every param; a plugin exposes only its pinned/curated subset. The LFO
// waveform enum (a compound-widget leader) is skipped so the port list reads cleanly.
std::vector<int> AudioNodeGraph::exposed_params(int node_id) const {
    std::vector<int> out;
    if (!s_ || node_id < 0) return out;
    if (P::session_audio_graph_node_is_plugin(s_, track_, node_id)) {
        const int np = P::session_audio_graph_node_param_pinned_count(s_, track_, node_id);
        for (int k = 0; k < np; ++k) {
            const int p = P::session_audio_graph_node_param_pinned_at(s_, track_, node_id, k);
            if (p >= 0) out.push_back(p);
        }
    } else {
        const int pc = P::session_audio_graph_node_param_count(s_, track_, node_id);
        for (int p = 0; p < pc; ++p)
            if (P::session_audio_graph_node_param_hint(s_, track_, node_id, p) != VIVID_DISPLAY_LFO)
                out.push_back(p);
    }
    return out;
}

// Card kind for a node id (small linear scan; n <= kGraphMaxNodes).
int AudioNodeGraph::param_port_hit(const AudioNodeBox& b, float mx, float my) const {
    const int n = static_cast<int>(exposed_params(b.node_id).size());
    for (int slot = 0; slot < n; ++slot) {
        const Rect r = param_port_rect(b, slot);
        if (mx >= r.x - 3.f && mx <= r.x + r.w + 3.f && my >= r.y - 2.f && my <= r.y + r.h + 2.f) return slot;
    }
    return -1;
}

Rect AudioNodeGraph::param_port_rect(const AudioNodeBox& b, int slot) const {
    const CardPorts cp = card_ports(b.node_id, b.kind);
    if (slot < 0 || slot >= cp.params) return { 0.f, 0.f, 0.f, 0.f };
    return { b.x - 5.f, cp.row_cy(b.y, cp.param_row(slot)) - 5.f, 10.f, 10.f };
}

Rect AudioNodeGraph::add_param_port_rect(const AudioNodeBox& b) const {
    const CardPorts cp = card_ports(b.node_id, b.kind);
    if (!cp.add_row) return { 0.f, 0.f, 0.f, 0.f };   // native node: all params already exposed
    return { b.x + 6.f, cp.row_cy(b.y, cp.add_row_index()) - 6.f, b.w - 12.f, 12.f };   // full-width "+ param" hit row
}

float AudioNodeGraph::card_height(int node_id) const {
    if (!s_) return kCardHeaderH + kPortRowH + kCardPrevH;
    int kind = 1;
    const int n = P::session_track_audio_graph_node_count(s_, track_);
    for (int i = 0; i < n; ++i)
        if (P::session_track_audio_graph_node_id(s_, track_, i) == node_id) { kind = P::session_track_audio_graph_node_kind(s_, track_, i); break; }
    return card_ports(node_id, kind).height();
}

std::vector<AudioNodeBox> AudioNodeGraph::layout() const {
    std::vector<AudioNodeBox> out;
    if (!s_ || track_ < 0 || !P::session_track_audio_graph_ok(s_, track_)) return out;
    const int n = P::session_track_audio_graph_node_count(s_, track_);
    if (n <= 0) return out;

    std::vector<int> id(n), kind(n), rank(n, 0), slot(n, 0);
    for (int i = 0; i < n; ++i) {
        id[i]   = P::session_track_audio_graph_node_id(s_, track_, i);
        kind[i] = P::session_track_audio_graph_node_kind(s_, track_, i);
    }
    auto idx_of = [&](int nid) { for (int i = 0; i < n; ++i) if (id[i] == nid) return i; return -1; };
    const int ne = P::session_track_audio_graph_edge_count(s_, track_);
    std::vector<int> ef(ne), et(ne);
    for (int e = 0; e < ne; ++e) {
        ef[e] = idx_of(P::session_track_audio_graph_edge_from(s_, track_, e));
        et[e] = idx_of(P::session_track_audio_graph_edge_to(s_, track_, e));
    }
    for (int pass = 0; pass < n; ++pass)
        for (int e = 0; e < ne; ++e)
            if (ef[e] >= 0 && et[e] >= 0) rank[et[e]] = std::max(rank[et[e]], rank[ef[e]] + 1);
    int max_rank = 0; for (int i = 0; i < n; ++i) max_rank = std::max(max_rank, rank[i]);
    for (int i = 0; i < n; ++i) if (kind[i] == 2) rank[i] = max_rank;
    std::vector<int> fill(max_rank + 1, 0);
    for (int i = 0; i < n; ++i) slot[i] = fill[rank[i]]++;

    // Every node has a stored WORLD position; layout returns world coords and the draw path applies the
    // canvas NodeView transform (ADR-0023 #3: the audio graph draws world-space through set_transform, like
    // the visuals graph — one coordinate model, "true zoom"). An unpositioned node is seeded from the
    // auto-layout (rank = signal depth, slot = fan-out order) and stuck — so the graph opens tidy, then
    // every node is freely draggable and the layout persists.
    out.reserve(n);
    // Seed spacing uses a generous row pitch — cards are now variable-height (param ports), so the
    // tallest a fresh column can get must not overlap the next row on first open.
    constexpr float kSeedRowPitch = 150.f;
    for (int i = 0; i < n; ++i) {
        float wx = 0.f, wy = 0.f;
        if (!P::session_track_audio_graph_node_pos(s_, track_, i, &wx, &wy)) {
            wx = kPad + rank[i] * (kCardW + kGapX);
            wy = kPad + slot[i] * (kSeedRowPitch + kGapY);
            P::session_audio_graph_node_set_pos(s_, track_, id[i], wx, wy);   // seed → draggable + persisted
        }
        out.push_back({ kind[i], id[i], wx, wy, kCardW, card_height(id[i]) });   // WORLD coords + size
    }
    return out;
}

std::vector<AudioParamCell> AudioNodeGraph::param_cells(int sel_node) const {
    std::vector<AudioParamCell> out;
    if (!s_ || sel_node < 0) return out;   // none selected
    const int pc = P::session_audio_graph_node_param_count(s_, track_, sel_node);
    if (pc <= 0) return out;
    // The LFO enum leader is claimed by its waveform preview (no knob); every other param is a knob,
    // including the ADSR channels (their preview groups them but they stay individually draggable).
    bool compound = false;
    std::vector<int> knobs;
    for (int i = 0; i < pc; ++i) {
        const int h = P::session_audio_graph_node_param_hint(s_, track_, sel_node, i);
        if (is_compound_widget(h)) compound = true;
        if (h != VIVID_DISPLAY_LFO) knobs.push_back(i);
    }
    if (knobs.empty()) return out;
    const Rect pr = param_region();
    // Wrap the knobs into a grid (a VST3 node has many params); cap at kPMaxRows — the full plugin
    // set stays reachable by double-clicking the node to open its native editor.
    const int perRow = std::max(1, static_cast<int>((pr.w - 12.f) / kPCellW));
    const int show = std::min(static_cast<int>(knobs.size()), perRow * kPMaxRows);
    const float top = pr.y + (compound ? kPreviewH : 8.f);
    out.reserve(show);
    for (int k = 0; k < show; ++k) {
        const int col = k % perRow, row = k / perRow;
        const float cx = pr.x + 6.f + col * kPCellW, cy = top + row * kPCellH;
        AudioParamCell c;
        c.index = knobs[k]; c.x = cx; c.y = cy; c.w = kPCellW; c.h = kPCellH - 4.f;
        c.knob_cx = cx + kPCellW * 0.5f; c.knob_cy = cy + 18.f; c.knob_r = 11.f;
        out.push_back(c);
    }
    return out;
}

// ADR-0023 Layer 1: a laid-out box -> the shared card-chrome shape (rect/accent/selection/health/title/
// error). This is exactly what draw()'s card loop needs, so it consumes this directly; collect_nodes
// wraps it for the polymorphic surface. Accent + title + broken mirror the old inline card computation
// 1:1, so it stays behavior-preserving.
AdapterNode AudioNodeGraph::node_from_box(const AudioNodeBox& b, int idx) const {
    const Style& sty = style();
    AdapterNode a;
    a.id = b.node_id;
    a.rect = { b.x, b.y, b.w, b.h };
    // kind: 0 instrument / 1 effect / 2 output / 3 MidiIn / 4 note-fx / 5 mod
    const float* acc = b.kind == 0 ? sty.audio
                     : (b.kind == 1 ? sty.fx
                     : (b.kind == 5 ? sty.mod
                     : ((b.kind == 3 || b.kind == 4) ? sty.control : sty.gold)));
    a.accent[0] = acc[0]; a.accent[1] = acc[1]; a.accent[2] = acc[2];
    a.selected = (b.node_id == sel_node_ && sel_node_ >= 0);
    // ADR-0019: a plugin node whose load terminally failed LOOKS broken (NOT while still loading).
    a.broken = P::session_audio_graph_node_plugin_failed(s_, track_, b.node_id) == 1;
    const char* type = P::session_track_audio_graph_node_type(s_, track_, idx);
    a.title = (type && *type) ? type : (b.kind == 2 ? "Output" : (b.kind == 3 ? "MIDI In" : "?"));
    a.error = a.broken ? "plugin unavailable \xE2\x80\x94 silent" : "";
    return a;
}

void AudioNodeGraph::collect_nodes(std::vector<AdapterNode>& out) const {
    out.clear();
    if (!s_ || track_ < 0 || !P::session_track_audio_graph_ok(s_, track_)) return;
    const std::vector<AudioNodeBox> boxes = layout();
    out.reserve(boxes.size());
    for (int i = 0; i < static_cast<int>(boxes.size()); ++i) out.push_back(node_from_box(boxes[i], i));
}

// ADR-0023 #3c: the audio-domain overlay drawn OVER each card by the shared canvas card loop — type
// label, kind tag, remove-x, the exposed-param ports (+ "+ param" on plugins), the live output-waveform
// preview well, and the signal-output nub. Reconstructs the laid-out box from the adapter node (idx maps
// to the same layout() node) so the existing box-relative geometry helpers apply unchanged.
void AudioNodeGraph::after_card(Renderer2D& r, const AdapterNode& a, int i) const {
    const Style& sty = style();
    const int kind = P::session_track_audio_graph_node_kind(s_, track_, i);
    const AudioNodeBox b{ kind, a.id, a.rect.x, a.rect.y, a.rect.w, a.rect.h };
    const float* acc = a.accent;
    const bool node_err = a.broken;
    r.draw_text(b.x + 10.f + (node_err ? node_error_label_shift : 0.f), b.y + 6.f, a.title.c_str(),
                sty.text[0], sty.text[1], sty.text[2], 1.0f, sty.fs_label);
    const char* tag = b.kind == 0 ? "inst"
                    : (b.kind == 1 ? "fx"
                    : (b.kind == 3 ? "midi" : (b.kind == 4 ? "note" : (b.kind == 5 ? "mod" : "out"))));
    { const float tw = r.text_width(tag, 0.6f);   // right-aligned kind tag in the header
      r.draw_text(b.x + b.w - tw - 8.f, b.y + 7.f, tag, acc[0], acc[1], acc[2], 0.85f, 0.6f); }
    if (b.kind == 1) {   // effect: removable
        const Rect x = remove_rect(b);
        r.draw_text(x.x, x.y - 3.f, "\xC3\x97", 0.7f, 0.5f, 0.5f, 1.0f, sty.fs_label);
    }

    // ADR-0022: params exposed as PORTS down the left edge (mirroring the visuals graph). Row 0
    // is the signal input (audio/notes) when the node takes one; then one row per exposed param.
    const CardPorts cp = card_ports(b.node_id, b.kind);   // shared card port-row layout
    if (cp.lead_rows > 0) {
        const float cy = cp.row_cy(b.y, 0);
        node_port(r, b.x, cy, 4.f, sty.dim[0], sty.dim[1], sty.dim[2]);
        r.draw_text(b.x + 9.f, cy - 5.f, b.kind == 4 ? "notes" : "in", sty.dim[0], sty.dim[1], sty.dim[2], 0.9f, 0.6f);
    }
    const std::vector<int> exp = exposed_params(b.node_id);
    for (int slot = 0; slot < static_cast<int>(exp.size()); ++slot) {
        const Rect pp = param_port_rect(b, slot);
        const float cx = pp.x + pp.w * 0.5f, cy = pp.y + pp.h * 0.5f;
        const bool wired = P::session_audio_graph_node_param_wired(s_, track_, b.node_id, exp[slot]) != 0;
        const float* pc = wired ? sty.mod : sty.dim;   // magenta when a control edge drives it
        node_port(r, cx, cy, 4.f, pc[0], pc[1], pc[2]);
        const char* pn = P::session_audio_graph_node_param_name(s_, track_, b.node_id, exp[slot]);
        r.draw_text(cx + 9.f, cy - 5.f, fit_text(r, pn ? pn : "", b.w - 22.f, 0.62f).c_str(), pc[0], pc[1], pc[2], 1.0f, 0.62f);
    }
    // A plugin node: the "+ param" row opens the searchable picker to expose one more param.
    if (P::session_audio_graph_node_is_plugin(s_, track_, b.node_id)) {
        const Rect ab = add_param_port_rect(b);
        r.draw_text(ab.x, ab.y + 1.f, "+ param", sty.dim[0], sty.dim[1], sty.dim[2], 0.9f, 0.6f);
    }

    // Live output-waveform preview — the node's real audio — in a recessed well BELOW the ports.
    const Rect pv = cp.preview({ b.x, b.y, b.w, b.h });
    if (pv.h > 6.f) {
        node_preview_panel(r, pv.x, pv.y, pv.w, pv.h);
        float scope[128];
        const int ns = P::session_track_audio_graph_node_scope(s_, track_, i, scope, 128);
        if (ns > 1) node_waveform(r, pv.x + 1.f, pv.y + 1.f, pv.w - 2.f, pv.h - 2.f, scope, ns, acc[0], acc[1], acc[2]);
        if (node_err) node_error_note(r, pv.x + 1.f, pv.y + 1.f, pv.w - 2.f, pv.h - 2.f, "plugin unavailable \xE2\x80\x94 silent");
    }
    // The signal output nub (right-edge centre; absent on the Output sink).
    if (b.kind != 2) { const Rect p = out_port_rect(b); node_port(r, p.x + 6.f, p.y + 6.f, 4.f, sty.audio[0], sty.audio[1], sty.audio[2]); }
}

void AudioNodeGraph::draw(Renderer2D& r, int sel_node, float cx, float cy) const {
    if (!s_ || track_ < 0) return;
    const Style& sty = style();
    if (x1_ - x0_ < 20.f || y1_ - y0_ < 20.f) return;

    if (!P::session_track_audio_graph_ok(s_, track_)) {
        r.draw_text(x0_ + 4.f, y0_ + 6.f, "This track has no audio graph yet \xE2\x80\x94 add an instrument or a device.",
                    sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, sty.fs_label);
        return;
    }

    const std::vector<AudioNodeBox> boxes = layout();   // WORLD coords (ADR-0023 #3)
    // Clip the graph area (2i): with pan/zoom, nodes can fall outside the region — keep them from
    // bleeding into the param strip or the rest of the dock. The clip is SCREEN-space (GPU scissor),
    // unaffected by the world transform we set next.
    const Rect gr = graph_region();
    r.push_clip_rect(gr.x, gr.y, gr.w, gr.h);
    // ADR-0023 #3: draw the graph content WORLD-space through the canvas camera ("true zoom" — cards,
    // text, ports and wires all scale with zoom, like the visuals graph). The param strip + "TAB to add"
    // chrome below reset to identity and stay screen-space.
    const NodeView& view = canvas_.view();
    r.set_transform(view.ox, view.oy, view.scale);
    // Edges (behind cards): iterate the raw edges again mapped to laid-out boxes by node index.
    const int n = P::session_track_audio_graph_node_count(s_, track_);
    std::vector<int> id(n);
    for (int i = 0; i < n; ++i) id[i] = P::session_track_audio_graph_node_id(s_, track_, i);
    auto box_of = [&](int nid) -> const AudioNodeBox* {
        for (int i = 0; i < n; ++i) if (id[i] == nid && i < static_cast<int>(boxes.size())) return &boxes[i];
        return nullptr;
    };
    const int ne = P::session_track_audio_graph_edge_count(s_, track_);
    for (int e = 0; e < ne; ++e) {
        const AudioNodeBox* a = box_of(P::session_track_audio_graph_edge_from(s_, track_, e));
        const AudioNodeBox* b = box_of(P::session_track_audio_graph_edge_to(s_, track_, e));
        if (!a || !b) continue;
        // Each signal is a different color so a wire's meaning is legible: audio (amber), note
        // (control gray — "data, not sound", ADR-0015), control/modulation (magenta, ADR-0022).
        const int ek = P::session_track_audio_graph_edge_kind(s_, track_, e);
        const float* wc = ek == 2 ? sty.mod : (ek == 1 ? sty.control : sty.audio);
        wire(r, a->x + a->w, a->y + a->h * 0.5f, b->x, b->y + b->h * 0.5f, wc);
    }

    // Cards: the shared canvas card loop (ADR-0023 #3c) draws each card's chrome and calls after_card
    // below for the audio-domain overlay. boxes[i] (for edges/ghost above/below) and the adapter node i
    // are the same node — both from the deterministic layout().
    canvas_.draw_cards(r, *this, *this);

    // Ghost wire while dragging a rewire from a node's output port to the cursor (ADR-0023 Layer 2).
    // Endpoints are WORLD (drawn under the transform): convert the screen cursor to world.
    if (wire_from >= 0) {
        double wcx, wcy; view.to_world(cx, cy, wcx, wcy);
        for (int i = 0; i < static_cast<int>(boxes.size()); ++i)
            if (id[i] == wire_from) { const Rect p = out_port_rect(boxes[i]);
                canvas_.ghost_wire(r, p.x + 6.f, p.y + 6.f,
                                   static_cast<float>(wcx), static_cast<float>(wcy), sty.gold); break; }
    }

    r.set_transform(0.f, 0.f, 1.f);   // ADR-0023 #3: back to identity — the chrome below is screen-space
    // Adding a node is Tab (the unified catalog: native ops + VST3 + CLAP). The old "+ FX" / "+ Src"
    // buttons are gone — they could only ever offer native ops, so they quietly hid half the catalog.
    { const Rect g = graph_region();
      r.draw_text(g.x + g.w - 108.f, g.y + 4.f, "TAB to add", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, sty.fs_kicker); }

    r.pop_clip_rect();   // end graph-area clip (2i)

    // Inline param strip for the selected node (drawn unclipped, in its fixed bottom band).
    const Rect pr = param_region();
    r.draw_rect(pr.x, pr.y, pr.w, 1.f, sty.border_soft[0], sty.border_soft[1], sty.border_soft[2], 1.0f);

    // Curated inspector (Phase 2b): a plugin node shows its PINNED params as vertical rows —
    // [ label · widget · value · × ] — plus a "+ Add param" button. Its full param surface lives
    // in the native plugin editor (the "Editor" button); here you keep only what you reach for.
    if (is_plugin_node(sel_node)) {
        const std::vector<AudioPinRow> rows = pinned_rows(sel_node);
        if (rows.empty())
            r.draw_text(pr.x + 8.f, pr.y + 13.f, "No params pinned \xE2\x80\x94 add the ones you want to control",
                        sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, sty.fs_label);
        for (const AudioPinRow& row : rows) {
            const char* nm  = P::session_audio_graph_node_param_name(s_, track_, sel_node, row.index);
            const char* disp = P::session_audio_graph_node_param_display(s_, track_, sel_node, row.index);
            const float v  = P::session_audio_graph_node_param_get(s_, track_, sel_node, row.index);
            const float mn = P::session_audio_graph_node_param_min(s_, track_, sel_node, row.index);
            const float mx = P::session_audio_graph_node_param_max(s_, track_, sel_node, row.index);
            const float norm = (mx > mn) ? std::clamp((v - mn) / (mx - mn), 0.f, 1.f) : 0.f;
            const bool mapped = map_ && map_->source_of(gnode_param_dest(track_, sel_node, row.index)) != nullptr;
            // label
            r.draw_text(row.label.x, row.label.y + 6.f, fit_text(r, nm ? nm : "", row.label.w, 0.72f).c_str(),
                        sty.body[0], sty.body[1], sty.body[2], 1.0f, 0.72f);
            // bridge map dot (return path): teal when a source drives this param
            if (mapped) r.draw_rect(row.mapdot.x, row.mapdot.y, row.mapdot.w, row.mapdot.h, 0.31f, 0.80f, 0.75f, 1.0f);
            else        r.draw_rect(row.mapdot.x, row.mapdot.y, row.mapdot.w, row.mapdot.h, sty.recess[0], sty.recess[1], sty.recess[2], 1.0f);
            // widget by type
            const NodeWidget wk = static_cast<NodeWidget>(row.widget);
            if (wk == NodeWidget::Toggle) {
                toggle(r, row.widget_rect.x, row.widget_rect.y, 30.f, row.widget_rect.h, norm > 0.5f, sty.audio);
            } else if (wk == NodeWidget::Enum) {
                dropdown_field(r, row.widget_rect.x, row.widget_rect.y, row.widget_rect.w, row.widget_rect.h,
                               (disp && *disp) ? disp : "", sty.audio, false);
            } else {
                slider(r, row.widget_rect.x, row.widget_rect.y, row.widget_rect.w, row.widget_rect.h, norm, nullptr, nullptr, sty.audio, mapped, hit(row.widget_rect, cx, cy));
                // ADR-0022: a control edge's reach + live position, over the groove.
                if (const ParamMod pm = param_mod(s_, track_, sel_node, row.index, mn, mx); pm.wired)
                    slider_mod_overlay(r, row.widget_rect.x, row.widget_rect.y, row.widget_rect.w, row.widget_rect.h, pm.lo01, pm.hi01, pm.live01);
            }
            // value (right-aligned) — the enum shows its label in the field, so skip it there
            if (wk != NodeWidget::Enum)
                draw_text_r(r, row.value.x + row.value.w, row.value.y + 6.f, (disp && *disp) ? disp : "",
                            sty.text, 1.0f, 0.7f);
            // remove (×)
            const bool rh = hit(row.remove, cx, cy);
            r.draw_text(row.remove.x + 2.f, row.remove.y - 1.f, "\xC3\x97", 0.72f, 0.45f, 0.45f, rh ? 1.0f : 0.75f, 0.95f);
        }
        const Rect ab = add_param_button_rect(sel_node);
        item_box(r, ab, sty.audio, hit(ab, cx, cy));
        r.draw_text(ab.x + 8.f, ab.y + 3.f, "+ Add param", sty.audio[0], sty.audio[1], sty.audio[2], 0.9f, sty.fs_label);
        return;
    }
    // UI-4a: compound-widget previews (ADSR envelope / LFO waveform) in the top strip. The channel
    // params still render as knobs below (the preview groups them); the LFO enum is preview-only.
    for (const auto& cp : compound_previews()) {
        if (cp.hint == VIVID_DISPLAY_ADSR) {
            auto nrm = [&](int p) {
                const float v = P::session_audio_graph_node_param_get(s_, track_, sel_node, p);
                const float mn = P::session_audio_graph_node_param_min(s_, track_, sel_node, p);
                const float mx = P::session_audio_graph_node_param_max(s_, track_, sel_node, p);
                return (mx > mn) ? std::clamp((v - mn) / (mx - mn), 0.f, 1.f) : 0.f; };
            draw_adsr(r, cp.rect, nrm(cp.index), nrm(cp.index + 1), nrm(cp.index + 2), nrm(cp.index + 3),
                      sty.audio, "ADSR");
        } else if (cp.hint == VIVID_DISPLAY_LFO) {
            const int w = std::clamp(static_cast<int>(std::lround(
                P::session_audio_graph_node_param_get(s_, track_, sel_node, cp.index))), 0, 3);
            draw_lfo(r, cp.rect, w, kLfoWaveNames[w], sty.audio, "LFO");
        }
    }
    // Key-range editor for a selected SOURCE node (two draggable handles + a note-name readout).
    // A key-split = two sources with disjoint ranges; drag lo/hi to carve the keyboard.
    if (sel_is_source(sel_node)) {
        int lo = 0, hi = 127;
        P::session_audio_graph_node_key_range_get(s_, track_, sel_node, &lo, &hi);
        const Rect kl = key_lo_rect(sel_node), kh = key_hi_rect(sel_node);
        r.draw_text(kl.x, kl.y - 12.f, "key range (drag)", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.62f);
        char a[8], z[8]; note_name(lo, a, sizeof a); note_name(hi, z, sizeof z);
        r.draw_rounded_rect(kl.x, kl.y, kl.w, kl.h, 3.f, sty.card_hi[0], sty.card_hi[1], sty.card_hi[2], 1.0f);
        r.draw_text(kl.x + 7.f, kl.y + 4.f, a, sty.body[0], sty.body[1], sty.body[2], 1.0f, sty.fs_label);
        r.draw_rounded_rect(kh.x, kh.y, kh.w, kh.h, 3.f, sty.card_hi[0], sty.card_hi[1], sty.card_hi[2], 1.0f);
        r.draw_text(kh.x + 7.f, kh.y + 4.f, z, sty.body[0], sty.body[1], sty.body[2], 1.0f, sty.fs_label);
    }

    const std::vector<AudioParamCell> cells = param_cells(sel_node);
    if (cells.empty() && compound_previews().empty() && !sel_is_source(sel_node)) {
        r.draw_text(pr.x + 4.f, pr.y + 20.f,
                    sel_node < 0 ? "click a node to edit its parameters" : "this node has no parameters",
                    sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, sty.fs_label);
        return;
    }
    for (const auto& c : cells) {
        const char* nm = P::session_audio_graph_node_param_name(s_, track_, sel_node, c.index);
        const float v  = P::session_audio_graph_node_param_get(s_, track_, sel_node, c.index);
        const float mn = P::session_audio_graph_node_param_min(s_, track_, sel_node, c.index);
        const float mx = P::session_audio_graph_node_param_max(s_, track_, sel_node, c.index);
        const float norm = (mx > mn) ? std::clamp((v - mn) / (mx - mn), 0.f, 1.f) : 0.f;
        // Prefer the plugin's own formatted value ("1.2 kHz", "On", "Lowpass") over a raw number;
        // fall back to %.2f for native ops / CLAP that don't provide one.
        const char* disp = P::session_audio_graph_node_param_display(s_, track_, sel_node, c.index);
        char vt[16]; std::snprintf(vt, sizeof vt, "%.2f", v);
        const std::string vtxt = fit_text(r, (disp && *disp) ? disp : vt, c.w - 2.f, 0.66f);
        knob(r, c.knob_cx, c.knob_cy, c.knob_r, norm, nullptr, vtxt.c_str(), sty.audio, false);
        // ADR-0022: if a modulator drives this param, ring it with the reachable range + a live dot.
        if (const ParamMod pm = param_mod(s_, track_, sel_node, c.index, mn, mx); pm.wired)
            knob_mod_overlay(r, c.knob_cx, c.knob_cy, c.knob_r, pm.lo01, pm.hi01, pm.live01);
        // Ellipsize the label to the cell so long plugin param names don't collide with neighbours.
        r.draw_text(c.x + 2.f, c.y + c.h - 10.f, fit_text(r, nm ? nm : "", c.w - 4.f, 0.65f).c_str(),
                    sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.65f);
        // Bridge map dot (return path): lit teal when a source drives this node param, dim gold
        // otherwise. Clicking it (input_graph) opens the map-source picker (emits a "gnode:" dest).
        const Rect md = ag_param_map_dot(c);
        const bool mapped = map_ && map_->source_of(gnode_param_dest(track_, sel_node, c.index)) != nullptr;
        if (mapped) r.draw_rect(md.x, md.y, md.w, md.h, 0.31f, 0.80f, 0.75f, 1.0f);
        else        r.draw_rect(md.x + 2.f, md.y + 2.f, md.w - 4.f, md.h - 4.f, sty.gold[0], sty.gold[1], sty.gold[2], 0.55f);
    }
}

}  // namespace vivid::ui
