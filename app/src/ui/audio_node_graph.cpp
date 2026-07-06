#include "ui/audio_node_graph.h"
#include "ui/ui_style.h"
#include "audio/vst3_host.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

namespace vivid::ui {

namespace {
namespace P = vivid::session;
constexpr float kCardW = 108.f, kCardH = 44.f, kGapX = 46.f, kGapY = 16.f, kPad = 14.f;

// A cubic Bézier wire with horizontal tangents (matches the visuals graph's wire look).
void wire(Renderer2D& r, float x0, float y0, float x1, float y1, const float* c) {
    constexpr int N = 24;
    float xs[N], ys[N];
    float dx = std::max(std::fabs(x1 - x0) * 0.5f, 22.f);
    const float c1x = x0 + dx, c2x = x1 - dx;
    for (int i = 0; i < N; ++i) {
        const float t = i / float(N - 1), u = 1.f - t;
        xs[i] = u*u*u*x0 + 3*u*u*t*c1x + 3*u*t*t*c2x + t*t*t*x1;
        ys[i] = u*u*u*y0 + 3*u*u*t*y0  + 3*u*t*t*y1  + t*t*t*y1;
    }
    r.draw_polyline(xs, ys, N, 2.0f, c[0], c[1], c[2], 0.9f);
}
}  // namespace

void AudioNodeGraph::draw(Renderer2D& r) const {
    if (!s_ || track_ < 0) return;
    const Style& sty = style();
    const float rw = x1_ - x0_, rh = y1_ - y0_;
    if (rw < 20.f || rh < 20.f) return;

    if (!P::session_track_audio_graph_ok(s_, track_)) {
        r.draw_text(x0_ + 4.f, y0_ + 6.f,
                    "No native audio graph for this track (a VST3 or audio track runs the inline path).",
                    sty.dim[0], sty.dim[1], sty.dim[2], 1.0f, sty.fs_label);
        r.draw_text(x0_ + 4.f, y0_ + 24.f,
                    "The graph appears for tracks whose instrument + effects are native operators.",
                    sty.dim[0], sty.dim[1], sty.dim[2], 0.8f, sty.fs_label);
        return;
    }

    const int n = P::session_track_audio_graph_node_count(s_, track_);
    if (n <= 0) return;
    std::vector<int> id(n), kind(n), rank(n, 0), slot(n, 0);
    std::vector<std::string> type(n);
    for (int i = 0; i < n; ++i) {
        id[i]   = P::session_track_audio_graph_node_id(s_, track_, i);
        kind[i] = P::session_track_audio_graph_node_kind(s_, track_, i);   // 0 inst / 1 fx / 2 out
        const char* t = P::session_track_audio_graph_node_type(s_, track_, i);
        type[i] = (t && *t) ? t : (kind[i] == 2 ? "Output" : "?");
    }
    auto index_of = [&](int nid) { for (int i = 0; i < n; ++i) if (id[i] == nid) return i; return -1; };

    // Rank by longest path (DAG → converges in <= n relaxations); output sits at the far right.
    const int ne = P::session_track_audio_graph_edge_count(s_, track_);
    std::vector<int> ef(ne), et(ne);
    for (int e = 0; e < ne; ++e) {
        ef[e] = index_of(P::session_track_audio_graph_edge_from(s_, track_, e));
        et[e] = index_of(P::session_track_audio_graph_edge_to(s_, track_, e));
    }
    for (int pass = 0; pass < n; ++pass)
        for (int e = 0; e < ne; ++e)
            if (ef[e] >= 0 && et[e] >= 0) rank[et[e]] = std::max(rank[et[e]], rank[ef[e]] + 1);
    int max_rank = 0;
    for (int i = 0; i < n; ++i) max_rank = std::max(max_rank, rank[i]);
    for (int i = 0; i < n; ++i) if (kind[i] == 2) rank[i] = max_rank;   // pin the sink to the last column

    // Slot (vertical index) within each column.
    std::vector<int> col_fill(max_rank + 1, 0);
    int max_slots = 1;
    for (int i = 0; i < n; ++i) { slot[i] = col_fill[rank[i]]++; max_slots = std::max(max_slots, col_fill[rank[i]]); }

    // World extent -> fit into the region (never upscale past 1:1; keep a margin).
    const float world_w = max_rank * (kCardW + kGapX) + kCardW;
    const float world_h = max_slots * kCardH + (max_slots - 1) * kGapY;
    const float scale = std::min({ (rw - 2 * kPad) / world_w, (rh - 2 * kPad) / world_h, 1.0f });
    const float cw = kCardW * scale, ch = kCardH * scale;
    const float ox = x0_ + (rw - world_w * scale) * 0.5f;
    const float oy = y0_ + (rh - world_h * scale) * 0.5f;
    auto node_x = [&](int i) { return ox + rank[i] * (kCardW + kGapX) * scale; };
    auto node_y = [&](int i) { return oy + slot[i] * (kCardH + kGapY) * scale; };

    // Wires first (behind the cards): source out-port -> dest in-port.
    for (int e = 0; e < ne; ++e) {
        if (ef[e] < 0 || et[e] < 0) continue;
        wire(r, node_x(ef[e]) + cw, node_y(ef[e]) + ch * 0.5f,
                node_x(et[e]),      node_y(et[e]) + ch * 0.5f, sty.audio);
    }

    // Cards: kind-colored accent (instrument amber / effect violet / output gray) + op name.
    for (int i = 0; i < n; ++i) {
        const float* acc = kind[i] == 0 ? sty.audio : (kind[i] == 1 ? sty.fx : sty.control);
        const float x = node_x(i), y = node_y(i);
        r.draw_rounded_rect(x, y, cw, ch, sty.radius, sty.card[0], sty.card[1], sty.card[2], 1.0f);
        r.draw_rect(x, y, 3.f, ch, acc[0], acc[1], acc[2], 1.0f);                 // left accent
        r.draw_text(x + 10.f, y + 7.f, type[i].c_str(), sty.body[0], sty.body[1], sty.body[2], 1.0f, sty.fs_label);
        const char* tag = kind[i] == 0 ? "instrument" : (kind[i] == 1 ? "effect" : "output");
        r.draw_text(x + 10.f, y + ch - 15.f, tag, acc[0], acc[1], acc[2], 0.9f, 0.7f);
    }
}

}  // namespace vivid::ui
