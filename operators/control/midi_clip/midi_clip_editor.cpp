#include "midi_clip_core.h"
#include "operator_api/editor_ui.h"
#include "operator_api/draw_ui_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <map>
#include <set>

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------
static constexpr float kHeaderH      = 38.0f;
static constexpr float kLoopBraceH   = 14.0f;  // strip between header and roll
static constexpr float kPianoW       = 52.0f;
static constexpr float kScrollW      = 12.0f;
static constexpr float kNumPitch     = 128.0f;
static constexpr float kModStripH    = 44.0f;
static constexpr float kModSepH      = 1.0f;
static constexpr float kHScrollH     = 8.0f;
static constexpr float kHScrollSep   = 1.0f;
static constexpr float kPitchBendMax = 12.0f;

// ---------------------------------------------------------------------------
// Scale helpers
// ---------------------------------------------------------------------------
static const int kScaleSizes[]     = {7, 7, 7, 5, 5};
static const int kScaleIntervals[][7] = {
    {0,2,4,5,7,9,11},   // Major
    {0,2,3,5,7,8,10},   // Natural Minor
    {0,2,3,5,7,8,11},   // Harmonic Minor
    {0,2,4,7,9,0,0},    // Pentatonic Major
    {0,3,5,7,10,0,0},   // Pentatonic Minor
};
static bool in_scale(int pitch, int root, int type) {
    if (root < 0) return false;
    int pc = ((pitch - root) % 12 + 12) % 12;
    for (int i = 0; i < kScaleSizes[type]; ++i)
        if (kScaleIntervals[type][i] == pc) return true;
    return false;
}
static bool is_root_note(int pitch, int root) {
    return root >= 0 && (pitch % 12) == (root % 12);
}

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static bool is_black_key(int pitch) {
    int pc = pitch % 12;
    return (pc == 1 || pc == 3 || pc == 6 || pc == 8 || pc == 10);
}

static int pitch_from_y(float gy, float scroll_y, float row_h) {
    int pitch = static_cast<int>((scroll_y + gy) / row_h);
    pitch = 127 - pitch;
    if (pitch < 0 || pitch > 127) return -1;
    return pitch;
}

static float y_from_pitch(int pitch, float scroll_y, float row_h) {
    return (127 - pitch) * row_h - scroll_y;
}

static double beat_from_x(float gx, float scroll_x, float beats_per_px) {
    return static_cast<double>(gx / beats_per_px + scroll_x);
}

// ---------------------------------------------------------------------------
// draw_editor
// ---------------------------------------------------------------------------
void MidiClipCore::draw_editor(VividEditorContext* ctx) {
    if (!ctx) return;

    auto& d   = ctx->draw;
    void* o   = d.opaque;
    const auto& th = ctx->theme;

    const float sw = ctx->surface_width;
    const float sh = ctx->surface_height;

    // --- Sync editor notes from string params ---
    const char* pat_str = (ctx->string_param_count > 0 && ctx->string_param_values)
        ? ctx->string_param_values[0] : "[]";
    if (!pat_str) pat_str = "[]";
    if (std::string(pat_str) != editor_submitted_str_) {
        editor_submitted_str_ = pat_str;
        midi_clip::parse_pattern(editor_submitted_str_, editor_notes_);
    }

    const float phase    = (ctx->output_count > 0) ? ctx->output_values[0] : 0.0f;
    const int   lb_idx   = (ctx->param_count > 0) ? static_cast<int>(ctx->param_values[0]) : 1;
    const int   grid_idx = (ctx->param_count > 1) ? static_cast<int>(ctx->param_values[1]) : 1;
    // Read loop region from struct members (already synced before draw_editor is called).
    const float loop_start_v = loop_start_beat.value;
    const float loop_end_v   = loop_end_beat.value;

    const int   num_bars   = midi_clip::bars_from_param(lb_idx);
    const int   bpb        = 4;
    const double pat_len   = static_cast<double>(num_bars * bpb);
    const double cell_beats = midi_clip::grid_cell_beats(grid_idx);

    // Resolve loop region for editor rendering
    const double ls_param = std::clamp(static_cast<double>(loop_start_v), 0.0, pat_len);
    const double le_param = std::clamp(static_cast<double>(loop_end_v),   0.0, pat_len);
    const bool   has_loop_region  = (le_param > ls_param + 1e-6);
    const double editor_loop_origin = has_loop_region ? ls_param : 0.0;
    const double editor_loop_len    = has_loop_region ? (le_param - ls_param) : pat_len;

    // --- Consume pending MIDI import ---
    if (has_pending_import_) {
        has_pending_import_ = false;
        float bpm = audio_bpm_.load(std::memory_order_relaxed);
        if (bpm < 1.0f) bpm = 120.0f;
        const double spb = 60.0 / bpm;

        auto seq = vivid::midi_file::parse_file(pending_import_path_);
        if (seq.ok() && !seq.events.empty()) {
            editor_notes_.clear();
            struct Active { double on_time; float velocity; };
            std::map<uint8_t, std::vector<Active>> active;
            for (const auto& ev : seq.events) {
                uint8_t kind   = ev.status & 0xF0u;
                uint8_t p      = ev.data1;
                bool    is_on  = (kind == 0x90u && ev.data2 > 0);
                bool    is_off = (kind == 0x80u || (kind == 0x90u && ev.data2 == 0));
                if (is_on) {
                    active[p].push_back({ev.time_seconds, ev.data2 / 127.0f});
                } else if (is_off && !active[p].empty()) {
                    auto& a = active[p].front();
                    double s = a.on_time / spb;
                    double d = (ev.time_seconds - a.on_time) / spb;
                    if (s < pat_len && d > 0.0) {
                        midi_clip::ParsedNote n{};
                        n.pitch = p; n.start_beat = s;
                        n.duration_beats = std::min(d, pat_len - s);
                        n.velocity = std::clamp(a.velocity, 0.01f, 1.0f);
                        editor_notes_.push_back(n);
                    }
                    active[p].erase(active[p].begin());
                }
            }
            for (auto& [p, stack] : active) {
                for (auto& a : stack) {
                    double s = a.on_time / spb;
                    if (s < pat_len) {
                        midi_clip::ParsedNote n{};
                        n.pitch = p; n.start_beat = s;
                        n.duration_beats = std::min(1.0, pat_len - s);
                        n.velocity = std::clamp(a.velocity, 0.01f, 1.0f);
                        editor_notes_.push_back(n);
                    }
                }
            }
            std::sort(editor_notes_.begin(), editor_notes_.end(),
                [](const auto& a, const auto& b) { return a.start_beat < b.start_beat; });
            commit_editor_notes(ctx);
            char msg[64];
            std::snprintf(msg, sizeof(msg), "Imported %zu notes",
                static_cast<size_t>(editor_notes_.size()));
            import_status_       = msg;
            import_status_until_ = ctx->time + 4.0;
        } else {
            import_status_       = seq.error.empty() ? "No notes found" : seq.error;
            import_status_until_ = ctx->time + 4.0;
        }
        if (ctx->commands.set_string_param)
            ctx->commands.set_string_param(ctx->commands.opaque, "midi_import", "");
        last_import_path_ = "";
    }

    // --- Layout ---
    // Loop brace strip sits between header and roll
    const float brace_y = kHeaderH;
    const float brace_h = kLoopBraceH;
    const float grid_x  = kPianoW;
    const float grid_y  = kHeaderH + kLoopBraceH;  // roll starts below brace
    const float grid_w  = sw - kPianoW - kScrollW;
    const float grid_h  = sh - grid_y;

    const float roll_h = grid_h - kModStripH * 3.0f - kModSepH * 3.0f - kHScrollH - kHScrollSep;
    const float vel_y  = grid_y + roll_h + kHScrollH + kHScrollSep + kModSepH;
    const float pb_y   = vel_y  + kModStripH + kModSepH;
    const float pr_y   = pb_y   + kModStripH + kModSepH;

    // --- Fold: build visible pitch list ---
    std::vector<int> folded_pitches;
    if (fold_rows_ && !editor_notes_.empty()) {
        std::set<int> used;
        for (const auto& n : editor_notes_) used.insert(static_cast<int>(n.pitch));
        std::set<int> expanded;
        for (int p : used) {
            if (p < 127) expanded.insert(p + 1);
            expanded.insert(p);
            if (p > 0)   expanded.insert(p - 1);
        }
        folded_pitches.assign(expanded.rbegin(), expanded.rend()); // high pitch = index 0
    }

    // Fold-aware pitch↔y converters
    auto y_fn = [&](int pitch) -> float {
        if (fold_rows_ && !folded_pitches.empty()) {
            auto it = std::find(folded_pitches.begin(), folded_pitches.end(), pitch);
            if (it == folded_pitches.end()) return -10000.0f;
            return static_cast<int>(it - folded_pitches.begin()) * row_h_ - scroll_y_;
        }
        return y_from_pitch(pitch, scroll_y_, row_h_);
    };
    auto p_fn = [&](float gy) -> int {
        if (fold_rows_ && !folded_pitches.empty()) {
            int idx = static_cast<int>((scroll_y_ + gy) / row_h_);
            if (idx < 0 || idx >= static_cast<int>(folded_pitches.size())) return -1;
            return folded_pitches[idx];
        }
        return pitch_from_y(gy, scroll_y_, row_h_);
    };

    // Dynamic content height
    auto get_content_h = [&]() -> float {
        return fold_rows_ && !folded_pitches.empty()
            ? static_cast<float>(folded_pitches.size()) * row_h_
            : kNumPitch * row_h_;
    };
    float content_h = get_content_h();

    // Mouse coords relative to grid area
    const float mx = ctx->mouse.x - grid_x;
    const float my = ctx->mouse.y - grid_y;
    const bool mouse_in_grid  = (ctx->mouse.x >= grid_x && ctx->mouse.x < grid_x + grid_w &&
                                  ctx->mouse.y >= grid_y && ctx->mouse.y < grid_y + grid_h);
    const bool mouse_in_roll  = (ctx->mouse.x >= grid_x && ctx->mouse.x < grid_x + grid_w &&
                                  ctx->mouse.y >= grid_y && ctx->mouse.y < grid_y + roll_h);
    const bool mouse_in_brace = (ctx->mouse.x >= grid_x && ctx->mouse.x < grid_x + grid_w &&
                                  ctx->mouse.y >= brace_y && ctx->mouse.y < brace_y + brace_h);
    const bool mouse_in_vel   = (ctx->mouse.x >= grid_x && ctx->mouse.x < grid_x + grid_w &&
                                  ctx->mouse.y >= vel_y   && ctx->mouse.y < vel_y   + kModStripH);
    const bool mouse_in_pb    = (ctx->mouse.x >= grid_x && ctx->mouse.x < grid_x + grid_w &&
                                  ctx->mouse.y >= pb_y    && ctx->mouse.y < pb_y    + kModStripH);
    const bool mouse_in_pr    = (ctx->mouse.x >= grid_x && ctx->mouse.x < grid_x + grid_w &&
                                  ctx->mouse.y >= pr_y    && ctx->mouse.y < pr_y    + kModStripH);

    // Zoom/scroll state — reset when pattern length changes
    if (lb_idx != last_lb_idx_) {
        last_lb_idx_       = lb_idx;
        editor_zoom_beats_ = 0.0f;
        editor_scroll_x_   = 0.0f;
    }
    const float full_beats = static_cast<float>(pat_len);
    float zoom_beats = (editor_zoom_beats_ > 0.0f) ? editor_zoom_beats_ : full_beats;
    zoom_beats       = std::clamp(zoom_beats, 4.0f, full_beats);
    editor_scroll_x_ = std::clamp(editor_scroll_x_, 0.0f,
                                   std::max(0.0f, full_beats - zoom_beats));
    const double beat_w  = (zoom_beats > 0.0f)
                           ? static_cast<double>(grid_w) / zoom_beats : 1.0;
    const double inv_bw  = (beat_w > 0.0) ? 1.0 / beat_w : 0.0;

    // Loop brace pixel positions (for both rendering and interaction)
    float loop_lx = grid_x + static_cast<float>((ls_param - editor_scroll_x_) * beat_w);
    float loop_rx = grid_x + static_cast<float>((le_param - editor_scroll_x_) * beat_w);
    // Clamped for rendering (actual positions used for interaction hit-test)
    const float loop_lx_draw = std::clamp(loop_lx, grid_x, grid_x + grid_w);
    const float loop_rx_draw = std::clamp(loop_rx, grid_x, grid_x + grid_w);

    // Mod strip hover
    auto find_nearest_note = [&](bool in_strip) -> int {
        if (!in_strip || drag_mode_ != DragMode::None) return -1;
        int best = -1; float best_dist = 10.0f;
        for (int i = 0; i < static_cast<int>(editor_notes_.size()); ++i) {
            float bx   = grid_x + static_cast<float>((editor_notes_[i].start_beat - editor_scroll_x_) * beat_w);
            float dist = std::fabs(ctx->mouse.x - bx);
            if (dist < best_dist) { best_dist = dist; best = i; }
        }
        return best;
    };
    const int hov_vel = find_nearest_note(mouse_in_vel);
    const int hov_pb  = find_nearest_note(mouse_in_pb);
    const int hov_pr  = find_nearest_note(mouse_in_pr);
    const int mod_hover_idx =
        (drag_mode_ == DragMode::VelocityDrag || drag_mode_ == DragMode::PitchBendDrag ||
         drag_mode_ == DragMode::PressureDrag) ? drag_note_idx_ :
        (hov_vel >= 0) ? hov_vel : (hov_pb >= 0) ? hov_pb : hov_pr;

    // -----------------------------------------------------------------------
    // Vertical scrollbar
    // -----------------------------------------------------------------------
    {
        const float sb_x = sw - kScrollW;
        const float sb_y = grid_y;
        const float sb_h = roll_h;

        d.draw_rect(o, sb_x, sb_y, kScrollW, sb_h, {0.1f, 0.1f, 0.12f, 1.0f});

        const float scroll_range = std::max(0.0f, content_h - roll_h);
        scroll_y_ = std::clamp(scroll_y_, 0.0f, scroll_range);

        if (scroll_range > 0.0f) {
            const float vis_frac  = roll_h / content_h;
            const float thumb_h   = std::max(20.0f, sb_h * vis_frac);
            const float travel    = sb_h - thumb_h;
            const float thumb_y   = sb_y + travel * (scroll_y_ / scroll_range);

            const vivid::ui::Rect thumb{sb_x + 1, thumb_y, kScrollW - 2, thumb_h};
            const bool thumb_hov = thumb.contains(ctx->mouse.x, ctx->mouse.y);
            d.draw_rounded_rect(o, thumb.x, thumb.y, thumb.w, thumb.h, 3.0f,
                {0.4f, 0.4f, 0.45f, thumb_hov ? 1.0f : 0.7f});

            if (thumb_hov && ctx->mouse.left_clicked) {
                scrollbar_dragging_          = true;
                scrollbar_drag_start_scroll_ = scroll_y_;
            }
            if (scrollbar_dragging_ && ctx->mouse.left_down) {
                const float drag_dy = ctx->mouse.y - ctx->mouse.prev_y;
                scroll_y_ = std::clamp(
                    scrollbar_drag_start_scroll_ + drag_dy * (scroll_range / travel),
                    0.0f, scroll_range);
            } else {
                scrollbar_dragging_ = false;
            }
        }

        // Scroll/zoom events
        for (uint32_t i = 0; i < ctx->event_count; ++i) {
            const auto& ev = ctx->events[i];
            if (ev.type == VIVID_EDITOR_EVENT_MOUSE_SCROLL && mouse_in_grid) {
                const bool cmd   = (ev.modifiers & 0x0008) != 0; // Cmd/⌘
                const bool shift = (ev.modifiers & 0x0001) != 0; // Shift
                const bool alt   = (ev.modifiers & 0x0004) != 0; // Option/Alt

                if (alt && ev.scroll_dy != 0.0f && mouse_in_roll) {
                    // Vertical zoom: keep pitch under cursor stationary
                    const int anchor = p_fn(my);
                    row_h_ = std::clamp(row_h_ * std::pow(1.2f, ev.scroll_dy), 6.0f, 40.0f);
                    content_h = get_content_h();
                    if (anchor >= 0 && !fold_rows_)
                        scroll_y_ = std::clamp((127 - anchor) * row_h_ - my,
                                               0.0f, std::max(0.0f, content_h - roll_h));
                } else if (cmd && ev.scroll_dy != 0.0f) {
                    // Horizontal zoom: keep beat under cursor stationary
                    const float bpp        = grid_w / zoom_beats;
                    const float mouse_beat = editor_scroll_x_ + mx / bpp;
                    const float factor     = std::pow(1.25f, ev.scroll_dy);
                    const float new_zoom   = std::clamp(zoom_beats / factor, 4.0f, full_beats);
                    const float new_bpp    = grid_w / new_zoom;
                    editor_zoom_beats_ = new_zoom;
                    editor_scroll_x_   = std::clamp(mouse_beat - mx / new_bpp,
                                                    0.0f, std::max(0.0f, full_beats - new_zoom));
                    zoom_beats = new_zoom;
                } else if (ev.scroll_dx != 0.0f || shift) {
                    // Horizontal pan
                    const float delta = (ev.scroll_dx != 0.0f ? -ev.scroll_dx : ev.scroll_dy)
                                        * zoom_beats * 0.05f;
                    editor_scroll_x_ = std::clamp(editor_scroll_x_ + delta,
                                                  0.0f, std::max(0.0f, full_beats - zoom_beats));
                } else {
                    // Vertical pitch scroll
                    scroll_y_ = std::clamp(scroll_y_ - ev.scroll_dy * row_h_ * 3.0f,
                                           0.0f, std::max(0.0f, content_h - roll_h));
                }
            }
        }
    }

    // -----------------------------------------------------------------------
    // Horizontal scrollbar
    // -----------------------------------------------------------------------
    {
        const float hsc_y = grid_y + roll_h + kHScrollSep;
        d.draw_rect(o, grid_x, hsc_y, grid_w, kHScrollH, {0.10f, 0.10f, 0.12f, 1.0f});
        if (zoom_beats < full_beats && full_beats > 0.0f) {
            const float vis_frac       = zoom_beats / full_beats;
            const float thumb_w        = std::max(20.0f, grid_w * vis_frac);
            const float travel         = grid_w - thumb_w;
            const float scroll_range_x = full_beats - zoom_beats;
            const float thumb_x        = grid_x + (scroll_range_x > 0.0f
                ? (editor_scroll_x_ / scroll_range_x) * travel : 0.0f);

            const vivid::ui::Rect thumb{thumb_x, hsc_y + 1.0f, thumb_w, kHScrollH - 2.0f};
            const bool thumb_hov = thumb.contains(ctx->mouse.x, ctx->mouse.y);
            d.draw_rounded_rect(o, thumb.x, thumb.y, thumb.w, thumb.h, 3.0f,
                {0.4f, 0.4f, 0.45f, thumb_hov ? 1.0f : 0.7f});

            if (thumb_hov && ctx->mouse.left_clicked) {
                hscroll_dragging_          = true;
                hscroll_drag_start_scroll_ = editor_scroll_x_;
                drag_start_mx_             = ctx->mouse.x;
            }
            if (hscroll_dragging_ && ctx->mouse.left_down) {
                const float total_dx = ctx->mouse.x - drag_start_mx_;
                editor_scroll_x_ = std::clamp(
                    hscroll_drag_start_scroll_ + total_dx * (scroll_range_x / travel),
                    0.0f, scroll_range_x);
            } else {
                hscroll_dragging_ = false;
            }
        }
    }

    // -----------------------------------------------------------------------
    // Background
    // -----------------------------------------------------------------------
    d.draw_rect(o, 0, 0, sw, sh, {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 1.0f});

    // -----------------------------------------------------------------------
    // Loop brace strip (between header and roll)
    // -----------------------------------------------------------------------
    {
        d.draw_rect(o, grid_x, brace_y, grid_w, brace_h, {0.08f, 0.08f, 0.10f, 1.0f});

        if (has_loop_region) {
            // Colored band
            if (loop_rx_draw > loop_lx_draw)
                d.draw_rect(o, loop_lx_draw, brace_y, loop_rx_draw - loop_lx_draw, brace_h,
                    {0.25f, 0.55f, 0.90f, 0.25f});
            // Left handle
            d.draw_rect(o, loop_lx_draw - 2.0f, brace_y, 4.0f, brace_h,
                {0.40f, 0.75f, 1.0f, 0.85f});
            // Right handle
            d.draw_rect(o, loop_rx_draw - 2.0f, brace_y, 4.0f, brace_h,
                {0.40f, 0.75f, 1.0f, 0.85f});
            // Start label
            char lbl[32];
            std::snprintf(lbl, sizeof(lbl), "%.2f", ls_param);
            d.draw_text(o, loop_lx_draw + 4.0f, brace_y + 1.0f, lbl,
                {0.7f, 0.85f, 1.0f, 0.75f}, 0.7f);
        } else {
            d.draw_text(o, grid_x + 4.0f, brace_y + 1.0f, "drag here to set loop region",
                {0.25f, 0.25f, 0.30f, 0.6f}, 0.7f);
        }

        // Playhead marker in brace
        const float pb_beat = static_cast<float>(editor_loop_origin + phase * editor_loop_len);
        const float pb_x    = grid_x + static_cast<float>((pb_beat - editor_scroll_x_) * beat_w);
        if (pb_x >= grid_x && pb_x <= grid_x + grid_w)
            d.draw_rect(o, pb_x, brace_y, 2.0f, brace_h, {1.0f, 0.8f, 0.2f, 0.7f});
    }

    // -----------------------------------------------------------------------
    // Piano roll clip region
    // -----------------------------------------------------------------------
    d.push_clip_rect(o, grid_x, grid_y, grid_w, roll_h);

    // --- Row backgrounds ---
    {
        if (fold_rows_ && !folded_pitches.empty()) {
            int top_row    = static_cast<int>(scroll_y_ / row_h_);
            int bottom_row = static_cast<int>((scroll_y_ + roll_h) / row_h_) + 1;
            top_row    = std::clamp(top_row, 0, static_cast<int>(folded_pitches.size()) - 1);
            bottom_row = std::clamp(bottom_row, 0, static_cast<int>(folded_pitches.size()));
            for (int ri = top_row; ri < bottom_row; ++ri) {
                int p  = folded_pitches[ri];
                float ry = y_fn(p) + grid_y;
                if (ry + row_h_ < grid_y || ry > grid_y + roll_h) continue;
                bool black = is_black_key(p);
                d.draw_rect(o, grid_x, ry, grid_w, row_h_,
                    black ? VividColor{0.10f, 0.10f, 0.11f, 1.0f}
                          : VividColor{0.14f, 0.14f, 0.16f, 1.0f});
                if (is_root_note(p, scale_root_))
                    d.draw_rect(o, grid_x, ry, grid_w, row_h_, {0.35f, 0.55f, 0.35f, 0.18f});
                else if (in_scale(p, scale_root_, scale_type_))
                    d.draw_rect(o, grid_x, ry, grid_w, row_h_, {0.25f, 0.40f, 0.25f, 0.10f});
                d.draw_rect(o, grid_x, ry + row_h_ - 1.0f, grid_w, 1.0f,
                    {0.08f, 0.08f, 0.09f, 1.0f});
            }
        } else {
            const int top_pitch    = 127 - static_cast<int>(scroll_y_ / row_h_);
            const int bottom_pitch = 127 - static_cast<int>((scroll_y_ + roll_h) / row_h_) - 1;
            for (int p = std::max(0, bottom_pitch - 1); p <= std::min(127, top_pitch + 1); ++p) {
                float ry = y_fn(p) + grid_y;
                if (ry + row_h_ < grid_y || ry > grid_y + roll_h) continue;
                bool black = is_black_key(p);
                d.draw_rect(o, grid_x, ry, grid_w, row_h_,
                    black ? VividColor{0.10f, 0.10f, 0.11f, 1.0f}
                          : VividColor{0.14f, 0.14f, 0.16f, 1.0f});
                if (is_root_note(p, scale_root_))
                    d.draw_rect(o, grid_x, ry, grid_w, row_h_, {0.35f, 0.55f, 0.35f, 0.18f});
                else if (in_scale(p, scale_root_, scale_type_))
                    d.draw_rect(o, grid_x, ry, grid_w, row_h_, {0.25f, 0.40f, 0.25f, 0.10f});
                d.draw_rect(o, grid_x, ry + row_h_ - 1.0f, grid_w, 1.0f,
                    {0.08f, 0.08f, 0.09f, 1.0f});
            }
        }
    }

    // --- Beat / bar grid lines ---
    {
        int total_cells = static_cast<int>(std::round(pat_len / cell_beats));
        for (int c = 0; c <= total_cells; ++c) {
            double beat = c * cell_beats;
            float  lx   = grid_x + static_cast<float>((beat - editor_scroll_x_) * beat_w);
            if (lx < grid_x - 1.0f || lx > grid_x + grid_w + 1.0f) continue;
            bool   is_bar = (std::fmod(beat, 4.0) < 1e-6);
            d.draw_rect(o, lx, grid_y, 1.0f, roll_h,
                {th.separator.r, th.separator.g, th.separator.b, is_bar ? 0.4f : 0.15f});
        }
    }

    // --- Loop region shading in roll ---
    if (has_loop_region) {
        // Dim area before loop start
        if (loop_lx_draw > grid_x)
            d.draw_rect(o, grid_x, grid_y, loop_lx_draw - grid_x, roll_h,
                {0.0f, 0.0f, 0.0f, 0.20f});
        // Dim area after loop end
        if (loop_rx_draw < grid_x + grid_w)
            d.draw_rect(o, loop_rx_draw, grid_y, (grid_x + grid_w) - loop_rx_draw, roll_h,
                {0.0f, 0.0f, 0.0f, 0.20f});
    }

    // --- Render notes ---
    {
        for (int idx = 0; idx < static_cast<int>(editor_notes_.size()); ++idx) {
            const auto& n = editor_notes_[idx];
            float ny  = y_fn(static_cast<int>(n.pitch)) + grid_y;
            float nx  = grid_x + static_cast<float>((n.start_beat - editor_scroll_x_) * beat_w);
            float nw  = std::max(4.0f, static_cast<float>(n.duration_beats * beat_w) - 1.0f);

            if (ny + row_h_ < grid_y || ny > grid_y + roll_h) continue;
            if (nx + nw < grid_x || nx > grid_x + grid_w) continue;

            bool active   = (idx == drag_note_idx_ && drag_mode_ != DragMode::None);
            bool strip_hl = (!active && idx == mod_hover_idx);
            d.draw_rounded_rect(o, nx, ny + 1.5f, nw, row_h_ - 3.0f, 2.0f,
                active   ? VividColor{0.55f, 0.80f, 1.0f, 1.0f}
                : strip_hl ? VividColor{0.42f, 0.72f, 1.0f, 0.97f}
                           : VividColor{0.30f, 0.65f, 0.95f, 0.92f});
            d.draw_rounded_rect(o, nx, ny + 1.5f, nw, row_h_ - 3.0f, 2.0f,
                active   ? VividColor{0.80f, 0.95f, 1.0f, 0.60f}
                : strip_hl ? VividColor{0.70f, 0.90f, 1.0f, 0.50f}
                           : VividColor{0.50f, 0.80f, 1.0f, 0.40f});
            if (nw > 10.0f) {
                d.draw_rounded_rect(o, nx + nw - 4.0f, ny + 2.5f, 3.0f, row_h_ - 5.0f, 1.0f,
                    {0.75f, 0.90f, 1.0f, (active || strip_hl) ? 0.9f : 0.5f});
            }
        }
    }

    // --- Playhead (roll) ---
    {
        const float phase_beat = static_cast<float>(editor_loop_origin + phase * editor_loop_len);
        const float phx = grid_x + static_cast<float>((phase_beat - editor_scroll_x_) * beat_w);
        if (phx >= grid_x && phx <= grid_x + grid_w)
            d.draw_rect(o, phx, grid_y, 2.0f, roll_h, {1.0f, 0.8f, 0.2f, 0.85f});
    }

    d.pop_clip_rect(o);

    // -----------------------------------------------------------------------
    // Mod strips
    // -----------------------------------------------------------------------
    {
        const float phase_beat_m = static_cast<float>(editor_loop_origin + phase * editor_loop_len);
        const float phx      = grid_x + static_cast<float>((phase_beat_m - editor_scroll_x_) * beat_w);
        const float inner_h  = kModStripH - 4.0f;
        const float ph_alpha = 0.55f;

        auto draw_strip_bg = [&](float sy, const char* label) {
            d.draw_rect(o, grid_x, sy - kModSepH, grid_w, kModSepH,
                {th.separator.r, th.separator.g, th.separator.b, 0.5f});
            d.draw_rect(o, grid_x, sy, grid_w, kModStripH, {0.08f, 0.08f, 0.10f, 1.0f});
            d.draw_text(o, grid_x + 4.0f, sy + 2.0f, label,
                {0.40f, 0.40f, 0.45f, 0.8f}, 0.75f);
        };

        // Velocity
        draw_strip_bg(vel_y, "vel");
        d.push_clip_rect(o, grid_x, vel_y, grid_w, kModStripH);
        for (int i = 0; i < static_cast<int>(editor_notes_.size()); ++i) {
            const auto& n = editor_notes_[i];
            float bx  = grid_x + static_cast<float>((n.start_beat - editor_scroll_x_) * beat_w);
            float bh  = std::max(2.0f, n.velocity * inner_h);
            float by  = vel_y + kModStripH - bh;
            bool  hot = (i == hov_vel) ||
                        (i == drag_note_idx_ && drag_mode_ == DragMode::VelocityDrag);
            d.draw_rect(o, bx - 2.0f, by, 5.0f, bh,
                hot ? VividColor{0.80f, 0.92f, 1.0f, 1.0f}
                    : VividColor{0.40f, 0.65f, 0.85f, 0.85f});
        }
        d.draw_rect(o, phx, vel_y, 2.0f, kModStripH, {1.0f, 0.8f, 0.2f, ph_alpha});
        d.pop_clip_rect(o);

        // Pitch bend
        draw_strip_bg(pb_y, "bend");
        d.push_clip_rect(o, grid_x, pb_y, grid_w, kModStripH);
        {
            const float center = pb_y + kModStripH * 0.5f;
            d.draw_rect(o, grid_x, center, grid_w, 1.0f, {0.25f, 0.25f, 0.30f, 1.0f});
        }
        for (int i = 0; i < static_cast<int>(editor_notes_.size()); ++i) {
            const auto& n = editor_notes_[i];
            if (n.pitch_bend == 0.0f) continue;
            float bx     = grid_x + static_cast<float>((n.start_beat - editor_scroll_x_) * beat_w);
            float center = pb_y + kModStripH * 0.5f;
            float frac   = n.pitch_bend / kPitchBendMax;
            float bh     = std::max(2.0f, std::fabs(frac) * (kModStripH * 0.5f - 2.0f));
            float by     = (frac >= 0.0f) ? (center - bh) : center;
            bool  hot    = (i == hov_pb) ||
                           (i == drag_note_idx_ && drag_mode_ == DragMode::PitchBendDrag);
            d.draw_rect(o, bx - 2.0f, by, 5.0f, bh,
                hot ? VividColor{1.0f, 0.85f, 0.50f, 1.0f}
                    : VividColor{0.75f, 0.60f, 0.25f, 0.85f});
        }
        d.draw_rect(o, phx, pb_y, 2.0f, kModStripH, {1.0f, 0.8f, 0.2f, ph_alpha});
        d.pop_clip_rect(o);

        // Pressure
        draw_strip_bg(pr_y, "pres");
        d.push_clip_rect(o, grid_x, pr_y, grid_w, kModStripH);
        for (int i = 0; i < static_cast<int>(editor_notes_.size()); ++i) {
            const auto& n = editor_notes_[i];
            if (n.pressure == 0.0f) continue;
            float bx  = grid_x + static_cast<float>((n.start_beat - editor_scroll_x_) * beat_w);
            float bh  = std::max(2.0f, n.pressure * inner_h);
            float by  = pr_y + kModStripH - bh;
            bool  hot = (i == hov_pr) ||
                        (i == drag_note_idx_ && drag_mode_ == DragMode::PressureDrag);
            d.draw_rect(o, bx - 2.0f, by, 5.0f, bh,
                hot ? VividColor{0.75f, 1.0f, 0.70f, 1.0f}
                    : VividColor{0.35f, 0.70f, 0.35f, 0.85f});
        }
        d.draw_rect(o, phx, pr_y, 2.0f, kModStripH, {1.0f, 0.8f, 0.2f, ph_alpha});
        d.pop_clip_rect(o);
    }

    // -----------------------------------------------------------------------
    // Piano keyboard (left margin)
    // -----------------------------------------------------------------------
    d.push_clip_rect(o, 0, grid_y, kPianoW, roll_h);
    {
        if (fold_rows_ && !folded_pitches.empty()) {
            int top_row    = static_cast<int>(scroll_y_ / row_h_);
            int bottom_row = static_cast<int>((scroll_y_ + roll_h) / row_h_) + 1;
            top_row    = std::clamp(top_row, 0, static_cast<int>(folded_pitches.size()) - 1);
            bottom_row = std::clamp(bottom_row, 0, static_cast<int>(folded_pitches.size()));
            for (int ri = top_row; ri < bottom_row; ++ri) {
                int p  = folded_pitches[ri];
                float ry = y_fn(p) + grid_y;
                if (ry + row_h_ < grid_y || ry > grid_y + roll_h) continue;
                bool black = is_black_key(p);
                d.draw_rect(o, 0, ry, kPianoW - 1, row_h_,
                    black ? VividColor{0.12f, 0.12f, 0.14f, 1.0f}
                          : VividColor{0.25f, 0.25f, 0.28f, 1.0f});
                if (p % 12 == 0) {
                    char label[8];
                    std::snprintf(label, sizeof(label), "C%d", p / 12 - 1);
                    float tw = d.text_width(o, label, 0.75f);
                    d.draw_text(o, kPianoW - tw - 4.0f, ry + 1.5f, label,
                        {0.55f, 0.55f, 0.6f, 1.0f}, 0.75f);
                }
            }
        } else {
            const int top_pitch    = 127 - static_cast<int>(scroll_y_ / row_h_);
            const int bottom_pitch = 127 - static_cast<int>((scroll_y_ + roll_h) / row_h_) - 1;
            for (int p = std::max(0, bottom_pitch - 1); p <= std::min(127, top_pitch + 1); ++p) {
                float ry = y_fn(p) + grid_y;
                if (ry + row_h_ < grid_y || ry > grid_y + roll_h) continue;
                bool black = is_black_key(p);
                d.draw_rect(o, 0, ry, kPianoW - 1, row_h_,
                    black ? VividColor{0.12f, 0.12f, 0.14f, 1.0f}
                          : VividColor{0.25f, 0.25f, 0.28f, 1.0f});
                if (p % 12 == 0) {
                    char label[8];
                    std::snprintf(label, sizeof(label), "C%d", p / 12 - 1);
                    float tw = d.text_width(o, label, 0.75f);
                    d.draw_text(o, kPianoW - tw - 4.0f, ry + 1.5f, label,
                        {0.55f, 0.55f, 0.6f, 1.0f}, 0.75f);
                }
            }
        }
    }
    d.pop_clip_rect(o);

    // -----------------------------------------------------------------------
    // Header bar
    // -----------------------------------------------------------------------
    d.draw_rect(o, 0, 0, sw, kHeaderH, {0.13f, 0.13f, 0.15f, 1.0f});
    d.draw_rect(o, 0, kHeaderH - 1, sw, 1.0f,
        {th.separator.r, th.separator.g, th.separator.b, 0.5f});

    {
        using namespace vivid::ui;
        float hx = 8.0f;

        // Length selector
        float lw = d.text_width(o, "Length:", 1.0f);
        d.draw_text(o, hx, 10.0f, "Length:",
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f}, 1.0f);
        hx += lw + 4.0f;

        static const char* kLengthLabels[] = {"1","2","4","8","16","32","64"};
        Rect lr{hx, 6.0f, 196.0f, 26.0f};
        auto lr_result = ui_radio(*ctx, lr, kLengthLabels, 7, lb_idx);
        if (lr_result.clicked && ctx->commands.set_param)
            ctx->commands.set_param(ctx->commands.opaque, "length_bars",
                static_cast<float>(lr_result.value));
        hx += 204.0f;

        // Grid selector
        float gw = d.text_width(o, "Grid:", 1.0f);
        d.draw_text(o, hx, 10.0f, "Grid:",
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f}, 1.0f);
        hx += gw + 4.0f;

        static const char* kGridLabels[] = {"1/32","1/16","1/8","1/4"};
        Rect gr{hx, 6.0f, 160.0f, 26.0f};
        auto gr_result = ui_radio(*ctx, gr, kGridLabels, 4, grid_idx);
        if (gr_result.clicked && ctx->commands.set_param)
            ctx->commands.set_param(ctx->commands.opaque, "quantize_grid",
                static_cast<float>(gr_result.value));
        hx += 168.0f;

        // Fold button
        {
            Rect fr{hx, 6.0f, 42.0f, 26.0f};
            bool fold_hov = fr.contains(ctx->mouse.x, ctx->mouse.y);
            d.draw_rounded_rect(o, fr.x, fr.y, fr.w, fr.h, 4.0f,
                fold_rows_  ? VividColor{0.30f, 0.55f, 0.85f, 0.85f}
                : fold_hov  ? VividColor{0.25f, 0.25f, 0.28f, 0.8f}
                            : VividColor{0.18f, 0.18f, 0.20f, 0.8f});
            d.draw_text(o, fr.x + fr.w * 0.5f - d.text_width(o, "Fold", 0.85f) * 0.5f,
                fr.y + 7.0f, "Fold", {0.85f, 0.85f, 0.90f, 1.0f}, 0.85f);
            if (fold_hov && ctx->mouse.left_clicked) {
                fold_rows_ = !fold_rows_;
                scroll_y_  = 0.0f;
            }
            hx += 48.0f;
        }

        // Scale selector (compact cycling button)
        {
            static const char* kRootNames[] = {
                "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
            static const char* kTypeNames[] = {"Maj","Min","Hrm","PM","Pm"};

            char scale_label[24];
            if (scale_root_ < 0)
                std::snprintf(scale_label, sizeof(scale_label), "Scale");
            else
                std::snprintf(scale_label, sizeof(scale_label), "%s %s",
                    kRootNames[scale_root_], kTypeNames[scale_type_]);

            float slw = std::max(52.0f, d.text_width(o, scale_label, 0.85f) + 16.0f);
            Rect  slr{hx, 6.0f, slw, 26.0f};
            bool  sl_hov = slr.contains(ctx->mouse.x, ctx->mouse.y);
            d.draw_rounded_rect(o, slr.x, slr.y, slr.w, slr.h, 4.0f,
                (scale_root_ >= 0) ? VividColor{0.25f, 0.48f, 0.25f, 0.85f}
                : sl_hov           ? VividColor{0.25f, 0.25f, 0.28f, 0.8f}
                                   : VividColor{0.18f, 0.18f, 0.20f, 0.8f});
            d.draw_text(o, slr.x + slr.w * 0.5f - d.text_width(o, scale_label, 0.85f) * 0.5f,
                slr.y + 7.0f, scale_label, {0.85f, 0.85f, 0.90f, 1.0f}, 0.85f);
            if (sl_hov && ctx->mouse.left_clicked)  // cycle root
                scale_root_ = (scale_root_ < 11) ? scale_root_ + 1 : -1;
            if (sl_hov && ctx->mouse.right_clicked && scale_root_ >= 0) // cycle type
                scale_type_ = (scale_type_ + 1) % 5;
            hx += slw + 6.0f;
        }

        // Clear button (further right, after scale)
        {
            Rect clr{hx, 6.0f, 50.0f, 26.0f};
            auto clr_result = ui_button(*ctx, clr, "Clear");
            if (clr_result.clicked) {
                editor_notes_.clear();
                commit_editor_notes(ctx);
            }
            hx += 56.0f;
        }

        // Horizontal zoom controls: [−] N bars [+]
        {
            Rect zor{hx, 6.0f, 22.0f, 26.0f};
            if (ui_button(*ctx, zor, "-").clicked) {
                float new_zoom = std::min(full_beats, zoom_beats * 1.5f);
                editor_zoom_beats_ = (new_zoom >= full_beats) ? 0.0f : new_zoom;
                editor_scroll_x_   = std::clamp(editor_scroll_x_, 0.0f,
                                        std::max(0.0f, full_beats - new_zoom));
                zoom_beats = new_zoom;
            }
            hx += 24.0f;

            char zoom_lbl[32];
            const int visible_beats = std::max(1, static_cast<int>(std::round(zoom_beats)));
            const int visible_bars  = std::max(1, visible_beats / bpb);
            if (visible_bars * bpb == visible_beats && visible_bars >= 1)
                std::snprintf(zoom_lbl, sizeof(zoom_lbl), "%d bar%s",
                    visible_bars, visible_bars != 1 ? "s" : "");
            else
                std::snprintf(zoom_lbl, sizeof(zoom_lbl), "%d beats", visible_beats);
            const float zl_w = d.text_width(o, zoom_lbl, 0.8f);
            d.draw_text(o, hx, 11.0f, zoom_lbl,
                {0.50f, 0.50f, 0.55f, 0.80f}, 0.8f);
            hx += zl_w + 4.0f;

            Rect zir{hx, 6.0f, 22.0f, 26.0f};
            if (ui_button(*ctx, zir, "+").clicked) {
                float new_zoom = std::max(static_cast<float>(bpb), zoom_beats / 1.5f);
                editor_zoom_beats_ = new_zoom;
                editor_scroll_x_   = std::clamp(editor_scroll_x_, 0.0f,
                                        std::max(0.0f, full_beats - new_zoom));
                zoom_beats = new_zoom;
            }
        }

        // Status / hint (right-aligned)
        float hint_x = sw - 8.0f;
        if (ctx->time < import_status_until_ && !import_status_.empty()) {
            float fade = static_cast<float>(
                std::min(1.0, (import_status_until_ - ctx->time) / 0.5));
            hint_x -= d.text_width(o, import_status_.c_str(), 0.85f);
            d.draw_text(o, hint_x, 11.0f, import_status_.c_str(),
                {0.5f, 0.9f, 0.5f, fade * 0.95f}, 0.85f);
        } else {
            const char* hint = "LMB: add | RMB: del | Opt+scroll: row size | Cmd+scroll: h-zoom";
            hint_x -= d.text_width(o, hint, 0.75f);
            d.draw_text(o, hint_x, 11.0f, hint,
                {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.40f}, 0.75f);
        }
    }

    // -----------------------------------------------------------------------
    // Mouse interaction
    // -----------------------------------------------------------------------
    {
        auto snap = [&](double beat) -> double {
            double cell = midi_clip::grid_cell_beats(grid_idx);
            if (cell <= 0.0) return beat;
            return std::round(beat / cell) * cell;
        };

        enum class HitZone { None, Body, ResizeRight };
        struct NoteHit { int idx = -1; HitZone zone = HitZone::None; };

        auto hit_test = [&](float px, float py) -> NoteHit {
            if (!vivid::ui::Rect{grid_x, grid_y, grid_w, roll_h}.contains(px, py))
                return {};
            const int    hover_pitch = p_fn(py - grid_y);
            const double hover_beat  = beat_from_x(px - grid_x, editor_scroll_x_,
                                                    static_cast<float>(inv_bw));
            for (int i = 0; i < static_cast<int>(editor_notes_.size()); ++i) {
                const auto& n = editor_notes_[i];
                if (static_cast<int>(n.pitch) != hover_pitch) continue;
                if (hover_beat < n.start_beat ||
                    hover_beat >= n.start_beat + n.duration_beats) continue;
                float note_right_px = grid_x +
                    static_cast<float>((n.start_beat + n.duration_beats - editor_scroll_x_) * beat_w);
                HitZone zone = (px >= note_right_px - 8.0f)
                    ? HitZone::ResizeRight : HitZone::Body;
                return {i, zone};
            }
            return {};
        };

        // Cursor for resize
        if (drag_mode_ == DragMode::None && mouse_in_roll) {
            auto hov = hit_test(ctx->mouse.x, ctx->mouse.y);
            if (hov.idx >= 0 && hov.zone == HitZone::ResizeRight)
                if (ctx->host.set_cursor)
                    ctx->host.set_cursor(ctx->host.opaque, VIVID_CURSOR_RESIZE_H);
        }
        if (drag_mode_ == DragMode::ResizingNote ||
            drag_mode_ == DragMode::LoopBraceLeft ||
            drag_mode_ == DragMode::LoopBraceRight) {
            if (ctx->host.set_cursor)
                ctx->host.set_cursor(ctx->host.opaque, VIVID_CURSOR_RESIZE_H);
        }

        // --- Loop brace interaction ---
        if (mouse_in_brace && ctx->mouse.left_clicked && drag_mode_ == DragMode::None) {
            drag_start_mx_ = ctx->mouse.x;
            if (has_loop_region && std::fabs(ctx->mouse.x - loop_lx) < 8.0f) {
                drag_mode_           = DragMode::LoopBraceLeft;
                drag_orig_loop_end_  = le_param;
            } else if (has_loop_region && std::fabs(ctx->mouse.x - loop_rx) < 8.0f) {
                drag_mode_            = DragMode::LoopBraceRight;
                drag_orig_loop_start_ = ls_param;
            } else if (has_loop_region && ctx->mouse.x > loop_lx && ctx->mouse.x < loop_rx) {
                drag_mode_            = DragMode::LoopBraceBody;
                drag_orig_loop_start_ = ls_param;
                drag_orig_loop_end_   = le_param;
            } else {
                drag_mode_            = DragMode::LoopBraceSweep;
                drag_orig_loop_start_ = beat_from_x(mx, editor_scroll_x_,
                                                    static_cast<float>(inv_bw));
            }
            if (ctx->host.capture_pointer)
                ctx->host.capture_pointer(ctx->host.opaque);
        }
        // Right-click in brace: clear loop region
        if (mouse_in_brace && ctx->mouse.right_clicked && has_loop_region) {
            if (ctx->commands.set_param) {
                ctx->commands.set_param(ctx->commands.opaque, "loop_start_beat", 0.0f);
                ctx->commands.set_param(ctx->commands.opaque, "loop_end_beat",   0.0f);
            }
        }

        // --- Mod strip press ---
        auto begin_strip_drag = [&](int best, DragMode mode) {
            if (!ctx->mouse.left_clicked || best < 0 || drag_mode_ != DragMode::None) return;
            drag_note_idx_ = best;
            drag_start_my_ = ctx->mouse.y;
            drag_orig_vel_ = editor_notes_[best].velocity;
            drag_orig_pb_  = editor_notes_[best].pitch_bend;
            drag_orig_pres_= editor_notes_[best].pressure;
            drag_mode_     = mode;
            if (ctx->host.capture_pointer)
                ctx->host.capture_pointer(ctx->host.opaque);
        };
        begin_strip_drag(hov_vel, DragMode::VelocityDrag);
        begin_strip_drag(hov_pb,  DragMode::PitchBendDrag);
        begin_strip_drag(hov_pr,  DragMode::PressureDrag);

        if (ctx->mouse.right_clicked && drag_mode_ == DragMode::None) {
            if (hov_vel >= 0) {
                editor_notes_[hov_vel].velocity = 0.8f; commit_editor_notes(ctx);
            } else if (hov_pb >= 0) {
                editor_notes_[hov_pb].pitch_bend = 0.0f; commit_editor_notes(ctx);
            } else if (hov_pr >= 0) {
                editor_notes_[hov_pr].pressure = 0.0f; commit_editor_notes(ctx);
            }
        }

        // --- Roll press ---
        if (ctx->mouse.left_clicked && mouse_in_roll) {
            auto hit = hit_test(ctx->mouse.x, ctx->mouse.y);
            if (hit.idx >= 0) {
                auto& n = editor_notes_[hit.idx];
                drag_note_idx_   = hit.idx;
                drag_start_mx_   = ctx->mouse.x;
                drag_start_my_   = ctx->mouse.y;
                drag_orig_start_ = n.start_beat;
                drag_orig_dur_   = n.duration_beats;
                drag_orig_pitch_ = n.pitch;
                drag_mode_ = (hit.zone == HitZone::ResizeRight)
                    ? DragMode::ResizingNote : DragMode::MovingNote;
                if (ctx->host.capture_pointer)
                    ctx->host.capture_pointer(ctx->host.opaque);
            } else {
                int pitch = p_fn(my);
                if (pitch >= 0 && pitch <= 127) {
                    double start = midi_clip::quantize_to_grid(
                        beat_from_x(mx, editor_scroll_x_, static_cast<float>(inv_bw)), grid_idx);
                    start = std::clamp(start, 0.0, pat_len - cell_beats);
                    midi_clip::ParsedNote n{};
                    n.pitch          = static_cast<uint8_t>(pitch);
                    n.start_beat     = start;
                    n.duration_beats = cell_beats;
                    n.velocity       = 0.8f;
                    editor_notes_.push_back(n);
                    drag_note_idx_   = static_cast<int>(editor_notes_.size()) - 1;
                    drag_start_mx_   = ctx->mouse.x;
                    drag_start_my_   = ctx->mouse.y;
                    drag_orig_start_ = start;
                    drag_orig_dur_   = cell_beats;
                    drag_orig_pitch_ = static_cast<uint8_t>(pitch);
                    drag_mode_       = DragMode::AddingNote;
                    commit_editor_notes(ctx);
                    if (ctx->host.capture_pointer)
                        ctx->host.capture_pointer(ctx->host.opaque);
                }
            }
        }

        if (ctx->mouse.right_clicked && mouse_in_roll && drag_mode_ == DragMode::None) {
            auto hit = hit_test(ctx->mouse.x, ctx->mouse.y);
            if (hit.idx >= 0) {
                editor_notes_.erase(editor_notes_.begin() + hit.idx);
                commit_editor_notes(ctx);
            }
        }

        // --- Drag update ---
        if (ctx->mouse.left_down) {
            const float dmx = ctx->mouse.x - drag_start_mx_;
            const float dmy = ctx->mouse.y - drag_start_my_;

            // Loop brace drags
            if (drag_mode_ == DragMode::LoopBraceSweep) {
                double cur_beat = beat_from_x(mx, editor_scroll_x_, static_cast<float>(inv_bw));
                double new_ls = std::min(drag_orig_loop_start_, cur_beat);
                double new_le = std::max(drag_orig_loop_start_, cur_beat);
                new_ls = std::clamp(new_ls, 0.0, pat_len);
                new_le = std::clamp(new_le, 0.0, pat_len);
                if (new_le - new_ls >= cell_beats && ctx->commands.set_param) {
                    ctx->commands.set_param(ctx->commands.opaque, "loop_start_beat",
                        static_cast<float>(new_ls));
                    ctx->commands.set_param(ctx->commands.opaque, "loop_end_beat",
                        static_cast<float>(new_le));
                }
            } else if (drag_mode_ == DragMode::LoopBraceLeft) {
                double new_ls = beat_from_x(mx, editor_scroll_x_, static_cast<float>(inv_bw));
                new_ls = std::clamp(new_ls, 0.0, drag_orig_loop_end_ - cell_beats);
                if (ctx->commands.set_param)
                    ctx->commands.set_param(ctx->commands.opaque, "loop_start_beat",
                        static_cast<float>(new_ls));
            } else if (drag_mode_ == DragMode::LoopBraceRight) {
                double new_le = beat_from_x(mx, editor_scroll_x_, static_cast<float>(inv_bw));
                new_le = std::clamp(new_le, drag_orig_loop_start_ + cell_beats, pat_len);
                if (ctx->commands.set_param)
                    ctx->commands.set_param(ctx->commands.opaque, "loop_end_beat",
                        static_cast<float>(new_le));
            } else if (drag_mode_ == DragMode::LoopBraceBody) {
                double delta   = dmx * inv_bw;
                double len     = drag_orig_loop_end_ - drag_orig_loop_start_;
                double new_ls  = std::clamp(drag_orig_loop_start_ + delta, 0.0, pat_len - len);
                double new_le  = new_ls + len;
                if (ctx->commands.set_param) {
                    ctx->commands.set_param(ctx->commands.opaque, "loop_start_beat",
                        static_cast<float>(new_ls));
                    ctx->commands.set_param(ctx->commands.opaque, "loop_end_beat",
                        static_cast<float>(new_le));
                }
            }

            // Note drags
            if (drag_note_idx_ >= 0 &&
                drag_note_idx_ < static_cast<int>(editor_notes_.size())) {
                auto& n = editor_notes_[drag_note_idx_];

                if (drag_mode_ == DragMode::MovingNote) {
                    double raw_start = drag_orig_start_ + dmx * inv_bw;
                    double new_start = std::clamp(snap(raw_start), 0.0, pat_len - drag_orig_dur_);
                    int drow = static_cast<int>(std::round(dmy / row_h_));
                    int new_pitch = std::clamp(
                        static_cast<int>(drag_orig_pitch_) - drow, 0, 127);
                    if (n.start_beat != new_start || static_cast<int>(n.pitch) != new_pitch) {
                        n.start_beat = new_start;
                        n.pitch      = static_cast<uint8_t>(new_pitch);
                        commit_editor_notes(ctx);
                    }
                } else if (drag_mode_ == DragMode::ResizingNote) {
                    double raw_right = drag_orig_start_ + drag_orig_dur_ + dmx * inv_bw;
                    double new_dur   = std::max(cell_beats, snap(raw_right) - drag_orig_start_);
                    new_dur = std::min(new_dur, pat_len - drag_orig_start_);
                    if (n.duration_beats != new_dur) {
                        n.duration_beats = new_dur; commit_editor_notes(ctx);
                    }
                } else if (drag_mode_ == DragMode::AddingNote) {
                    double raw_right = drag_orig_start_ + drag_orig_dur_ + dmx * inv_bw;
                    double new_right = std::max(drag_orig_start_ + cell_beats, snap(raw_right));
                    new_right = std::min(new_right, pat_len);
                    double new_dur = new_right - drag_orig_start_;
                    if (n.duration_beats != new_dur) {
                        n.duration_beats = new_dur; commit_editor_notes(ctx);
                    }
                } else if (drag_mode_ == DragMode::VelocityDrag) {
                    float new_vel = std::clamp(
                        1.0f - (ctx->mouse.y - vel_y) / (kModStripH - 4.0f), 0.01f, 1.0f);
                    if (n.velocity != new_vel) { n.velocity = new_vel; commit_editor_notes(ctx); }
                } else if (drag_mode_ == DragMode::PitchBendDrag) {
                    float t      = (ctx->mouse.y - pb_y) / kModStripH;
                    float new_pb = std::clamp((0.5f - t) * 2.0f * kPitchBendMax,
                                              -kPitchBendMax, kPitchBendMax);
                    if (std::fabs(new_pb) < 0.25f) new_pb = 0.0f;
                    if (n.pitch_bend != new_pb) { n.pitch_bend = new_pb; commit_editor_notes(ctx); }
                } else if (drag_mode_ == DragMode::PressureDrag) {
                    float new_pr = std::clamp(
                        1.0f - (ctx->mouse.y - pr_y) / (kModStripH - 4.0f), 0.0f, 1.0f);
                    if (n.pressure != new_pr) { n.pressure = new_pr; commit_editor_notes(ctx); }
                }
            }
        }

        // --- Release ---
        if (!ctx->mouse.left_down && drag_mode_ != DragMode::None) {
            if (ctx->host.release_pointer)
                ctx->host.release_pointer(ctx->host.opaque);
            drag_mode_     = DragMode::None;
            drag_note_idx_ = -1;
        }
    }
}
