#include "ui/audio_node_graph.h"
#include "ui/ui_style.h"
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
constexpr float kParamBand = 60.f;   // bottom strip that hosts the selected node's params

void wire(Renderer2D& r, float x0, float y0, float x1, float y1, const float* c) {
    constexpr int N = 24; float xs[N], ys[N];
    const float dx = std::max(std::fabs(x1 - x0) * 0.5f, 22.f);
    const float c1x = x0 + dx, c2x = x1 - dx;
    for (int i = 0; i < N; ++i) {
        const float t = i / float(N - 1), u = 1.f - t;
        xs[i] = u*u*u*x0 + 3*u*u*t*c1x + 3*u*t*t*c2x + t*t*t*x1;
        ys[i] = u*u*u*y0 + 3*u*u*t*y0  + 3*u*t*t*y1  + t*t*t*y1;
    }
    r.draw_polyline(xs, ys, N, 2.0f, c[0], c[1], c[2], 0.9f);
}
}  // namespace

Rect AudioNodeGraph::param_region() const { return { x0_, y1_ - kParamBand, x1_ - x0_, kParamBand }; }
Rect AudioNodeGraph::graph_region() const { return { x0_, y0_, x1_ - x0_, (y1_ - kParamBand) - y0_ }; }

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
    for (int i = 0; i < n; ++i)
        out.push_back({ kind[i], id[i],
                        ox + rank[i] * (kCardW + kGapX) * scale, oy + slot[i] * (kCardH + kGapY) * scale,
                        kCardW * scale, kCardH * scale });
    return out;
}

std::vector<AudioParamCell> AudioNodeGraph::param_cells(int sel_node) const {
    std::vector<AudioParamCell> out;
    if (!s_ || sel_node < 0) return out;   // none selected
    const int pc = P::session_audio_graph_node_param_count(s_, track_, sel_node);
    if (pc <= 0) return out;
    const Rect pr = param_region();
    const float cellW = std::min(78.f, (pr.w - 12.f) / static_cast<float>(pc));
    out.reserve(pc);
    for (int i = 0; i < pc; ++i) {
        const float cx = pr.x + 6.f + i * cellW;
        AudioParamCell c;
        c.index = i; c.x = cx; c.y = pr.y + 4.f; c.w = cellW; c.h = pr.h - 8.f;
        c.knob_cx = cx + cellW * 0.5f; c.knob_cy = pr.y + 24.f; c.knob_r = 11.f;
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
        r.draw_rounded_rect(b.x, b.y, b.w, b.h, sty.radius,
                            sel ? sty.card_hi[0] : sty.card[0], sel ? sty.card_hi[1] : sty.card[1],
                            sel ? sty.card_hi[2] : sty.card[2], 1.0f);
        if (sel) r.draw_rect(b.x, b.y + b.h - 2.f, b.w, 2.f, sty.gold[0], sty.gold[1], sty.gold[2], 1.0f);
        r.draw_rect(b.x, b.y, 3.f, b.h, acc[0], acc[1], acc[2], 1.0f);
        const char* type = P::session_track_audio_graph_node_type(s_, track_, i);
        const char* label = (type && *type) ? type : (b.kind == 2 ? "Output" : "?");
        r.draw_text(b.x + 10.f, b.y + 7.f, label, sty.body[0], sty.body[1], sty.body[2], 1.0f, sty.fs_label);
        const char* tag = b.kind == 0 ? "instrument" : (b.kind == 1 ? "effect" : "output");
        r.draw_text(b.x + 10.f, b.y + b.h - 15.f, tag, acc[0], acc[1], acc[2], 0.9f, 0.7f);
        if (b.kind == 1) {   // effect: removable
            const Rect x = remove_rect(b);
            r.draw_text(x.x, x.y - 3.f, "\xC3\x97", 0.7f, 0.5f, 0.5f, 1.0f, sty.fs_label);
        }
        // Wire ports: an output dot (source; not on Output) and an input dot (target; not on inst).
        if (b.kind != 2) { const Rect p = out_port_rect(b);
            r.draw_rounded_rect(p.x + 2.f, p.y + 2.f, 8.f, 8.f, 4.f, sty.audio[0], sty.audio[1], sty.audio[2], 1.0f); }
        if (b.kind != 0) { const Rect p = in_port_rect(b);
            r.draw_rounded_rect(p.x + 2.f, p.y + 2.f, 8.f, 8.f, 4.f, sty.dim[0], sty.dim[1], sty.dim[2], 1.0f); }
    }

    // Ghost wire while dragging a rewire from a node's output port to the cursor.
    if (wire_from >= 0) {
        for (int i = 0; i < static_cast<int>(boxes.size()); ++i)
            if (id[i] == wire_from) { const Rect p = out_port_rect(boxes[i]);
                wire(r, p.x + 6.f, p.y + 6.f, cx, cy, sty.gold); break; }
    }

    // "+ FX" affordance.
    { const Rect a = add_button_rect();
      r.draw_rounded_rect(a.x, a.y, a.w, a.h, 4.f, sty.card[0], sty.card[1], sty.card[2], 1.0f);
      r.draw_text(a.x + 7.f, a.y + 1.f, "+ FX", sty.audio[0], sty.audio[1], sty.audio[2], 1.0f, sty.fs_label); }

    // Inline param strip for the selected node.
    const Rect pr = param_region();
    r.draw_rect(pr.x, pr.y, pr.w, 1.f, sty.border_soft[0], sty.border_soft[1], sty.border_soft[2], 1.0f);
    const std::vector<AudioParamCell> cells = param_cells(sel_node);
    if (cells.empty()) {
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
