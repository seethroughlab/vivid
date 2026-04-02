#include "euclidean_core.h"
#include "operator_api/thumbnail.h"
#include <cstdio>
#include <algorithm>

// Recompute the Euclidean pattern from params for thumbnail rendering.
// This avoids relying on the operator's internal pattern_ which may be
// on a different thread (audio) or not yet computed.
static void compute_pattern_for_thumb(int h, int n, int rot, int* pattern) {
    for (int i = 0; i < 32; ++i) pattern[i] = 0;
    if (n <= 0) return;
    h = std::clamp(h, 0, n);
    if (h == 0) return;
    if (h == n) {
        for (int i = 0; i < n; ++i) pattern[i] = 1;
        // rotate
        if (rot > 0) {
            rot = rot % n;
            int tmp[32];
            for (int i = 0; i < n; ++i) tmp[i] = pattern[(i + rot) % n];
            for (int i = 0; i < n; ++i) pattern[i] = tmp[i];
        }
        return;
    }

    int seqs[32][32];
    int slen[32];
    for (int i = 0; i < h; ++i)  { seqs[i][0] = 1; slen[i] = 1; }
    for (int i = h; i < n; ++i)  { seqs[i][0] = 0; slen[i] = 1; }

    int left = h;
    int right = n - h;
    while (right > 1) {
        int pairs = std::min(left, right);
        for (int i = 0; i < pairs; ++i) {
            int src = left + i;
            for (int j = 0; j < slen[src]; ++j)
                seqs[i][slen[i] + j] = seqs[src][j];
            slen[i] += slen[src];
        }
        if (left > right) {
            right = left - pairs;
            left = pairs;
        } else {
            int extra_start = left + pairs;
            int extra_count = right - pairs;
            for (int i = 0; i < extra_count; ++i) {
                int src = extra_start + i;
                int dst = pairs + i;
                for (int j = 0; j < slen[src]; ++j)
                    seqs[dst][j] = seqs[src][j];
                slen[dst] = slen[src];
            }
            right = right - pairs;
            left = pairs;
        }
    }
    int pos = 0;
    int total = left + right;
    for (int i = 0; i < total && pos < 32; ++i)
        for (int j = 0; j < slen[i] && pos < 32; ++j)
            pattern[pos++] = seqs[i][j];

    if (rot > 0) {
        rot = rot % n;
        if (rot > 0) {
            int tmp[32];
            for (int i = 0; i < n; ++i) tmp[i] = pattern[(i + rot) % n];
            for (int i = 0; i < n; ++i) pattern[i] = tmp[i];
        }
    }
}

void EuclideanCore::draw_thumbnail(const VividThumbnailContext* ctx) {
    if (!ctx || !ctx->draw.opaque) return;
    const auto& d = ctx->draw;
    void* o = d.opaque;

    // Read params: hits=0, steps=1, rotation=2
    int h   = (ctx->param_count > 0) ? std::clamp(static_cast<int>(ctx->param_values[0]), 0, 32) : 3;
    int n   = (ctx->param_count > 1) ? std::clamp(static_cast<int>(ctx->param_values[1]), 1, 32) : 8;
    int rot = (ctx->param_count > 2) ? std::clamp(static_cast<int>(ctx->param_values[2]), 0, 31) : 0;

    // Recompute pattern from params (thread-safe, no reliance on internal state)
    int pat[32] = {};
    compute_pattern_for_thumb(h, n, rot, pat);

    float cur_step = (ctx->output_count > 2) ? ctx->output_values[2] : 0.0f;
    float gate     = (ctx->output_count > 1) ? ctx->output_values[1] : 0.0f;
    int cur = static_cast<int>(cur_step);

    float w = static_cast<float>(ctx->thumbnail_logical_width ? ctx->thumbnail_logical_width : ctx->thumbnail_width);
    float height = static_cast<float>(ctx->thumbnail_logical_height ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

    // Dark background
    d.draw_rect(o, 0, 0, w, height, {0.08f, 0.08f, 0.1f, 0.9f});

    // Label: "hits/steps"
    char label[16];
    std::snprintf(label, sizeof(label), "%d/%d", h, n);
    d.draw_text(o, 6, 3, label, {0.45f, 0.55f, 0.65f, 1.0f}, 1.0f);

    // Cell grid
    float margin_x = 6.0f;
    float top_y = 24.0f;
    float bot_pad = 6.0f;
    float cell_area_w = w - 2 * margin_x;
    float cell_h = height - top_y - bot_pad;
    float gap = (n <= 16) ? 2.0f : 1.0f;
    float cell_w = (cell_area_w - gap * (n - 1)) / n;
    cell_w = std::min(cell_w, 16.0f);
    float cr = std::min(2.0f, cell_w * 0.2f);

    // Center the grid
    float grid_w = cell_w * n + gap * (n - 1);
    float grid_x0 = margin_x + (cell_area_w - grid_w) * 0.5f;

    VividColor accent = {0.45f, 0.55f, 0.65f, 1.0f};
    VividColor dim    = {0.15f, 0.17f, 0.2f, 1.0f};

    for (int i = 0; i < n; ++i) {
        bool is_hit = pat[i] != 0;
        bool is_current = (i == cur);
        bool is_gating = is_current && (gate > 0.5f);
        float cx = grid_x0 + i * (cell_w + gap);

        // Current step highlight border
        if (is_current) {
            d.draw_rounded_rect(o, cx - 1.5f, top_y - 1.5f, cell_w + 3, cell_h + 3, cr + 1,
                                {accent.r * 1.3f, accent.g * 1.3f, accent.b * 1.3f, 0.9f});
        }

        VividColor fill;
        if (is_hit) {
            float a = is_gating ? 1.0f : 0.55f;
            fill = {accent.r, accent.g, accent.b, a};
        } else {
            float a = is_current ? 0.4f : 0.2f;
            fill = {dim.r, dim.g, dim.b, a};
        }

        d.draw_rounded_rect(o, cx, top_y, cell_w, cell_h, cr, fill);
    }
}
