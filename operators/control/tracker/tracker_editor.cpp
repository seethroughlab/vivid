// Dedicated editor window for Tracker. Multi-channel pattern grid
// (Protracker / Renoise / FastTracker lineage): rows down, 8 channels
// across, each channel showing note + velocity + effect columns.
//
// Keyboard-first — note entry on the piano row, hex entry in velocity
// and effect fields. Mouse places cursor and scrolls. Pattern data is
// the source of truth; the editor deserializes pattern_data at the top
// of the frame, mutates the working song, serializes back via
// commands.set_string_param when anything changes.

#include "tracker_core.h"
#include "tracker_editor_shared.h"
#include "operator_api/draw_ui_helpers.h"
#include "operator_api/editor_keys.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace trk_ed {

constexpr float kInset      = 8.0f;
constexpr float kTopBarH    = 28.0f;
constexpr float kHeaderH    = 18.0f;
constexpr float kRowH       = 16.0f;
constexpr float kRowNumW    = 42.0f;
constexpr float kNoteW      = 30.0f;
constexpr float kVelW       = 22.0f;
constexpr float kFxW        = 30.0f;
constexpr float kChGap      = 6.0f;
constexpr float kSidePanelW = 240.0f;

constexpr float channel_w() {
    return kNoteW + 2.0f + kVelW + 2.0f + kFxW;
}
constexpr float channels_total_w() {
    return (channel_w() + kChGap) * ::tracker::MAX_CHANNELS - kChGap;
}

// Per-channel header colours — reused for column tinting so eyes can
// latch onto a channel mid-pattern.
constexpr float kChColors[8][3] = {
    {0.39f, 0.63f, 0.86f}, {0.86f, 0.47f, 0.31f},
    {0.31f, 0.78f, 0.55f}, {0.78f, 0.71f, 0.24f},
    {0.63f, 0.39f, 0.78f}, {0.24f, 0.71f, 0.78f},
    {0.78f, 0.39f, 0.63f}, {0.55f, 0.78f, 0.31f},
};

struct FieldLayout {
    ::vivid::ui::Rect note, vel, fx;
};

inline FieldLayout channel_layout(float grid_x, int ch, float row_y) {
    const float chx = grid_x + kRowNumW + 4.0f
                    + static_cast<float>(ch) * (channel_w() + kChGap);
    FieldLayout L;
    L.note = {chx,                         row_y, kNoteW, kRowH};
    L.vel  = {chx + kNoteW + 2.0f,         row_y, kVelW,  kRowH};
    L.fx   = {chx + kNoteW + kVelW + 4.0f, row_y, kFxW,   kRowH};
    return L;
}

// Reverse of channel_layout: given a click position, resolve (channel,
// field). Returns false when the click isn't on a cell column.
inline bool hit_test_channel(float grid_x, float mx,
                             int* ch_out, ::vivid::tracker_editor::Field* f_out) {
    namespace te = ::vivid::tracker_editor;
    for (int ch = 0; ch < ::tracker::MAX_CHANNELS; ++ch) {
        FieldLayout L = channel_layout(grid_x, ch, 0.0f);
        if (mx >= L.note.x && mx < L.note.x + L.note.w) {
            *ch_out = ch; *f_out = te::Field::Note; return true;
        }
        if (mx >= L.vel.x && mx < L.vel.x + L.vel.w) {
            *ch_out = ch; *f_out = te::Field::Velocity; return true;
        }
        if (mx >= L.fx.x && mx < L.fx.x + L.fx.w) {
            *ch_out = ch; *f_out = te::Field::Effect; return true;
        }
    }
    return false;
}

} // namespace trk_ed


VividEditorMetadata TrackerCore::editor_metadata() {
    VividEditorMetadata m{};
    m.default_width  = 1200;
    m.default_height = 720;
    m.min_width      = 900;
    m.min_height     = 500;
    m.title_suffix   = "Tracker Editor";
    return m;
}

void TrackerCore::draw_editor(VividEditorContext* ctx) {
    if (!ctx) return;
    namespace ed = ::trk_ed;
    namespace ek = ::vivid::editor_keys;
    namespace te = ::vivid::tracker_editor;

    auto& d = ctx->draw;
    void* o = d.opaque;
    const auto& th = ctx->theme;

    // ---- Deserialize the pattern into a working copy. Mutations land
    //      on this copy and ride back via set_string_param. ----
    tracker::TrackerSong work_song;
    const bool has_data = !pattern_data.str_value.empty() &&
        tracker::deserialize_song(pattern_data.str_value, work_song);
    if (!has_data) work_song = tracker::TrackerSong{};

    const int pat_idx = std::clamp(edit_pattern.int_value(), 0,
                                   static_cast<int>(work_song.num_patterns) - 1);
    auto& pat = work_song.patterns[pat_idx];
    const int num_rows = std::max<int>(1, pat.num_rows);

    // ---- Clamp editor state into the current pattern bounds. ----
    te::clamp_cursor(num_rows,
                     &editor_cursor_row_, &editor_cursor_channel_,
                     &editor_cursor_field_, &editor_cursor_effect_char_);

    // ---- Layout ----
    const float surf_w = ctx->surface_width;
    const float surf_h = ctx->surface_height;
    const float top_y  = ed::kInset;
    const float top_h  = ed::kTopBarH;

    const float grid_x = ed::kInset;
    const float grid_y = top_y + top_h + ed::kInset;
    const float grid_w = std::max(0.0f,
        surf_w - 3.0f * ed::kInset - ed::kSidePanelW);
    const float grid_h = std::max(0.0f, surf_h - grid_y - ed::kInset);

    const float rows_y = grid_y + ed::kHeaderH;
    const float rows_h = std::max(0.0f, grid_h - ed::kHeaderH);
    const int visible_rows = static_cast<int>(rows_h / ed::kRowH);

    const float side_x = grid_x + grid_w + ed::kInset;
    const float side_y = grid_y;
    const float side_w = ed::kSidePanelW;
    const float side_h = grid_h;

    // ---- Convenience mutators ----
    bool song_changed = false;
    auto commit_song = [&]() {
        if (!ctx->commands.set_string_param) return;
        ctx->commands.set_string_param(ctx->commands.opaque, "pattern_data",
            tracker::serialize_song(work_song).c_str());
        song_changed = true;
    };
    auto set_named_int = [&](const char* name, int v) {
        if (ctx->commands.set_param)
            ctx->commands.set_param(ctx->commands.opaque, name,
                                    static_cast<float>(v));
    };
    auto reset_hex_accum = [&]() {
        editor_vel_chars_ = 0;
        editor_fx_chars_  = 0;
    };
    auto sanitize_selection = [&]() {
        if (editor_selection_row_lo_ < 0 || editor_selection_row_hi_ < 0) return;
        editor_selection_row_lo_ = std::clamp(editor_selection_row_lo_, 0, num_rows - 1);
        editor_selection_row_hi_ = std::clamp(editor_selection_row_hi_, 0, num_rows - 1);
        if (editor_selection_row_hi_ < editor_selection_row_lo_)
            std::swap(editor_selection_row_lo_, editor_selection_row_hi_);
    };
    sanitize_selection();

    auto clear_selection = [&]() {
        editor_selection_row_lo_ = editor_selection_row_hi_ = editor_selection_anchor_ = -1;
    };
    auto extend_selection_to_cursor = [&]() {
        if (editor_selection_anchor_ < 0)
            editor_selection_anchor_ = editor_cursor_row_;
        editor_selection_row_lo_ = std::min(editor_selection_anchor_, editor_cursor_row_);
        editor_selection_row_hi_ = std::max(editor_selection_anchor_, editor_cursor_row_);
    };

    // Follow-playhead: auto-scroll the view so current_row_ stays in sight.
    if (editor_follow_playhead_) {
        const int bottom = editor_scroll_row_ + visible_rows - 2;
        if (current_row_ > bottom)
            editor_scroll_row_ = std::max(0,
                std::min(current_row_ - (visible_rows - 2), num_rows - visible_rows));
        else if (current_row_ < editor_scroll_row_)
            editor_scroll_row_ = std::max(0, current_row_);
    }
    editor_scroll_row_ = std::clamp(editor_scroll_row_, 0,
                                    std::max(0, num_rows - visible_rows));

    // ---- Mouse: click to place cursor, wheel to scroll ----
    const auto& mouse = ctx->mouse;
    for (uint32_t i = 0; i < ctx->event_count; ++i) {
        const auto& e = ctx->events[i];
        if (e.type == VIVID_EDITOR_EVENT_MOUSE_SCROLL) {
            // 3 rows per wheel tick (GLFW scroll is tiny at 1.0).
            editor_scroll_row_ = std::clamp(
                editor_scroll_row_ - static_cast<int>(e.scroll_dy * 3.0f),
                0, std::max(0, num_rows - visible_rows));
        }
    }
    if (mouse.left_clicked && mouse.x >= grid_x && mouse.x < grid_x + grid_w &&
        mouse.y >= rows_y && mouse.y < rows_y + rows_h) {
        const int row_in_view = static_cast<int>((mouse.y - rows_y) / ed::kRowH);
        const int row = std::clamp(editor_scroll_row_ + row_in_view, 0, num_rows - 1);
        int ch;
        te::Field f;
        if (::trk_ed::hit_test_channel(grid_x, mouse.x, &ch, &f)) {
            if (mouse.shift_down) {
                if (editor_selection_anchor_ < 0)
                    editor_selection_anchor_ = editor_cursor_row_;
                editor_cursor_row_ = row;
                extend_selection_to_cursor();
            } else {
                editor_cursor_row_ = row;
                editor_cursor_channel_ = ch;
                editor_cursor_field_ = f;
                editor_cursor_effect_char_ = 0;
                editor_selection_anchor_ = row;
                editor_selection_row_lo_ = editor_selection_row_hi_ = row;
                reset_hex_accum();
            }
        }
    }

    // ---- Keyboard ----
    ctx->wants_keyboard = 1;
    for (uint32_t ei = 0; ei < ctx->event_count; ++ei) {
        const auto& e = ctx->events[ei];
        if (e.type != VIVID_EDITOR_EVENT_KEY) continue;
        if (e.action != ek::kPress && e.action != ek::kRepeat) continue;

        const bool shift = (e.modifiers & ek::kModShift) != 0;
        const bool cmd_or_ctrl = ek::is_cmd_or_ctrl(e.modifiers);
        auto& cell = pat.cells[editor_cursor_channel_][editor_cursor_row_];

        // --- Navigation ---
        int dx = 0, dy = 0;
        if      (e.key == ek::kLeft)  dx = -1;
        else if (e.key == ek::kRight) dx = +1;
        else if (e.key == ek::kUp)    dy = -1;
        else if (e.key == ek::kDown)  dy = +1;

        if (dx != 0 || dy != 0) {
            // Seed the selection anchor from the PRE-move cursor so the
            // very first shift+arrow includes the starting row.
            if (shift && editor_selection_anchor_ < 0)
                editor_selection_anchor_ = editor_cursor_row_;
            if (dy != 0) {
                editor_cursor_row_ = std::clamp(editor_cursor_row_ + dy, 0, num_rows - 1);
            }
            if (dx > 0) {
                // Left→right across fields, then to next channel.
                if (editor_cursor_field_ == te::Field::Note) {
                    editor_cursor_field_ = te::Field::Velocity;
                } else if (editor_cursor_field_ == te::Field::Velocity) {
                    editor_cursor_field_ = te::Field::Effect;
                    editor_cursor_effect_char_ = 0;
                } else {
                    if (editor_cursor_channel_ < ::tracker::MAX_CHANNELS - 1) {
                        ++editor_cursor_channel_;
                        editor_cursor_field_ = te::Field::Note;
                    }
                }
            } else if (dx < 0) {
                if (editor_cursor_field_ == te::Field::Effect) {
                    editor_cursor_field_ = te::Field::Velocity;
                } else if (editor_cursor_field_ == te::Field::Velocity) {
                    editor_cursor_field_ = te::Field::Note;
                } else {
                    if (editor_cursor_channel_ > 0) {
                        --editor_cursor_channel_;
                        editor_cursor_field_ = te::Field::Effect;
                        editor_cursor_effect_char_ = 0;
                    }
                }
            }
            reset_hex_accum();
            if (shift) extend_selection_to_cursor();
            else       clear_selection();
            continue;
        }

        // Jump to next / prev channel with Tab.
        if (e.key == ek::kTab) {
            if (shift && editor_cursor_channel_ > 0)
                --editor_cursor_channel_;
            else if (!shift && editor_cursor_channel_ < ::tracker::MAX_CHANNELS - 1)
                ++editor_cursor_channel_;
            editor_cursor_field_ = te::Field::Note;
            reset_hex_accum();
            continue;
        }
        if (e.key == ek::kHome) {
            editor_cursor_row_ = 0;
            reset_hex_accum();
            if (shift) extend_selection_to_cursor(); else clear_selection();
            continue;
        }
        if (e.key == ek::kEnd) {
            editor_cursor_row_ = num_rows - 1;
            reset_hex_accum();
            if (shift) extend_selection_to_cursor(); else clear_selection();
            continue;
        }
        if (e.key == ek::kPageUp) {
            editor_cursor_row_ = std::max(0, editor_cursor_row_ - 16);
            reset_hex_accum();
            if (shift) extend_selection_to_cursor(); else clear_selection();
            continue;
        }
        if (e.key == ek::kPageDown) {
            editor_cursor_row_ = std::min(num_rows - 1, editor_cursor_row_ + 16);
            reset_hex_accum();
            if (shift) extend_selection_to_cursor(); else clear_selection();
            continue;
        }

        // Pattern navigation: '-' prev, '=' next. Writes to the operator's
        // edit_pattern param so the change is visible everywhere.
        if (e.key == ek::kMinus && !cmd_or_ctrl) {
            set_named_int("edit_pattern",
                std::max(0, edit_pattern.int_value() - 1));
            reset_hex_accum();
            continue;
        }
        if (e.key == ek::kEqual && !cmd_or_ctrl) {
            set_named_int("edit_pattern",
                std::min(static_cast<int>(work_song.num_patterns) - 1,
                         edit_pattern.int_value() + 1));
            reset_hex_accum();
            continue;
        }

        // Octave shift: [ / ]
        if (e.key == ek::kLeftBracket) {
            editor_octave_ = std::max(0, editor_octave_ - 1);
            continue;
        }
        if (e.key == ek::kRightBracket) {
            editor_octave_ = std::min(8, editor_octave_ + 1);
            continue;
        }

        // Toggle follow-playhead. Only fires in the Note field — in
        // Velocity / Effect the same keycap is a hex digit.
        if (e.key == ek::kF && !cmd_or_ctrl &&
            editor_cursor_field_ == te::Field::Note) {
            editor_follow_playhead_ = !editor_follow_playhead_;
            continue;
        }

        // Delete / `.` → clear the current cell (whole cell, not just field).
        if (e.key == ek::kDelete || e.key == ek::kPeriod) {
            cell = ::tracker::TrackerCell{};
            reset_hex_accum();
            commit_song();
            continue;
        }
        if (e.key == ek::kBackspace) {
            // Reverse of entry: scrub the current field back to empty.
            if (editor_cursor_field_ == te::Field::Note) {
                cell.note = ::tracker::NOTE_EMPTY;
            } else if (editor_cursor_field_ == te::Field::Velocity) {
                cell.velocity = 0;
                editor_vel_chars_ = 0;
            } else {
                cell.effect_type = 0;
                cell.effect_param = 0;
                editor_fx_chars_ = 0;
            }
            commit_song();
            continue;
        }
        if (e.key == ek::kEscape) {
            reset_hex_accum();
            clear_selection();
            continue;
        }

        // Clipboard: Cmd+C / Cmd+V on row range (selection) — or single
        // cursor row if no selection.
        if (cmd_or_ctrl && e.key == ek::kC) {
            const int a = (editor_selection_row_lo_ >= 0)
                ? editor_selection_row_lo_ : editor_cursor_row_;
            const int b = (editor_selection_row_hi_ >= 0)
                ? editor_selection_row_hi_ : editor_cursor_row_;
            te::copy_rows(pat, a, b, &editor_row_clipboard_);
            continue;
        }
        if (cmd_or_ctrl && e.key == ek::kV) {
            const int written = te::paste_rows(
                pat, editor_cursor_row_, editor_row_clipboard_);
            if (written > 0) commit_song();
            continue;
        }

        // --- Note entry on piano-row keys ---
        if (editor_cursor_field_ == te::Field::Note && !cmd_or_ctrl) {
            int bump = 0;
            const int semi = te::piano_key_to_semitone(e.key, &bump);
            if (semi == te::kNoteOff) {
                cell.note = ::tracker::NOTE_OFF;
                commit_song();
                te::advance_cursor_row(num_rows, /*wrap*/false,
                                       &editor_cursor_row_, &editor_scroll_row_,
                                       visible_rows);
                continue;
            }
            if (semi != te::kNoNote) {
                const int octave = std::clamp(editor_octave_ + bump, 0, 9);
                const int midi = std::clamp((octave + 1) * 12 + semi, 0, 127);
                cell.note = static_cast<uint8_t>(midi);
                if (cell.velocity == 0) cell.velocity = 0x7F;  // default on first entry
                commit_song();
                te::advance_cursor_row(num_rows, /*wrap*/false,
                                       &editor_cursor_row_, &editor_scroll_row_,
                                       visible_rows);
                continue;
            }
        }

        // --- Hex entry (velocity / effect fields) ---
        if (!cmd_or_ctrl &&
            (editor_cursor_field_ == te::Field::Velocity ||
             editor_cursor_field_ == te::Field::Effect)) {
            // Map the keycode to a hex nibble. Use codepoint via CHAR events
            // would be ideal, but the inspector/edit loop already dispatches
            // from KEY events — keep that consistency.
            int nibble = -1;
            if (ek::is_digit_key(e.key)) {
                nibble = ek::digit_value(e.key);
            } else if (e.key >= ek::kA && e.key <= ek::kF) {
                nibble = 10 + (e.key - ek::kA);
            }
            if (nibble < 0) continue;

            if (editor_cursor_field_ == te::Field::Velocity) {
                if (editor_vel_chars_ == 0) {
                    cell.velocity = static_cast<uint8_t>(nibble << 4);
                    editor_vel_chars_ = 1;
                } else {
                    cell.velocity = static_cast<uint8_t>(
                        (cell.velocity & 0xF0) | (nibble & 0x0F));
                    editor_vel_chars_ = 0;
                    te::advance_cursor_row(num_rows, /*wrap*/false,
                                           &editor_cursor_row_, &editor_scroll_row_,
                                           visible_rows);
                }
                commit_song();
                continue;
            }
            // Effect: 3 chars (type nibble → param hi → param lo).
            if (editor_fx_chars_ == 0) {
                cell.effect_type  = static_cast<uint8_t>(nibble & 0x0F);
                cell.effect_param = 0;
                editor_fx_chars_  = 1;
            } else if (editor_fx_chars_ == 1) {
                cell.effect_param = static_cast<uint8_t>(nibble << 4);
                editor_fx_chars_  = 2;
            } else {
                cell.effect_param = static_cast<uint8_t>(
                    (cell.effect_param & 0xF0) | (nibble & 0x0F));
                editor_fx_chars_  = 0;
                te::advance_cursor_row(num_rows, /*wrap*/false,
                                       &editor_cursor_row_, &editor_scroll_row_,
                                       visible_rows);
            }
            commit_song();
            continue;
        }
    }

    // ---- Drawing ----
    vivid::draw_ui::draw_panel(d, o, grid_x, grid_y, grid_w, grid_h,
        {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.92f});

    // Top bar.
    if (d.draw_text) {
        char buf[96];
        std::snprintf(buf, sizeof(buf),
            "Pattern %d/%d   Rows %d   Oct %d%s",
            pat_idx + 1, work_song.num_patterns, num_rows, editor_octave_,
            editor_follow_playhead_ ? "   [Follow]" : "");
        d.draw_text(o, grid_x, top_y + 6.0f, buf,
            {th.bright_text.r, th.bright_text.g, th.bright_text.b, 0.95f},
            1.0f);

        const char* hints =
            "z-/ = notes  ·  0-9 A-F = hex  ·  Tab = chan  ·  "
            "-/= = pattern  ·  [/] = oct  ·  F = follow  ·  Cmd+C/V";
        const float hints_scale = 0.7f;
        const float hints_w = d.text_width
            ? d.text_width(o, hints, hints_scale) : 520.0f;
        d.draw_text(o, grid_x + grid_w - hints_w, top_y + 8.0f, hints,
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.7f}, hints_scale);
    }

    // Channel headers.
    for (int ch = 0; ch < ::tracker::MAX_CHANNELS; ++ch) {
        const float chx = grid_x + ed::kRowNumW + 4.0f
                        + static_cast<float>(ch) * (ed::channel_w() + ed::kChGap);
        const VividColor bg{ed::kChColors[ch][0] * 0.3f,
                            ed::kChColors[ch][1] * 0.3f,
                            ed::kChColors[ch][2] * 0.3f, 0.9f};
        vivid::draw_ui::draw_panel(d, o, chx, grid_y,
            ed::channel_w(), ed::kHeaderH, bg);
        if (d.draw_text) {
            char label[12];
            std::snprintf(label, sizeof(label), "Ch %d", ch + 1);
            d.draw_text(o, chx + 4.0f, grid_y + 3.0f, label,
                {ed::kChColors[ch][0], ed::kChColors[ch][1],
                 ed::kChColors[ch][2], 0.95f}, 1.0f);
        }
    }

    // Row band: playhead, selection, cursor.
    const int first_row = editor_scroll_row_;
    const int last_row  = std::min(num_rows - 1, first_row + visible_rows - 1);

    // Selection highlight.
    if (editor_selection_row_lo_ >= 0 && d.draw_rect) {
        const int lo = std::max(editor_selection_row_lo_, first_row);
        const int hi = std::min(editor_selection_row_hi_, last_row);
        if (hi >= lo) {
            const float y  = rows_y + (lo - first_row) * ed::kRowH;
            const float h  = (hi - lo + 1) * ed::kRowH;
            d.draw_rect(o, grid_x, y, grid_w, h,
                {th.accent.r * 0.5f, th.accent.g * 0.5f,
                 th.accent.b * 0.5f, 0.25f});
        }
    }

    // Playhead row (live compute position).
    if (current_row_ >= first_row && current_row_ <= last_row && d.draw_rect) {
        const float y = rows_y + (current_row_ - first_row) * ed::kRowH;
        d.draw_rect(o, grid_x, y, grid_w, ed::kRowH,
            {th.accent.r, th.accent.g, th.accent.b, 0.25f});
    }

    // Beat separators every 4 rows.
    for (int r = first_row; r <= last_row; ++r) {
        if (r % 4 == 0 && d.draw_rect) {
            const float y = rows_y + (r - first_row) * ed::kRowH;
            d.draw_rect(o, grid_x, y, grid_w, 1.0f,
                {th.separator.r, th.separator.g, th.separator.b, 0.5f});
        }
    }

    // Row numbers + cells.
    char note_buf[4], vel_buf[3], fx_buf[4], row_buf[8];
    for (int r = first_row; r <= last_row; ++r) {
        const float row_y = rows_y + (r - first_row) * ed::kRowH;

        if (d.draw_text) {
            std::snprintf(row_buf, sizeof(row_buf), "%3d", r);
            d.draw_text(o, grid_x + 4.0f, row_y + 2.0f, row_buf,
                {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.75f}, 1.0f);
        }

        for (int ch = 0; ch < ::tracker::MAX_CHANNELS; ++ch) {
            const auto& cell = pat.cells[ch][r];
            const ::trk_ed::FieldLayout L =
                ::trk_ed::channel_layout(grid_x, ch, row_y);

            tracker::format_cell_note(cell.note, note_buf);
            tracker::format_cell_vel (cell.velocity, vel_buf);
            tracker::format_cell_fx  (cell.effect_type, cell.effect_param, fx_buf);

            const VividColor note_col =
                (cell.note == tracker::NOTE_EMPTY)
                    ? VividColor{th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.55f}
                    : VividColor{ed::kChColors[ch][0],
                                 ed::kChColors[ch][1],
                                 ed::kChColors[ch][2], 0.95f};
            const VividColor vel_col =
                (cell.velocity == 0)
                    ? VividColor{th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.5f}
                    : VividColor{th.bright_text.r, th.bright_text.g,
                                 th.bright_text.b, 0.9f};
            const VividColor fx_col =
                (cell.effect_type == 0 && cell.effect_param == 0)
                    ? VividColor{th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.5f}
                    : VividColor{0.9f, 0.7f, 0.3f, 0.95f};

            if (d.draw_text) {
                d.draw_text(o, L.note.x + 2.0f, L.note.y + 2.0f, note_buf,
                            note_col, 1.0f);
                d.draw_text(o, L.vel.x + 2.0f,  L.vel.y + 2.0f,  vel_buf,
                            vel_col, 1.0f);
                d.draw_text(o, L.fx.x + 2.0f,   L.fx.y + 2.0f,   fx_buf,
                            fx_col, 1.0f);
            }
        }
    }

    // Cursor outline.
    if (editor_cursor_row_ >= first_row && editor_cursor_row_ <= last_row) {
        const float row_y = rows_y + (editor_cursor_row_ - first_row) * ed::kRowH;
        const ::trk_ed::FieldLayout L =
            ::trk_ed::channel_layout(grid_x, editor_cursor_channel_, row_y);
        ::vivid::ui::Rect cursor_r = L.note;
        if (editor_cursor_field_ == te::Field::Velocity) cursor_r = L.vel;
        else if (editor_cursor_field_ == te::Field::Effect) cursor_r = L.fx;
        vivid::draw_ui::draw_panel(d, o,
            cursor_r.x, cursor_r.y, cursor_r.w, cursor_r.h,
            {0, 0, 0, 0},
            {th.bright_text.r, th.bright_text.g, th.bright_text.b, 1.0f},
            0.0f, 1.5f);
    }

    // ---- Side panel ----
    vivid::draw_ui::draw_panel(d, o, side_x, side_y, side_w, side_h,
        {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.85f},
        {th.separator.r, th.separator.g, th.separator.b, 0.8f}, 4.0f, 1.0f);
    if (d.draw_text) {
        constexpr float kSpPad = 10.0f;
        char line[128];

        std::snprintf(line, sizeof(line), "Row %d · Ch %d",
            editor_cursor_row_, editor_cursor_channel_ + 1);
        d.draw_text(o, side_x + kSpPad, side_y + kSpPad, line,
            {th.bright_text.r, th.bright_text.g, th.bright_text.b, 0.95f}, 1.0f);

        const char* field_name =
            editor_cursor_field_ == te::Field::Note ? "note"
          : editor_cursor_field_ == te::Field::Velocity ? "velocity"
          : "effect";
        std::snprintf(line, sizeof(line), "field: %s", field_name);
        d.draw_text(o, side_x + kSpPad, side_y + kSpPad + 22.0f, line,
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f}, 1.0f);

        // Cursor cell readout.
        const auto& cur = pat.cells[editor_cursor_channel_][editor_cursor_row_];
        char buf[16];
        tracker::format_cell_note(cur.note, buf);
        std::snprintf(line, sizeof(line), "note: %s", buf);
        d.draw_text(o, side_x + kSpPad, side_y + kSpPad + 44.0f, line,
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f}, 1.0f);
        tracker::format_cell_vel(cur.velocity, buf);
        std::snprintf(line, sizeof(line), "vel:  %s  (0x%02X)", buf, cur.velocity);
        d.draw_text(o, side_x + kSpPad, side_y + kSpPad + 62.0f, line,
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f}, 1.0f);
        tracker::format_cell_fx(cur.effect_type, cur.effect_param, buf);
        std::snprintf(line, sizeof(line), "fx:   %s", buf);
        d.draw_text(o, side_x + kSpPad, side_y + kSpPad + 80.0f, line,
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f}, 1.0f);

        std::snprintf(line, sizeof(line), "octave: %d   [ / ] to change",
            editor_octave_);
        d.draw_text(o, side_x + kSpPad, side_y + kSpPad + 108.0f, line,
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f}, 1.0f);

        if (editor_row_clipboard_.has_content) {
            std::snprintf(line, sizeof(line), "clipboard: %d rows",
                editor_row_clipboard_.rows);
            d.draw_text(o, side_x + kSpPad, side_y + kSpPad + 130.0f, line,
                {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.85f}, 1.0f);
        }
    }

    // Suppress unused-variable warnings for helpers that can be empty
    // in certain keyboard paths.
    (void)song_changed;
}
