#include "ui/audio_node_graph.h"
#include "ui/ui_style.h"
#include "ui/node_canvas.h"       // shared node-editor drawing (node_card / node_wire / node_port)
#include "ui/compound_widget.h"   // UI-4a: ADSR / LFO compound-widget previews
#include "audio/vst3_host.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace vivid::ui {

namespace {
namespace P = vivid::session;
constexpr float kCardW = 108.f, kCardH = 44.f, kGapX = 46.f, kGapY = 16.f, kPad = 12.f;
constexpr float kParamBand = 60.f;      // bottom strip that hosts the selected node's params
constexpr float kParamBandTall = 118.f; // grown to host a compound-widget preview above the knobs
constexpr float kPreviewH = 46.f;       // the compound-preview strip at the top of a tall band
// LFO waveform names, indexed by the enum value (matches the SineSynth convention). The session
// API exposes the value but not the enum choice labels, so the widget names them by convention.
const char* kLfoWaveNames[4] = { "sine", "triangle", "square", "saw" };

// Forward to the shared substrate bezier so both editors draw identical wires.
inline void wire(Renderer2D& r, float x0, float y0, float x1, float y1, const float* c) {
    node_wire(r, x0, y0, x1, y1, c[0], c[1], c[2]);
}
}  // namespace

// Does the selected node carry any compound-widget group (ADSR/LFO)? Drives the band height so
// the preview strip has room. Scans the selection's param hints (draw + input agree via sel_node_).
float AudioNodeGraph::param_band_h() const {
    if (!s_ || sel_node_ < 0) return kParamBand;
    const int pc = P::session_audio_graph_node_param_count(s_, track_, sel_node_);
    for (int i = 0; i < pc; ++i)
        if (is_compound_widget(P::session_audio_graph_node_param_hint(s_, track_, sel_node_, i)))
            return kParamBandTall;
    return kParamBand;
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

Rect AudioNodeGraph::add_button_rect() const {
    const Rect g = graph_region();
    return { g.x + g.w - 54.f, g.y + 2.f, 48.f, 16.f };
}

Rect AudioNodeGraph::remove_rect(const AudioNodeBox& b) const {
    return { b.x + b.w - 13.f, b.y + 3.f, 10.f, 10.f };
}

Rect AudioNodeGraph::out_port_rect(const AudioNodeBox& b) const {
    return { b.x + b.w - 6.f, b.y + b.h * 0.5f - 6.f, 12.f, 12.f };   // right-edge dot hit area
}
Rect AudioNodeGraph::in_port_rect(const AudioNodeBox& b) const {
    return { b.x - 6.f, b.y + b.h * 0.5f - 6.f, 12.f, 12.f };         // left-edge dot hit area
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
    std::vector<int> fill(max_rank + 1, 0); int max_slots = 1;
    for (int i = 0; i < n; ++i) { slot[i] = fill[rank[i]]++; max_slots = std::max(max_slots, fill[rank[i]]); }

    const Rect g = graph_region();
    const float world_w = max_rank * (kCardW + kGapX) + kCardW;
    const float world_h = max_slots * kCardH + (max_slots - 1) * kGapY;
    const float scale = std::min({ (g.w - 2 * kPad) / world_w, (g.h - 2 * kPad) / world_h, 1.0f });
    const float ox = g.x + (g.w - world_w * scale) * 0.5f;
    const float oy = g.y + (g.h - world_h * scale) * 0.5f;
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        // Auto-fit base position, then the user view transform (2i): zoom around the region origin
        // + pan. zoom 1 / pan 0 leaves the fitted layout untouched.
        const float bx = ox + rank[i] * (kCardW + kGapX) * scale;
        const float by = oy + slot[i] * (kCardH + kGapY) * scale;
        out.push_back({ kind[i], id[i],
                        g.x + (bx - g.x) * zoom_ + pan_x_, g.y + (by - g.y) * zoom_ + pan_y_,
                        kCardW * scale * zoom_, kCardH * scale * zoom_ });
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
    std::vector<int> knobs;
    for (int i = 0; i < pc; ++i)
        if (P::session_audio_graph_node_param_hint(s_, track_, sel_node, i) != VIVID_DISPLAY_LFO)
            knobs.push_back(i);
    if (knobs.empty()) return out;
    const Rect pr = param_region();
    const bool tall = pr.h > kParamBand + 1.f;
    const float row_y = pr.y + (tall ? kPreviewH : 4.f);   // knobs sit below the preview strip
    const float row_h = pr.h - (tall ? kPreviewH : 0.f) - 8.f;
    const float cellW = std::min(78.f, (pr.w - 12.f) / static_cast<float>(knobs.size()));
    out.reserve(knobs.size());
    for (size_t k = 0; k < knobs.size(); ++k) {
        const float cx = pr.x + 6.f + k * cellW;
        AudioParamCell c;
        c.index = knobs[k]; c.x = cx; c.y = row_y; c.w = cellW; c.h = row_h;
        c.knob_cx = cx + cellW * 0.5f; c.knob_cy = row_y + 20.f; c.knob_r = 11.f;
        out.push_back(c);
    }
    return out;
}

void AudioNodeGraph::draw(Renderer2D& r, int sel_node, int wire_from, float cx, float cy) const {
    if (!s_ || track_ < 0) return;
    const Style& sty = style();
    if (x1_ - x0_ < 20.f || y1_ - y0_ < 20.f) return;

    if (!P::session_track_audio_graph_ok(s_, track_)) {
        r.draw_text(x0_ + 4.f, y0_ + 6.f,
                    "No native audio graph for this track (a VST3 or audio track runs the inline path).",
                    sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, sty.fs_label);
        return;
    }

    const std::vector<AudioNodeBox> boxes = layout();
    // Clip the graph area (2i): with pan/zoom, nodes can fall outside the region — keep them from
    // bleeding into the param strip or the rest of the dock.
    const Rect gr = graph_region();
    r.push_clip_rect(gr.x, gr.y, gr.w, gr.h);
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
        if (a && b) wire(r, a->x + a->w, a->y + a->h * 0.5f, b->x, b->y + b->h * 0.5f, sty.audio);
    }

    // Cards: fill + accent + type label + kind tag (+ remove-x on effects). boxes[i] corresponds
    // to node index i, so the type name is looked up by the same index.
    for (int i = 0; i < static_cast<int>(boxes.size()); ++i) {
        const AudioNodeBox& b = boxes[i];
        const float* acc = b.kind == 0 ? sty.audio : (b.kind == 1 ? sty.fx : sty.control);
        const bool sel = (b.node_id == sel_node && sel_node >= 0);
        node_card(r, b.x, b.y, b.w, b.h, acc, sel);   // shared: border/body/header/top accent + blue sel ring
        const char* type = P::session_track_audio_graph_node_type(s_, track_, i);
        const char* label = (type && *type) ? type : (b.kind == 2 ? "Output" : "?");
        r.draw_text(b.x + 10.f, b.y + 6.f, label, sty.text[0], sty.text[1], sty.text[2], 1.0f, sty.fs_label);
        const char* tag = b.kind == 0 ? "instrument" : (b.kind == 1 ? "effect" : "output");
        r.draw_text(b.x + 10.f, b.y + b.h - 13.f, tag, acc[0], acc[1], acc[2], 0.9f, 0.66f);
        if (b.kind == 1) {   // effect: removable
            const Rect x = remove_rect(b);
            r.draw_text(x.x, x.y - 3.f, "\xC3\x97", 0.7f, 0.5f, 0.5f, 1.0f, sty.fs_label);
        }
        // Wire ports: an output nub (source; not on Output) and an input nub (target; not on inst).
        if (b.kind != 2) { const Rect p = out_port_rect(b); node_port(r, p.x + 6.f, p.y + 6.f, 4.f, sty.audio[0], sty.audio[1], sty.audio[2]); }
        if (b.kind != 0) { const Rect p = in_port_rect(b);  node_port(r, p.x + 6.f, p.y + 6.f, 4.f, sty.dim[0], sty.dim[1], sty.dim[2]); }
    }

    // Ghost wire while dragging a rewire from a node's output port to the cursor.
    if (wire_from >= 0) {
        for (int i = 0; i < static_cast<int>(boxes.size()); ++i)
            if (id[i] == wire_from) { const Rect p = out_port_rect(boxes[i]);
                wire(r, p.x + 6.f, p.y + 6.f, cx, cy, sty.gold); break; }
    }

    // "+ FX" affordance.
    { const Rect a = add_button_rect();
      r.draw_rect(a.x, a.y, a.w, a.h, sty.card[0], sty.card[1], sty.card[2], 1.0f);
      r.draw_text(a.x + 7.f, a.y + 1.f, "+ FX", sty.audio[0], sty.audio[1], sty.audio[2], 1.0f, sty.fs_label); }

    r.pop_clip_rect();   // end graph-area clip (2i)

    // Inline param strip for the selected node (drawn unclipped, in its fixed bottom band).
    const Rect pr = param_region();
    r.draw_rect(pr.x, pr.y, pr.w, 1.f, sty.border_soft[0], sty.border_soft[1], sty.border_soft[2], 1.0f);
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
    const std::vector<AudioParamCell> cells = param_cells(sel_node);
    if (cells.empty() && compound_previews().empty()) {
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
        char vt[16]; std::snprintf(vt, sizeof vt, "%.2f", v);
        knob(r, c.knob_cx, c.knob_cy, c.knob_r, norm, nullptr, vt, sty.audio, false);
        r.draw_text(c.x + 2.f, c.y + c.h - 10.f, nm ? nm : "", sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, 0.65f);
    }
}

}  // namespace vivid::ui
