#include "midi_clip_core.h"
#include "operator_api/editor_ui.h"
#include "operator_api/draw_ui_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <set>

// ---------------------------------------------------------------------------
// Layout constants
// ---------------------------------------------------------------------------
static constexpr float kHeaderH      = 64.0f;
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
    return static_cast<double>(gx * beats_per_px + scroll_x);
}

// ---------------------------------------------------------------------------
// draw_thumbnail  — miniature piano-roll preview
// ---------------------------------------------------------------------------
void MidiClipCore::draw_thumbnail(const VividThumbnailContext* ctx) {
    if (!ctx || !ctx->draw.opaque) return;
    const auto& d = ctx->draw;
    void* o = d.opaque;

    const float W = ctx->thumbnail_logical_width  ? (float)ctx->thumbnail_logical_width
                                                  : (float)ctx->thumbnail_width;
    const float H = ctx->thumbnail_logical_height ? (float)ctx->thumbnail_logical_height
                                                  : (float)ctx->thumbnail_height;

    d.draw_rect(o, 0.f, 0.f, W, H, {0.07f, 0.08f, 0.09f, 0.9f});

    const std::vector<midi_clip::ParsedNote>& notes = thumbnail_notes_;

    if (notes.empty()) {
        if (d.draw_text) {
            const char* lbl = file_error_.empty() ? "empty" : file_error_.c_str();
            d.draw_text(o, 4.f, H * 0.5f - 4.f, lbl, {0.28f, 0.32f, 0.38f, 0.8f}, 0.7f);
        }
        return;
    }

    // Use the operator's internally-computed clip length (set from file/pattern in
    // refresh_file_sequence) rather than ctx->param_values[0], which is the host's
    // copy and may lag until draw_inspector pushes the update.
    const float total_beats = (clip_length_beats_ > 0.0)
        ? static_cast<float>(clip_length_beats_)
        : static_cast<float>(ctx->param_count > 0 ? ctx->param_values[0] : 2.f) * 4.f;

    const float loop_start_b = (ctx->param_count > 11) ? ctx->param_values[11] : 0.f;
    const float loop_end_b   = (ctx->param_count > 12) ? ctx->param_values[12] : 0.f;

    // output_values[0] = phase (0..1 playhead position through the clip)
    const float phase    = (ctx->output_count > 0) ? ctx->output_values[0] : -1.f;
    const float ph_beat  = (phase >= 0.f) ? phase * total_beats : -1.f;

    // Compute pitch range from notes, add margin, enforce minimum 12-semitone span
    int pitch_min = 127, pitch_max = 0;
    for (const auto& n : notes) {
        pitch_min = std::min(pitch_min, (int)n.pitch);
        pitch_max = std::max(pitch_max, (int)n.pitch);
    }
    pitch_min = std::max(0,   pitch_min - 2);
    pitch_max = std::min(127, pitch_max + 2);
    if (pitch_max - pitch_min < 11) {
        const int center = (pitch_min + pitch_max) / 2;
        pitch_min = std::max(0,   center - 6);
        pitch_max = std::min(127, center + 5);
    }
    const int pitch_range = pitch_max - pitch_min + 1;

    constexpr float kMargin = 2.f;
    const float gx = kMargin, gy = kMargin;
    const float gw = W - 2.f * kMargin;
    const float gh = H - 2.f * kMargin;

    const float px_per_beat = (total_beats > 0.f) ? gw / total_beats : gw;
    const float row_h       = std::max(1.f, gh / (float)pitch_range);

    // Bar gridlines
    if (d.draw_line) {
        const int bars = (int)std::ceil(total_beats / 4.f);
        for (int b = 0; b <= bars; ++b) {
            const float bx = gx + b * 4.f * px_per_beat;
            if (bx < gx - 0.5f || bx > gx + gw + 0.5f) continue;
            const VividColor lc = (b % 4 == 0)
                ? VividColor{0.22f, 0.24f, 0.29f, 0.9f}
                : VividColor{0.14f, 0.16f, 0.19f, 0.6f};
            d.draw_line(o, bx, gy, bx, gy + gh, 0.5f, lc);
        }
    }

    // Loop region overlay
    const bool has_loop = (loop_end_b > loop_start_b + 0.01f);
    if (has_loop) {
        constexpr VividColor kDim = {0.f, 0.f, 0.f, 0.35f};
        const float ls_x = gx + loop_start_b * px_per_beat;
        const float le_x = gx + loop_end_b   * px_per_beat;
        if (ls_x > gx)
            d.draw_rect(o, gx, gy, ls_x - gx, gh, kDim);
        if (le_x < gx + gw)
            d.draw_rect(o, le_x, gy, gx + gw - le_x, gh, kDim);
    }

    // Notes — stride-based sampling so large files always show a representative
    // cross-section rather than cutting off after an arbitrary count.
    const size_t N = notes.size();
    const size_t stride = std::max<size_t>(1, N / 2048);
    for (size_t i = 0; i < N; i += stride) {
        const auto& n = notes[i];

        const int pitch = (int)n.pitch;
        if (pitch < pitch_min || pitch > pitch_max) continue;

        const float nx = gx + (float)n.start_beat * px_per_beat;
        const float nw = std::max(1.f, (float)n.duration_beats * px_per_beat - 0.5f);
        const float ny = gy + (pitch_max - pitch) * row_h;
        const float nh = std::max(1.f, row_h - 0.5f);

        if (nx + nw < gx || nx > gx + gw) continue;

        const bool is_playing = (ph_beat >= 0.f &&
                                 ph_beat >= (float)n.start_beat &&
                                 ph_beat <  (float)(n.start_beat + n.duration_beats));

        VividColor c = is_playing
            ? VividColor{0.94f, 0.72f, 0.22f, 1.0f}
            : VividColor{0.31f, 0.55f, 0.92f, 0.85f};
        c.a *= 0.4f + n.velocity * 0.6f;

        if (nh >= 2.5f && d.draw_rounded_rect)
            d.draw_rounded_rect(o, nx, ny, nw, nh, 1.f, c);
        else
            d.draw_rect(o, nx, ny, nw, nh, c);
    }

    // Playhead
    if (ph_beat >= 0.f && d.draw_line) {
        const float phx = gx + ph_beat * px_per_beat;
        if (phx >= gx && phx <= gx + gw)
            d.draw_line(o, phx, gy, phx, gy + gh, 1.5f, {1.f, 0.78f, 0.31f, 0.7f});
    }
}

// ---------------------------------------------------------------------------
// draw_editor
// ---------------------------------------------------------------------------
void MidiClipCore::draw_editor(VividEditorContext* ctx) {
    if (!ctx) return;
    ctx->wants_keyboard = 1;

    auto& d   = ctx->draw;
    void* o   = d.opaque;
    const auto& th = ctx->theme;

    const float sw = ctx->surface_width;
    const float sh = ctx->surface_height;
    if (!editor_view_params_initialized_) {
        fold_rows_ = editor_fold_.bool_value();
        scale_root_ = editor_scale_root_.int_value();
        scale_type_ = editor_scale_type_.int_value();
        if (editor_zoom_beat_.value > 0.0f) editor_zoom_beats_ = editor_zoom_beat_.value;
        editor_scroll_x_ = std::max(0.0f, editor_scroll_beat_.value);
        row_h_ = std::clamp(editor_row_height_.value, 6.0f, 40.0f);
        editor_view_params_initialized_ = true;
    }

    // --- Sync editor notes from string params ---
    const char* pat_str = (ctx->string_param_count > 0 && ctx->string_param_values)
        ? ctx->string_param_values[0] : "[]";
    if (!pat_str) pat_str = "[]";
    const std::string pat_value = pat_str;
    const bool file_backed = !file.str_value.empty() && (pat_value.empty() || pat_value == "[]");
    const bool clip_ref_backed = file.str_value.empty() &&
        !clip_data_ref_.str_value.empty() && (pat_value.empty() || pat_value == "[]");
    if (!file_backed && !clip_ref_backed && pat_value != editor_submitted_str_) {
        editor_submitted_str_ = pat_str;
        midi_clip::parse_pattern(editor_submitted_str_, editor_notes_);
        note_selected_.assign(editor_notes_.size(), false);
        mark_editor_note_order_dirty();
    }

    if (file_backed) {
        const uint64_t generation = sequence_generation_.load(std::memory_order_acquire);
        const float file_bpm = std::max(1.0f, audio_bpm_.load(std::memory_order_relaxed));
        if (generation != editor_file_generation_ ||
            std::fabs(file_bpm - editor_file_bpm_) > 0.001f) {
            editor_file_generation_ = generation;
            editor_file_bpm_ = file_bpm;
            editor_notes_.clear();
            note_selected_.clear();
            SequenceData* seq = sequence_.load(std::memory_order_acquire);
            {
                std::lock_guard<std::mutex> lock(pattern_mutex_);
                editor_notes_ = imported_audio_notes_;
            }
            mark_editor_note_order_dirty();
            if (seq) {
                note_selected_.assign(editor_notes_.size(), false);
                if (clip_length_beats_ > 0.0) {
                    const double total_beats = clip_length_beats_;
                    length_bars.value = static_cast<float>(std::min(8192.0, clip_length_bars_));
                    if (editor_zoom_beats_ <= 0.0f && total_beats > 64.0)
                        editor_zoom_beats_ = 64.0f;
                }
            }
        }
    }
    if (clip_ref_backed && clip_data_ref_.str_value != editor_loaded_clip_data_ref_) {
        editor_loaded_clip_data_ref_ = clip_data_ref_.str_value;
        {
            std::lock_guard<std::mutex> lock(pattern_mutex_);
            editor_notes_ = audio_notes_;
        }
        note_selected_.assign(editor_notes_.size(), false);
        mark_editor_note_order_dirty();
        if (clip_data_beat_length_ > 0.0) {
            length_bars.value = static_cast<float>(std::min(8192.0, clip_length_bars_));
            if (editor_zoom_beats_ <= 0.0f && clip_data_beat_length_ > 64.0)
                editor_zoom_beats_ = 64.0f;
        }
    }

    const float phase    = (ctx->output_count > 0) ? ctx->output_values[0] : 0.0f;
    const float lb_val   = (file_backed || clip_ref_backed)
        ? static_cast<float>(std::max(clip_length_bars_, 0.25))
        : ((ctx->param_count > 0) ? ctx->param_values[0] : 2.0f);
    if ((file_backed || clip_ref_backed) && ctx->commands.set_param &&
        ctx->param_count > 0 && ctx->param_values &&
        std::fabs(ctx->param_values[0] - lb_val) > 0.001f) {
        ctx->commands.set_param(ctx->commands.opaque, "length_bars", lb_val);
    }
    const int   grid_idx = (ctx->param_count > 1) ? static_cast<int>(ctx->param_values[1]) : 1;
    // Read loop region from struct members (already synced before draw_editor is called).
    const float loop_start_v = loop_start_beat.value;
    const float loop_end_v   = loop_end_beat.value;

    const int    bpb      = 4;
    const double pat_len  = static_cast<double>(lb_val) * static_cast<double>(bpb);
    const double cell_beats = midi_clip::grid_cell_beats(grid_idx);

    // Resolve loop region for editor rendering
    const double ls_param = std::clamp(static_cast<double>(loop_start_v), 0.0, pat_len);
    const double le_param = std::clamp(static_cast<double>(loop_end_v),   0.0, pat_len);
    const bool   has_loop_region  = (le_param > ls_param + 1e-6);
    const double editor_loop_origin = has_loop_region ? ls_param : 0.0;
    const double editor_loop_len    = has_loop_region ? (le_param - ls_param) : pat_len;

    // --- Promote legacy midi_import into file-backed playback. This is the
    // only work kept in draw_editor for old graphs: no parsing or serialization.
    if (has_pending_import_) {
        has_pending_import_ = false;
        if (ctx->commands.set_string_param) {
            ctx->commands.set_string_param(ctx->commands.opaque, "file",
                                           pending_import_path_.c_str());
            ctx->commands.set_string_param(ctx->commands.opaque, "midi_import", "");
        }
        file.str_value = pending_import_path_;
        import_status_ = "Queued MIDI file";
        import_status_until_ = ctx->time + 2.0;
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

    // Zoom/scroll state — reset when pattern length changes.
    // last_lb_val_ starts at -1 as a sentinel: on the first frame we seed it
    // without resetting zoom/scroll so params restored by the init block above
    // (editor_zoom_beats_, editor_scroll_x_) are not immediately discarded.
    if (last_lb_val_ < 0.0f) {
        last_lb_val_ = lb_val;
    } else if (std::fabs(lb_val - last_lb_val_) > 0.01f) {
        last_lb_val_       = lb_val;
        editor_zoom_beats_ = 0.0f;
        editor_scroll_x_   = 0.0f;
    }
    const float full_beats = static_cast<float>(pat_len);
    float zoom_beats = (editor_zoom_beats_ > 0.0f) ? editor_zoom_beats_ : full_beats;
    zoom_beats       = std::clamp(zoom_beats, 4.0f, full_beats);
    editor_scroll_x_ = std::clamp(editor_scroll_x_, 0.0f,
                                   std::max(0.0f, full_beats - zoom_beats));
    vivid::ui::Viewport1D beat_view{
        0.0, static_cast<double>(full_beats),
        grid_x, grid_w,
        static_cast<double>(editor_scroll_x_),
        static_cast<double>(zoom_beats)
    };
    vivid::ui::clamp_viewport(&beat_view, static_cast<double>(bpb));
    editor_scroll_x_ = static_cast<float>(beat_view.view_start);
    zoom_beats = static_cast<float>(beat_view.view_size);
    const double beat_w  = beat_view.pixels_per_unit();
    const double inv_bw  = beat_view.units_per_pixel();
    const double visible_start_beat = beat_view.visible_range().start;
    const double visible_end_beat = beat_view.visible_range().end;
    std::vector<int> visible_note_indices;
    visible_note_indices.reserve(std::min<size_t>(editor_notes_.size(), 1024));
    auto rebuild_visible_note_indices = [&]() {
        rebuild_editor_note_order_if_needed();
        visible_note_indices.clear();
        const double candidate_start = visible_start_beat - editor_max_note_duration_;
        auto first_visible = std::lower_bound(
            editor_note_order_by_start_.begin(), editor_note_order_by_start_.end(), candidate_start,
            [&](int idx, double beat) {
                return editor_notes_[static_cast<size_t>(idx)].start_beat < beat;
            });
        editor_last_visible_scan_count_ = 0;
        for (auto it = first_visible; it != editor_note_order_by_start_.end(); ++it) {
            const int i = *it;
            const auto& n = editor_notes_[i];
            if (n.start_beat > visible_end_beat)
                break;
            ++editor_last_visible_scan_count_;
            if (n.start_beat + n.duration_beats < visible_start_beat) {
                continue;
            }
            visible_note_indices.push_back(i);
        }
    };
    rebuild_visible_note_indices();

    // Loop brace pixel positions (for both rendering and interaction)
    float loop_lx = beat_view.world_to_screen(ls_param);
    float loop_rx = beat_view.world_to_screen(le_param);
    // Clamped for rendering (actual positions used for interaction hit-test)
    const float loop_lx_draw = std::clamp(loop_lx, grid_x, grid_x + grid_w);
    const float loop_rx_draw = std::clamp(loop_rx, grid_x, grid_x + grid_w);

    // Mod strip hover
    auto find_nearest_note = [&](bool in_strip) -> int {
        if (!in_strip || drag_mode_ != DragMode::None) return -1;
        int best = -1; float best_dist = 10.0f;
        for (int i : visible_note_indices) {
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
        auto sb_res = vivid::ui::ui_scrollbar(
            *ctx, vivid::ui::Rect{grid_x, hsc_y, grid_w, kHScrollH},
            vivid::ui::Orientation::Horizontal,
            &beat_view, &hscroll_state_);
        if (sb_res.changed) {
            editor_scroll_x_ = static_cast<float>(beat_view.view_start);
        }
    }

    auto snap = [&](double beat) -> double {
        double cell = midi_clip::grid_cell_beats(grid_idx);
        if (cell <= 0.0) return beat;
        return std::round(beat / cell) * cell;
    };

    // -----------------------------------------------------------------------
    // Keyboard shortcuts (when length field is not focused)
    // -----------------------------------------------------------------------
    if (!length_field_state_.focused) {
        namespace ek = ::vivid::editor_keys;
        for (uint32_t i = 0; i < ctx->event_count; ++i) {
            const auto& e = ctx->events[i];
            if (e.type != VIVID_EDITOR_EVENT_KEY) continue;
            if (e.action != ek::kPress && e.action != ek::kRepeat) continue;
            const bool cmd   = ek::is_cmd_or_ctrl(e.modifiers);
            const bool shift = (e.modifiers & ek::kModShift) != 0;

            if (cmd && e.key == ek::kZ) {
                if (shift) apply_redo(ctx);
                else        apply_undo(ctx);
                continue;
            }

            if (e.key == ek::kDelete || e.key == ek::kBackspace) {
                bool any = false;
                for (size_t j = 0; j < note_selected_.size(); ++j)
                    if (note_selected_[j]) { any = true; break; }
                if (any) push_undo_snapshot();
                for (int j = (int)editor_notes_.size() - 1; j >= 0; --j) {
                    if (j < (int)note_selected_.size() && note_selected_[j]) {
                        editor_notes_.erase(editor_notes_.begin() + j);
                        note_selected_.erase(note_selected_.begin() + j);
                    }
                }
                commit_editor_notes(ctx);
            }
            else if (e.key == ek::kUp || e.key == ek::kDown) {
                int delta = (e.key == ek::kUp ? 1 : -1) * (shift ? 12 : 1);
                bool any = false;
                for (size_t j = 0; j < note_selected_.size(); ++j)
                    if (note_selected_[j]) { any = true; break; }
                if (any) {
                    push_undo_snapshot();
                    for (int j = 0; j < (int)editor_notes_.size(); ++j) {
                        if (j >= (int)note_selected_.size() || !note_selected_[j]) continue;
                        editor_notes_[j].pitch = static_cast<uint8_t>(
                            std::clamp(static_cast<int>(editor_notes_[j].pitch) + delta, 0, 127));
                    }
                    commit_editor_notes(ctx);
                }
            }
            else if (!cmd && (e.key == ek::kLeft || e.key == ek::kRight)) {
                double delta = (e.key == ek::kRight ? 1.0 : -1.0) * cell_beats;
                bool any = false;
                for (size_t j = 0; j < note_selected_.size(); ++j)
                    if (note_selected_[j]) { any = true; break; }
                if (any) {
                    push_undo_snapshot();
                    for (int j = 0; j < (int)editor_notes_.size(); ++j) {
                        if (j >= (int)note_selected_.size() || !note_selected_[j]) continue;
                        editor_notes_[j].start_beat = std::clamp(
                            editor_notes_[j].start_beat + delta,
                            0.0, pat_len - editor_notes_[j].duration_beats);
                    }
                    commit_editor_notes(ctx);
                }
            }
            else if (cmd && e.key == ek::kA) {
                note_selected_.assign(editor_notes_.size(), true);
            }
            else if (cmd && e.key == ek::kC) {
                editor_clipboard_.clear();
                for (int j = 0; j < (int)editor_notes_.size(); ++j) {
                    if (j < (int)note_selected_.size() && note_selected_[j])
                        editor_clipboard_.push_back(editor_notes_[j]);
                }
                if (!editor_clipboard_.empty()) {
                    double max_end = 0.0;
                    for (const auto& n : editor_clipboard_)
                        max_end = std::max(max_end, n.start_beat + n.duration_beats);
                    paste_cursor_beat_ = max_end;
                }
            }
            else if (cmd && e.key == ek::kD) {
                double min_start = pat_len, max_end = 0.0;
                for (int j = 0; j < (int)editor_notes_.size(); ++j) {
                    if (j >= (int)note_selected_.size() || !note_selected_[j]) continue;
                    min_start = std::min(min_start, editor_notes_[j].start_beat);
                    max_end   = std::max(max_end,
                        editor_notes_[j].start_beat + editor_notes_[j].duration_beats);
                }
                if (max_end > min_start) {
                    push_undo_snapshot();
                    double span = max_end - min_start;
                    std::vector<midi_clip::ParsedNote> copies;
                    for (int j = 0; j < (int)editor_notes_.size(); ++j) {
                        if (j >= (int)note_selected_.size() || !note_selected_[j]) continue;
                        auto n = editor_notes_[j];
                        n.start_beat += span;
                        if (n.start_beat < pat_len) copies.push_back(n);
                    }
                    std::fill(note_selected_.begin(), note_selected_.end(), false);
                    for (auto& n : copies) {
                        editor_notes_.push_back(n);
                        note_selected_.push_back(true);
                    }
                    commit_editor_notes(ctx);
                }
            }
            else if (cmd && e.key == ek::kV) {
                if (!editor_clipboard_.empty()) {
                    push_undo_snapshot();
                    double min_start = editor_clipboard_[0].start_beat;
                    for (const auto& n : editor_clipboard_)
                        min_start = std::min(min_start, n.start_beat);
                    double paste_at = snap(paste_cursor_beat_ >= 0.0
                                          ? paste_cursor_beat_ : editor_scroll_x_);
                    std::fill(note_selected_.begin(), note_selected_.end(), false);
                    double pasted_end = paste_at;
                    for (auto n : editor_clipboard_) {
                        const double offset = n.start_beat - min_start;
                        n.start_beat = std::clamp(paste_at + offset, 0.0,
                                                  pat_len - n.duration_beats);
                        pasted_end = std::max(pasted_end, n.start_beat + n.duration_beats);
                        editor_notes_.push_back(n);
                        note_selected_.push_back(true);
                    }
                    paste_cursor_beat_ = std::min(pasted_end, pat_len);
                    commit_editor_notes(ctx);
                }
            }
            else if (cmd && e.key == ek::kU) {
                bool any_selected = false;
                for (uint8_t selected : note_selected_) {
                    if (selected) { any_selected = true; break; }
                }
                auto pre_notes = editor_notes_;
                auto pre_sel   = note_selected_;
                bool changed = false;
                for (int j = 0; j < (int)editor_notes_.size(); ++j) {
                    if (any_selected && (j >= (int)note_selected_.size() || !note_selected_[j]))
                        continue;
                    auto& n = editor_notes_[j];
                    const double old_start = n.start_beat;
                    const double old_dur = n.duration_beats;
                    const double end = n.start_beat + n.duration_beats;
                    n.start_beat = std::clamp(snap(n.start_beat), 0.0, pat_len - cell_beats);
                    const double snapped_end = std::clamp(snap(end), n.start_beat + cell_beats, pat_len);
                    n.duration_beats = std::max(cell_beats, snapped_end - n.start_beat);
                    changed = changed || old_start != n.start_beat || old_dur != n.duration_beats;
                }
                if (changed) {
                    undo_stack_.push_back({std::move(pre_notes), std::move(pre_sel)});
                    if (static_cast<int>(undo_stack_.size()) > kMaxUndoDepth)
                        undo_stack_.erase(undo_stack_.begin());
                    redo_stack_.clear();
                    commit_editor_notes(ctx);
                }
            }
        }
    }
    rebuild_visible_note_indices();

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
            vivid::ui::draw_range_brace(d, o,
                vivid::ui::Rect{grid_x, brace_y, grid_w, brace_h},
                loop_lx, loop_rx,
                {0.25f, 0.55f, 0.90f, 0.25f},
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
        const float pb_x    = beat_view.world_to_screen(pb_beat);
        if (pb_x >= grid_x && pb_x <= grid_x + grid_w)
            vivid::ui::draw_playhead_line(d, o, pb_x, brace_y, brace_h,
                {1.0f, 0.8f, 0.2f, 0.7f});

        // Paste cursor (green vertical line, shows where next Cmd+V will land)
        if (paste_cursor_beat_ >= 0.0 && !editor_clipboard_.empty()) {
            const float pcx = beat_view.world_to_screen(static_cast<float>(paste_cursor_beat_));
            if (pcx >= grid_x && pcx <= grid_x + grid_w)
                vivid::ui::draw_playhead_line(d, o, pcx, brace_y, brace_h,
                    {0.35f, 0.85f, 0.40f, 0.90f});
        }
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
        vivid::ui::draw_timeline_grid(d, o,
            vivid::ui::Rect{grid_x, grid_y, grid_w, roll_h},
            beat_view, cell_beats, 4.0,
            {th.separator.r, th.separator.g, th.separator.b, 1.0f});
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
        for (int idx : visible_note_indices) {
            const auto& n = editor_notes_[idx];
            float ny  = y_fn(static_cast<int>(n.pitch)) + grid_y;
            float nx  = grid_x + static_cast<float>((n.start_beat - editor_scroll_x_) * beat_w);
            float nw  = std::max(4.0f, static_cast<float>(n.duration_beats * beat_w) - 1.0f);

            if (!std::isfinite(nx) || !std::isfinite(ny) || !std::isfinite(nw)) continue;
            if (ny + row_h_ < grid_y || ny > grid_y + roll_h) continue;
            if (nx + nw < grid_x || nx > grid_x + grid_w) continue;

            bool active   = (idx == drag_note_idx_ && drag_mode_ != DragMode::None);
            bool strip_hl = (!active && idx == mod_hover_idx);
            bool selected = (idx < (int)note_selected_.size() && note_selected_[idx]);
            const VividColor fill =
                active     ? VividColor{0.55f, 0.80f, 1.0f, 1.0f}
                : selected ? VividColor{0.95f, 0.75f, 0.20f, 0.95f}
                : strip_hl ? VividColor{0.42f, 0.72f, 1.0f, 0.97f}
                           : VividColor{0.30f, 0.65f, 0.95f, 0.92f};
            const VividColor sheen =
                active     ? VividColor{0.80f, 0.95f, 1.0f, 0.60f}
                : selected ? VividColor{1.0f,  0.92f, 0.55f, 0.55f}
                : strip_hl ? VividColor{0.70f, 0.90f, 1.0f, 0.50f}
                           : VividColor{0.50f, 0.80f, 1.0f, 0.40f};
            const float note_y = ny + 1.5f;
            const float note_h = row_h_ - 3.0f;
            if (nw < 10.0f || note_h < 8.0f) {
                d.draw_rect(o, nx, note_y, nw, note_h, fill);
                d.draw_rect(o, nx, note_y, nw, std::min(2.0f, note_h), sheen);
            } else {
                d.draw_rounded_rect(o, nx, note_y, nw, note_h, 2.0f, fill);
                d.draw_rounded_rect(o, nx, note_y, nw, note_h, 2.0f, sheen);
            }
            if (nw > 10.0f) {
                d.draw_rect(o, nx + nw - 4.0f, ny + 2.5f, 3.0f, row_h_ - 5.0f,
                    {0.75f, 0.90f, 1.0f, (active || strip_hl) ? 0.9f : 0.5f});
            }
        }
    }

    // --- Box-select rect ---
    if (drag_mode_ == DragMode::BoxSelect) {
        const float bx1 = grid_x + static_cast<float>((box_sel_start_beat_ - editor_scroll_x_) * beat_w);
        const float bx2 = ctx->mouse.x;
        const float by1 = grid_y + y_fn(box_sel_start_pitch_);
        const float by2 = ctx->mouse.y;
        const float rx = std::min(bx1, bx2);
        const float ry = std::min(by1, by2);
        const float rw = std::fabs(bx2 - bx1);
        const float rh = std::fabs(by2 - by1);
        vivid::ui::draw_selection_rect(d, o,
            vivid::ui::Rect{rx, ry, rw, rh},
            {0.50f, 0.75f, 1.0f, 1.0f});
    }

    // --- Playhead (roll) ---
    {
        const float phase_beat = static_cast<float>(editor_loop_origin + phase * editor_loop_len);
        const float phx = beat_view.world_to_screen(phase_beat);
        if (phx >= grid_x && phx <= grid_x + grid_w)
            vivid::ui::draw_playhead_line(d, o, phx, grid_y, roll_h,
                {1.0f, 0.8f, 0.2f, 0.85f});
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
            vivid::ui::draw_compact_strip_background(d, o,
                vivid::ui::Rect{grid_x, sy, grid_w, kModStripH},
                label,
                {th.separator.r, th.separator.g, th.separator.b, 0.5f},
                {0.08f, 0.08f, 0.10f, 1.0f},
                {0.40f, 0.40f, 0.45f, 0.8f},
                kModSepH);
        };

        // Velocity
        draw_strip_bg(vel_y, "vel");
        d.push_clip_rect(o, grid_x, vel_y, grid_w, kModStripH);
        for (int i : visible_note_indices) {
            const auto& n = editor_notes_[i];
            float bx  = grid_x + static_cast<float>((n.start_beat - editor_scroll_x_) * beat_w);
            if (!std::isfinite(bx)) continue;
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
        for (int i : visible_note_indices) {
            const auto& n = editor_notes_[i];
            if (n.pitch_bend == 0.0f) continue;
            float bx     = grid_x + static_cast<float>((n.start_beat - editor_scroll_x_) * beat_w);
            if (!std::isfinite(bx)) continue;
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
        for (int i : visible_note_indices) {
            const auto& n = editor_notes_[i];
            if (n.pressure == 0.0f) continue;
            float bx  = grid_x + static_cast<float>((n.start_beat - editor_scroll_x_) * beat_w);
            if (!std::isfinite(bx)) continue;
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
        constexpr float kPad = 8.0f;
        constexpr float kTopY = 5.0f;
        constexpr float kBottomY = 35.0f;
        constexpr float kRowH = 24.0f;
        constexpr float kSectionGap = 8.0f;

        ToolbarRow top_row = toolbar_row(Rect{0.0f, kTopY, sw, kRowH}, kPad, kSectionGap);
        const Rect actions_bounds = toolbar_reserve_right(
            top_row, toolbar_actions_open_ ? 244.0f : 136.0f);

        // Top row: clip identity and safe actions.
        {
            ToolbarSection clip = toolbar_section(*ctx, top_row, "CLIP",
                                                  top_row.end_x - top_row.cursor_x,
                                                  160.0f, 44.0f);
            std::string source = "Authored";
            if (!file.str_value.empty())
                source = std::filesystem::path(file.str_value).filename().string();
            else if (!clip_data_source_file_.empty())
                source = std::filesystem::path(clip_data_source_file_).filename().string();
            else if (!clip_data_ref_.str_value.empty())
                source = "Edited clip";
            if (source.size() > 34) source = source.substr(0, 32) + "..";

            char meta[96];
            const size_t note_count = file_backed
                ? file_note_count_
                : (clip_ref_backed ? clip_data_note_count_ : editor_notes_.size());
            if (file_backed && !file_error_.empty()) {
                std::snprintf(meta, sizeof(meta), "%s", file_error_.c_str());
            } else if (ctx->time < import_status_until_ && !import_status_.empty()) {
                std::snprintf(meta, sizeof(meta), "%s", import_status_.c_str());
            } else {
                std::snprintf(meta, sizeof(meta), "%.0f bars  %zu notes", lb_val, note_count);
            }

            toolbar_text(*ctx, toolbar_item(clip, std::min(220.0f, clip.content.w * 0.48f)),
                         source.c_str(),
                         {th.bright_text.r, th.bright_text.g, th.bright_text.b, 0.95f},
                         0.82f);
            toolbar_text(*ctx, toolbar_remaining(clip), meta,
                         file_error_.empty() ? VividColor{0.52f, 0.68f, 0.82f, 0.88f}
                                             : VividColor{1.0f, 0.45f, 0.35f, 0.95f},
                         0.76f);
        }

        {
            ToolbarRow actions_row = toolbar_row(actions_bounds, 0.0f, 6.0f);
            ToolbarSection actions_section = toolbar_section(*ctx, actions_row, "ACTIONS",
                                                             actions_bounds.w, 118.0f, 58.0f);
            const bool show_clear = toolbar_actions_open_ && actions_section.content.w >= 184.0f;
            if (show_clear) {
                const bool long_or_imported = file_backed || clip_ref_backed ||
                    editor_notes_.size() > 64 || lb_val > 16.0f;
                const char* clear_label = (long_or_imported && !toolbar_clear_confirm_)
                    ? "Clear Clip..." : "Clear Clip";
                auto clear_result = ui_button(*ctx, toolbar_item(actions_section, 92.0f),
                    clear_label, toolbar_clear_confirm_,
                    {0.22f, 0.14f, 0.14f, 0.95f},
                    {0.55f, 0.18f, 0.15f, 1.0f});
                if (clear_result.clicked) {
                    if (long_or_imported && !toolbar_clear_confirm_) {
                        toolbar_clear_confirm_ = true;
                        import_status_ = "Click Clear Clip again";
                        import_status_until_ = ctx->time + 3.0;
                    } else {
                        push_undo_snapshot();
                        editor_notes_.clear();
                        note_selected_.clear();
                        toolbar_clear_confirm_ = false;
                        toolbar_actions_open_ = false;
                        commit_editor_notes(ctx);
                    }
                }
            }

            auto actions = ui_button(*ctx,
                show_clear ? toolbar_item(actions_section, 84.0f)
                           : toolbar_remaining(actions_section),
                toolbar_actions_open_ ? "Close" : "Actions",
                toolbar_actions_open_);
            if (actions.clicked) {
                toolbar_actions_open_ = !toolbar_actions_open_;
                toolbar_clear_confirm_ = false;
            }
        }

        // Bottom row: timing, musical view, and navigation controls.
        ToolbarRow bottom_row = toolbar_row(Rect{0.0f, kBottomY, sw, kRowH}, kPad, kSectionGap);
        {
            ToolbarSection time = toolbar_section(*ctx, bottom_row, "TIME",
                                                  file_backed ? 318.0f : 360.0f,
                                                  file_backed ? 280.0f : 320.0f,
                                                  42.0f);
            toolbar_text(*ctx, toolbar_item(time, 28.0f), "Len",
                         {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.72f}, 0.76f);

            if (!length_field_state_.focused)
                std::snprintf(length_field_buf_, sizeof(length_field_buf_), "%.4g", lb_val);

            if (file_backed) {
                char len_label[32];
                std::snprintf(len_label, sizeof(len_label), "%.0f bars", lb_val);
                toolbar_value_pill(*ctx, toolbar_item(time, 82.0f), len_label, true);
            } else {
                Rect lf = toolbar_item(time, 58.0f);
                auto lf_r = ui_text_field(*ctx, lf, length_field_buf_,
                                          sizeof(length_field_buf_), &length_field_state_, "bars");
                if (lf_r.committed) {
                    float v = static_cast<float>(std::atof(length_field_buf_));
                    v = std::clamp(v, 0.25f, 8192.0f);
                    if (ctx->commands.set_param)
                        ctx->commands.set_param(ctx->commands.opaque, "length_bars", v);
                }
                if (lf_r.cancelled)
                    std::snprintf(length_field_buf_, sizeof(length_field_buf_), "%.4g", lb_val);
                toolbar_text(*ctx, toolbar_item(time, 34.0f), "bars",
                             {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.60f}, 0.76f);
            }

            toolbar_text(*ctx, toolbar_item(time, 36.0f), "Grid",
                         {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.72f}, 0.76f);
            static const char* kGridLabels[] = {"1/32","1/16","1/8","1/4"};
            Rect gr = toolbar_item(time, 128.0f);
            auto gr_result = ui_radio(*ctx, gr, kGridLabels, 4, grid_idx);
            if (gr_result.clicked && ctx->commands.set_param)
                ctx->commands.set_param(ctx->commands.opaque, "quantize_grid",
                    static_cast<float>(gr_result.value));
        }

        {
            ToolbarSection music = toolbar_section(*ctx, bottom_row, "MUSIC",
                                                   286.0f, 244.0f, 56.0f);
            static const char* kRootNames[] = {
                "C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};
            static const char* kTypeNames[] = {"Maj","Min","Hrm","PM","Pm"};

            auto fold_result = ui_toggle(*ctx, toolbar_item(music, 48.0f),
                                         "Fold", fold_rows_);
            if (fold_result.clicked) {
                fold_rows_ = fold_result.value;
                scroll_y_  = 0.0f;
            }

            char root_label[16];
            if (scale_root_ < 0) std::snprintf(root_label, sizeof(root_label), "Root -");
            else std::snprintf(root_label, sizeof(root_label), "Root %s", kRootNames[scale_root_]);
            if (ui_button(*ctx, toolbar_item(music, 72.0f),
                          root_label, scale_root_ >= 0).clicked) {
                scale_root_ = (scale_root_ < 11) ? scale_root_ + 1 : -1;
            }

            char type_label[16];
            std::snprintf(type_label, sizeof(type_label), "%s", kTypeNames[scale_type_]);
            if (ui_button(*ctx, toolbar_item(music, 54.0f),
                          type_label, scale_root_ >= 0).clicked) {
                scale_type_ = (scale_type_ + 1) % 5;
                if (scale_root_ < 0) scale_root_ = 0;
            }
        }

        {
            ToolbarSection nav = toolbar_section(*ctx, bottom_row, "NAV",
                                                 bottom_row.end_x - bottom_row.cursor_x,
                                                 236.0f, 36.0f);
            if (ui_button(*ctx, toolbar_item(nav, 46.0f), "Out").clicked) {
                float new_zoom = std::min(full_beats, zoom_beats * 1.5f);
                editor_zoom_beats_ = (new_zoom >= full_beats) ? 0.0f : new_zoom;
                editor_scroll_x_   = std::clamp(editor_scroll_x_, 0.0f,
                                        std::max(0.0f, full_beats - new_zoom));
                zoom_beats = new_zoom;
            }

            if (ui_button(*ctx, toolbar_item(nav, 38.0f), "In").clicked) {
                float new_zoom = std::max(static_cast<float>(bpb), zoom_beats / 1.5f);
                editor_zoom_beats_ = new_zoom;
                editor_scroll_x_   = std::clamp(editor_scroll_x_, 0.0f,
                                        std::max(0.0f, full_beats - new_zoom));
                zoom_beats = new_zoom;
            }

            if (ui_button(*ctx, toolbar_item(nav, 38.0f), "Fit",
                          editor_zoom_beats_ <= 0.0f).clicked) {
                editor_zoom_beats_ = 0.0f;
                editor_scroll_x_ = 0.0f;
                zoom_beats = full_beats;
            }

            const int total_bars = std::max(1, static_cast<int>(std::ceil(full_beats / bpb)));
            const int start_bar = std::clamp(static_cast<int>(std::floor(editor_scroll_x_ / bpb)) + 1,
                                             1, total_bars);
            const int end_bar = std::clamp(static_cast<int>(
                std::ceil((editor_scroll_x_ + zoom_beats) / bpb)), start_bar, total_bars);
            char range_label[48];
            std::snprintf(range_label, sizeof(range_label), "Bars %d-%d / %d",
                          start_bar, end_bar, total_bars);
            toolbar_text(*ctx, toolbar_remaining(nav), range_label,
                         {0.58f, 0.62f, 0.68f, 0.90f}, 0.76f);
        }
    }

    // -----------------------------------------------------------------------
    // Mouse interaction
    // -----------------------------------------------------------------------
    {
        enum class HitZone { None, Body, ResizeRight };
        struct NoteHit { int idx = -1; HitZone zone = HitZone::None; };

        auto hit_test = [&](float px, float py) -> NoteHit {
            if (!vivid::ui::Rect{grid_x, grid_y, grid_w, roll_h}.contains(px, py))
                return {};
            const int    hover_pitch = p_fn(py - grid_y);
            const double hover_beat  = beat_view.screen_to_world(px);
            for (int i : visible_note_indices) {
                const auto& n = editor_notes_[i];
                if (static_cast<int>(n.pitch) != hover_pitch) continue;
                if (hover_beat < n.start_beat ||
                    hover_beat >= n.start_beat + n.duration_beats) continue;
                float note_right_px = beat_view.world_to_screen(n.start_beat + n.duration_beats);
                if (!std::isfinite(note_right_px)) continue;
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
            const auto range_hit = vivid::ui::hit_test_range(
                vivid::ui::Rect{grid_x, brace_y, grid_w, brace_h},
                beat_view, ls_param, le_param, 8.0f,
                ctx->mouse.x, ctx->mouse.y);
            if (has_loop_region && range_hit.zone == vivid::ui::RangeDragMode::Left) {
                drag_mode_           = DragMode::LoopBraceLeft;
                drag_orig_loop_end_  = le_param;
            } else if (has_loop_region && range_hit.zone == vivid::ui::RangeDragMode::Right) {
                drag_mode_            = DragMode::LoopBraceRight;
                drag_orig_loop_start_ = ls_param;
            } else if (has_loop_region && range_hit.zone == vivid::ui::RangeDragMode::Body) {
                drag_mode_            = DragMode::LoopBraceBody;
                drag_orig_loop_start_ = ls_param;
                drag_orig_loop_end_   = le_param;
            } else {
                drag_mode_            = DragMode::LoopBraceSweep;
                drag_orig_loop_start_ = beat_view.screen_to_world(ctx->mouse.x);
            }
            if (ctx->host.capture_pointer)
                ctx->host.capture_pointer(ctx->host.opaque);
        }
        // Right-click in brace: clear loop region, or set paste cursor if no loop region
        if (mouse_in_brace && ctx->mouse.right_clicked) {
            if (has_loop_region) {
                if (ctx->commands.set_param) {
                    ctx->commands.set_param(ctx->commands.opaque, "loop_start_beat", 0.0f);
                    ctx->commands.set_param(ctx->commands.opaque, "loop_end_beat",   0.0f);
                }
            } else if (!editor_clipboard_.empty()) {
                paste_cursor_beat_ = midi_clip::quantize_to_grid(
                    beat_view.screen_to_world(ctx->mouse.x), grid_idx);
            }
        }

        // --- Mod strip press ---
        auto begin_strip_drag = [&](int best, DragMode mode) {
            if (!ctx->mouse.left_clicked || best < 0 || drag_mode_ != DragMode::None) return;
            push_undo_snapshot();
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
                push_undo_snapshot();
                editor_notes_[hov_vel].velocity = 0.8f; commit_editor_notes(ctx);
            } else if (hov_pb >= 0) {
                push_undo_snapshot();
                editor_notes_[hov_pb].pitch_bend = 0.0f; commit_editor_notes(ctx);
            } else if (hov_pr >= 0) {
                push_undo_snapshot();
                editor_notes_[hov_pr].pressure = 0.0f; commit_editor_notes(ctx);
            }
        }

        // --- Roll press ---
        if (ctx->mouse.left_clicked && mouse_in_roll) {
            const bool shift = ctx->mouse.shift_down;
            auto hit = hit_test(ctx->mouse.x, ctx->mouse.y);
            if (hit.idx >= 0) {
                if (shift) {
                    // Shift+click: toggle selection without starting a drag
                    if (hit.idx < (int)note_selected_.size())
                        note_selected_[hit.idx] = !note_selected_[hit.idx];
                } else {
                    // Plain click on note: exclusive select + start drag
                    note_selected_.assign(editor_notes_.size(), false);
                    if (hit.idx < (int)note_selected_.size())
                        note_selected_[hit.idx] = true;
                    auto& n = editor_notes_[hit.idx];
                    drag_note_idx_   = hit.idx;
                    drag_start_mx_   = ctx->mouse.x;
                    drag_start_my_   = ctx->mouse.y;
                    drag_orig_start_ = n.start_beat;
                    drag_orig_dur_   = n.duration_beats;
                    drag_orig_pitch_ = n.pitch;
                    push_undo_snapshot();
                    drag_orig_notes_ = editor_notes_;
                    drag_selected_indices_.clear();
                    for (int j = 0; j < (int)note_selected_.size(); ++j) {
                        if (note_selected_[j])
                            drag_selected_indices_.push_back(j);
                    }
                    drag_mode_ = (hit.zone == HitZone::ResizeRight)
                        ? DragMode::ResizingNote : DragMode::MovingNote;
                    if (ctx->host.capture_pointer)
                        ctx->host.capture_pointer(ctx->host.opaque);
                }
            } else {
                int pitch = p_fn(my);
                if (pitch >= 0 && pitch <= 127) {
                    drag_start_mx_       = ctx->mouse.x;
                    drag_start_my_       = ctx->mouse.y;
                    if (shift) {
                        box_sel_start_beat_  = beat_from_x(mx, editor_scroll_x_,
                                                           static_cast<float>(inv_bw));
                        box_sel_start_pitch_ = pitch;
                        box_sel_additive_    = true;
                        drag_mode_           = DragMode::BoxSelect;
                    } else {
                        note_selected_.assign(editor_notes_.size(), 0);
                        double start = midi_clip::quantize_to_grid(
                            beat_from_x(mx, editor_scroll_x_, static_cast<float>(inv_bw)),
                            grid_idx);
                        start = std::clamp(start, 0.0, pat_len - cell_beats);
                        midi_clip::ParsedNote n{};
                        n.pitch = static_cast<uint8_t>(pitch);
                        n.start_beat = start;
                        n.duration_beats = cell_beats;
                        n.velocity = 0.8f;
                        push_undo_snapshot();
                        editor_notes_.push_back(n);
                        note_selected_.push_back(true);
                        drag_note_idx_ = static_cast<int>(editor_notes_.size()) - 1;
                        drag_orig_start_ = start;
                        drag_orig_dur_ = cell_beats;
                        drag_orig_pitch_ = n.pitch;
                        drag_orig_notes_ = editor_notes_;
                        drag_selected_indices_ = {drag_note_idx_};
                        drag_mode_ = DragMode::AddingNote;
                        commit_editor_notes(ctx);
                    }
                    if (ctx->host.capture_pointer)
                        ctx->host.capture_pointer(ctx->host.opaque);
                }
            }
        }

        if (ctx->mouse.right_clicked && mouse_in_roll && drag_mode_ == DragMode::None) {
            auto hit = hit_test(ctx->mouse.x, ctx->mouse.y);
            if (hit.idx >= 0) {
                push_undo_snapshot();
                editor_notes_.erase(editor_notes_.begin() + hit.idx);
                if (hit.idx < (int)note_selected_.size())
                    note_selected_.erase(note_selected_.begin() + hit.idx);
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

            // Box-select live update
            if (drag_mode_ == DragMode::BoxSelect) {
                const double cur_beat  = beat_from_x(mx, editor_scroll_x_,
                                                     static_cast<float>(inv_bw));
                const int    cur_pitch = p_fn(my);
                const double min_beat  = std::min(box_sel_start_beat_, cur_beat);
                const double max_beat  = std::max(box_sel_start_beat_, cur_beat);
                const int    min_pitch = std::min(box_sel_start_pitch_,
                                                  std::max(0, cur_pitch));
                const int    max_pitch = std::max(box_sel_start_pitch_,
                                                  std::max(0, cur_pitch));
                for (int j = 0; j < (int)editor_notes_.size(); ++j) {
                    if (j >= (int)note_selected_.size()) continue;
                    const auto& n = editor_notes_[j];
                    bool in_box = (n.start_beat >= min_beat && n.start_beat <= max_beat &&
                                   (int)n.pitch >= min_pitch && (int)n.pitch <= max_pitch);
                    // Additive when shift was held at drag start
                    if (!box_sel_additive_)
                        note_selected_[j] = in_box;
                    else if (in_box)
                        note_selected_[j] = true;
                }
            }

            // Note drags
            if (drag_note_idx_ >= 0 &&
                drag_note_idx_ < static_cast<int>(editor_notes_.size())) {
                auto& n = editor_notes_[drag_note_idx_];

                if (drag_mode_ == DragMode::MovingNote) {
                    const double snapped_delta = snap(drag_orig_start_ + dmx * inv_bw) - drag_orig_start_;
                    const int drow = static_cast<int>(std::round(dmy / row_h_));
                    bool changed = false;
                    for (int idx : drag_selected_indices_) {
                        if (idx < 0 || idx >= (int)editor_notes_.size() ||
                            idx >= (int)drag_orig_notes_.size()) {
                            continue;
                        }
                        const auto& orig = drag_orig_notes_[idx];
                        auto& dst_note = editor_notes_[idx];
                        double new_start = std::clamp(orig.start_beat + snapped_delta,
                                                      0.0, pat_len - orig.duration_beats);
                        int new_pitch = std::clamp(static_cast<int>(orig.pitch) - drow, 0, 127);
                        changed = changed || dst_note.start_beat != new_start ||
                                  static_cast<int>(dst_note.pitch) != new_pitch;
                        dst_note.start_beat = new_start;
                        dst_note.pitch = static_cast<uint8_t>(new_pitch);
                    }
                    if (changed) commit_editor_notes(ctx);
                } else if (drag_mode_ == DragMode::ResizingNote) {
                    bool changed = false;
                    for (int idx : drag_selected_indices_) {
                        if (idx < 0 || idx >= (int)editor_notes_.size() ||
                            idx >= (int)drag_orig_notes_.size()) {
                            continue;
                        }
                        const auto& orig = drag_orig_notes_[idx];
                        auto& dst_note = editor_notes_[idx];
                        double raw_right = orig.start_beat + orig.duration_beats + dmx * inv_bw;
                        double new_dur = std::max(cell_beats, snap(raw_right) - orig.start_beat);
                        new_dur = std::min(new_dur, pat_len - orig.start_beat);
                        changed = changed || dst_note.duration_beats != new_dur;
                        dst_note.duration_beats = new_dur;
                    }
                    if (changed) commit_editor_notes(ctx);
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
            drag_orig_notes_.clear();
            drag_selected_indices_.clear();
        }
    }

    if (ctx->commands.set_param) {
        auto set_if_changed = [&](const char* name, float current, float next) {
            if (std::fabs(current - next) > 1e-4f)
                ctx->commands.set_param(ctx->commands.opaque, name, next);
        };
        set_if_changed("_editor_fold", editor_fold_.bool_value() ? 1.0f : 0.0f,
                       fold_rows_ ? 1.0f : 0.0f);
        set_if_changed("_editor_scale_root", static_cast<float>(editor_scale_root_.int_value()),
                       static_cast<float>(scale_root_));
        set_if_changed("_editor_scale_type", static_cast<float>(editor_scale_type_.int_value()),
                       static_cast<float>(scale_type_));
        set_if_changed("_editor_zoom_beats", editor_zoom_beat_.value, editor_zoom_beats_);
        set_if_changed("_editor_scroll_beat", editor_scroll_beat_.value, editor_scroll_x_);
        set_if_changed("_editor_row_height", editor_row_height_.value, row_h_);
    }
}
