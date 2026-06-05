#include <nlohmann/json.hpp>
#include "ui/graph/node_graph.h"
#include "ui/graph/node_graph_constants.h"
#include "ui/graph/node_graph_draw_inspector_helpers.h"
#include "ui/graph/node_graph_util.h"
#include "ui/rendering/renderer_2d.h"
#include "ui/rendering/text_util.h"
#include "ui/style/i18n.h"
#include "common/string_util.h"
#include "runtime/graph/compiled_graph.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace vivid::ui {

using vivid::format_float;
using vivid::format_int;
using vivid::format_uint;

// Inspector draw-helpers (build_semantic_hint, draw_inspector_card/env_chip/
// text_button/left_accent, find_param_connection) + InspectorCardBox /
// ParamConnectionInfo now live in node_graph_draw_inspector_helpers.h (audit
// 08-R2-F1).

void NodeGraphUI::draw_clip_inspector(Renderer2D& tr, uint32_t /*w*/, uint32_t h) {
    const auto* track = snap_.session.find_track(selected_clip_track_);
    const SessionClipSnap* clip = nullptr;
    if (track) {
        for (const auto& c : track->clips)
            if (c.id == selected_clip_id_) { clip = &c; break; }
    }
    if (!clip) { selected_clip_track_.clear(); selected_clip_id_.clear(); return; }

    const float insp_x = inspector_x();
    const float pad = kInspPadX;
    tr.draw_rect(insp_x, 0, kInspectorW, static_cast<float>(h),
                 style_.inspector_bg[0], style_.inspector_bg[1], style_.inspector_bg[2], 0.95f);
    tr.draw_rect(insp_x, 0, 2, static_cast<float>(h),
                 style_.separator[0], style_.separator[1], style_.separator[2]);

    const float px = insp_x + pad;
    const float panel_w = kInspectorW - pad * 2.0f;
    float py = 14.0f;
    tr.draw_text(px, py, ("Clip:  " + clip->name).c_str(),
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], 1.05f);
    py += 20.0f;
    tr.draw_text(px, py, ("Track: " + track->name).c_str(),
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.85f);
    py += 18.0f;
    if (clip->has_fade) {
        char fb[48]; std::snprintf(fb, sizeof(fb), "Fade: %.2g bars on launch", clip->fade_bars);
        tr.draw_text(px, py, fb, style_.accent[0], style_.accent[1], style_.accent[2], 0.85f);
        py += 18.0f;
    }

    // Header actions: Launch (make this clip's values live) / Update from live (re-capture).
    {
        const float bh = 22.0f;
        auto draw_btn = [&](float bx, float bw, const char* label, int action, bool enabled) {
            const bool hov = enabled && mouse_.x >= bx && mouse_.x <= bx + bw &&
                             mouse_.y >= py && mouse_.y <= py + bh;
            tr.draw_rect(bx, py, bw, bh, style_.accent[0], style_.accent[1], style_.accent[2],
                         enabled ? (hov ? 0.35f : 0.18f) : 0.07f);
            const auto& tc = enabled ? style_.bright_text : style_.dim_text;
            tr.draw_text(bx + 8.0f, py + 4.0f, label, tc[0], tc[1], tc[2], 0.85f);
            if (enabled) clip_header_btn_rects_.push_back({bx, py, bw, bh, action});
        };
        draw_btn(px, 78.0f, "Launch", 0, true);
        draw_btn(px + 86.0f, panel_w - 86.0f, "Update from live", 1, track->dirty);
        py += bh + 10.0f;
    }

    tr.draw_rect(px, py, panel_w, 1.0f,
                 style_.separator[0], style_.separator[1], style_.separator[2], 0.6f);
    py += 8.0f;
    tr.draw_text(px, py, "Nodes \xE2\x80\x94 click a row to edit live, then Update",
                 style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.72f);
    py += 18.0f;

    // Navigable owning-node rows (click -> select the node, opening its real inspector).
    for (const auto& node_id : track->owned_node_ids) {
        const auto pit = clip->params.find(node_id);
        const auto sit = clip->string_params.find(node_id);
        const auto bit = clip->bypass.find(node_id);
        const size_t nval = (pit != clip->params.end() ? pit->second.size() : 0) +
                            (sit != clip->string_params.end() ? sit->second.size() : 0) +
                            (bit != clip->bypass.end() ? 1u : 0u);

        const float row_h = 18.0f;
        const bool row_hov = mouse_.x >= px && mouse_.x <= px + panel_w &&
                             mouse_.y >= py - 2.0f && mouse_.y <= py - 2.0f + row_h;
        if (row_hov)
            tr.draw_rect(px - 2.0f, py - 2.0f, panel_w + 4.0f, row_h,
                         style_.accent[0], style_.accent[1], style_.accent[2], 0.14f);
        tr.draw_text(px, py, node_id.c_str(),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], 0.95f);
        char sb[48];
        if (nval) std::snprintf(sb, sizeof(sb), "%zu val%s  edit \xE2\x96\xB8", nval, nval == 1 ? "" : "s");
        else      std::snprintf(sb, sizeof(sb), "edit \xE2\x96\xB8");
        const float sw = tr.text_width(sb);
        tr.draw_text(px + panel_w - sw, py, sb,
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.78f);
        clip_node_link_rects_.push_back({px - 2.0f, py - 2.0f, panel_w + 4.0f, row_h, node_id});
        py += row_h;

        // Dim read-out of the stored values, kept for reference.
        if (bit != clip->bypass.end()) {
            tr.draw_text(px + 12.0f, py, bit->second ? "bypassed = on" : "bypassed = off",
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f);
            py += 13.0f;
        }
        if (pit != clip->params.end()) {
            for (const auto& [pname, pval] : pit->second) {
                char line[160];
                std::snprintf(line, sizeof(line), "%s = %.4g", pname.c_str(), static_cast<double>(pval));
                tr.draw_text(px + 12.0f, py, line,
                             style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f);
                py += 13.0f;
            }
        }
        if (sit != clip->string_params.end()) {
            for (const auto& [pname, pval] : sit->second) {
                std::string v = pval;
                if (v.size() > 34) v = v.substr(0, 34) + "\xE2\x80\xA6";
                tr.draw_text(px + 12.0f, py, (pname + " = \"" + v + "\"").c_str(),
                             style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.7f);
                py += 13.0f;
            }
        }
        py += 8.0f;
    }
    inspector_.insp_content_h = py;
}

// Footer chip shown over the node inspector while editing a node that was opened
// from a clip — lets you re-capture into that clip or jump back to it.
void NodeGraphUI::draw_clip_breadcrumb(Renderer2D& tr, uint32_t /*w*/, uint32_t h) {
    if (clip_return_clip_.empty() || selected_node_ids_.size() != 1) return;
    const auto* rtrack = snap_.session.find_track(clip_return_track_);
    if (!rtrack) { clip_return_track_.clear(); clip_return_clip_.clear(); return; }
    const std::string& sel = *selected_node_ids_.begin();
    bool owned = false;
    for (const auto& nid : rtrack->owned_node_ids) if (nid == sel) { owned = true; break; }
    if (!owned) return;   // selected something outside the clip's track — hide the chip
    std::string clip_name = clip_return_clip_;
    for (const auto& c : rtrack->clips) if (c.id == clip_return_clip_) { clip_name = c.name; break; }

    const float insp_x = inspector_x();
    const float pad = kInspPadX;
    const float fh = 26.0f;
    const float fy = static_cast<float>(h) - fh;
    tr.draw_rect(insp_x, fy, kInspectorW, fh, style_.accent[0], style_.accent[1], style_.accent[2], 0.16f);
    tr.draw_rect(insp_x, fy, kInspectorW, 1.0f, style_.separator[0], style_.separator[1], style_.separator[2], 0.7f);

    std::string back = "\xE2\x86\x90 " + clip_name;   // ← <clip name>
    const float backw = tr.text_width(back.c_str());
    tr.draw_text(insp_x + pad, fy + 7.0f, back.c_str(),
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], 0.9f);
    clip_header_btn_rects_.push_back({insp_x + pad - 2.0f, fy, backw + 10.0f, fh, 2});  // back

    const char* ub = "Update Clip";
    const float ubw = tr.text_width(ub) + 16.0f;
    const float ubx = insp_x + kInspectorW - pad - ubw;
    const bool uhov = mouse_.x >= ubx && mouse_.x <= ubx + ubw && mouse_.y >= fy + 3.0f && mouse_.y <= fy + fh - 3.0f;
    tr.draw_rect(ubx, fy + 3.0f, ubw, fh - 6.0f, style_.accent[0], style_.accent[1], style_.accent[2], uhov ? 0.42f : 0.26f);
    tr.draw_text(ubx + 8.0f, fy + 7.0f, ub,
                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], 0.85f);
    clip_header_btn_rects_.push_back({ubx, fy + 3.0f, ubw, fh - 6.0f, 3});  // update + return
}

void NodeGraphUI::draw_inspector(Renderer2D& tr, uint32_t w, uint32_t h) {
    clip_node_link_rects_.clear();
    clip_header_btn_rects_.clear();
    inspector_.slider_rects.clear();
    inspector_.xy_pad_rects.clear();
    inspector_.xy_toggle_rects.clear();
    inspector_.xy_tab_rects.clear();
    inspector_.surface.begin_frame();
    inspector_.color_swatch_rects.clear();
    inspector_.bool_rects.clear();
    inspector_.value_text_rects.clear();
    inspector_.dropdown_rects.clear();
    inspector_.file_button_rects.clear();
    inspector_.resolution_rects.clear();
    inspector_.preset_dropdown_rects.clear();
    inspector_.preset_save_rects.clear();
    inspector_.docs_link_rects.clear();
    inspector_.open_editor_rects.clear();
    inspector_.bypass_button_rects.clear();
    inspector_.node_id_rects.clear();
    inspector_.midi_remove_rects.clear();
    inspector_.midi_range_rects.clear();
    patch_jacks_.clear();
    patch_wires_.clear();
    inspector_.group_header_rects.clear();
    inspector_.state_preset_rects.clear();
    inspector_.state_header_rects.clear();
    inspector_.lock_badge_rects.clear();
    inspector_.label_rects.clear();
    inspector_.wire_remap_rects.clear();
    inspector_.wire_clamp_rects.clear();
    inspector_.wire_curve_rects.clear();
    inspector_.mod_assign_rects.clear();
    inspector_.mod_amount_rects.clear();

    // Wire inspector (when a wire is selected and no nodes are)
    if (selected_node_ids_.empty() && wire_inspector_visible()) {
        const auto& c = snap_.connections[selected_wire_idx_];
        float insp_x = inspector_x();
        tr.draw_rect(insp_x, 0, kInspectorW, static_cast<float>(h),
                     style_.inspector_bg[0], style_.inspector_bg[1], style_.inspector_bg[2], 0.95f);
        tr.draw_rect(insp_x, 0, 2, static_cast<float>(h),
                     style_.separator[0], style_.separator[1], style_.separator[2]);

        float px = insp_x + kInspPadX;
        float py = kPerfBarH + 8;

        // Header
        tr.draw_text(px, py, T("wire", "Wire"), style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], 1.0f, 1.2f);
        py += 22;
        std::string label = c.from_node + "/" + c.from_port + " \xE2\x86\x92 " + c.to_node + "/" + c.to_port;
        tr.draw_text(px, py, label.c_str(), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.85f);
        py += 20;
        if (c.invalid) {
            std::string broken = c.invalid_reason.empty() ? T("broken_connection", "Broken connection") : c.invalid_reason;
            tr.draw_text(px, py, broken.c_str(), 1.0f, 0.45f, 0.38f, 0.85f);
            py += 20;
        }

        if (!c.dropped && c.supports_remap()) {
        // Remap fields
        const char* field_labels[4] = {
            T("from_min", "From Min"), T("from_max", "From Max"),
            T("to_min", "To Min"), T("to_max", "To Max") };
        float vals[4] = { c.from_min, c.from_max, c.to_min, c.to_max };
        float field_w = kInspectorW - kInspPadX * 2 - 80;

        for (int f = 0; f < 4; ++f) {
            py += 4;
            tr.draw_text(px, py + 2, field_labels[f],
                         style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.85f);

            float fx = px + 80;
            float fw = field_w;
            float fh = 18;

            // Background for text field
            bool editing_this = inspector_.editing_wire_remap && inspector_.edit_wire_remap_field == f;
            tr.draw_rect(fx, py, fw, fh,
                         editing_this ? style_.accent[0] * 0.3f : style_.inspector_bg[0] * 0.7f,
                         editing_this ? style_.accent[1] * 0.3f : style_.inspector_bg[1] * 0.7f,
                         editing_this ? style_.accent[2] * 0.3f : style_.inspector_bg[2] * 0.7f, 0.8f);
            tr.draw_rect(fx, py, fw, 1, style_.separator[0], style_.separator[1], style_.separator[2], 0.5f);
            tr.draw_rect(fx, py + fh - 1, fw, 1, style_.separator[0], style_.separator[1], style_.separator[2], 0.5f);

            if (editing_this) {
                if (text_edit_.has_selection()) {
                    int lo = text_edit_.sel_min();
                    int hi = text_edit_.sel_max();
                    float sel_x0 = fx + 4 + tr.text_width(inspector_.edit_buffer.substr(0, lo).c_str());
                    float sel_x1 = fx + 4 + tr.text_width(inspector_.edit_buffer.substr(0, hi).c_str());
                    tr.draw_rect(sel_x0, py + 1, sel_x1 - sel_x0, fh - 2,
                                 style_.accent[0], style_.accent[1], style_.accent[2], 0.3f);
                }
                tr.draw_text(fx + 4, py + 2, inspector_.edit_buffer.c_str(),
                             style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
                if (cursor_blink_on()) {
                    int cpos = std::max(0, std::min(text_edit_.cursor, static_cast<int>(inspector_.edit_buffer.size())));
                    float cx = fx + 4 + tr.text_width(inspector_.edit_buffer.substr(0, cpos).c_str());
                    tr.draw_rect(cx, py + 1, 1.0f, fh - 2,
                                 style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
                }
            } else {
                char buf[32];
                std::snprintf(buf, sizeof(buf), "%.3g", vals[f]);
                tr.draw_text(fx + 4, py + 2, buf,
                             style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
            }

            inspector_.wire_remap_rects.push_back({fx, py, fw, fh, f});
            py += fh;
        }

        // Clamp checkbox
        py += 8;
        float cb_size = 14;
        tr.draw_rect(px, py, cb_size, cb_size,
                     style_.inspector_bg[0] * 0.7f, style_.inspector_bg[1] * 0.7f, style_.inspector_bg[2] * 0.7f, 0.8f);
        tr.draw_rect(px, py, cb_size, 1, style_.separator[0], style_.separator[1], style_.separator[2], 0.5f);
        tr.draw_rect(px, py + cb_size - 1, cb_size, 1, style_.separator[0], style_.separator[1], style_.separator[2], 0.5f);
        if (c.clamp) {
            tr.draw_rect(px + 3, py + 3, cb_size - 6, cb_size - 6,
                         style_.accent[0], style_.accent[1], style_.accent[2], 0.9f);
        }
        inspector_.wire_clamp_rects.push_back({px, py, cb_size, cb_size});
        tr.draw_text(px + cb_size + 6, py + 1, T("clamp", "Clamp"),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.85f);
        py += cb_size + 8;

        // Curve dropdown
        tr.draw_text(px, py + 2, T("curve", "Curve"),
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2], 0.85f);
        float cx = px + 80;
        float cw = kInspectorW - kInspPadX * 2 - 80;
        float ch = 18;
        tr.draw_rect(cx, py, cw, ch,
                     style_.slider_track[0], style_.slider_track[1], style_.slider_track[2]);
        auto curve_enum = static_cast<RemapCurve>(c.curve);
        tr.draw_text(cx + 6, py + 2, remap_curve_label(curve_enum),
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2]);
        tr.draw_text(cx + cw - 16, py + 2, "\xE2\x96\xBE",
                     style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        inspector_.wire_curve_rects.push_back({cx, py, cw, ch});
        } // !c.dropped

        inspector_.insp_content_h = 0;
        return;
    }

    if (selected_node_ids_.empty()) {
        // A selected session clip takes the panel when no node/wire is selected.
        if (has_clip_selection()) { draw_clip_inspector(tr, w, h); return; }
        return;
    }
    // A node is selected → it owns the inspector; drop any clip selection.
    selected_clip_track_.clear();
    selected_clip_id_.clear();

    // Inspector background + separator (drawn outside clip rect)
    float insp_x = inspector_x();
    tr.draw_rect(insp_x, 0, kInspectorW, static_cast<float>(h), style_.inspector_bg[0], style_.inspector_bg[1], style_.inspector_bg[2], 0.95f);
    tr.draw_rect(insp_x, 0, 2, static_cast<float>(h), style_.separator[0], style_.separator[1], style_.separator[2]);

    // Multi-selection panel
    if (selected_node_ids_.size() > 1) {
        if (selected_node_ids_.size() == 2) {
            // 2-node selection: show connection matrix between the pair
            auto it = selected_node_ids_.begin();
            const std::string& id_a = *it++;
            const std::string& id_b = *it;
            const auto* node_a = snap_.find_node(id_a);
            const auto* node_b = snap_.find_node(id_b);
            if (!node_a || !node_b || !node_a->op_info || !node_b->op_info) {
                float px = insp_x + kInspPadX;
                float py = kPerfBarH + 8;
                tr.draw_text(px, py, T("node_not_found", "Node not found"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
                inspector_.insp_content_h = 0;
                return;
            }

            // Reset scroll when the selected pair changes
            std::string scroll_key = id_a + "+" + id_b;
            if (scroll_key != inspector_.insp_scroll_node_id) {
                inspector_.insp_scroll_y = 0.0f;
                inspector_.insp_scroll_node_id = scroll_key;
            }

            float viewport_top = kPerfBarH;
            float viewport_h = static_cast<float>(h) - viewport_top;
            float max_scroll = std::max(0.0f, inspector_.insp_content_h - viewport_h);
            inspector_.insp_scroll_y = std::max(0.0f, std::min(inspector_.insp_scroll_y, max_scroll));

            tr.push_clip_rect(insp_x, viewport_top, kInspectorW, viewport_h);

            float px = insp_x + kInspPadX;
            float py = viewport_top + 8 - inspector_.insp_scroll_y;

            // Header: both node names with env colors
            const float* clr_a = node_accent_color(node_a->is_gpu, node_a->active_cadence);
            const float* clr_b = node_accent_color(node_b->is_gpu, node_b->active_cadence);
            const std::string& label_a = node_a->op_info->display_name.empty()
                ? node_a->op_info->name : node_a->op_info->display_name;
            const std::string& label_b = node_b->op_info->display_name.empty()
                ? node_b->op_info->name : node_b->op_info->display_name;
            tr.draw_text(px, py, label_a.c_str(), clr_a[0], clr_a[1], clr_a[2]);
            float name_w = tr.text_width(label_a.c_str());
            tr.draw_text(px + name_w + 4, py, " + ", style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
            float plus_w = tr.text_width(" + ");
            tr.draw_text(px + name_w + 4 + plus_w, py, label_b.c_str(), clr_b[0], clr_b[1], clr_b[2]);
            py += kLineH;

            tr.draw_text(px, py, T("delete_to_remove", "Delete / Backspace to remove"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
            py += kLineH + 8;

            tr.draw_rect(px, py, kInspContentW, 1, style_.separator[0], style_.separator[1], style_.separator[2]);
            py += 8;

            draw_patch_panel(tr, *node_a, *node_b, px, py);

            tr.pop_clip_rect();

            inspector_.insp_content_h = (py + inspector_.insp_scroll_y) - viewport_top;
            draw_inspector_scrollbar(tr);
        } else {
            // 3+ nodes: summary
            float px = insp_x + kInspPadX;
            float py = kPerfBarH + 8;
            std::string label = std::to_string(selected_node_ids_.size()) + " nodes selected";
            tr.draw_text(px, py, label.c_str(), 1.0f, 1.0f, 1.0f);
            py += kLineH + 4;
            tr.draw_text(px, py, T("delete_to_remove", "Delete / Backspace to remove"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);

            inspector_.insp_content_h = 0;
        }
        return;
    }

    const auto& sel_id = single_selected_id();

    // Reset scroll when selection changes
    if (sel_id != inspector_.insp_scroll_node_id) {
        inspector_.insp_scroll_y = 0.0f;
        inspector_.insp_scroll_node_id = sel_id;
    }

    // Find the selected node in snapshot
    const auto* sel_node = snap_.find_node(sel_id);
    if (!sel_node || !sel_node->op_info) {
        tr.draw_text(insp_x + kInspPadX, 20, T("node_not_found", "Node not found"), style_.dim_text[0], style_.dim_text[1], style_.dim_text[2]);
        return;
    }

    float viewport_top = kPerfBarH;
    float viewport_h = static_cast<float>(h) - viewport_top;

    // Clamp scroll before drawing
    float max_scroll = std::max(0.0f, inspector_.insp_content_h - viewport_h);
    inspector_.insp_scroll_y = std::max(0.0f, std::min(inspector_.insp_scroll_y, max_scroll));

    // Clip rect for scrollable content
    tr.push_clip_rect(insp_x, viewport_top, kInspectorW, viewport_h);

    float px = insp_x + kInspPadX;
    float py = viewport_top + 8 - inspector_.insp_scroll_y;

    draw_inspector_header(tr, *sel_node, px, py);

    // Error banner for errored nodes (includes compile errors where errored=false).
    // Safe-mode-suppressed nodes (disabled or quarantined) get an amber prefix
    // rather than red "Error:" — they were intentionally suppressed, not broken.
    if (!sel_node->error_message.empty()) {
        const bool quarantined = sel_node->quarantined;
        const bool disabled    = sel_node->disabled_by_safe_mode;
        const bool suppressed  = quarantined || disabled;
        const char* prefix =
              quarantined ? T("quarantined_label", "Quarantined:")
            : disabled    ? T("disabled_label",    "Disabled:")
            :               T("error_label",       "Error:");
        const auto& col = suppressed ? kDisabledAccent : kErrorAccent;
        const std::string label = std::string(prefix) + " " + sel_node->error_message;
        tr.draw_text(px, py, label.c_str(), col[0], col[1], col[2]);
        py += kLineH + 4;
        if (suppressed) {
            const char* hint = quarantined
                ? T("quarantined_hint",
                    "This operator has crashed repeatedly. "
                    "Launch without --safe-mode only after fixing the underlying issue.")
                : T("disabled_hint",
                    "Restart without --safe-mode to re-enable this operator.");
            tr.draw_text(px, py, hint, col[0], col[1], col[2], 0.7f);
            py += kLineH + 4;
        }
    }

    bool has_visible_standard_params = false;
    if (sel_node->op_info) {
        for (const auto& pd : sel_node->op_info->params) {
            if (pd.display_hint != VIVID_DISPLAY_HIDDEN &&
                pd.display_hint != VIVID_DISPLAY_EDITOR &&
                pd.display_hint != VIVID_DISPLAY_TRANSIENT) {
                has_visible_standard_params = true;
                break;
            }
        }
    }
    bool has_custom_inspector = sel_node->op_info && sel_node->op_info->has_custom_inspector;

    if (sel_node->op_info && sel_node->op_info->has_custom_inspector) {
        if (sel_node->op_info->inspector_mode == VIVID_INSPECTOR_STANDARD && has_visible_standard_params) {
            draw_section_separator(tr, px, py, kInspContentW, T("controls", "Controls"));
            draw_inspector_params(tr, *sel_node, px, py);
        }
        if (has_custom_inspector) {
            draw_section_separator(tr, px, py, kInspContentW, T("custom", "Custom"));
        }
        draw_custom_inspector(tr, *sel_node, px, py);
    } else {
        if (has_visible_standard_params) {
            draw_section_separator(tr, px, py, kInspContentW, T("controls", "Controls"));
            draw_inspector_params(tr, *sel_node, px, py);
        }
    }
    if (sel_node->is_module_instance) {
        draw_inspector_modulation(tr, *sel_node, px, py);
    }
    // --- Performance section (module instances with performance-tagged params) ---
    if (sel_node->is_module_instance) {
        draw_inspector_performance(tr, *sel_node, px, py);
    }
    // --- Voice lifecycle section (poly synths with voice breakouts) ---
    draw_inspector_voices(tr, *sel_node, px, py);
    // --- Technical section ---
    {
        bool has_resolution = sel_node->is_gpu && sel_node->gpu_tex_width > 0;
        bool has_state_presets = sel_node->param_indices.count("states") > 0;
        bool has_outputs = !sel_node->output_port_indices.empty();
        if (has_resolution || has_state_presets || has_outputs)
            draw_section_separator(tr, px, py, kInspContentW, T("technical", "Technical"));
        draw_inspector_resolution(tr, *sel_node, px, py);
        draw_inspector_state_presets(tr, *sel_node, px, py);
        draw_inspector_outputs(tr, *sel_node, px, py);
    }

    // Inspector widget hover highlights
    if (inspector_.hovered_slider_idx >= 0 && inspector_.hovered_slider_idx < static_cast<int>(inspector_.slider_rects.size())) {
        const auto& r = inspector_.slider_rects[inspector_.hovered_slider_idx];
        tr.draw_rect(r.x, r.y, r.w, r.h,
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], kWidgetHoverAlpha);
    }
    if (inspector_.hovered_bool_idx >= 0 && inspector_.hovered_bool_idx < static_cast<int>(inspector_.bool_rects.size())) {
        const auto& r = inspector_.bool_rects[inspector_.hovered_bool_idx];
        tr.draw_rect(r.x - 2, r.y - 2, r.w + 4, r.h + 4,
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], kWidgetHoverAlpha);
    }
    if (inspector_.hovered_dropdown_idx >= 0 && inspector_.hovered_dropdown_idx < static_cast<int>(inspector_.dropdown_rects.size())) {
        const auto& r = inspector_.dropdown_rects[inspector_.hovered_dropdown_idx];
        tr.draw_rect(r.x, r.y, r.w, r.h,
                     style_.bright_text[0], style_.bright_text[1], style_.bright_text[2], kWidgetHoverAlpha);
    }

    tr.pop_clip_rect();

    // Compute content height from final py (relative to viewport top)
    inspector_.insp_content_h = (py + inspector_.insp_scroll_y) - viewport_top;

    // Draw scrollbar outside clip rect
    draw_inspector_scrollbar(tr);
}


} // namespace vivid::ui
