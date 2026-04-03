#include "ui/graph/node_graph.h"
#include "ui/graph/node_graph_constants.h"
#include "ui/graph/node_graph_util.h"
#include "ui/rendering/renderer_2d.h"
#include "ui/rendering/thumbnail_cache.h"
#include "ui/rendering/thumbnail_renderer.h"
#include "ui/style/i18n.h"
#include "common/system_info.h"
#include "common/string_util.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

namespace vivid::ui {

static constexpr uint64_t kMcpStaleMs = 30000;

using vivid::format_float;

// -----------------------------------------------------------------------
// Overlays — rendered in a separate pass after GPU thumbnails so that
// popups (context menu, dropdown) appear on top of everything.
// -----------------------------------------------------------------------
void NodeGraphUI::draw_overlays(Renderer2D& tr) {
    // Perf bar expanded panels
    if (perf_mem_hovered_) draw_perf_expanded(tr);
    if (perf_audio_hovered_) draw_perf_audio_expanded(tr);

    // Inspector — drawn in overlay pass so it paints over GPU thumbnails
    draw_inspector(tr, win_w_, win_h_);

    // Error tooltip — drawn after inspector so it appears above GPU thumbnails
    draw_node_error_tooltip(tr);

    // Param description tooltip — shown after hovering a param label for ~1s
    draw_param_tooltip(tr);

    // Operator chooser — drawn here (overlay pass) so it appears above GPU thumbnails
    draw_chooser(tr);

    // Parameter picker popup
    draw_param_picker(tr);

    // Stash renderer ref for input-time submenu width calculations
    inspector_.dropdown_tr = &tr;

    // Dropdown popup
    if (inspector_.dropdown_open && !inspector_.dropdown_labels.empty()) {
        float item_h = kDropdownItemH;

        // Preset dropdowns use hierarchical submenu rendering
        if ((inspector_.dropdown_is_preset || inspector_.dropdown_is_state_preset) && !inspector_.dropdown_submenu_stack.empty()) {
            for (size_t lvl = 0; lvl < inspector_.dropdown_submenu_stack.size(); ++lvl) {
                auto& level = inspector_.dropdown_submenu_stack[lvl];
                if (!level.items || level.items->empty()) continue;
                int count = static_cast<int>(level.items->size());
                float popup_h = count * item_h + 4;
                // Auto-size width from content
                float auto_w = level.w;
                for (const auto& n : *level.items) {
                    float tw = tr.text_width(n.label.c_str()) + 24.0f;
                    if (n.is_folder) tw += 12.0f;
                    if (tw > auto_w) auto_w = tw;
                }
                level.w = auto_w;
                float lw = level.w;
                float lx = level.x;
                float ly = level.y;
                draw_popup_bg(tr, style_, lx, ly, lw, popup_h);

                for (int i = 0; i < count; ++i) {
                    const auto& node = (*level.items)[i];
                    float iy = ly + 2 + i * item_h;

                    // Highlight hovered item
                    if (i == level.hovered_idx) {
                        tr.draw_rect(lx + 2, iy, lw - 4, item_h,
                                     style_.node_sel_bg[0], style_.node_sel_bg[1], style_.node_sel_bg[2], 0.9f);
                    }

                    // Color: folders use bright, factory leaves use dim, user leaves use bright
                    float cr, cg, cb;
                    if (node.is_folder) {
                        cr = style_.bright_text[0]; cg = style_.bright_text[1]; cb = style_.bright_text[2];
                    } else if (node.is_factory) {
                        cr = style_.dim_text[0]; cg = style_.dim_text[1]; cb = style_.dim_text[2];
                    } else {
                        cr = style_.bright_text[0]; cg = style_.bright_text[1]; cb = style_.bright_text[2];
                    }

                    tr.draw_text(lx + 8, iy + 2, node.label.c_str(), cr, cg, cb);

                    // Folder indicator arrow on right side
                    if (node.is_folder) {
                        tr.draw_text(lx + lw - 16, iy + 2, ">",
                                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
                    }
                }
            }
        } else {
            // Non-preset flat dropdown (param selectors, etc.)
            float popup_h = inspector_.dropdown_labels.size() * item_h + 4;
            draw_popup_bg(tr, style_, inspector_.dropdown_x, inspector_.dropdown_y, inspector_.dropdown_w, popup_h);
            for (int i = 0; i < static_cast<int>(inspector_.dropdown_labels.size()); ++i) {
                float iy = inspector_.dropdown_y + 2 + i * item_h;
                if (i == inspector_.dropdown_flat_hovered_idx || i == inspector_.dropdown_sel) {
                    tr.draw_rect(inspector_.dropdown_x + 2, iy, inspector_.dropdown_w - 4, item_h,
                                 style_.node_sel_bg[0], style_.node_sel_bg[1], style_.node_sel_bg[2],
                                 (i == inspector_.dropdown_flat_hovered_idx) ? 0.9f : 0.5f);
                }
                tr.draw_text(inspector_.dropdown_x + 8, iy + 2, inspector_.dropdown_labels[i].c_str(),
                             style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
            }
        }
    }

    // Record codec dropdown
    if (record_dropdown_open_) {
        static const char* codec_labels[] = { "H.264", "H.265", "ProRes 4444" };
        constexpr int codec_count = 3;
        float item_h = kDropdownItemH;
        float popup_h = codec_count * item_h + 4;
        float popup_w = kPerfCodecDropW;
        float dx = record_dropdown_x_;
        float dy = record_dropdown_y_;
        draw_popup_bg(tr, style_, dx, dy, popup_w, popup_h);
        for (int i = 0; i < codec_count; ++i) {
            float iy = dy + 2 + i * item_h;
            bool hovered = mouse_.x >= dx && mouse_.x <= dx + popup_w &&
                           mouse_.y >= iy && mouse_.y <= iy + item_h;
            if (i == record_codec_sel_ || hovered) {
                tr.draw_rect(dx + 2, iy, popup_w - 4, item_h,
                             style_.node_sel_bg[0], style_.node_sel_bg[1], style_.node_sel_bg[2],
                             hovered ? 0.9f : 0.5f);
            }
            tr.draw_text(dx + 8, iy + 2, codec_labels[i],
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        }
    }

    // Right-click context menu
    if (context_menu_open_) {
        int item_count = 1;
        if (!context_node_id_.empty() && context_node_has_shader_)
            item_count = 2;
        if (context_wire_idx_ >= 0)
            item_count = 2;
        if (context_bg_menu_)
            item_count = 2;  // "Re-layout All" + "Add Sticky Note"
        // Solo item for node context menus
        bool show_solo = !context_node_id_.empty() && !context_bg_menu_;
        if (show_solo) item_count++;

        float menu_h = kCtxMenuPadTop + item_count * kCtxMenuItemH + 2.0f;
        float mx = context_menu_x_, my = context_menu_y_;

        // Background
        draw_popup_bg(tr, style_, mx, my, kCtxMenuW, menu_h);

        // Item labels
        std::string delete_label;
        const char* labels[5];
        int label_idx = 0;
        bool is_sticky_ctx = (context_node_id_ == "__sticky__");
        if (is_sticky_ctx) {
            item_count = 2;
            menu_h = kCtxMenuPadTop + item_count * kCtxMenuItemH + 2.0f;
        }
        if (context_bg_menu_) {
            labels[label_idx++] = "Re-layout All";
            labels[label_idx++] = "Add Sticky Note";
        } else if (is_sticky_ctx) {
            labels[label_idx++] = "Delete Note";
            labels[label_idx++] = "Change Color";
        } else if (!context_node_id_.empty()) {
            if (selected_node_ids_.count(context_node_id_) && selected_node_ids_.size() > 1) {
                delete_label = "Delete " + std::to_string(selected_node_ids_.size()) + " Nodes";
                labels[label_idx++] = delete_label.c_str();
            } else {
                labels[label_idx++] = "Delete Node";
            }
            if (context_node_has_shader_)
                labels[label_idx++] = "Clone & Edit";
            // Solo/Unsolo
            bool is_soloed = (!snap_.solo_node_id.empty() && snap_.solo_node_id == context_node_id_);
            labels[label_idx++] = is_soloed ? "Unsolo" : "Solo";
        } else {
            labels[label_idx++] = "Delete Wire";
            labels[label_idx++] = "Insert Node";
        }

        for (int i = 0; i < item_count; ++i) {
            float item_y = my + kCtxMenuPadTop + i * kCtxMenuItemH;
            // Per-item hover highlight
            if (mouse_.x >= mx && mouse_.x <= mx + kCtxMenuW &&
                mouse_.y >= item_y && mouse_.y <= item_y + kCtxMenuItemH) {
                tr.draw_rect(mx + 2, item_y, kCtxMenuW - 4, kCtxMenuItemH,
                             style_.node_sel_bg[0], style_.node_sel_bg[1], style_.node_sel_bg[2], 0.9f);
            }
            tr.draw_text(mx + 8, item_y + 3, labels[i], style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        }
    }

    draw_color_popup(tr);
    // draw_preferences moved to DialogManager
    // draw_package_browser, draw_example_browser, draw_graph_meta_editor moved to DialogManager
    // Feed live data to MCP setup dialog
    dialogs_.mcp_setup.mcp_main_last_ping_ms  = snap_.mcp_main_last_ping_ms;
    dialogs_.mcp_setup.mcp_opdev_last_ping_ms = snap_.mcp_opdev_last_ping_ms;
    dialogs_.mcp_setup.graph_path             = dialogs_.graph_meta_data().path;
    dialogs_.set_frame_counter(perf_frame_counter_);
    dialogs_.draw(tr, mouse_, style_, popup_opacity_, win_w_, win_h_, text_edit_, cursor_blink_on());
    draw_async_add_overlay(tr);
    draw_async_graph_load_overlay(tr);
    draw_status_banner(tr);
}

void NodeGraphUI::draw_async_add_overlay(Renderer2D& tr) {
    if (!async_add_active_) return;

    tr.draw_rect(0.0f, 0.0f, static_cast<float>(win_w_), static_cast<float>(win_h_),
                 0.02f, 0.03f, 0.04f, 0.55f);

    const float panel_w = 360.0f;
    const float panel_h = 132.0f;
    const float px = (static_cast<float>(win_w_) - panel_w) * 0.5f;
    const float py = (static_cast<float>(win_h_) - panel_h) * 0.5f;
    draw_popup_bg(tr, style_, px, py, panel_w, panel_h);
    tr.draw_rect(px, py, panel_w, 2.0f, style_.accent[0], style_.accent[1], style_.accent[2]);

    const char* title = "Adding Operator";
    const char* stage = "Preparing operator...";
    switch (async_add_stage_) {
        case AsyncAddStage::Preparing: stage = "Preparing operator..."; break;
        case AsyncAddStage::Compiling: stage = "Compiling graph..."; break;
        case AsyncAddStage::Applying: stage = "Applying graph..."; break;
    }

    static const char* spinner_frames[] = {"...", ".  ", ".. "};
    int spinner_idx = static_cast<int>(cursor_blink_time_ * 6.0f) % 3;

    tr.draw_text(px + 16.0f, py + 16.0f, title,
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    tr.draw_text(px + 16.0f, py + 50.0f, spinner_frames[spinner_idx],
                 style_.accent[0], style_.accent[1], style_.accent[2]);
    tr.draw_text(px + 52.0f, py + 50.0f, stage,
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);

    std::string body = async_add_display_name_.empty()
        ? "Please wait while Vivid prepares the selected operator."
        : ("Please wait while Vivid prepares " + async_add_display_name_ + ".");
    tr.push_clip_rect(px + 16.0f, py + 78.0f, panel_w - 32.0f, 24.0f);
    tr.draw_text(px + 16.0f, py + 78.0f, body.c_str(),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    tr.pop_clip_rect();
}

void NodeGraphUI::draw_async_graph_load_overlay(Renderer2D& tr) {
    if (!async_graph_load_active_) return;

    tr.draw_rect(0.0f, 0.0f, static_cast<float>(win_w_), static_cast<float>(win_h_),
                 0.02f, 0.03f, 0.04f, 0.55f);

    const float panel_w = 380.0f;
    const float panel_h = 132.0f;
    const float px = (static_cast<float>(win_w_) - panel_w) * 0.5f;
    const float py = (static_cast<float>(win_h_) - panel_h) * 0.5f;
    draw_popup_bg(tr, style_, px, py, panel_w, panel_h);
    tr.draw_rect(px, py, panel_w, 2.0f, style_.accent[0], style_.accent[1], style_.accent[2]);

    const char* title = "Loading Graph";
    const char* stage = "Loading graph...";
    switch (async_graph_load_stage_) {
        case AsyncGraphLoadStage::Loading: stage = "Loading graph..."; break;
        case AsyncGraphLoadStage::PreparingOperators: stage = "Preparing operators..."; break;
        case AsyncGraphLoadStage::Compiling: stage = "Compiling graph..."; break;
        case AsyncGraphLoadStage::Applying: stage = "Applying graph..."; break;
    }

    static const char* spinner_frames[] = {"...", ".  ", ".. "};
    int spinner_idx = static_cast<int>(cursor_blink_time_ * 6.0f) % 3;

    tr.draw_text(px + 16.0f, py + 16.0f, title,
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    tr.draw_text(px + 16.0f, py + 50.0f, spinner_frames[spinner_idx],
                 style_.accent[0], style_.accent[1], style_.accent[2]);
    tr.draw_text(px + 52.0f, py + 50.0f, stage,
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);

    std::string body = async_graph_load_display_name_.empty()
        ? "Please wait while Vivid prepares the requested graph."
        : ("Please wait while Vivid prepares " + async_graph_load_display_name_ + ".");
    tr.push_clip_rect(px + 16.0f, py + 78.0f, panel_w - 32.0f, 24.0f);
    tr.draw_text(px + 16.0f, py + 78.0f, body.c_str(),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    tr.pop_clip_rect();
}

void NodeGraphUI::draw_status_banner(Renderer2D& tr) {
    if (status_banner_error_.empty()) return;

    const float banner_max_w = std::min(520.0f, static_cast<float>(win_w_) - 32.0f);
    const float banner_h = 30.0f;
    const float bx = (static_cast<float>(win_w_) - banner_max_w) * 0.5f;
    const float by = kPerfBarH + 12.0f;

    tr.draw_rect(bx, by, banner_max_w, banner_h, 0.26f, 0.07f, 0.07f, 0.94f);
    tr.draw_rect(bx, by, banner_max_w, 1.0f, 0.95f, 0.36f, 0.36f, 0.9f);
    tr.push_clip_rect(bx + 10.0f, by + 6.0f, banner_max_w - 20.0f, banner_h - 12.0f);
    tr.draw_text(bx + 10.0f, by + 7.0f, status_banner_error_.c_str(),
                 0.98f, 0.82f, 0.82f, 1.0f);
    tr.pop_clip_rect();
}

// -----------------------------------------------------------------------
// Performance bar
// -----------------------------------------------------------------------
void NodeGraphUI::draw_perf_bar(Renderer2D& tr) {
    // Update smoothed values (EMA)
    constexpr float kSmooth = 0.05f;
    float raw_fps = (dt_ > 0.0f) ? 1.0f / dt_ : 0.0f;
    float raw_ms = dt_ * 1000.0f;

    if (perf_frame_counter_ == 0) {
        smoothed_fps_ = raw_fps;
        smoothed_ms_ = raw_ms;
    } else {
        smoothed_fps_ += kSmooth * (raw_fps - smoothed_fps_);
        smoothed_ms_ += kSmooth * (raw_ms - smoothed_ms_);
    }

    fps_history_.push(smoothed_fps_);
    frame_time_history_.push(smoothed_ms_);

    // Sample memory at lower cadence
    if (perf_frame_counter_ % kPerfMemSampleInterval == 0) {
        uint64_t mem_bytes = vivid::get_process_memory_bytes();
        float mem_mb = static_cast<float>(mem_bytes) / (1024.0f * 1024.0f);
        smoothed_mem_mb_ = mem_mb;
        memory_history_.push(mem_mb);
    }
    constexpr int kPerfDisplayInterval = 30;  // update display text ~2x/sec at 60fps
    if (perf_frame_counter_ == 0 || perf_frame_counter_ % kPerfDisplayInterval == 0) {
        display_fps_ = smoothed_fps_;
        display_ms_ = smoothed_ms_;
    }
    perf_frame_counter_++;

    float fw = static_cast<float>(win_w_);

    // Bar background
    tr.draw_rect(0, 0, fw, kPerfBarH,
                 kPerfBarBg[0], kPerfBarBg[1], kPerfBarBg[2], kPerfBarBg[3]);

    // Bottom separator line
    tr.draw_rect(0, kPerfBarH - 1, fw, 1, 0.20f, 0.22f, 0.25f, 0.6f);

    float x = kPerfBarPadX;
    float text_y = (kPerfBarH - tr.line_height()) * 0.5f;

    // --- FPS ---
    // Color-code: green >= 55, yellow >= 30, red < 30
    float fr, fg, fb;
    if (display_fps_ >= 55.0f) {
        fr = kPerfFpsColor[0]; fg = kPerfFpsColor[1]; fb = kPerfFpsColor[2];
    } else if (display_fps_ >= 30.0f) {
        fr = 0.95f; fg = 0.85f; fb = 0.30f; // yellow
    } else {
        fr = 0.95f; fg = 0.35f; fb = 0.30f; // red
    }

    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.0f FPS", display_fps_);
    float fps_field_w = tr.text_width("000 FPS");
    float fps_text_w = tr.text_width(buf);
    tr.draw_text(x + (fps_field_w - fps_text_w), text_y, buf, fr, fg, fb);
    x += fps_field_w + kPerfSepMargin;

    // Separator
    tr.draw_rect(x, 4, kPerfSepW, kPerfBarH - 8, 0.30f, 0.32f, 0.35f, 0.5f);
    x += kPerfSepW + kPerfSepMargin;

    // --- Audio rate ---
    audio_load_history_.push(snap_.audio_load);
    {
        float audio_section_start = x;

        // "48kHz" label
        char audio_buf[32];
        if (snap_.audio_sample_rate > 0)
            std::snprintf(audio_buf, sizeof(audio_buf), "%ukHz", snap_.audio_sample_rate / 1000);
        else
            std::snprintf(audio_buf, sizeof(audio_buf), "--kHz");
        tr.draw_text(x, text_y, audio_buf,
                     kPerfAudioColor[0], kPerfAudioColor[1], kPerfAudioColor[2]);
        x += tr.text_width(audio_buf) + kPerfSepMargin;

        // Separator
        tr.draw_rect(x, 4, kPerfSepW, kPerfBarH - 8, 0.30f, 0.32f, 0.35f, 0.5f);
        x += kPerfSepW + kPerfSepMargin;

        // Audio load "N%" label (color-coded)
        std::snprintf(buf, sizeof(buf), "%.0f%%", snap_.audio_load * 100.0f);
        float load_field_w = tr.text_width("100%");
        float load_text_w = tr.text_width(buf);
        float load = snap_.audio_load;
        float lr, lg, lb;
        if (load < 0.5f) {
            lr = kPerfAudioColor[0]; lg = kPerfAudioColor[1]; lb = kPerfAudioColor[2];
        } else if (load < 0.8f) {
            lr = 0.95f; lg = 0.85f; lb = 0.30f;
        } else {
            lr = 0.95f; lg = 0.35f; lb = 0.30f;
        }
        tr.draw_text(x + (load_field_w - load_text_w), text_y, buf, lr, lg, lb);
        x += load_field_w + 4.0f;

        // Mini sparkline for audio load
        float ag_x = x;
        float ag_y = (kPerfBarH - kPerfMiniGraphH) * 0.5f;
        perf_audio_graph_x_ = ag_x;
        perf_audio_graph_y_ = ag_y;

        tr.draw_rect(ag_x, ag_y, kPerfMiniGraphW, kPerfMiniGraphH,
                     0.04f, 0.05f, 0.06f, 0.8f);
        draw_perf_sparkline(tr, audio_load_history_.values, kPerfHistoryLen,
                            audio_load_history_.write_idx, audio_load_history_.filled,
                            ag_x, ag_y, kPerfMiniGraphW, kPerfMiniGraphH,
                            lr, lg, lb, 0.7f);
        x = ag_x + kPerfMiniGraphW + kPerfSepMargin;

        // Track full audio section bounds for hover
        perf_audio_section_x_ = audio_section_start;
        perf_audio_section_w_ = x - audio_section_start;
    }

    // Separator
    tr.draw_rect(x, 4, kPerfSepW, kPerfBarH - 8, 0.30f, 0.32f, 0.35f, 0.5f);
    x += kPerfSepW + kPerfSepMargin;

    // --- Frame time ---
    std::snprintf(buf, sizeof(buf), "%.1f ms", display_ms_);
    float ms_field_w = tr.text_width("000.0 ms");
    float ms_text_w = tr.text_width(buf);
    tr.draw_text(x + (ms_field_w - ms_text_w), text_y, buf, kPerfMsColor[0], kPerfMsColor[1], kPerfMsColor[2]);
    x += ms_field_w + kPerfSepMargin;

    // Separator
    tr.draw_rect(x, 4, kPerfSepW, kPerfBarH - 8, 0.30f, 0.32f, 0.35f, 0.5f);
    x += kPerfSepW + kPerfSepMargin;

    // --- Memory ---
    char mem_buf[64];
    vivid::format_memory(mem_buf, sizeof(mem_buf),
                         static_cast<uint64_t>(smoothed_mem_mb_ * 1024.0f * 1024.0f));
    std::snprintf(buf, sizeof(buf), "MEM %s", mem_buf);
    tr.draw_text(x, text_y, buf, kPerfMemColor[0], kPerfMemColor[1], kPerfMemColor[2]);
    x += tr.text_width(buf) + kPerfSepMargin;

    // --- Mini memory sparkline ---
    float graph_x = x;
    float graph_y = (kPerfBarH - kPerfMiniGraphH) * 0.5f;
    perf_mem_graph_x_ = graph_x;
    perf_mem_graph_y_ = graph_y;

    // Dark background for sparkline
    tr.draw_rect(graph_x, graph_y, kPerfMiniGraphW, kPerfMiniGraphH,
                 0.04f, 0.05f, 0.06f, 0.8f);

    draw_perf_sparkline(tr, memory_history_.values, kPerfHistoryLen,
                        memory_history_.write_idx, memory_history_.filled,
                        graph_x, graph_y, kPerfMiniGraphW, kPerfMiniGraphH,
                        kPerfMemColor[0], kPerfMemColor[1], kPerfMemColor[2], 0.7f);

    x = graph_x + kPerfMiniGraphW + kPerfSepMargin;

    // --- XRUN counter ---
    if (snap_.audio_underrun_count > 0) {
        tr.draw_rect(x, 4, kPerfSepW, kPerfBarH - 8, 0.30f, 0.32f, 0.35f, 0.5f);
        x += kPerfSepW + kPerfSepMargin;
        std::snprintf(buf, sizeof(buf), "XRUN %u", snap_.audio_underrun_count);
        float xr = kErrorAccent[0], xg = kErrorAccent[1], xb = kErrorAccent[2];
        if (snap_.audio_underrun_active) { xr = 1.0f; xg = 0.4f; xb = 0.4f; }
        tr.draw_text(x, text_y, buf, xr, xg, xb);
        x += tr.text_width(buf) + kPerfSepMargin;
    }

    // --- Right-aligned Record / Snapshot buttons ---
    perf_button_rects_.clear();
    {
        float btn_y = (kPerfBarH - kPerfBtnH) * 0.5f;
        float rx = fw - kPerfBarPadX;  // right edge cursor
        bool can_undo = commands_.can_undo();
        bool can_redo = commands_.can_redo();

        // Snapshot button
        {
            const char* snap_label = "Snap";
            float tw = tr.text_width(snap_label);
            float btn_w = tw + kPerfBtnPadX * 2;
            rx -= btn_w;
            // Button background
            bool snap_hovered = mouse_.x >= rx && mouse_.x <= rx + btn_w &&
                                mouse_.y >= btn_y && mouse_.y <= btn_y + kPerfBtnH;
            float bg_a = snap_hovered ? 0.35f : 0.20f;
            tr.draw_rounded_rect(rx, btn_y, btn_w, kPerfBtnH, 3.0f,
                                 0.30f, 0.32f, 0.35f, bg_a);
            tr.draw_text(rx + kPerfBtnPadX, btn_y + (kPerfBtnH - tr.line_height()) * 0.5f,
                         snap_label, style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
            perf_button_rects_.push_back({rx, btn_y, btn_w, kPerfBtnH, 1, true});
            rx -= kPerfBtnMargin;
        }

        // Build console toggle
        {
            const char* build_label = "Build";
            float tw = tr.text_width(build_label);
            float btn_w = tw + kPerfBtnPadX * 2;
            rx -= btn_w;
            bool hovered = mouse_.x >= rx && mouse_.x <= rx + btn_w &&
                           mouse_.y >= btn_y && mouse_.y <= btn_y + kPerfBtnH;
            bool active = build_console_panel_.is_open();
            float br = active ? style_.accent[0] : 0.30f;
            float bg = active ? style_.accent[1] : 0.32f;
            float bb = active ? style_.accent[2] : 0.35f;
            float ba = hovered ? (active ? 0.42f : 0.35f) : (active ? 0.28f : 0.20f);
            tr.draw_rounded_rect(rx, btn_y, btn_w, kPerfBtnH, 3.0f, br, bg, bb, ba);
            tr.draw_text(rx + kPerfBtnPadX, btn_y + (kPerfBtnH - tr.line_height()) * 0.5f,
                         build_label, style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
            perf_button_rects_.push_back({rx, btn_y, btn_w, kPerfBtnH, 4, true});
            rx -= kPerfBtnMargin;
        }

        // Redo button
        {
            const char* redo_label = "Redo";
            float tw = tr.text_width(redo_label);
            float btn_w = tw + kPerfBtnPadX * 2;
            rx -= btn_w;
            bool hovered = mouse_.x >= rx && mouse_.x <= rx + btn_w &&
                           mouse_.y >= btn_y && mouse_.y <= btn_y + kPerfBtnH;
            float bg_a = can_redo ? (hovered ? 0.35f : 0.20f) : 0.10f;
            tr.draw_rounded_rect(rx, btn_y, btn_w, kPerfBtnH, 3.0f,
                                 0.30f, 0.32f, 0.35f, bg_a);
            float trr = can_redo ? style_.bright_text[0] : kDimText[0];
            float trg = can_redo ? style_.bright_text[1] : kDimText[1];
            float trb = can_redo ? style_.bright_text[2] : kDimText[2];
            tr.draw_text(rx + kPerfBtnPadX, btn_y + (kPerfBtnH - tr.line_height()) * 0.5f,
                         redo_label, trr, trg, trb);
            perf_button_rects_.push_back({rx, btn_y, btn_w, kPerfBtnH, 3, can_redo});
            rx -= kPerfBtnMargin;
        }

        // Undo button
        {
            const char* undo_label = "Undo";
            float tw = tr.text_width(undo_label);
            float btn_w = tw + kPerfBtnPadX * 2;
            rx -= btn_w;
            bool hovered = mouse_.x >= rx && mouse_.x <= rx + btn_w &&
                           mouse_.y >= btn_y && mouse_.y <= btn_y + kPerfBtnH;
            float bg_a = can_undo ? (hovered ? 0.35f : 0.20f) : 0.10f;
            tr.draw_rounded_rect(rx, btn_y, btn_w, kPerfBtnH, 3.0f,
                                 0.30f, 0.32f, 0.35f, bg_a);
            float tur = can_undo ? style_.bright_text[0] : kDimText[0];
            float tug = can_undo ? style_.bright_text[1] : kDimText[1];
            float tub = can_undo ? style_.bright_text[2] : kDimText[2];
            tr.draw_text(rx + kPerfBtnPadX, btn_y + (kPerfBtnH - tr.line_height()) * 0.5f,
                         undo_label, tur, tug, tub);
            perf_button_rects_.push_back({rx, btn_y, btn_w, kPerfBtnH, 2, can_undo});
            rx -= kPerfBtnMargin;
        }

        // MCP status dots — [● MCP] [● DEV]
        mcp_dot_rects_.clear();
        {
            // Current steady_clock time for staleness check (30 s threshold)
            auto now_ms = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now().time_since_epoch()).count());

            struct McpDotDef { const char* label; uint64_t last_ping_ms; int idx; };
            McpDotDef dots[2] = {
                { "MCP", snap_.mcp_main_last_ping_ms,  0 },
                { "DEV", snap_.mcp_opdev_last_ping_ms, 1 },
            };

            // Place rightmost dot first (DEV), then MCP, going left
            for (int di = 1; di >= 0; --di) {
                const auto& dot = dots[di];
                bool connected = (dot.last_ping_ms > 0 &&
                                  now_ms - dot.last_ping_ms < kMcpStaleMs);
                float dot_r   = connected ? 0.30f : 0.40f;
                float dot_g   = connected ? 0.85f : 0.40f;
                float dot_b   = connected ? 0.40f : 0.45f;

                float lbl_w = tr.text_width(dot.label);
                float dot_diam = 7.0f;
                float pill_w = kPerfBtnPadX + dot_diam + 3.0f + lbl_w + kPerfBtnPadX;
                rx -= pill_w;

                bool hovered = mouse_.x >= rx && mouse_.x <= rx + pill_w &&
                               mouse_.y >= btn_y && mouse_.y <= btn_y + kPerfBtnH;
                float bg_a = hovered ? 0.28f : 0.14f;
                tr.draw_rounded_rect(rx, btn_y, pill_w, kPerfBtnH, 3.0f,
                                     0.30f, 0.32f, 0.35f, bg_a);

                float dot_cx = rx + kPerfBtnPadX + dot_diam * 0.5f;
                float dot_cy = btn_y + kPerfBtnH * 0.5f;
                tr.draw_rounded_rect(dot_cx - dot_diam * 0.5f, dot_cy - dot_diam * 0.5f,
                                     dot_diam, dot_diam, dot_diam * 0.5f,
                                     dot_r, dot_g, dot_b, 0.9f);
                tr.draw_text(rx + kPerfBtnPadX + dot_diam + 3.0f,
                             btn_y + (kPerfBtnH - tr.line_height()) * 0.5f,
                             dot.label, kDimText[0], kDimText[1], kDimText[2]);

                mcp_dot_rects_.push_back({rx, btn_y, pill_w, kPerfBtnH, dot.idx});
                rx -= kPerfBtnMargin;
            }

            // Tooltip for hovered dot
            for (const auto& dr : mcp_dot_rects_) {
                if (mouse_.x >= dr.x && mouse_.x <= dr.x + dr.w &&
                    mouse_.y >= dr.y && mouse_.y <= dr.y + dr.h) {
                    uint64_t ping_ms = (dr.idx == 0) ? snap_.mcp_main_last_ping_ms
                                                     : snap_.mcp_opdev_last_ping_ms;
                    bool connected = (ping_ms > 0 && now_ms - ping_ms < kMcpStaleMs);
                    const char* srv = (dr.idx == 0) ? "vivid" : "opdev";
                    const char* status = connected ? "connected" : "not connected";
                    char tip[64];
                    std::snprintf(tip, sizeof(tip), "%s \xe2\x80\x94 %s", srv, status);
                    float tip_w = tr.text_width(tip) + 12.0f;
                    float tip_x = dr.x + dr.w * 0.5f - tip_w * 0.5f;
                    float tip_y = kPerfBarH + 4.0f;
                    tr.draw_rounded_rect(tip_x - 2.0f, tip_y, tip_w, tr.line_height() + 6.0f,
                                         3.0f, 0.10f, 0.11f, 0.13f, 0.95f);
                    tr.draw_text(tip_x + 4.0f, tip_y + 3.0f, tip,
                                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
                    break;
                }
            }
        }

        // Unsaved graph badge (non-interactive)
        if (snap_.graph_dirty) {
            const char* dirty_label = "Unsaved";
            float tw = tr.text_width(dirty_label);
            float badge_w = tw + kPerfBtnPadX * 2;
            rx -= badge_w;
            tr.draw_rounded_rect(rx, btn_y, badge_w, kPerfBtnH, 3.0f,
                                 0.72f, 0.40f, 0.14f, 0.32f);
            tr.draw_text(rx + kPerfBtnPadX, btn_y + (kPerfBtnH - tr.line_height()) * 0.5f,
                         dirty_label, 1.0f, 0.88f, 0.74f);
            rx -= kPerfBtnMargin;
        }

        // Record / Stop button
        if (snap_.is_recording) {
            // --- Recording active: [● REC  MM:SS  NNNf  Stop] ---

            // Stop button
            {
                const char* stop_label = "Stop";
                float tw = tr.text_width(stop_label);
                float btn_w = tw + kPerfBtnPadX * 2;
                rx -= btn_w;
                bool stop_hovered = mouse_.x >= rx && mouse_.x <= rx + btn_w &&
                                    mouse_.y >= btn_y && mouse_.y <= btn_y + kPerfBtnH;
                float bg_a = stop_hovered ? 0.50f : 0.35f;
                tr.draw_rounded_rect(rx, btn_y, btn_w, kPerfBtnH, 3.0f,
                                     0.60f, 0.20f, 0.20f, bg_a);
                tr.draw_text(rx + kPerfBtnPadX, btn_y + (kPerfBtnH - tr.line_height()) * 0.5f,
                             stop_label, 1.0f, 0.85f, 0.85f);
                perf_button_rects_.push_back({rx, btn_y, btn_w, kPerfBtnH, 0, true});
                rx -= kPerfBtnMargin;
            }

            // Frame count
            {
                char fc[32];
                std::snprintf(fc, sizeof(fc), "%lluf",
                              static_cast<unsigned long long>(snap_.recording_frame_count));
                float tw = tr.text_width(fc);
                rx -= tw;
                tr.draw_text(rx, text_y, fc, kDimText[0], kDimText[1], kDimText[2]);
                rx -= kPerfBtnMargin;
            }

            // Duration MM:SS
            {
                int total_sec = static_cast<int>(snap_.recording_duration_sec);
                int mm = total_sec / 60;
                int ss = total_sec % 60;
                char dur[16];
                std::snprintf(dur, sizeof(dur), "%02d:%02d", mm, ss);
                float tw = tr.text_width(dur);
                rx -= tw;
                tr.draw_text(rx, text_y, dur, style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
                rx -= kPerfBtnMargin;
            }

            // Animated red dot + REC label
            {
                float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(perf_frame_counter_) * 0.12f);
                const char* rec_label = "REC";
                float tw = tr.text_width(rec_label);
                float dot_space = kPerfRecDotR * 2 + 4.0f;
                rx -= tw + dot_space;
                float dot_cx = rx + kPerfRecDotR;
                float dot_cy = kPerfBarH * 0.5f;
                tr.draw_rounded_rect(dot_cx - kPerfRecDotR, dot_cy - kPerfRecDotR,
                                     kPerfRecDotR * 2, kPerfRecDotR * 2, kPerfRecDotR,
                                     0.95f, 0.15f, 0.15f, 0.5f + 0.5f * pulse);
                tr.draw_text(rx + dot_space, text_y, rec_label, 0.95f, 0.30f, 0.30f);
            }
        } else {
            // --- Not recording: [● REC ▾] button ---
            const char* rec_label = "REC";
            float tw = tr.text_width(rec_label);
            float arrow_w = tr.text_width("\xe2\x96\xbe"); // ▾
            float dot_space = kPerfRecDotR * 2 + 4.0f;
            float btn_w = kPerfBtnPadX + dot_space + tw + 4.0f + arrow_w + kPerfBtnPadX;
            rx -= btn_w;

            bool rec_hovered = mouse_.x >= rx && mouse_.x <= rx + btn_w &&
                               mouse_.y >= btn_y && mouse_.y <= btn_y + kPerfBtnH;
            float bg_a = rec_hovered ? 0.35f : 0.20f;
            tr.draw_rounded_rect(rx, btn_y, btn_w, kPerfBtnH, 3.0f,
                                 0.30f, 0.32f, 0.35f, bg_a);

            float ix = rx + kPerfBtnPadX;
            // Red dot
            float dot_cx = ix + kPerfRecDotR;
            float dot_cy = kPerfBarH * 0.5f;
            tr.draw_rounded_rect(dot_cx - kPerfRecDotR, dot_cy - kPerfRecDotR,
                                 kPerfRecDotR * 2, kPerfRecDotR * 2, kPerfRecDotR,
                                 0.85f, 0.20f, 0.20f, 0.9f);
            ix += dot_space;
            // REC text
            float label_y = btn_y + (kPerfBtnH - tr.line_height()) * 0.5f;
            tr.draw_text(ix, label_y, rec_label, style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
            ix += tw + 4.0f;
            // Dropdown arrow
            tr.draw_text(ix, label_y, "\xe2\x96\xbe", kDimText[0], kDimText[1], kDimText[2]);

            perf_button_rects_.push_back({rx, btn_y, btn_w, kPerfBtnH, 0, true});
        }
    }

    // Check hover over mini graph
    bool in_mini = mouse_.x >= graph_x && mouse_.x <= graph_x + kPerfMiniGraphW &&
                   mouse_.y >= graph_y && mouse_.y <= graph_y + kPerfMiniGraphH;

    // Also check if mouse is in the expanded popup region (prevents flicker)
    float exp_x = graph_x;
    float exp_right = exp_x + kPerfExpandedW;
    if (exp_right > fw - 10.0f) exp_x = fw - 10.0f - kPerfExpandedW;
    bool in_expanded = perf_mem_hovered_ &&
                       mouse_.x >= exp_x && mouse_.x <= exp_x + kPerfExpandedW &&
                       mouse_.y >= kPerfBarH && mouse_.y <= kPerfBarH + kPerfExpandedH + 30.0f;

    perf_mem_hovered_ = in_mini || in_expanded;

    // perf_mem_hovered_ drawn in draw_overlays()

    // Check hover over audio section (kHz label + load% + sparkline)
    {
        bool in_audio_section = mouse_.x >= perf_audio_section_x_ &&
                                mouse_.x <= perf_audio_section_x_ + perf_audio_section_w_ &&
                                mouse_.y >= 0 && mouse_.y <= kPerfBarH;

        float audio_exp_x = perf_audio_section_x_;
        if (audio_exp_x + kPerfExpandedW > fw - 10.0f)
            audio_exp_x = fw - 10.0f - kPerfExpandedW;
        constexpr float kAudioExpandedH = 200.0f;
        bool in_audio_expanded = perf_audio_hovered_ &&
                                 mouse_.x >= audio_exp_x && mouse_.x <= audio_exp_x + kPerfExpandedW &&
                                 mouse_.y >= kPerfBarH && mouse_.y <= kPerfBarH + kAudioExpandedH;

        perf_audio_hovered_ = in_audio_section || in_audio_expanded;

        // perf_audio_hovered_ drawn in draw_overlays()
    }
}

void NodeGraphUI::draw_perf_sparkline(Renderer2D& tr, const float* buf, uint32_t buf_len,
                                      uint32_t write_idx, bool filled,
                                      float x, float y, float w, float h,
                                      float r, float g, float b, float a) {
    uint32_t count = filled ? buf_len : write_idx;
    if (count == 0) return;

    // Find min/max for auto-scaling
    uint32_t first_idx = filled ? write_idx % buf_len : 0;
    float vmin = buf[first_idx], vmax = buf[first_idx];
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t idx = filled ? (write_idx + i) % buf_len : i;
        float v = buf[idx];
        if (v < vmin) vmin = v;
        if (v > vmax) vmax = v;
    }
    float range = vmax - vmin;
    if (range < 0.001f) range = 1.0f;

    float bar_w = w / static_cast<float>(buf_len);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t idx = filled ? (write_idx + i) % buf_len : i;
        float v = buf[idx];
        float t = (v - vmin) / range;
        float bh = std::max(1.0f, t * h);
        float bx = x + static_cast<float>(i) * bar_w;
        float by = y + h - bh;
        tr.draw_rect(bx, by, std::max(1.0f, bar_w - 0.3f), bh, r, g, b, a);
    }
}

void NodeGraphUI::draw_perf_expanded(Renderer2D& tr) {
    float fw = static_cast<float>(win_w_);

    // Position below the perf bar, aligned to the mini graph X
    float ex = perf_mem_graph_x_;
    if (ex + kPerfExpandedW > fw - 10.0f) {
        ex = fw - 10.0f - kPerfExpandedW;
    }
    float ey = kPerfBarH;

    float pad = 8.0f;
    float total_h = kPerfExpandedH + 30.0f; // extra for title/labels

    // Panel background
    tr.draw_rect(ex, ey, kPerfExpandedW, total_h, 0.08f, 0.09f, 0.10f, 0.95f);
    // Top accent
    tr.draw_rect(ex, ey, kPerfExpandedW, 2, kPerfMemColor[0], kPerfMemColor[1], kPerfMemColor[2], 0.8f);

    // Title
    float tx = ex + pad;
    float ty = ey + pad;
    tr.draw_text(tx, ty, T("memory", "Memory"), 0.85f, 0.87f, 0.90f);

    // Current value
    char mem_buf[64];
    vivid::format_memory(mem_buf, sizeof(mem_buf),
                         static_cast<uint64_t>(smoothed_mem_mb_ * 1024.0f * 1024.0f));
    float val_w = tr.text_width(mem_buf);
    tr.draw_text(ex + kPerfExpandedW - pad - val_w, ty, mem_buf,
                 kPerfMemColor[0], kPerfMemColor[1], kPerfMemColor[2]);

    // Sparkline area
    float graph_y = ty + tr.line_height() + 4.0f;
    float graph_h = total_h - (graph_y - ey) - pad;
    float graph_x = tx;
    float graph_w = kPerfExpandedW - pad * 2;

    // Dark background
    tr.draw_rect(graph_x, graph_y, graph_w, graph_h, 0.04f, 0.05f, 0.06f, 0.8f);

    draw_perf_sparkline(tr, memory_history_.values, kPerfHistoryLen,
                        memory_history_.write_idx, memory_history_.filled,
                        graph_x, graph_y, graph_w, graph_h,
                        kPerfMemColor[0], kPerfMemColor[1], kPerfMemColor[2], 0.7f);

    // Min/max labels
    uint32_t count = memory_history_.filled ? kPerfHistoryLen : memory_history_.write_idx;
    if (count > 0) {
        float vmin = memory_history_.values[0], vmax = memory_history_.values[0];
        for (uint32_t i = 0; i < count; ++i) {
            uint32_t idx = memory_history_.filled
                ? (memory_history_.write_idx + i) % kPerfHistoryLen : i;
            float v = memory_history_.values[idx];
            if (v < vmin) vmin = v;
            if (v > vmax) vmax = v;
        }
        char label[32];
        std::snprintf(label, sizeof(label), "%.0f", vmax);
        tr.draw_text(graph_x + 2, graph_y, label, style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f);
        std::snprintf(label, sizeof(label), "%.0f", vmin);
        tr.draw_text(graph_x + 2, graph_y + graph_h - tr.line_height(), label,
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f);
    }
}


void NodeGraphUI::draw_perf_audio_expanded(Renderer2D& tr) {
    float fw = static_cast<float>(win_w_);

    float ex = perf_audio_section_x_;
    if (ex + kPerfExpandedW > fw - 10.0f)
        ex = fw - 10.0f - kPerfExpandedW;
    float ey = kPerfBarH;

    float pad = 8.0f;
    float line_h = tr.line_height();
    float row_h = line_h + 2.0f;

    // Count info rows: rate, buffer, latency, channels, nodes, xruns, load
    constexpr int kInfoRows = 7;
    float info_h = kInfoRows * row_h;
    float sparkline_h = 60.0f;
    float total_h = pad + line_h + 4.0f + info_h + 4.0f + sparkline_h + pad;

    // Panel background
    tr.draw_rect(ex, ey, kPerfExpandedW, total_h, 0.08f, 0.09f, 0.10f, 0.95f);
    // Top accent
    tr.draw_rect(ex, ey, kPerfExpandedW, 2,
                 kPerfAudioColor[0], kPerfAudioColor[1], kPerfAudioColor[2], 0.8f);

    float tx = ex + pad;
    float ty = ey + pad;
    float right_x = ex + kPerfExpandedW - pad;

    // Title + current load %
    tr.draw_text(tx, ty, T("audio", "Audio"), 0.85f, 0.87f, 0.90f);
    char load_buf[32];
    std::snprintf(load_buf, sizeof(load_buf), "%.0f%%", snap_.audio_load * 100.0f);
    float load_w = tr.text_width(load_buf);
    tr.draw_text(right_x - load_w, ty, load_buf,
                 kPerfAudioColor[0], kPerfAudioColor[1], kPerfAudioColor[2]);

    ty += line_h + 4.0f;

    // Helper to draw a label: value row
    auto draw_row = [&](const char* label, const char* value) {
        tr.draw_text(tx, ty, label, style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        float vw = tr.text_width(value);
        tr.draw_text(right_x - vw, ty, value, 0.85f, 0.87f, 0.90f);
        ty += row_h;
    };

    // Sample rate
    char val[64];
    std::snprintf(val, sizeof(val), "%u Hz", snap_.audio_sample_rate);
    draw_row(T("rate", "Rate"), val);

    // Buffer size
    std::snprintf(val, sizeof(val), "%u samples", snap_.audio_buffer_size);
    draw_row(T("buffer", "Buffer"), val);

    // Latency
    if (snap_.audio_sample_rate > 0) {
        float latency_ms = static_cast<float>(snap_.audio_buffer_size)
                           / static_cast<float>(snap_.audio_sample_rate) * 1000.0f;
        std::snprintf(val, sizeof(val), "%.1f ms", latency_ms);
    } else {
        std::snprintf(val, sizeof(val), "-- ms");
    }
    draw_row(T("latency", "Latency"), val);

    // Channels
    draw_row(T("channels", "Channels"), "2");

    // Nodes
    std::snprintf(val, sizeof(val), "%u", snap_.audio_node_count);
    draw_row(T("nodes", "Nodes"), val);

    // XRUNs
    std::snprintf(val, sizeof(val), "%u", snap_.audio_underrun_count);
    draw_row(T("xruns", "XRUNs"), val);

    // Load
    std::snprintf(val, sizeof(val), "%.1f%%", snap_.audio_load * 100.0f);
    draw_row(T("load", "Load"), val);

    // Sparkline
    ty += 4.0f;
    float graph_x = tx;
    float graph_w = kPerfExpandedW - pad * 2;

    tr.draw_rect(graph_x, ty, graph_w, sparkline_h, 0.04f, 0.05f, 0.06f, 0.8f);

    // Color by current load
    float load = snap_.audio_load;
    float lr, lg, lb;
    if (load < 0.5f) {
        lr = kPerfAudioColor[0]; lg = kPerfAudioColor[1]; lb = kPerfAudioColor[2];
    } else if (load < 0.8f) {
        lr = 0.95f; lg = 0.85f; lb = 0.30f;
    } else {
        lr = 0.95f; lg = 0.35f; lb = 0.30f;
    }

    draw_perf_sparkline(tr, audio_load_history_.values, kPerfHistoryLen,
                        audio_load_history_.write_idx, audio_load_history_.filled,
                        graph_x, ty, graph_w, sparkline_h,
                        lr, lg, lb, 0.7f);
}

// -----------------------------------------------------------------------
// GPU thumbnail overlay
// -----------------------------------------------------------------------
void NodeGraphUI::draw_thumbnails(ThumbnailRenderer& renderer, const ThumbnailCache& cache,
                                  WGPUCommandEncoder encoder, WGPUTextureView surface,
                                  uint32_t w, uint32_t h) {
    renderer.begin(encoder, surface, w, h);
    for (const auto& r : node_rects_) {
        bool should_draw_thumb = (r.is_gpu) ||
                                 (custom_thumb_nodes_.count(r.node_id) > 0);
        if (!should_draw_thumb) continue;
        WGPUTextureView thumb_view = cache.get_view(r.node_id);
        if (!thumb_view) continue;
        // Viewport units are physical pixels — apply zoom/pan then dpi_scale
        float tx = gx_to_sx(r.x) * dpi_scale_;
        float ty = gy_to_sy(r.y + kAccentBarH) * dpi_scale_;
        float tw = g_to_s(r.w) * dpi_scale_;
        float th = g_to_s(kGpuThumbH) * dpi_scale_;
        if (tw <= 0 || th <= 0) continue;
        // Compute visible intersection with render target
        float fw = static_cast<float>(w), fh = static_cast<float>(h);
        float vis_x0 = std::max(tx, 0.0f);
        float vis_y0 = std::max(ty, 0.0f);
        float vis_x1 = std::min(tx + tw, fw);
        float vis_y1 = std::min(ty + th, fh);
        if (vis_x0 >= vis_x1 || vis_y0 >= vis_y1) continue;  // fully off-screen
        uint32_t sc_x = static_cast<uint32_t>(vis_x0);
        uint32_t sc_y = static_cast<uint32_t>(vis_y0);
        uint32_t sc_w = static_cast<uint32_t>(vis_x1 - vis_x0);
        uint32_t sc_h = static_cast<uint32_t>(vis_y1 - vis_y0);
        if (sc_w == 0 || sc_h == 0) continue;
        renderer.draw(thumb_view, tx, ty, tw, th, sc_x, sc_y, sc_w, sc_h);
    }
    renderer.end();
}

// -----------------------------------------------------------------------
// Parameter picker popup
// -----------------------------------------------------------------------
void NodeGraphUI::draw_param_picker(Renderer2D& tr) {
    if (!inspector_.param_picker_open || inspector_.param_picker_items.empty()) return;

    int visible = std::min(static_cast<int>(inspector_.param_picker_items.size()), kPickerMaxVisible);
    float popup_h = visible * kPickerItemH + 4;

    float px = inspector_.param_picker_x;
    float py = inspector_.param_picker_y;

    // Clamp to window bounds
    if (px + kPickerW > static_cast<float>(win_w_)) px = static_cast<float>(win_w_) - kPickerW;
    if (py + popup_h > static_cast<float>(win_h_)) py = static_cast<float>(win_h_) - popup_h;

    // Background
    draw_popup_bg(tr, style_, px, py, kPickerW, popup_h);

    // Items
    float pk_list_top = py + 2;
    float pk_list_area_h = visible * kPickerItemH;
    int pk_first = std::max(0, static_cast<int>(std::floor(inspector_.param_picker_scroll / kPickerItemH)));
    float pk_offset = inspector_.param_picker_scroll - pk_first * kPickerItemH;
    int pk_draw_count = std::min(static_cast<int>(inspector_.param_picker_items.size()) - pk_first, kPickerMaxVisible + 1);

    tr.push_clip_rect(px, pk_list_top, kPickerW, pk_list_area_h);
    for (int vi = 0; vi < pk_draw_count; ++vi) {
        int idx = pk_first + vi;
        if (idx >= static_cast<int>(inspector_.param_picker_items.size())) break;

        float iy = pk_list_top - pk_offset + vi * kPickerItemH;
        if (idx == inspector_.param_picker_sel) {
            tr.draw_rect(px + 2, iy, kPickerW - 4, kPickerItemH,
                         style_.node_sel_bg[0], style_.node_sel_bg[1], style_.node_sel_bg[2], 0.9f);
        }

        bool is_param = (!inspector_.param_picker_item_is_param.empty() &&
                         idx < static_cast<int>(inspector_.param_picker_item_is_param.size()) &&
                         inspector_.param_picker_item_is_param[idx]);
        if (is_param) {
            std::string display = "\xC2\xB7 " + inspector_.param_picker_items[idx];  // "· name"
            tr.draw_text(px + 8, iy + 3, display.c_str(),
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.6f);
        } else {
            tr.draw_text(px + 8, iy + 3, inspector_.param_picker_items[idx].c_str(),
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        }
    }
    tr.pop_clip_rect();
}

} // namespace vivid::ui
