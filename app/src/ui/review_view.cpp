#include "ui/review_view.h"
#include "ui/editor_controls.h"
#include "ui/ui_style.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vivid::ui {

namespace {
constexpr float kGap = 10.f;

void button(Renderer2D& r, Rect b, const char* label, bool hot, bool enabled = true, const float* accent = nullptr, bool selected = false) {
    const Style& s = style();
    const float* bg = !enabled ? s.recess : ((hot || selected) ? s.card_hi : s.card);
    r.draw_rect(b.x, b.y, b.w, b.h, bg[0], bg[1], bg[2], 1.0f);
    const float* fr = selected ? s.sel : (hot && enabled ? s.border : s.border_soft);
    r.draw_rect_outline(b.x, b.y, b.w, b.h, selected ? 2.f : 1.f, fr[0], fr[1], fr[2], 1.0f);
    const float* tc = !enabled ? s.dim : (accent ? accent : (hot ? s.text : s.body));
    control_text(r, b, label, s.fs_label, tc, enabled ? 1.0f : 0.6f);
}

// Wrapped body text inside `b`; returns the y just below the last line drawn.
float paragraph(Renderer2D& r, Rect b, const std::string& text, const float* c, float scale, int max_lines = 100) {
    const Style& s = style();
    const float lh = r.line_height() * scale;
    float y = b.y;
    int n = 0;
    for (const auto& line : r.wrap_text(text.c_str(), b.w, scale)) {
        if (n++ >= max_lines || y + lh > b.y + b.h + 0.5f) break;
        r.draw_text(b.x, y, line.c_str(), c[0], c[1], c[2], 1.0f, scale);
        y += lh;
    }
    (void)s;
    return y;
}
}  // namespace

void draw_workspace_switch(Renderer2D& r, bool review_selected, int pending, double mx, double my) {
    const Style& s = style();
    const Rect b = workspace_switch_rect();
    const int hot = segmented_hit(b, 2, mx, my);
    segmented(r, b, { "Create", "Review" }, review_selected ? 1 : 0, hot, review_selected ? s.gold : nullptr);
    if (pending > 0) {   // a count badge on the Review segment: pending decisions, never an auto-switch
        char t[8]; std::snprintf(t, sizeof t, "%d", std::min(pending, 99));
        const float bw = 16.f;
        const Rect badge{ b.x + b.w - bw - 4.f, b.y + 2.f, bw, b.h - 4.f };
        r.draw_rounded_rect(badge.x, badge.y, badge.w, badge.h, 3.f, s.gold[0], s.gold[1], s.gold[2], 1.0f);
        control_text(r, badge, t, s.fs_kicker, s.bg);
    }
}

void draw_review(Renderer2D& r, const ReviewModel& m, const ReviewGeom& g, double mx, double my, double now) {
    const Style& s = style();
    const float lh = r.line_height() * s.fs_body;
    // Canvas.
    r.draw_rect(0.f, kTopBarH, static_cast<float>(g.main.x + g.main.w + kPaneMargin), static_cast<float>(g.main.y + g.main.h + kPaneMargin) - kTopBarH,
                s.bg[0], s.bg[1], s.bg[2], 1.0f);

    // ---------------- left column ----------------
    {   // Current version
        const Rect in = panel(r, g.current, "CURRENT VERSION", s.gold);
        r.draw_text(in.x, in.y, m.preferred_label.c_str(), s.text[0], s.text[1], s.text[2], 1.0f, s.fs_body);
        r.draw_text(in.x, in.y + lh, m.preferred_sub.c_str(), s.dim[0], s.dim[1], s.dim[2], 1.0f, s.fs_label);
        r.draw_text(in.x, in.y + 2.f * lh, m.project_name.c_str(), s.body[0], s.body[1], s.body[2], 1.0f, s.fs_label);
    }
    {   // Since your last review
        const Rect in = panel(r, g.since, "SINCE YOUR LAST REVIEW", s.teal);
        if (m.since_lines.empty())
            r.draw_text(in.x, in.y, "Nothing new.", s.dim[0], s.dim[1], s.dim[2], 1.0f, s.fs_label);
        float y = in.y;
        for (std::size_t i = 0; i < m.since_lines.size() && i < 6; ++i, y += lh) {
            const std::string t = fit_text(r, m.since_lines[i], in.w, s.fs_label);
            r.draw_text(in.x, y, t.c_str(), s.body[0], s.body[1], s.body[2], 1.0f, s.fs_label);
        }
    }
    {   // Direction & protections
        const Rect in = panel(r, g.brief, m.brief_rev > 0 ? "DIRECTION & PROTECTIONS" : "DIRECTION & PROTECTIONS — NONE YET", s.audio);
        float y = in.y;
        if (m.brief_direction.empty()) {
            r.draw_text(in.x, y, "No brief recorded for this project.", s.dim[0], s.dim[1], s.dim[2], 1.0f, s.fs_label);
        } else {
            y = paragraph(r, { in.x, y, in.w, 2.f * lh }, m.brief_direction, s.text, s.fs_label, 2);
            for (const auto& line : m.brief_lines) {
                if (y + lh > in.y + in.h + 0.5f) break;
                const float* c = line.is_preference ? s.body : s.audio;
                const char* glyph = line.is_preference ? "~" : (line.hard ? "\xE2\x9B\x94" : "\xE2\x97\x8B");   // ⛔ / ○
                r.draw_text(in.x, y, glyph, c[0], c[1], c[2], 1.0f, s.fs_label);
                const std::string t = fit_text(r, line.text, in.w - 18.f, s.fs_label);
                r.draw_text(in.x + 18.f, y, t.c_str(), c[0], c[1], c[2], 1.0f, s.fs_label);
                y += lh;
            }
        }
    }
    {   // Decisions (the queue)
        const Rect outer{ g.left.x, g.queue.y - s.panel_hd_h - s.s3, g.left.w, g.queue.h + s.panel_hd_h + 2.f * s.s3 };
        char title[48]; std::snprintf(title, sizeof title, "DECISIONS  %d", static_cast<int>(m.queue.size()));
        panel(r, outer, title, s.gold);
        if (m.queue.empty())
            r.draw_text(g.queue.x, g.queue.y, "No decision is waiting.", s.dim[0], s.dim[1], s.dim[2], 1.0f, s.fs_label);
        r.push_clip_rect(g.queue.x, g.queue.y, g.queue.w, g.queue.h);
        for (int i = 0; i < g.queue_rows_visible; ++i) {
            const auto& row = m.queue[static_cast<std::size_t>(i)];
            const Rect b = g.queue_row(i);
            const bool hot = hit(b, mx, my);
            item_box(r, b, row.resolved ? s.dim : s.gold, hot, row.selected);
            const std::string q = fit_text(r, row.question, b.w - 14.f, s.fs_label);
            r.draw_text(b.x + 8.f, b.y + 4.f, q.c_str(), s.text[0], s.text[1], s.text[2], 1.0f, s.fs_label);
            r.draw_text(b.x + 8.f, b.y + 4.f + lh * 0.95f, row.meta.c_str(), s.dim[0], s.dim[1], s.dim[2], 1.0f, s.fs_kicker);
        }
        r.pop_clip_rect();
    }

    // ---------------- main column ----------------
    {   // Work status strip: honest about what is (not) running.
        const Rect b = g.status_strip;
        r.draw_rect(b.x, b.y, b.w, b.h, s.region[0], s.region[1], s.region[2], 1.0f);
        r.draw_rect_outline(b.x, b.y, b.w, b.h, 1.f, s.border_soft[0], s.border_soft[1], s.border_soft[2], 1.0f);
        const float* dot = m.work_status == "working" ? s.green : m.work_status == "ready" ? s.gold : s.dim;
        r.draw_rounded_rect(b.x + 8.f, b.y + 8.f, 8.f, 8.f, 2.f, dot[0], dot[1], dot[2], 1.0f);
        char t[200]; std::snprintf(t, sizeof t, "Background work: %s \xE2\x80\x94 %s", m.work_status.c_str(), m.work_status_reason.c_str());
        r.draw_text(b.x + 22.f, b.y + 5.f, t, s.body[0], s.body[1], s.body[2], 1.0f, s.fs_label);
        if (!m.notice.empty())
            draw_text_r(r, b.x + b.w - 8.f, b.y + 5.f, fit_text(r, m.notice, b.w * 0.55f, s.fs_label).c_str(), s.gold, 1.0f, s.fs_label);
    }
    if (!m.has_item) {
        const Rect b{ g.main.x, g.header.y, g.main.w, 120.f };
        panel(r, b, "REVIEW", s.gold);
        paragraph(r, { b.x + s.s4, b.y + s.panel_hd_h + s.s4, b.w - 2.f * s.s4, b.h - s.panel_hd_h - 2.f * s.s4 }, m.empty_reason, s.body, s.fs_body, 4);
        return;
    }
    const ReviewItemView& it = m.item;
    {   // Header: the one question + passage + status.
        const Rect b = g.header;
        r.draw_rect(b.x, b.y, b.w, b.h, s.region[0], s.region[1], s.region[2], 1.0f);
        r.draw_rect_outline(b.x, b.y, b.w, b.h, 1.f, s.border[0], s.border[1], s.border[2], 1.0f);
        r.draw_rect(b.x, b.y, 3.f, b.h, s.gold[0], s.gold[1], s.gold[2], 1.0f);
        const std::string q = fit_text(r, it.question, b.w - 24.f, s.fs_title);
        r.draw_text(b.x + 12.f, b.y + 6.f, q.c_str(), s.text[0], s.text[1], s.text[2], 1.0f, s.fs_title);
        std::string sub = it.passage;
        if (it.resolved) sub += "   \xC2\xB7   " + it.resolution;
        const float* sc = it.resolved ? s.gold : s.dim;
        r.draw_text(b.x + 12.f, b.y + 30.f, sub.c_str(), sc[0], sc[1], sc[2], 1.0f, s.fs_label);
    }
    // Source tiles.
    for (std::size_t i = 0; i < it.sources.size() && i < g.tiles.size(); ++i) {
        const auto& src = it.sources[i];
        const Rect t = g.tiles[i], lb = g.tile_labels[i];
        const bool hot = hit(t, mx, my) || hit(lb, mx, my);
        recess(r, t, true);
        if (src.tex) {
            // Letterbox the frame inside the tile.
            float w = t.w, h = t.w / src.aspect;
            if (h > t.h) { h = t.h; w = t.h * src.aspect; }
            r.draw_texture(t.x + (t.w - w) * 0.5f, t.y + (t.h - h) * 0.5f, w, h, src.tex);
        } else {
            const char* msg = !src.error.empty() ? src.error.c_str() : (src.ready ? "Press play" : "Loading\xE2\x80\xA6");
            control_text(r, t, msg, s.fs_label, !src.error.empty() ? s.red : s.dim);
        }
        // Audible ring.
        const float* fr = src.audible ? s.gold : (hot ? s.border : s.border_soft);
        r.draw_rect_outline(t.x, t.y, t.w, t.h, src.audible ? 2.f : 1.f, fr[0], fr[1], fr[2], 1.0f);
        // Label bar: name · sublabel · status; a speaker mark on the audible one.
        r.draw_rect(lb.x, lb.y, lb.w, lb.h, src.audible ? s.card_hi[0] : s.card[0], src.audible ? s.card_hi[1] : s.card[1], src.audible ? s.card_hi[2] : s.card[2], 1.0f);
        r.draw_rect_outline(lb.x, lb.y, lb.w, lb.h, 1.f, s.border_soft[0], s.border_soft[1], s.border_soft[2], 1.0f);
        char key[8]; std::snprintf(key, sizeof key, "%d", static_cast<int>(i) + 1);
        r.draw_text(lb.x + 6.f, lb.y + 6.f, key, s.dim[0], s.dim[1], s.dim[2], 1.0f, s.fs_kicker);
        std::string name = src.label;
        if (!src.sublabel.empty()) name += "  \xC2\xB7  " + src.sublabel;
        const std::string nm = fit_text(r, name, lb.w - 90.f, s.fs_label);
        r.draw_text(lb.x + 20.f, lb.y + 5.f, nm.c_str(), src.audible ? s.text[0] : s.body[0], src.audible ? s.text[1] : s.body[1], src.audible ? s.text[2] : s.body[2], 1.0f, s.fs_label);
        if (src.audible) draw_text_r(r, lb.x + lb.w - 6.f, lb.y + 5.f, "\xE2\x99\xAA audible", s.gold, 1.0f, s.fs_kicker);   // ♪
        else if (!src.status.empty() && src.status != "proposed")
            draw_text_r(r, lb.x + lb.w - 6.f, lb.y + 5.f, src.status.c_str(), src.status == "promoted" ? s.green : s.dim, 1.0f, s.fs_kicker);
    }
    {   // Transport.
        button(r, g.play, "", hit(g.play, mx, my), it.all_ready);
        {   // play / pause glyphs drawn as shapes (the UI font has no ⏸), matching the transport bar
            const float cx = g.play.x + g.play.w * 0.5f, cy = g.play.y + g.play.h * 0.5f;
            const float* c = it.all_ready ? (it.playing ? s.gold : s.green) : s.dim;
            if (it.playing) { r.draw_rect(cx - 5.f, cy - 6.f, 3.5f, 12.f, c[0], c[1], c[2], 1.0f); r.draw_rect(cx + 1.5f, cy - 6.f, 3.5f, 12.f, c[0], c[1], c[2], 1.0f); }
            else r.draw_tri(cx - 4.f, cy - 6.f, cx - 4.f, cy + 6.f, cx + 6.f, cy, c[0], c[1], c[2], 1.0f);
        }
        recess(r, g.scrub, true);
        if (it.looping) {
            const float lx = g.scrub.x + static_cast<float>(it.loop01_start) * g.scrub.w;
            const float lw = static_cast<float>(it.loop01_end - it.loop01_start) * g.scrub.w;
            r.draw_rect(lx, g.scrub.y, std::max(1.f, lw), g.scrub.h, s.gold[0] * 0.5f, s.gold[1] * 0.5f, s.gold[2] * 0.5f, 0.6f);
        }
        const float px = g.scrub.x + static_cast<float>(it.position01) * g.scrub.w;
        r.draw_rect(g.scrub.x, g.scrub.y, std::max(0.f, px - g.scrub.x), g.scrub.h, s.card_hi[0], s.card_hi[1], s.card_hi[2], 1.0f);
        r.draw_rect(px - 1.f, g.scrub.y - 3.f, 2.f, g.scrub.h + 6.f, s.roll_head[0], s.roll_head[1], s.roll_head[2], 1.0f);
        r.draw_text(g.position.x, g.position.y + 5.f, it.position_text.c_str(), s.body[0], s.body[1], s.body[2], 1.0f, s.fs_label);
        button(r, g.loop_btn, it.looping ? "Loop passage: on" : "Loop passage", hit(g.loop_btn, mx, my), true, nullptr, it.looping);
        // The level match is audition-only and says so (ADR-0064 §3): the label carries the word, the
        // status strip explains while it is on, and toggling off restores the files as rendered.
        button(r, g.level_btn, it.level_match ? "Match levels: on" : "Match levels", hit(g.level_btn, mx, my), it.level_match_available, nullptr, it.level_match);
    }
    // Actions (candidates only). Disabled once the review is resolved.
    for (std::size_t i = 0; i < it.sources.size() && i < g.act_use.size(); ++i) {
        const auto& src = it.sources[i];
        if (!src.is_candidate) continue;
        const bool en = !it.resolved && src.status != "dismissed" && src.status != "promoted";
        button(r, g.act_use[i],     "Use this",  hit(g.act_use[i], mx, my),     en, en ? s.green : nullptr);
        button(r, g.act_keep[i],    src.status == "kept" ? "Kept" : "Keep", hit(g.act_keep[i], mx, my), en && src.status != "kept");
        button(r, g.act_revise[i],  "Revise",    hit(g.act_revise[i], mx, my),  en);
        button(r, g.act_dismiss[i], "Dismiss",   hit(g.act_dismiss[i], mx, my), en);
    }
    button(r, g.neither, "Neither \xE2\x80\x94 dismiss all", hit(g.neither, mx, my), !it.resolved);
    button(r, g.open_create, "Open in Create \xE2\x86\x92", hit(g.open_create, mx, my));
    {   // Comment box.
        const Rect b = g.comment_box;
        recess(r, b, true);
        if (it.comment_active) r.draw_rect_outline(b.x, b.y, b.w, b.h, 1.f, s.sel[0], s.sel[1], s.sel[2], 1.0f);
        if (it.comment_buf.empty() && !it.comment_active)
            r.draw_text(b.x + 8.f, b.y + 5.f, "Comment on what you hear/see (Enter to save)\xE2\x80\xA6", s.dim[0], s.dim[1], s.dim[2], 1.0f, s.fs_label);
        else {
            std::string shown = it.comment_buf;
            if (it.comment_active && std::fmod(now, 1.0) < 0.5) shown += "|";
            // Show the tail when the text overflows.
            while (r.text_width(shown.c_str(), s.fs_label) > b.w - 16.f && shown.size() > 1) shown.erase(0, 1);
            r.draw_text(b.x + 8.f, b.y + 5.f, shown.c_str(), s.text[0], s.text[1], s.text[2], 1.0f, s.fs_label);
        }
        const std::string send = "Add comment " + it.comment_at;
        button(r, g.comment_send, send.c_str(), hit(g.comment_send, mx, my), !it.comment_buf.empty());
    }
    {   // This review's feedback.
        const Rect b = g.feedback;
        if (b.h > 30.f) {
            section_header(r, b.x, b.y, "FEEDBACK ON THIS REVIEW", s.teal);
            r.push_clip_rect(b.x, b.y + 16.f, b.w, b.h - 16.f);
            float y = b.y + 18.f;
            if (it.feedback.empty()) r.draw_text(b.x, y, "None yet.", s.dim[0], s.dim[1], s.dim[2], 1.0f, s.fs_label);
            for (const auto& f : it.feedback) {
                if (y + lh > b.y + b.h) break;
                const std::string head = f.when + "  " + f.where + (f.kind == "decision" ? "  [decision]" : "");
                r.draw_text(b.x, y, head.c_str(), s.dim[0], s.dim[1], s.dim[2], 1.0f, s.fs_kicker);
                const std::string t = fit_text(r, f.text, b.w - 220.f, s.fs_label);
                r.draw_text(b.x + 210.f, y, t.c_str(), s.text[0], s.text[1], s.text[2], 1.0f, s.fs_label);
                y += lh;
            }
            r.pop_clip_rect();
        }
    }
}

}  // namespace vivid::ui
