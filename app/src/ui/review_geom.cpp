#include "ui/review_geom.h"

#include <algorithm>
#include <cmath>

namespace vivid::ui {

namespace {
constexpr float kLeftW      = 320.f;
constexpr float kGap        = 10.f;
constexpr float kBtnH       = 24.f;    // buttons
constexpr float kTileLabelH = 26.f;
constexpr float kActionsH   = 26.f;
// Style metrics mirrored from ui_style.h (renderer-free here): header strip, paddings, body line.
struct GeomStyle { float panel_hd_h = kPanelHdH, s3 = 6.f, s4 = kPanePad, fs_body = 0.88f; };
inline const GeomStyle& style() { static const GeomStyle g; return g; }
}  // namespace

Rect ReviewGeom::queue_row(int i) const {
    return { queue.x, queue.y + i * (queue_row_h + 4.f), queue.w, queue_row_h };
}
int ReviewGeom::tile_at(double mx, double my) const {
    for (std::size_t i = 0; i < tiles.size(); ++i)
        if (hit(tiles[i], mx, my) || hit(tile_labels[i], mx, my)) return static_cast<int>(i);
    return -1;
}
int ReviewGeom::queue_row_at(double mx, double my) const {
    for (int i = 0; i < queue_rows_visible; ++i) if (hit(queue_row(i), mx, my)) return i;
    return -1;
}

ReviewGeom review_geom(int win_w, int win_h, int n_sources, int n_queue, int n_since, int n_brief) {
    const GeomStyle& s = style();
    ReviewGeom g;
    const float W = static_cast<float>(win_w), H = static_cast<float>(win_h);
    const float m = kPaneMargin;
    const float top = kTopBarH + m;
    const float lh = 15.f * s.fs_body;   // approximate body line height for panel sizing
    // The left column narrows on small windows so the media column keeps a usable width.
    const float left_w = std::clamp(W * 0.3f, 220.f, kLeftW);
    g.left = { m, top, left_w, H - top - m };
    g.main = { m + left_w + kGap, top, W - (m + left_w + kGap) - m, H - top - m };

    // Left column: fixed-height panels, the queue takes the rest.
    float y = g.left.y;
    g.current = { g.left.x, y, g.left.w, s.panel_hd_h + 3.f * lh + 2.f * s.s4 };  y += g.current.h + kGap;
    const int since_rows = std::clamp(n_since, 1, 6);
    g.since   = { g.left.x, y, g.left.w, s.panel_hd_h + since_rows * lh + 2.f * s.s4 };  y += g.since.h + kGap;
    const int brief_rows = std::clamp(n_brief + 2, 2, 9);   // direction (2 lines) + protections/preferences
    g.brief   = { g.left.x, y, g.left.w, s.panel_hd_h + brief_rows * lh + 2.f * s.s4 };  y += g.brief.h + kGap;
    const float queue_h = std::max(80.f, g.left.y + g.left.h - y);
    g.queue   = { g.left.x, y, g.left.w, queue_h };
    // The queue panel's rows live inside its content rect.
    {
        const Rect inner{ g.queue.x + s.s4, g.queue.y + s.panel_hd_h + s.s3, g.queue.w - 2.f * s.s4, g.queue.h - s.panel_hd_h - 2.f * s.s3 };
        g.queue = inner;   // callers draw the panel from the outer rect they recompute; rows use inner
        g.queue_rows_visible = std::min(n_queue, static_cast<int>(std::floor((inner.h + 4.f) / (g.queue_row_h + 4.f))));
    }

    // Main column.
    y = g.main.y;
    g.status_strip = { g.main.x, y, g.main.w, 24.f };                     y += g.status_strip.h + kGap;
    g.header       = { g.main.x, y, g.main.w, 52.f };                     y += g.header.h + kGap;
    // Source tiles: side by side, 16:9, capped so the actions + comment fit below.
    const int n = std::max(1, n_sources);
    const float tile_gap = kGap;
    const float avail_w = g.main.w - (n - 1) * tile_gap;
    float tile_w = std::floor(avail_w / n);
    // Everything that must fit under the tiles (the feedback list takes whatever is left, possibly 0).
    const float reserved_below = kGap + kBtnH + kGap + kActionsH + kGap + kBtnH + kGap + kBtnH + kGap;
    const float max_tile_h = std::max(90.f, g.main.y + g.main.h - y - kTileLabelH - reserved_below);
    float tile_h = std::floor(tile_w * 9.f / 16.f);
    if (tile_h > max_tile_h) { tile_h = max_tile_h; tile_w = std::floor(tile_h * 16.f / 9.f); }
    const float row_w = n * tile_w + (n - 1) * tile_gap;
    const float x0 = g.main.x + std::floor((g.main.w - row_w) * 0.5f);
    for (int i = 0; i < n; ++i) {
        const float x = x0 + i * (tile_w + tile_gap);
        g.tiles.push_back({ x, y, tile_w, tile_h });
        g.tile_labels.push_back({ x, y + tile_h, tile_w, kTileLabelH });
    }
    y += tile_h + kTileLabelH + kGap;
    // Transport row: play · scrub · position · loop · level.
    {   // Button widths scale with the column so the row never overflows; the scrub takes the rest.
        const float x = g.main.x;
        const float bw = std::clamp(std::floor((g.main.w - 28.f - 4.f * kGap - 60.f) / 3.f), 64.f, 118.f);
        g.play      = { x, y, 28.f, kBtnH };
        g.level_btn = { g.main.x + g.main.w - bw, y, bw, kBtnH };
        g.loop_btn  = { g.level_btn.x - kGap - bw, y, bw, kBtnH };
        g.position  = { g.loop_btn.x - kGap - bw, y, bw, kBtnH };
        g.scrub     = { x + 28.f + kGap, y + 6.f, std::max(40.f, g.position.x - (x + 28.f + 2.f * kGap)), kBtnH - 12.f };
    }
    y += kBtnH + kGap;
    // Per-source actions under each tile (baseline gets none).
    for (int i = 0; i < n; ++i) {
        const Rect t = g.tiles[static_cast<std::size_t>(i)];
        const float bw = std::floor((t.w - 3.f * 4.f) / 4.f);
        g.act_use.push_back    ({ t.x,                  y, bw, kActionsH });
        g.act_keep.push_back   ({ t.x + (bw + 4.f),     y, bw, kActionsH });
        g.act_revise.push_back ({ t.x + 2 * (bw + 4.f), y, bw, kActionsH });
        g.act_dismiss.push_back({ t.x + 3 * (bw + 4.f), y, t.w - 3 * (bw + 4.f), kActionsH });
    }
    y += kActionsH + kGap;
    const float half = std::floor((g.main.w - kGap) * 0.5f);
    g.neither     = { g.main.x, y, std::min(200.f, half), kBtnH };
    g.open_create = { g.main.x + g.main.w - std::min(160.f, half), y, std::min(160.f, half), kBtnH };
    y += kBtnH + kGap;
    const float send_w = std::min(150.f, std::floor(g.main.w * 0.35f));
    g.comment_send = { g.main.x + g.main.w - send_w, y, send_w, kBtnH };
    g.comment_box  = { g.main.x, y, g.main.w - send_w - kGap, kBtnH };
    y += kBtnH + kGap;
    g.feedback = { g.main.x, y, g.main.w, std::max(0.f, g.main.y + g.main.h - y) };
    return g;
}

}  // namespace vivid::ui
