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
    if (diagnostics_panel_open_) {
        draw_diagnostics_panel(tr);
    } else {
        diagnostics_panel_rect_ = {};
        diagnostics_mcp_rects_.clear();
    }

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
            labels[label_idx++] = T("relayout_all", "Re-layout All");
            labels[label_idx++] = T("add_sticky_note", "Add Sticky Note");
        } else if (is_sticky_ctx) {
            labels[label_idx++] = T("delete_note", "Delete Note");
            labels[label_idx++] = T("change_color", "Change Color");
        } else if (!context_node_id_.empty()) {
            if (selected_node_ids_.count(context_node_id_) && selected_node_ids_.size() > 1) {
                char delete_buf[64];
                std::snprintf(delete_buf, sizeof(delete_buf),
                              T("delete_n_nodes", "Delete %d Nodes"),
                              static_cast<int>(selected_node_ids_.size()));
                delete_label = delete_buf;
                labels[label_idx++] = delete_label.c_str();
            } else {
                labels[label_idx++] = T("delete_node", "Delete Node");
            }
            if (context_node_has_shader_)
                labels[label_idx++] = T("clone_and_edit", "Clone & Edit");
            // Solo/Unsolo
            bool is_soloed = (!snap_.solo_node_id.empty() && snap_.solo_node_id == context_node_id_);
            labels[label_idx++] = is_soloed ? T("unsolo", "Unsolo") : T("solo", "Solo");
        } else {
            labels[label_idx++] = T("delete_wire", "Delete Wire");
            labels[label_idx++] = T("insert_node", "Insert Node");
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

    const char* title = T("adding_operator", "Adding Operator");
    const char* stage = T("preparing_operator", "Preparing operator...");
    switch (async_add_stage_) {
        case AsyncAddStage::Preparing: stage = T("preparing_operator", "Preparing operator..."); break;
        case AsyncAddStage::Compiling: stage = T("compiling_graph", "Compiling graph..."); break;
        case AsyncAddStage::Applying: stage = T("applying_graph", "Applying graph..."); break;
    }

    static const char* spinner_frames[] = {"...", ".  ", ".. "};
    int spinner_idx = static_cast<int>(cursor_blink_time_ * 6.0f) % 3;

    tr.draw_text(px + 16.0f, py + 16.0f, title,
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    tr.draw_text(px + 16.0f, py + 50.0f, spinner_frames[spinner_idx],
                 style_.accent[0], style_.accent[1], style_.accent[2]);
    tr.draw_text(px + 52.0f, py + 50.0f, stage,
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);

    std::string body;
    if (async_add_display_name_.empty()) {
        body = T("wait_operator", "Please wait while Vivid prepares the selected operator.");
    } else {
        char buf_body[256];
        std::snprintf(buf_body, sizeof(buf_body),
                      T("wait_operator_named", "Please wait while Vivid prepares %s."),
                      async_add_display_name_.c_str());
        body = buf_body;
    }
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

    const char* title = T("loading_graph", "Loading Graph");
    const char* stage = T("loading_graph_stage", "Loading graph...");
    switch (async_graph_load_stage_) {
        case AsyncGraphLoadStage::Loading: stage = T("loading_graph_stage", "Loading graph..."); break;
        case AsyncGraphLoadStage::PreparingOperators: stage = T("preparing_operators", "Preparing operators..."); break;
        case AsyncGraphLoadStage::Compiling: stage = T("compiling_graph", "Compiling graph..."); break;
        case AsyncGraphLoadStage::Applying: stage = T("applying_graph", "Applying graph..."); break;
    }

    static const char* spinner_frames[] = {"...", ".  ", ".. "};
    int spinner_idx = static_cast<int>(cursor_blink_time_ * 6.0f) % 3;

    tr.draw_text(px + 16.0f, py + 16.0f, title,
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    tr.draw_text(px + 16.0f, py + 50.0f, spinner_frames[spinner_idx],
                 style_.accent[0], style_.accent[1], style_.accent[2]);
    tr.draw_text(px + 52.0f, py + 50.0f, stage,
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);

    std::string body;
    if (async_graph_load_display_name_.empty()) {
        body = T("wait_graph", "Please wait while Vivid prepares the requested graph.");
    } else {
        char buf_body[256];
        std::snprintf(buf_body, sizeof(buf_body),
                      T("wait_graph_named", "Please wait while Vivid prepares %s."),
                      async_graph_load_display_name_.c_str());
        body = buf_body;
    }
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
// Workspace header
// -----------------------------------------------------------------------
void NodeGraphUI::draw_workspace_header(Renderer2D& tr) {
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

    if (perf_frame_counter_ % kPerfMemSampleInterval == 0) {
        uint64_t mem_bytes = vivid::get_process_memory_bytes();
        float mem_mb = static_cast<float>(mem_bytes) / (1024.0f * 1024.0f);
        smoothed_mem_mb_ = mem_mb;
        memory_history_.push(mem_mb);
    }
    constexpr int kPerfDisplayInterval = 30;
    if (perf_frame_counter_ == 0 || perf_frame_counter_ % kPerfDisplayInterval == 0) {
        display_fps_ = smoothed_fps_;
        display_ms_ = smoothed_ms_;
    }
    perf_frame_counter_++;
    audio_load_history_.push(snap_.audio_load);

    float fw = static_cast<float>(win_w_);
    tr.draw_rect(0, 0, fw, kPerfBarH,
                 kPerfBarBg[0], kPerfBarBg[1], kPerfBarBg[2], kPerfBarBg[3]);
    tr.draw_rect(0, kPerfBarH - 1, fw, 1, 0.20f, 0.22f, 0.25f, 0.6f);

    auto active_variation_name = [&]() -> std::string {
        if (snap_.active_variation >= 0 &&
            snap_.active_variation < static_cast<int>(snap_.variations.size())) {
            return snap_.variations[snap_.active_variation].name;
        }
        return "none";
    };
    auto queued_variation_name = [&]() -> std::string {
        if (snap_.queued_variation >= 0 &&
            snap_.queued_variation < static_cast<int>(snap_.variations.size())) {
            return snap_.variations[snap_.queued_variation].name;
        }
        return "";
    };
    auto now_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    const bool main_connected = (snap_.mcp_main_last_ping_ms > 0 &&
                                 now_ms - snap_.mcp_main_last_ping_ms < kMcpStaleMs);
    const bool opdev_connected = (snap_.mcp_opdev_last_ping_ms > 0 &&
                                  now_ms - snap_.mcp_opdev_last_ping_ms < kMcpStaleMs);

    float diag_r = 0.30f, diag_g = 0.85f, diag_b = 0.40f;
    if (snap_.audio_underrun_active || snap_.audio_underrun_count > 0) {
        diag_r = 0.95f; diag_g = 0.35f; diag_b = 0.30f;
    } else if (!main_connected || !opdev_connected) {
        diag_r = 0.95f; diag_g = 0.82f; diag_b = 0.30f;
    }

    float btn_y = (kPerfBarH - kPerfBtnH) * 0.5f;
    float text_y = (kPerfBarH - tr.line_height()) * 0.5f;
    float left_x = kPerfBarPadX;
    float right_x = fw - kPerfBarPadX;

    perf_button_rects_.clear();
    diagnostics_button_rect_ = {};
    transport_bpm_rect_ = {};

    auto draw_divider = [&](float x) {
        tr.draw_rect(x, 4.0f, 1.0f, kPerfBarH - 8.0f, 0.30f, 0.32f, 0.35f, 0.5f);
    };

    auto draw_left_button = [&](const char* label, int action, bool enabled, bool active = false) {
        float tw = tr.text_width(label);
        float btn_w = tw + kPerfBtnPadX * 2;
        bool hovered = mouse_.x >= left_x && mouse_.x <= left_x + btn_w &&
                       mouse_.y >= btn_y && mouse_.y <= btn_y + kPerfBtnH;
        float br = active ? style_.accent[0] : 0.30f;
        float bg = active ? style_.accent[1] : 0.32f;
        float bb = active ? style_.accent[2] : 0.35f;
        float ba = !enabled ? 0.10f : (hovered ? (active ? 0.42f : 0.35f) : (active ? 0.28f : 0.20f));
        tr.draw_rounded_rect(left_x, btn_y, btn_w, kPerfBtnH, 3.0f, br, bg, bb, ba);
        float trr = enabled ? style_.bright_text[0] : kDimText[0];
        float trg = enabled ? style_.bright_text[1] : kDimText[1];
        float trb = enabled ? style_.bright_text[2] : kDimText[2];
        tr.draw_text(left_x + kPerfBtnPadX, btn_y + (kPerfBtnH - tr.line_height()) * 0.5f,
                     label, trr, trg, trb);
        perf_button_rects_.push_back({left_x, btn_y, btn_w, kPerfBtnH, action, enabled});
        left_x += btn_w + kPerfBtnMargin;
        return btn_w;
    };

    auto draw_right_button = [&](const char* label, int action, bool enabled, bool active = false) {
        float tw = tr.text_width(label);
        float btn_w = tw + kPerfBtnPadX * 2;
        right_x -= btn_w;
        bool hovered = mouse_.x >= right_x && mouse_.x <= right_x + btn_w &&
                       mouse_.y >= btn_y && mouse_.y <= btn_y + kPerfBtnH;
        float br = active ? style_.accent[0] : 0.30f;
        float bg = active ? style_.accent[1] : 0.32f;
        float bb = active ? style_.accent[2] : 0.35f;
        float ba = !enabled ? 0.10f : (hovered ? (active ? 0.42f : 0.35f) : (active ? 0.28f : 0.20f));
        tr.draw_rounded_rect(right_x, btn_y, btn_w, kPerfBtnH, 3.0f, br, bg, bb, ba);
        float trr = enabled ? style_.bright_text[0] : kDimText[0];
        float trg = enabled ? style_.bright_text[1] : kDimText[1];
        float trb = enabled ? style_.bright_text[2] : kDimText[2];
        tr.draw_text(right_x + kPerfBtnPadX, btn_y + (kPerfBtnH - tr.line_height()) * 0.5f,
                     label, trr, trg, trb);
        perf_button_rects_.push_back({right_x, btn_y, btn_w, kPerfBtnH, action, enabled});
        right_x -= kPerfBtnMargin;
        return btn_w;
    };

    auto draw_chip = [&](float& x, float max_x, const std::string& label,
                         float r, float g, float b, float alpha,
                         bool bright_text = false, bool dirty = false) -> bool {
        float dot_w = dirty ? 10.0f : 0.0f;
        float chip_w = tr.text_width(label.c_str()) + kPerfBtnPadX * 2 + dot_w;
        if (x + chip_w > max_x) return false;
        tr.draw_rounded_rect(x, btn_y, chip_w, kPerfBtnH, 3.0f, r, g, b, alpha);
        float tx = x + kPerfBtnPadX;
        if (dirty) {
            tr.draw_rounded_rect(tx, btn_y + kPerfBtnH * 0.5f - 2.0f,
                                 4.0f, 4.0f, 1.0f, 0.78f, 0.46f, 0.14f, 0.95f);
            tx += 10.0f;
        }
        const auto& tc = bright_text ? style_.bright_text : style_.dim_text;
        tr.draw_text(tx, btn_y + (kPerfBtnH - tr.line_height()) * 0.5f,
                     label.c_str(), tc[0], tc[1], tc[2]);
        x += chip_w + 6.0f;
        return true;
    };

    {
        const char* diag_label = T("diag", "Diag");
        float label_w = tr.text_width(diag_label);
        float btn_w = label_w + kPerfBtnPadX * 2 + 12.0f;
        bool hovered = mouse_.x >= left_x && mouse_.x <= left_x + btn_w &&
                       mouse_.y >= btn_y && mouse_.y <= btn_y + kPerfBtnH;
        float alpha = diagnostics_panel_open_ ? 0.28f : (hovered ? 0.24f : 0.16f);
        tr.draw_rounded_rect(left_x, btn_y, btn_w, kPerfBtnH, 3.0f,
                             0.28f, 0.30f, 0.34f, alpha);
        tr.draw_rounded_rect(left_x + kPerfBtnPadX, btn_y + kPerfBtnH * 0.5f - 3.0f,
                             6.0f, 6.0f, 2.0f, diag_r, diag_g, diag_b, 0.95f);
        tr.draw_text(left_x + kPerfBtnPadX + 12.0f,
                     btn_y + (kPerfBtnH - tr.line_height()) * 0.5f,
                     diag_label,
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        diagnostics_button_rect_ = {left_x, btn_y, btn_w, kPerfBtnH, true};
        perf_button_rects_.push_back({left_x, btn_y, btn_w, kPerfBtnH, 2, true});
        left_x += btn_w + kPerfBtnMargin;
    }

    if (snap_.graph_dirty && fw > 980.0f) {
        const char* dirty_label = T("unsaved", "Unsaved");
        float badge_w = tr.text_width(dirty_label) + kPerfBtnPadX * 2;
        tr.draw_rounded_rect(left_x, btn_y, badge_w, kPerfBtnH, 3.0f,
                             0.72f, 0.40f, 0.14f, 0.28f);
        tr.draw_text(left_x + kPerfBtnPadX, btn_y + (kPerfBtnH - tr.line_height()) * 0.5f,
                     dirty_label, 1.0f, 0.88f, 0.74f);
        left_x += badge_w + kPerfBtnMargin;
    }

    draw_divider(left_x - kPerfBtnMargin * 0.5f);
    left_x += 6.0f;

    if (snap_.is_recording) {
        draw_right_button(T("stop", "Stop"), 0, true, true);

        char dur[16];
        int total_sec = static_cast<int>(snap_.recording_duration_sec);
        std::snprintf(dur, sizeof(dur), "%02d:%02d", total_sec / 60, total_sec % 60);
        float dur_w = tr.text_width(dur);
        right_x -= dur_w;
        tr.draw_text(right_x, text_y, dur, style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        right_x -= kPerfBtnMargin;

        const char* rec_label = "REC";
        float rec_w = tr.text_width(rec_label) + 14.0f;
        right_x -= rec_w;
        float pulse = 0.5f + 0.5f * std::sin(static_cast<float>(perf_frame_counter_) * 0.12f);
        tr.draw_rounded_rect(right_x, btn_y + kPerfBtnH * 0.5f - 3.0f, 6.0f, 6.0f, 2.0f,
                             0.95f, 0.18f, 0.18f, 0.5f + 0.5f * pulse);
        tr.draw_text(right_x + 10.0f, text_y, rec_label, 0.95f, 0.30f, 0.30f);
        right_x -= kPerfBtnMargin;
    } else {
        const char* rec_label = "REC";
        float tw = tr.text_width(rec_label);
        float arrow_w = tr.text_width("\xe2\x96\xbe");
        float dot_space = kPerfRecDotR * 2 + 4.0f;
        float btn_w = kPerfBtnPadX + dot_space + tw + 4.0f + arrow_w + kPerfBtnPadX;
        right_x -= btn_w;

        bool rec_hovered = mouse_.x >= right_x && mouse_.x <= right_x + btn_w &&
                           mouse_.y >= btn_y && mouse_.y <= btn_y + kPerfBtnH;
        float bg_a = rec_hovered ? 0.35f : 0.20f;
        tr.draw_rounded_rect(right_x, btn_y, btn_w, kPerfBtnH, 3.0f,
                             0.42f, 0.17f, 0.17f, bg_a);

        float ix = right_x + kPerfBtnPadX;
        float dot_cx = ix + kPerfRecDotR;
        float dot_cy = kPerfBarH * 0.5f;
        tr.draw_rounded_rect(dot_cx - kPerfRecDotR, dot_cy - kPerfRecDotR,
                             kPerfRecDotR * 2, kPerfRecDotR * 2, kPerfRecDotR,
                             0.85f, 0.20f, 0.20f, 0.9f);
        ix += dot_space;
        tr.draw_text(ix, btn_y + (kPerfBtnH - tr.line_height()) * 0.5f, rec_label,
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        ix += tw + 4.0f;
        tr.draw_text(ix, btn_y + (kPerfBtnH - tr.line_height()) * 0.5f, "\xe2\x96\xbe",
                     kDimText[0], kDimText[1], kDimText[2]);
        perf_button_rects_.push_back({right_x, btn_y, btn_w, kPerfBtnH, 0, true});
        right_x -= kPerfBtnMargin;
    }

    draw_right_button(T("snap", "Snap"), 1, true);

    {
        const float blink_size = 8.0f;
        const float blink_y = btn_y + (kPerfBtnH - blink_size) * 0.5f;
        const float alpha = 0.28f + 0.62f * std::max(0.0f, 1.0f - snap_.metronome_beat_phase);
        tr.draw_rounded_rect(left_x, blink_y, blink_size, blink_size, 4.0f,
                             style_.accent[0], style_.accent[1], style_.accent[2], alpha);
        left_x += blink_size + 8.0f;
    }

    {
        const float bpm = std::max(1.0f, snap_.metronome_bpm);
        const float rounded_bpm = std::round(bpm);
        std::string bpm_label = std::fabs(bpm - rounded_bpm) < 0.05f
            ? (std::to_string(static_cast<int>(rounded_bpm)) + " BPM")
            : (format_float(bpm, 1) + " BPM");
        float pill_w = std::max(tr.text_width("300.0 BPM"), tr.text_width(bpm_label.c_str()))
                     + kPerfBtnPadX * 2;
        bool hovered = mouse_.x >= left_x && mouse_.x <= left_x + pill_w &&
                       mouse_.y >= btn_y && mouse_.y <= btn_y + kPerfBtnH;
        transport_bpm_rect_ = {left_x, btn_y, pill_w, kPerfBtnH, true};
        if (transport_bpm_editing_) {
            draw_editing_text_field(tr, style_, left_x, btn_y, pill_w, kPerfBtnH,
                                    transport_bpm_edit_buffer_, text_edit_,
                                    cursor_blink_on(), kPerfBtnPadX, 3.0f);
        } else {
            const float alpha = hovered ? 0.82f : 0.70f;
            tr.draw_rounded_rect(left_x, btn_y, pill_w, kPerfBtnH, 3.0f,
                                 0.18f, 0.20f, 0.23f, alpha);
            tr.draw_text(left_x + kPerfBtnPadX, btn_y + (kPerfBtnH - tr.line_height()) * 0.5f,
                         bpm_label.c_str(),
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        }
        left_x += pill_w + kPerfBtnMargin;
    }

    draw_left_button("-", 6, true);
    {
        char meter_buf[16];
        std::snprintf(meter_buf, sizeof(meter_buf), "%d/4", std::max(1, snap_.metronome_beats_per_bar));
        float pill_w = tr.text_width(meter_buf) + kPerfBtnPadX * 2;
        tr.draw_rounded_rect(left_x, btn_y, pill_w, kPerfBtnH, 3.0f,
                             0.18f, 0.20f, 0.23f, 0.70f);
        tr.draw_text(left_x + kPerfBtnPadX, btn_y + (kPerfBtnH - tr.line_height()) * 0.5f,
                     meter_buf,
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        left_x += pill_w + kPerfBtnMargin;
    }
    draw_left_button("+", 7, true);

    float session_zone_right = right_x;
    if (left_x + 70.0f < session_zone_right) {
        draw_divider(left_x - kPerfBtnMargin * 0.5f);
        left_x += 8.0f;
        draw_chip(left_x, session_zone_right, active_variation_name(),
                  style_.accent[0], style_.accent[1], style_.accent[2], 0.20f, true, snap_.variation_dirty);
        const std::string queued = queued_variation_name();
        if (!queued.empty() && fw > 1080.0f) {
            draw_chip(left_x, session_zone_right, std::string("> ") + queued,
                      style_.accent[0], style_.accent[1], style_.accent[2], 0.12f, false, false);
        }
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

void NodeGraphUI::draw_diagnostics_panel(Renderer2D& tr) {
    diagnostics_mcp_rects_.clear();
    diagnostics_panel_rect_ = {};

    float fw = static_cast<float>(win_w_);
    constexpr float kPanelW = 372.0f;
    constexpr float kPanelH = 364.0f;
    const float pad = 10.0f;
    const float section_gap = 10.0f;
    const float col_gap = 14.0f;
    const float panel_w = std::min(kPanelW, fw - 20.0f);
    float ex = diagnostics_button_rect_.visible ? diagnostics_button_rect_.x : 10.0f;
    if (ex + panel_w > fw - 10.0f) ex = fw - 10.0f - panel_w;
    float ey = kPerfBarH + 6.0f;
    diagnostics_panel_rect_ = {ex, ey, panel_w, kPanelH, true};

    tr.draw_rect(ex, ey, panel_w, kPanelH, 0.07f, 0.08f, 0.10f, 0.97f);
    tr.draw_rect(ex, ey, panel_w, 2.0f, style_.accent[0], style_.accent[1], style_.accent[2], 0.85f);
    tr.draw_rect(ex, ey + kPanelH - 1.0f, panel_w, 1.0f, 0.18f, 0.20f, 0.23f, 0.8f);

    const float line_h = tr.line_height();
    float header_y = ey + pad;
    tr.draw_text(ex + pad, header_y, T("diagnostics", "Diagnostics"),
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);

    auto now_ms = static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    const bool main_connected = (snap_.mcp_main_last_ping_ms > 0 &&
                                 now_ms - snap_.mcp_main_last_ping_ms < kMcpStaleMs);
    const bool opdev_connected = (snap_.mcp_opdev_last_ping_ms > 0 &&
                                  now_ms - snap_.mcp_opdev_last_ping_ms < kMcpStaleMs);
    const char* status_label = (snap_.audio_underrun_active || snap_.audio_underrun_count > 0)
        ? T("attention", "Attention")
        : ((main_connected && opdev_connected) ? T("stable", "Stable") : T("partial", "Partial"));
    float sr = 0.30f, sg = 0.85f, sb = 0.40f;
    if (snap_.audio_underrun_active || snap_.audio_underrun_count > 0) {
        sr = 0.95f; sg = 0.35f; sb = 0.30f;
    } else if (!main_connected || !opdev_connected) {
        sr = 0.95f; sg = 0.82f; sb = 0.30f;
    }
    float status_w = tr.text_width(status_label);
    tr.draw_rounded_rect(ex + panel_w - pad - status_w - 10.0f, header_y + 4.0f,
                         6.0f, 6.0f, 2.0f, sr, sg, sb, 0.95f);
    tr.draw_text(ex + panel_w - pad - status_w, header_y, status_label, sr, sg, sb);

    float content_y = header_y + line_h + 8.0f;
    float col_w = (panel_w - pad * 2.0f - col_gap) * 0.5f;
    float left_x = ex + pad;
    float right_x = left_x + col_w + col_gap;

    auto draw_key_value = [&](float x, float y, float w, const char* label, const char* value,
                              float vr, float vg, float vb) {
        tr.draw_text(x, y, label, style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        float value_w = tr.text_width(value);
        tr.draw_text(x + std::max(0.0f, w - value_w), y, value, vr, vg, vb);
    };
    auto draw_hot_list = [&](float x, float y, float w, const char* title,
                             const std::vector<AudioHotNodeSnapshot>& items,
                             bool lane_state_list) {
        tr.draw_text(x, y, title, style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        y += line_h + 3.0f;
        const float box_h = (line_h + 4.0f) * 3.0f + 8.0f;
        tr.draw_rect(x, y, w, box_h, 0.04f, 0.05f, 0.06f, 0.85f);
        if (items.empty()) {
            tr.draw_text(x + 6.0f, y + 5.0f, T("none", "none"),
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
            return;
        }
        char metric[64];
        size_t count = std::min<size_t>(3, items.size());
        for (size_t i = 0; i < count; ++i) {
            float row_top = y + 5.0f + static_cast<float>(i) * (line_h + 4.0f);
            const auto& item = items[i];
            tr.draw_text(x + 6.0f, row_top, item.node_id.c_str(),
                         style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
            if (lane_state_list) {
                std::snprintf(metric, sizeof(metric), "%u state / %u lanes",
                              item.lane_state_entries, item.last_lane_count);
            } else {
                std::snprintf(metric, sizeof(metric), "%.1f%% / %uus",
                              item.last_block_budget_pct, item.ema_block_us);
            }
            float metric_w = tr.text_width(metric);
            tr.draw_text(x + std::max(6.0f, w - metric_w - 6.0f), row_top, metric,
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        }
    };

    char value[64];
    float row_y = content_y;
    draw_key_value(left_x, row_y, col_w, T("fps", "FPS"),
                   format_int(static_cast<int>(std::lround(display_fps_))).c_str(),
                   kPerfFpsColor[0], kPerfFpsColor[1], kPerfFpsColor[2]);
    row_y += line_h + 4.0f;
    std::snprintf(value, sizeof(value), "%.1f ms", display_ms_);
    draw_key_value(left_x, row_y, col_w, T("frame", "Frame"), value,
                   kPerfMsColor[0], kPerfMsColor[1], kPerfMsColor[2]);
    row_y += line_h + 4.0f;
    char mem_buf[64];
    vivid::format_memory(mem_buf, sizeof(mem_buf),
                         static_cast<uint64_t>(smoothed_mem_mb_ * 1024.0f * 1024.0f));
    draw_key_value(left_x, row_y, col_w, T("memory", "Memory"), mem_buf,
                   kPerfMemColor[0], kPerfMemColor[1], kPerfMemColor[2]);
    row_y += line_h + 4.0f;
    std::snprintf(value, sizeof(value), "%.1f%%", snap_.audio_load * 100.0f);
    draw_key_value(left_x, row_y, col_w, T("audio_load", "Audio Load"), value,
                   kPerfAudioColor[0], kPerfAudioColor[1], kPerfAudioColor[2]);
    row_y += line_h + section_gap;

    float graph_w = col_w;
    float graph_h = 44.0f;
    tr.draw_text(left_x, row_y, T("load_history", "Load History"),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    row_y += line_h + 3.0f;
    tr.draw_rect(left_x, row_y, graph_w, graph_h, 0.04f, 0.05f, 0.06f, 0.85f);
    draw_perf_sparkline(tr, audio_load_history_.values, kPerfHistoryLen,
                        audio_load_history_.write_idx, audio_load_history_.filled,
                        left_x, row_y, graph_w, graph_h,
                        kPerfAudioColor[0], kPerfAudioColor[1], kPerfAudioColor[2], 0.72f);

    float right_row_y = content_y;
    std::snprintf(value, sizeof(value), "%u Hz", snap_.audio_sample_rate);
    draw_key_value(right_x, right_row_y, col_w, T("rate", "Rate"), value,
                   style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    right_row_y += line_h + 4.0f;
    std::snprintf(value, sizeof(value), "%u samp", snap_.audio_buffer_size);
    draw_key_value(right_x, right_row_y, col_w, T("buffer", "Buffer"), value,
                   style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    right_row_y += line_h + 4.0f;
    if (snap_.audio_sample_rate > 0) {
        float latency_ms = static_cast<float>(snap_.audio_buffer_size)
                         / static_cast<float>(snap_.audio_sample_rate) * 1000.0f;
        std::snprintf(value, sizeof(value), "%.1f ms", latency_ms);
    } else {
        std::snprintf(value, sizeof(value), "-- ms");
    }
    draw_key_value(right_x, right_row_y, col_w, T("latency", "Latency"), value,
                   style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
    right_row_y += line_h + 4.0f;
    std::snprintf(value, sizeof(value), "%u", snap_.audio_underrun_count);
    float xr = snap_.audio_underrun_count > 0 ? 0.95f : style_.bright_text[0];
    float xg = snap_.audio_underrun_count > 0 ? 0.35f : style_.bright_text[1];
    float xb = snap_.audio_underrun_count > 0 ? 0.30f : style_.bright_text[2];
    draw_key_value(right_x, right_row_y, col_w, T("xruns", "XRUNs"), value, xr, xg, xb);
    right_row_y += line_h + section_gap;

    tr.draw_text(right_x, right_row_y, T("memory_history", "Memory History"),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    right_row_y += line_h + 3.0f;
    tr.draw_rect(right_x, right_row_y, graph_w, graph_h, 0.04f, 0.05f, 0.06f, 0.85f);
    draw_perf_sparkline(tr, memory_history_.values, kPerfHistoryLen,
                        memory_history_.write_idx, memory_history_.filled,
                        right_x, right_row_y, graph_w, graph_h,
                        kPerfMemColor[0], kPerfMemColor[1], kPerfMemColor[2], 0.72f);

    float list_y = std::max(row_y + graph_h, right_row_y + graph_h) + section_gap;
    draw_hot_list(left_x, list_y, col_w, T("hot_nodes", "Hot Nodes"), snap_.audio_top_nodes, false);
    draw_hot_list(right_x, list_y, col_w, T("lane_state", "Lane State"), snap_.audio_top_lane_state_nodes, true);

    float mcp_y = ey + kPanelH - pad - (line_h + 8.0f) * 2.0f - 8.0f;
    tr.draw_rect(ex + pad, mcp_y - 4.0f, panel_w - pad * 2.0f, 1.0f, 0.20f, 0.22f, 0.25f, 0.8f);
    tr.draw_text(ex + pad, mcp_y, T("connectivity", "Connectivity"),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
    mcp_y += line_h + 4.0f;

    struct McpRow { const char* name; bool connected; int idx; };
    const McpRow rows[] = {
        {"vivid MCP", main_connected, 0},
        {"opdev MCP", opdev_connected, 1},
    };
    for (const auto& row : rows) {
        const float row_h = line_h + 6.0f;
        tr.draw_rounded_rect(ex + pad, mcp_y - 2.0f, panel_w - pad * 2.0f, row_h, 3.0f,
                             0.18f, 0.20f, 0.23f, 0.65f);
        float dot_r = row.connected ? 0.30f : 0.95f;
        float dot_g = row.connected ? 0.85f : 0.35f;
        float dot_b = row.connected ? 0.40f : 0.30f;
        tr.draw_rounded_rect(ex + pad + 8.0f, mcp_y + line_h * 0.5f - 1.0f,
                             6.0f, 6.0f, 2.0f, dot_r, dot_g, dot_b, 0.95f);
        tr.draw_text(ex + pad + 20.0f, mcp_y, row.name,
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        const char* state = row.connected ? T("connected", "Connected") : T("setup", "Setup");
        float state_w = tr.text_width(state);
        tr.draw_text(ex + panel_w - pad - state_w - 6.0f, mcp_y, state,
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        diagnostics_mcp_rects_.push_back({ex + pad, mcp_y - 2.0f, panel_w - pad * 2.0f, row_h, row.idx});
        mcp_y += row_h + 4.0f;
    }
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
        float source_aspect = 0.0f;
        const auto* ns = snap_.find_node(r.node_id);
        if (ns && ns->gpu_tex_width > 0 && ns->gpu_tex_height > 0)
            source_aspect = static_cast<float>(ns->gpu_tex_width) / static_cast<float>(ns->gpu_tex_height);
        renderer.draw(thumb_view, tx, ty, tw, th, sc_x, sc_y, sc_w, sc_h, source_aspect);
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
