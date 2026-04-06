#include "tracker_core.h"
#include <algorithm>
#include <cstdio>
#include <string>

namespace tracker_insp {
static constexpr int kKeyEscape = 256;
static constexpr int kKeyEnter = 257;
static constexpr int kKeyBackspace = 259;
static constexpr int kKeyDelete = 261;
static constexpr int kKeyRight = 262;
static constexpr int kKeyLeft = 263;
static constexpr int kKeyDown = 264;
static constexpr int kKeyUp = 265;

static constexpr float kRowH = 16.0f;
static constexpr float kRowNumW = 20.0f;
static constexpr float kNoteW = 34.0f;
static constexpr float kVelW = 22.0f;
static constexpr float kFxW = 30.0f;
static constexpr int kMaxVisRows = 16;
static constexpr float kTabW = 80.0f;
static constexpr float kChTabW = 28.0f;
static constexpr float kChTabH = 18.0f;
static constexpr float kLineH = 18.0f;

static constexpr float kChColors[8][3] = {
    {0.39f, 0.63f, 0.86f}, {0.86f, 0.47f, 0.31f}, {0.31f, 0.78f, 0.55f}, {0.78f, 0.71f, 0.24f},
    {0.63f, 0.39f, 0.78f}, {0.24f, 0.71f, 0.78f}, {0.78f, 0.39f, 0.63f}, {0.55f, 0.78f, 0.31f}
};

void format_note(uint8_t note, char out[4]) {
    if (note == tracker::NOTE_EMPTY) { out[0]='.'; out[1]='.'; out[2]='.'; out[3]=0; return; }
    if (note == tracker::NOTE_OFF) { out[0]='='; out[1]='='; out[2]='='; out[3]=0; return; }
    static const char* names[] = {"C-","C#","D-","D#","E-","F-","F#","G-","G#","A-","A#","B-"};
    const char* prefix = names[note % 12];
    out[0] = prefix[0];
    out[1] = prefix[1];
    int oct = (note / 12) - 1;
    out[2] = (oct >= 0 && oct <= 9) ? static_cast<char>('0' + oct) : '?';
    out[3] = 0;
}

void format_hex2(uint8_t val, char out[3]) {
    static const char hex[] = "0123456789ABCDEF";
    if (val == 0) { out[0]='.'; out[1]='.'; out[2]=0; return; }
    out[0] = hex[(val >> 4) & 0xF];
    out[1] = hex[val & 0xF];
    out[2] = 0;
}

void format_hex3(uint8_t type, uint8_t param, char out[4]) {
    static const char hex[] = "0123456789ABCDEF";
    if (type == 0 && param == 0) { out[0]='.'; out[1]='.'; out[2]='.'; out[3]=0; return; }
    out[0] = hex[type & 0xF];
    out[1] = hex[(param >> 4) & 0xF];
    out[2] = hex[param & 0xF];
    out[3] = 0;
}

uint8_t parse_note_str(const char* s) {
    if (!s || !s[0]) return tracker::NOTE_EMPTY;
    if (s[0] == '=') return tracker::NOTE_OFF;
    int semi = -1;
    switch (s[0]) {
        case 'C': case 'c': semi = 0; break;
        case 'D': case 'd': semi = 2; break;
        case 'E': case 'e': semi = 4; break;
        case 'F': case 'f': semi = 5; break;
        case 'G': case 'g': semi = 7; break;
        case 'A': case 'a': semi = 9; break;
        case 'B': case 'b': semi = 11; break;
        default: return tracker::NOTE_EMPTY;
    }
    int idx = 1;
    if (s[1] == '#') { semi++; idx = 2; }
    else if (s[1] == '-' || s[1] == ' ') { idx = 2; }
    if (!s[idx] || s[idx] < '0' || s[idx] > '9') return tracker::NOTE_EMPTY;
    int octave = s[idx] - '0';
    int note = (octave + 1) * 12 + semi;
    return static_cast<uint8_t>(std::clamp(note, 0, 127));
}

uint8_t parse_hex2_str(const char* s) {
    if (!s || !s[0] || !s[1]) return 0;
    auto hex_val = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        return -1;
    };
    int hi = hex_val(s[0]);
    int lo = hex_val(s[1]);
    if (hi < 0 || lo < 0) return 0;
    return static_cast<uint8_t>((hi << 4) | lo);
}

bool hit_test(float mx, float my, float rx, float ry, float rw, float rh) {
    return mx >= rx && mx < rx + rw && my >= ry && my < ry + rh;
}
} // namespace tracker_insp

void TrackerCore::draw_inspector(VividInspectorContext* ctx) {
#define namespace_ti namespace ti = tracker_insp
namespace_ti;
    auto& draw = ctx->draw;
    auto& cmds = ctx->commands;
    const auto& theme = ctx->theme;
    const auto& mouse = ctx->mouse;

    float px = ctx->content_x;
    float py = ctx->content_y;
    float panel_w = ctx->content_width;

    int edit_pat = (ctx->param_count > 5) ? std::clamp(static_cast<int>(ctx->param_values[5]), 0, 63) : 0;
    int edit_ch  = (ctx->param_count > 6) ? std::clamp(static_cast<int>(ctx->param_values[6]), 0, 7) : 0;
    int mute_mask = (ctx->param_count > 7) ? std::clamp(static_cast<int>(ctx->param_values[7]), 0, 255) : 0;

    tracker::TrackerSong disp_song;
    bool has_data = false;
    if (ctx->string_param_count > 0 && ctx->string_param_values && ctx->string_param_values[0]) {
        const char* pd = ctx->string_param_values[0];
        if (pd[0] != '\0')
            has_data = tracker::deserialize_song(std::string(pd), disp_song);
    }

    int playback_row = (ctx->output_count > 3) ? static_cast<int>(ctx->output_values[3]) : -1;
    int playback_pat = (ctx->output_count > 4) ? static_cast<int>(ctx->output_values[4]) : -1;

    for (uint32_t ki = 0; ki < ctx->key_event_count; ++ki) {
        auto& ke = ctx->key_events[ki];
        if (ke.action != 1 && ke.action != 2) continue;

        if (insp_editing_) {
            if (ke.key == ti::kKeyEscape) {
                insp_editing_ = false;
                insp_edit_buffer_.clear();
            } else if (ke.key == ti::kKeyBackspace) {
                if (!insp_edit_buffer_.empty())
                    insp_edit_buffer_.pop_back();
            } else if (ke.key == ti::kKeyDelete) {
                if (has_data) {
                    int pat_idx = std::clamp(edit_pat, 0, disp_song.num_patterns - 1);
                    auto& cell = disp_song.patterns[pat_idx].cells[edit_ch][insp_cursor_row_];
                    if (insp_cursor_col_ == 0) cell.note = tracker::NOTE_EMPTY;
                    else if (insp_cursor_col_ == 1) cell.velocity = 0;
                    else { cell.effect_type = 0; cell.effect_param = 0; }
                    cmds.set_string_param(cmds.opaque, "pattern_data",
                                          tracker::serialize_song(disp_song).c_str());
                }
                insp_editing_ = false;
                insp_edit_buffer_.clear();
            } else if (ke.key == ti::kKeyUp) {
                insp_cursor_row_ = std::max(0, insp_cursor_row_ - 1);
                insp_edit_buffer_.clear();
                if (insp_cursor_row_ < insp_scroll_row_)
                    insp_scroll_row_ = insp_cursor_row_;
            } else if (ke.key == ti::kKeyDown) {
                insp_cursor_row_++;
                insp_edit_buffer_.clear();
                if (insp_cursor_row_ >= insp_scroll_row_ + ti::kMaxVisRows)
                    insp_scroll_row_ = insp_cursor_row_ - ti::kMaxVisRows + 1;
            } else if (ke.key == ti::kKeyLeft) {
                insp_cursor_col_ = std::max(0, insp_cursor_col_ - 1);
                insp_edit_buffer_.clear();
                insp_edit_max_chars_ = (insp_cursor_col_ == 0) ? 3 : (insp_cursor_col_ == 1) ? 2 : 3;
            } else if (ke.key == ti::kKeyRight) {
                insp_cursor_col_ = std::min(2, insp_cursor_col_ + 1);
                insp_edit_buffer_.clear();
                insp_edit_max_chars_ = (insp_cursor_col_ == 0) ? 3 : (insp_cursor_col_ == 1) ? 2 : 3;
            } else if (ke.key == ti::kKeyEnter) {
                insp_editing_ = false;
                insp_edit_buffer_.clear();
            }
        } else {
            if (ke.key == ti::kKeyUp) {
                insp_cursor_row_ = std::max(0, insp_cursor_row_ - 1);
                if (insp_cursor_row_ < insp_scroll_row_)
                    insp_scroll_row_ = insp_cursor_row_;
            } else if (ke.key == ti::kKeyDown) {
                insp_cursor_row_++;
                if (insp_cursor_row_ >= insp_scroll_row_ + ti::kMaxVisRows)
                    insp_scroll_row_ = insp_cursor_row_ - ti::kMaxVisRows + 1;
            } else if (ke.key == ti::kKeyLeft) {
                insp_cursor_col_ = std::max(0, insp_cursor_col_ - 1);
            } else if (ke.key == ti::kKeyRight) {
                insp_cursor_col_ = std::min(2, insp_cursor_col_ + 1);
            }
        }
    }

    for (uint32_t ci = 0; ci < ctx->char_event_count; ++ci) {
        if (!insp_editing_) continue;
        char ch = static_cast<char>(ctx->char_events[ci]);
        if (insp_cursor_col_ == 0) {
            if ((ch >= 'A' && ch <= 'G') || (ch >= 'a' && ch <= 'g') ||
                ch == '#' || ch == '-' || ch == '=' || (ch >= '0' && ch <= '9')) {
                if (ch >= 'a' && ch <= 'g') ch = ch - 'a' + 'A';
                insp_edit_buffer_ += ch;
            }
        } else {
            if ((ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f')) {
                if (ch >= 'a' && ch <= 'f') ch = ch - 'a' + 'A';
                insp_edit_buffer_ += ch;
            }
        }

        if (static_cast<int>(insp_edit_buffer_.size()) >= insp_edit_max_chars_) {
            if (has_data) {
                int pat_idx = std::clamp(edit_pat, 0, disp_song.num_patterns - 1);
                auto& cell = disp_song.patterns[pat_idx].cells[edit_ch][insp_cursor_row_];
                if (insp_cursor_col_ == 0) {
                    if (insp_edit_buffer_[0] == '=')
                        cell.note = tracker::NOTE_OFF;
                    else
                        cell.note = ti::parse_note_str(insp_edit_buffer_.c_str());
                } else if (insp_cursor_col_ == 1) {
                    cell.velocity = ti::parse_hex2_str(insp_edit_buffer_.c_str());
                } else {
                    if (insp_edit_buffer_.size() >= 3) {
                        char type_buf[3] = {'0', insp_edit_buffer_[0], 0};
                        cell.effect_type = ti::parse_hex2_str(type_buf);
                        char param_buf[3] = {insp_edit_buffer_[1], insp_edit_buffer_[2], 0};
                        cell.effect_param = ti::parse_hex2_str(param_buf);
                    }
                }
                cmds.set_string_param(cmds.opaque, "pattern_data",
                                      tracker::serialize_song(disp_song).c_str());
            }
            insp_edit_buffer_.clear();
            insp_editing_ = false;
            insp_cursor_row_++;
            if (insp_cursor_row_ >= insp_scroll_row_ + ti::kMaxVisRows)
                insp_scroll_row_ = insp_cursor_row_ - ti::kMaxVisRows + 1;
        }
    }

    py += 4;
    float start_y = py;

    static const char* kTabLabels[] = {"Pattern", "Song", "Settings"};
    float tab_h = 18.0f;

    for (int t = 0; t < 3; ++t) {
        float tx = px + t * ti::kTabW;
        bool active = (insp_tab_ == t);
        if (active) {
            draw.draw_rect(draw.opaque, tx, py, ti::kTabW, tab_h, theme.dark_bg);
            VividColor accent_full = theme.accent; accent_full.a = 1.0f;
            draw.draw_rect(draw.opaque, tx, py + tab_h - 2, ti::kTabW, 2, accent_full);
        }
        VividColor tab_text = theme.dim_text;
        if (active) { tab_text.r *= 1.5f; tab_text.g *= 1.5f; tab_text.b *= 1.5f; }
        tab_text.a = active ? 1.0f : 0.5f;
        draw.draw_text(draw.opaque, tx + 8, py + 3, kTabLabels[t], tab_text, 1.0f);

        if (mouse.left_clicked && ti::hit_test(mouse.x + px, mouse.y + py - (py - start_y + 4),
                                                tx, py, ti::kTabW, tab_h)) {
        }
    }
    if (mouse.left_clicked) {
        float abs_mx = mouse.x + px;
        float abs_my = mouse.y + ctx->content_y;
        for (int t = 0; t < 3; ++t) {
            float tx = px + t * ti::kTabW;
            if (ti::hit_test(abs_mx, abs_my, tx, py, ti::kTabW, tab_h)) {
                insp_tab_ = t;
                insp_editing_ = false;
            }
        }
    }
    py += tab_h + 2;

    if (insp_tab_ == 0) {
        float ch_y = py;
        if (mouse.left_clicked) {
            float abs_mx = mouse.x + px;
            float abs_my = mouse.y + ctx->content_y;
            for (int c = 0; c < tracker::MAX_CHANNELS; ++c) {
                float cx = px + c * ti::kChTabW;
                if (ti::hit_test(abs_mx, abs_my, cx, ch_y, ti::kChTabW, ti::kChTabH)) {
                    cmds.set_param(cmds.opaque, "edit_channel", static_cast<float>(c));
                }
            }
        }

        for (int c = 0; c < tracker::MAX_CHANNELS; ++c) {
            float cx = px + c * ti::kChTabW;
            bool active = (edit_ch == c);
            bool muted = (mute_mask >> c) & 1;

            if (active) {
                draw.draw_rect(draw.opaque, cx, ch_y, ti::kChTabW, ti::kChTabH, theme.dark_bg);
                VividColor ch_color = {ti::kChColors[c][0], ti::kChColors[c][1], ti::kChColors[c][2], 1.0f};
                draw.draw_rect(draw.opaque, cx, ch_y + ti::kChTabH - 2, ti::kChTabW, 2, ch_color);
            }

            char label[4];
            std::snprintf(label, sizeof(label), "%d", c + 1);
            float alpha = muted ? 0.3f : (active ? 1.0f : 0.5f);
            VividColor ch_text = {ti::kChColors[c][0], ti::kChColors[c][1], ti::kChColors[c][2], alpha};
            draw.draw_text(draw.opaque, cx + 8, ch_y + 3, label, ch_text, 1.0f);
        }
        py += ti::kChTabH + 2;

        {
            char pat_label[16];
            std::snprintf(pat_label, sizeof(pat_label), "P%02d", edit_pat);
            VividColor dim07 = theme.dim_text; dim07.a = 0.7f;
            draw.draw_text(draw.opaque, px, py, pat_label, dim07, 1.0f);
            py += ti::kLineH;
        }

        if (!has_data) {
            VividColor dim05 = theme.dim_text; dim05.a = 0.5f;
            draw.draw_text(draw.opaque, px, py, "(no pattern data)", dim05, 1.0f);
            py += ti::kLineH;
            ctx->consumed_height = py - ctx->content_y;
            ctx->wants_keyboard = insp_editing_ ? 1 : 0;
            return;
        }

        int pat_idx = std::clamp(edit_pat, 0, disp_song.num_patterns - 1);
        const auto& pat = disp_song.patterns[pat_idx];
        int num_rows = pat.num_rows;
        if (num_rows <= 0) {
            py += 4;
            ctx->consumed_height = py - ctx->content_y;
            ctx->wants_keyboard = insp_editing_ ? 1 : 0;
            return;
        }

        insp_cursor_row_ = std::clamp(insp_cursor_row_, 0, num_rows - 1);
        int visible_rows = std::min(num_rows, ti::kMaxVisRows);
        insp_scroll_row_ = std::clamp(insp_scroll_row_, 0, std::max(0, num_rows - visible_rows));

        float grid_h = visible_rows * ti::kRowH;
        VividColor dark09 = theme.dark_bg; dark09.a = 0.9f;
        draw.draw_rect(draw.opaque, px, py, panel_w, grid_h + 2, dark09);

        float col_x = px + ti::kRowNumW;
        VividColor dim04 = theme.dim_text; dim04.a = 0.4f;
        draw.draw_text(draw.opaque, px + 2, py, "#", dim04, 1.0f);
        draw.draw_text(draw.opaque, col_x + 2, py, "Not", dim04, 1.0f);
        draw.draw_text(draw.opaque, col_x + ti::kNoteW + 2, py, "Vl", dim04, 1.0f);
        draw.draw_text(draw.opaque, col_x + ti::kNoteW + ti::kVelW + 2, py, "Fx", dim04, 1.0f);
        py += ti::kRowH;

        bool playback_on_this_pat = (playback_pat == pat_idx);

        if (mouse.left_clicked) {
            float abs_mx = mouse.x + px;
            float abs_my = mouse.y + ctx->content_y;
            for (int vi = 0; vi < visible_rows; ++vi) {
                int row = insp_scroll_row_ + vi;
                if (row >= num_rows) break;
                float ry = py + vi * ti::kRowH;
                float nx = px + ti::kRowNumW;
                if (ti::hit_test(abs_mx, abs_my, nx, ry, ti::kNoteW, ti::kRowH)) {
                    insp_cursor_row_ = row; insp_cursor_col_ = 0;
                    insp_editing_ = true; insp_edit_buffer_.clear();
                    insp_edit_max_chars_ = 3;
                }
                float vx = nx + ti::kNoteW;
                if (ti::hit_test(abs_mx, abs_my, vx, ry, ti::kVelW, ti::kRowH)) {
                    insp_cursor_row_ = row; insp_cursor_col_ = 1;
                    insp_editing_ = true; insp_edit_buffer_.clear();
                    insp_edit_max_chars_ = 2;
                }
                float ex = vx + ti::kVelW;
                if (ti::hit_test(abs_mx, abs_my, ex, ry, ti::kFxW, ti::kRowH)) {
                    insp_cursor_row_ = row; insp_cursor_col_ = 2;
                    insp_editing_ = true; insp_edit_buffer_.clear();
                    insp_edit_max_chars_ = 3;
                }
            }
        }

        for (int vi = 0; vi < visible_rows; ++vi) {
            int row = insp_scroll_row_ + vi;
            if (row >= num_rows) break;
            float ry = py + vi * ti::kRowH;
            const auto& cell = pat.cells[edit_ch][row];

            if (playback_on_this_pat && row == playback_row) {
                VividColor hl = theme.accent; hl.a = 0.15f;
                draw.draw_rect(draw.opaque, px, ry, panel_w, ti::kRowH, hl);
            }

            if (row > 0 && (row % 4) == 0) {
                VividColor sep = theme.separator; sep.a = 0.3f;
                draw.draw_rect(draw.opaque, px, ry, panel_w, 1, sep);
            }

            char row_str[4];
            std::snprintf(row_str, sizeof(row_str), "%02X", row);
            VividColor dim_row = theme.dim_text;
            dim_row.a = ((row % 4) == 0) ? 0.7f : 0.35f;
            draw.draw_text(draw.opaque, px + 2, ry + 1, row_str, dim_row, 1.0f);

            float nx = px + ti::kRowNumW;
            char note_str[4];
            ti::format_note(cell.note, note_str);
            VividColor note_clr = {ti::kChColors[edit_ch][0], ti::kChColors[edit_ch][1], ti::kChColors[edit_ch][2],
                                   (cell.note == tracker::NOTE_EMPTY) ? 0.2f : 0.9f};
            if (cell.note == tracker::NOTE_OFF) { note_clr.r = 0.7f; note_clr.g = 0.3f; note_clr.b = 0.3f; }

            if (insp_cursor_row_ == row && insp_cursor_col_ == 0) {
                VividColor cursor_bg = {0.3f, 0.4f, 0.6f, 0.3f};
                draw.draw_rect(draw.opaque, nx, ry, ti::kNoteW, ti::kRowH, cursor_bg);
            }
            draw.draw_text(draw.opaque, nx + 2, ry + 1, note_str, note_clr, 1.0f);

            float vx = nx + ti::kNoteW;
            char vel_str[3];
            ti::format_hex2(cell.velocity, vel_str);
            VividColor vel_clr = theme.bright_text;
            vel_clr.a = (cell.velocity == 0) ? 0.2f : 0.7f;
            if (insp_cursor_row_ == row && insp_cursor_col_ == 1) {
                VividColor cursor_bg = {0.3f, 0.4f, 0.6f, 0.3f};
                draw.draw_rect(draw.opaque, vx, ry, ti::kVelW, ti::kRowH, cursor_bg);
            }
            draw.draw_text(draw.opaque, vx + 2, ry + 1, vel_str, vel_clr, 1.0f);

            float ex = vx + ti::kVelW;
            char fx_str[4];
            ti::format_hex3(cell.effect_type, cell.effect_param, fx_str);
            VividColor fx_clr = theme.bright_text;
            fx_clr.a = (cell.effect_type == 0 && cell.effect_param == 0) ? 0.2f : 0.7f;
            if (insp_cursor_row_ == row && insp_cursor_col_ == 2) {
                VividColor cursor_bg = {0.3f, 0.4f, 0.6f, 0.3f};
                draw.draw_rect(draw.opaque, ex, ry, ti::kFxW, ti::kRowH, cursor_bg);
            }
            draw.draw_text(draw.opaque, ex + 2, ry + 1, fx_str, fx_clr, 1.0f);
        }

        py += grid_h + 4;

        if (num_rows > ti::kMaxVisRows) {
            float scroll_frac = static_cast<float>(insp_scroll_row_) /
                                static_cast<float>(num_rows - visible_rows);
            float bar_h = 4.0f;
            float bar_w = panel_w * static_cast<float>(visible_rows) / static_cast<float>(num_rows);
            float bar_x = px + scroll_frac * (panel_w - bar_w);
            VividColor accent05 = theme.accent; accent05.a = 0.5f;
            draw.draw_rect(draw.opaque, bar_x, py, bar_w, bar_h, accent05);
            py += bar_h + 2;
        }

        if (insp_editing_) {
            char edit_label[32];
            std::snprintf(edit_label, sizeof(edit_label), "Edit: %s", insp_edit_buffer_.c_str());
            VividColor accent08 = theme.accent; accent08.a = 0.8f;
            draw.draw_text(draw.opaque, px, py, edit_label, accent08, 1.0f);
            py += ti::kLineH;
        }

    } else if (insp_tab_ == 1) {
        if (!has_data) {
            VividColor dim05 = theme.dim_text; dim05.a = 0.5f;
            draw.draw_text(draw.opaque, px, py, "(no pattern data)", dim05, 1.0f);
            py += ti::kLineH;
            ctx->consumed_height = py - ctx->content_y;
            ctx->wants_keyboard = insp_editing_ ? 1 : 0;
            return;
        }

        VividColor dim07 = theme.dim_text; dim07.a = 0.7f;
        draw.draw_text(draw.opaque, px, py, "Arrangement", dim07, 1.0f);
        py += ti::kLineH;

        int current_order = (ctx->output_count > 5) ? static_cast<int>(ctx->output_values[5]) : -1;

        float item_h = ti::kLineH;
        float list_h = std::min(static_cast<int>(disp_song.arrangement_length), 16) * item_h;
        VividColor dark09 = theme.dark_bg; dark09.a = 0.9f;
        draw.draw_rect(draw.opaque, px, py, panel_w, list_h + 2, dark09);

        if (mouse.left_clicked) {
            float abs_mx = mouse.x + px;
            float abs_my = mouse.y + ctx->content_y;
            for (int i = 0; i < disp_song.arrangement_length && i < 16; ++i) {
                float iy = py + i * item_h;
                if (ti::hit_test(abs_mx, abs_my, px, iy, panel_w, item_h)) {
                    int cur = disp_song.arrangement[i];
                    int next = (cur + 1) % disp_song.num_patterns;
                    disp_song.arrangement[i] = static_cast<uint8_t>(next);
                    cmds.set_string_param(cmds.opaque, "pattern_data",
                                          tracker::serialize_song(disp_song).c_str());
                }
            }
        }

        for (int i = 0; i < disp_song.arrangement_length && i < 16; ++i) {
            float iy = py + i * item_h;
            if (i == current_order) {
                VividColor hl = theme.accent; hl.a = 0.15f;
                draw.draw_rect(draw.opaque, px, iy, panel_w, item_h, hl);
            }
            char entry[16];
            std::snprintf(entry, sizeof(entry), "%02d: P%02d", i, disp_song.arrangement[i]);
            VividColor text_clr = theme.bright_text;
            text_clr.a = (i == current_order) ? 1.0f : 0.6f;
            draw.draw_text(draw.opaque, px + 4, iy + 1, entry, text_clr, 1.0f);
        }
        py += list_h + 4;

        float btn_w = 60.0f;
        float btn_h = 18.0f;
        float btn_gap = 4.0f;

        draw.draw_rect(draw.opaque, px, py, btn_w, btn_h, theme.slider_track);
        VividColor dim08 = theme.dim_text; dim08.a = 0.8f;
        draw.draw_text(draw.opaque, px + 8, py + 2, "+ Add", dim08, 1.0f);

        draw.draw_rect(draw.opaque, px + btn_w + btn_gap, py, btn_w, btn_h, theme.slider_track);
        draw.draw_text(draw.opaque, px + btn_w + btn_gap + 8, py + 2, "- Remove", dim08, 1.0f);

        if (mouse.left_clicked) {
            float abs_mx = mouse.x + px;
            float abs_my = mouse.y + ctx->content_y;
            if (ti::hit_test(abs_mx, abs_my, px, py, btn_w, btn_h)) {
                if (disp_song.arrangement_length < tracker::MAX_ARRANGEMENT) {
                    disp_song.arrangement[disp_song.arrangement_length] = 0;
                    disp_song.arrangement_length++;
                    cmds.set_string_param(cmds.opaque, "pattern_data",
                                          tracker::serialize_song(disp_song).c_str());
                }
            }
            if (ti::hit_test(abs_mx, abs_my, px + btn_w + btn_gap, py, btn_w, btn_h)) {
                if (disp_song.arrangement_length > 1) {
                    disp_song.arrangement_length--;
                    cmds.set_string_param(cmds.opaque, "pattern_data",
                                          tracker::serialize_song(disp_song).c_str());
                }
            }
        }

        py += btn_h + 4;

    } else {
        VividColor dim04 = theme.dim_text; dim04.a = 0.4f;
        draw.draw_text(draw.opaque, px, py, "See params above", dim04, 1.0f);
        py += ti::kLineH;
    }

    ctx->consumed_height = py - ctx->content_y;
    ctx->wants_keyboard = insp_editing_ ? 1 : 0;
#undef namespace_ti
}
