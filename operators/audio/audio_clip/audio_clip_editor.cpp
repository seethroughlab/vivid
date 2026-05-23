#include "audio_clip.h"
#include "audio_clip_editor_shared.h"
#include "operator_api/draw_ui_helpers.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace ace {  // audio_clip_editor constants

// Param indices — must match collect_params order in audio_clip.cpp.
// String params (file, warp_points) occupy slots but have float value 0.
enum PI {
    kFile       = 0,
    kAutoPlay   = 1,
    kLoop       = 2,
    kLoopStart  = 3,
    kLoopEnd    = 4,
    kVolume     = 5,
    kSpeed      = 6,
    kPitch      = 7,
    kFileBpm    = 8,
    kRateMode   = 9,
    kStretch    = 10,
    kClipStart  = 11,
    kClipEnd    = 12,
    kWarpPoints = 13,
    kWarpEnabled = 14,
    kWarpMode = 15,
    kTransientPoints = 16,
    kShowTransients = 17,
    kTransientSensitivity = 18,
    kLaunchMode = 19,
    kLaunchQuantize = 20,
    kReverse = 21,
    kFadeInMs = 22,
    kFadeOutMs = 23,
    kLoopCrossfadeMs = 24,
    kSliceMode = 25,
    kSlicePoints = 26,
    kSliceIndex = 27,
};

// Output port indices
enum OI {
    kAudioOut   = 0,
    kPositionOut = 1,
    kDoneOut    = 2,
    kLaunchPendingOut = 3,
    kSliceCountOut = 4,
    kActiveSliceOut = 5,
};

constexpr float kTopH       = 26.0f;   // info row height
constexpr float kRuleH      = 20.0f;   // ruler height
constexpr float kSideW      = 215.0f;  // side panel width
constexpr float kHandleGrab = 7.0f;    // grab radius in px for vertical handles
constexpr float kBraceH     = 12.0f;   // height of the loop brace hit area at top of waveform
constexpr float kPad        = 6.0f;    // side panel internal padding
constexpr float kRowH       = 18.0f;   // side panel row height
constexpr float kGap        = 3.0f;    // side panel row gap

static const char* kRateModeLabels[] = {"free", "ext", "sync"};
static const char* kWarpModeLabels[] = {"complex", "beats", "repitch"};
static const char* kLaunchModeLabels[] = {"trigger", "gate", "toggle", "repeat"};
static const char* kLaunchQuantizeLabels[] = {"instant", "beat", "bar", "4bar"};
static const char* kSliceModeLabels[] = {"off", "trans", "manual", "16"};

}  // namespace ace

VividEditorMetadata AudioClip::editor_metadata() {
    VividEditorMetadata m{};
    m.default_width  = 880;
    m.default_height = 460;
    m.min_width      = 640;
    m.min_height     = 320;
    m.title_suffix   = "Waveform";
    return m;
}

void AudioClip::draw_editor(VividEditorContext* ctx) {
    if (!ctx) return;
    namespace ed  = ace;
    namespace aed = audio_clip_ed;
    using namespace vivid::ui;
    namespace ek = vivid::editor_keys;

    auto& d    = ctx->draw;
    void* o    = d.opaque;
    const auto& th    = ctx->theme;
    const auto& mouse = ctx->mouse;

    // ---- Read param values ----
    const uint32_t pc = ctx->param_count;
    auto pv = [&](int i) -> float {
        return (i >= 0 && static_cast<uint32_t>(i) < pc) ? ctx->param_values[i] : 0.0f;
    };
    const float p_cs      = pv(ed::kClipStart);
    const float p_ce      = pv(ed::kClipEnd);
    const float p_ls      = pv(ed::kLoopStart);
    const float p_le      = pv(ed::kLoopEnd);
    const bool  p_loop    = pv(ed::kLoop) > 0.5f;
    // Effective loop bounds clamped to clip region (mirrors audio engine clamp)
    const auto  eff_loop  = aed::effective_loop_bounds(p_cs, p_ce, p_ls, p_le);
    const float eff_ls    = eff_loop.start;
    const float eff_le    = eff_loop.end;
    const bool  p_stretch = pv(ed::kStretch) > 0.5f;
    const int   p_rmode   = static_cast<int>(pv(ed::kRateMode) + 0.5f);
    const bool  p_warp    = pv(ed::kWarpEnabled) > 0.5f;
    const int   p_wmode   = static_cast<int>(pv(ed::kWarpMode) + 0.5f);
    const bool  p_show_trans = pv(ed::kShowTransients) > 0.5f;
    const float p_trans_sens = pv(ed::kTransientSensitivity);
    const int   p_launch_mode = static_cast<int>(pv(ed::kLaunchMode) + 0.5f);
    const int   p_launch_quant = static_cast<int>(pv(ed::kLaunchQuantize) + 0.5f);
    const bool  p_reverse = pv(ed::kReverse) > 0.5f;
    const float p_fade_in_ms = pv(ed::kFadeInMs);
    const float p_fade_out_ms = pv(ed::kFadeOutMs);
    const float p_xfade_ms = pv(ed::kLoopCrossfadeMs);
    const int   p_slice_mode = static_cast<int>(pv(ed::kSliceMode) + 0.5f);
    const float p_speed   = pv(ed::kSpeed);
    const float p_pitch   = pv(ed::kPitch);
    const float p_fbpm    = pv(ed::kFileBpm);
    const float position  = (ctx->output_count > static_cast<uint32_t>(ed::kPositionOut))
                            ? ctx->output_values[ed::kPositionOut] : 0.0f;
    const bool launch_pending = (ctx->output_count > static_cast<uint32_t>(ed::kLaunchPendingOut))
                            ? ctx->output_values[ed::kLaunchPendingOut] > 0.5f : false;
    const int  active_slice  = (ctx->output_count > static_cast<uint32_t>(ed::kActiveSliceOut))
                            ? static_cast<int>(ctx->output_values[ed::kActiveSliceOut] + 0.5f) : -1;

    // Filename from first string param
    const char* filepath = (ctx->string_param_count > 0 && ctx->string_param_values
                            && ctx->string_param_values[0])
                           ? ctx->string_param_values[0] : "";
    const char* fname = filepath;
    for (const char* p = filepath; *p; ++p) {
        if (*p == '/' || *p == '\\') fname = p + 1;
    }

    // ---- Layout ----
    const Rect full{0.0f, 0.0f, ctx->surface_width, ctx->surface_height};
    const float safe_w = std::max(1.0f, full.w);
    const float side_w = std::min(ed::kSideW, safe_w * 0.38f);
    auto [content, side] = ui_split_h(full, 1.0f - side_w / safe_w, 4.0f);

    const Rect top_row {content.x, content.y, content.w, ed::kTopH};
    const float ruler_y = content.y + content.h - ed::kRuleH;
    const Rect ruler_row{content.x, ruler_y, content.w, ed::kRuleH};
    const float wave_y  = content.y + ed::kTopH + 4.0f;
    const float wave_h  = std::max(1.0f, ruler_y - wave_y - 4.0f);
    const Rect wave_area{content.x, wave_y, content.w, wave_h};

    // ---- Viewport init + update ----
    if (!timeline_vp_init_) {
        timeline_vp_.content_min = 0.0;
        timeline_vp_.content_max = 1.0;
        timeline_vp_.view_start  = 0.0;
        timeline_vp_.view_size   = 1.0;
        timeline_vp_init_        = true;
    }
    timeline_vp_.screen_min  = wave_area.x;
    timeline_vp_.screen_size = std::max(1.0f, wave_area.w);

    // Normalized → screen x
    auto ns2x = [&](float n) -> float {
        return timeline_vp_.world_to_screen(static_cast<double>(n));
    };

    const float cs_sx = ns2x(p_cs);
    const float ce_sx = ns2x(p_ce);
    const float ls_sx = ns2x(eff_ls);
    const float le_sx = ns2x(eff_le);

    // ---- Handle scroll events (zoom / pan) ----
    const bool mouse_in_wave = wave_area.contains(mouse.x, mouse.y);
    for (uint32_t ei = 0; ei < ctx->event_count; ++ei) {
        const auto& e = ctx->events[ei];
        if (e.type != VIVID_EDITOR_EVENT_MOUSE_SCROLL) continue;
        if (!mouse_in_wave) continue;
        const bool zoom = (e.modifiers & (ek::kModControl | ek::kModSuper)) != 0;
        if (zoom) {
            const double factor = (e.scroll_dy > 0.0f) ? 0.8 : 1.25;
            zoom_viewport_at(&timeline_vp_, mouse.x, factor, 0.001);
        } else {
            pan_viewport(&timeline_vp_, -static_cast<double>(e.scroll_dy) * 0.04);
        }
        clamp_viewport(&timeline_vp_);
    }

    // ---- Begin drag on click ----
    auto set_p = [&](const char* name, float v) {
        if (ctx->commands.set_param)
            ctx->commands.set_param(ctx->commands.opaque, name, v);
    };

    auto set_sp = [&](const char* name, const char* v) {
        if (ctx->commands.set_string_param)
            ctx->commands.set_string_param(ctx->commands.opaque, name, v);
    };

    // ---- Pre-compute waveform overlay geometry (needed by both hit-detection and draw) ----
    const auto* wf = display_waveform_;
    const float ppsn = (wf && wf->frame_count > 0)
        ? 1.0f / static_cast<float>(wf->frame_count) : 0.0f;
    float fade_in_n  = 0.0f, fade_out_n = 0.0f;
    float fade_in_sx = -1e6f, fade_out_sx = -1e6f;
    if (ppsn > 0.0f) {
        fade_in_n  = std::min(p_ce - p_cs,
            p_fade_in_ms  * static_cast<float>(wf->file_sample_rate) / 1000.0f * ppsn);
        fade_out_n = std::min(p_ce - p_cs,
            p_fade_out_ms * static_cast<float>(wf->file_sample_rate) / 1000.0f * ppsn);
        if (fade_in_n  > 0.0f) fade_in_sx  = ns2x(p_cs + fade_in_n);
        if (fade_out_n > 0.0f) fade_out_sx = ns2x(p_ce - fade_out_n);
    }

    if (mouse.left_clicked && mouse_in_wave) {
        // Loop brace body: top strip of wave area, between loop handles
        const bool in_brace_h = (mouse.x >= ls_sx && mouse.x <= le_sx);
        const bool in_brace_v = (mouse.y >= wave_area.y &&
                                 mouse.y < wave_area.y + ed::kBraceH + 4.0f);
        const bool in_brace   = in_brace_h && in_brace_v && p_loop;

        // Priority: clip handles (bold) → loop handles → loop body
        if (aed::hit_handle_x(mouse.x, cs_sx, ed::kHandleGrab)) {
            ui_drag_handle_begin(*ctx, &clip_start_drag_);
            clip_start_orig_ = p_cs;
        } else if (aed::hit_handle_x(mouse.x, ce_sx, ed::kHandleGrab)) {
            ui_drag_handle_begin(*ctx, &clip_end_drag_);
            clip_end_orig_ = p_ce;
        } else if (p_loop && aed::hit_handle_x(mouse.x, ls_sx, ed::kHandleGrab)) {
            ui_drag_handle_begin(*ctx, &loop_start_drag_);
            loop_start_orig_ = eff_ls;
        } else if (p_loop && aed::hit_handle_x(mouse.x, le_sx, ed::kHandleGrab)) {
            ui_drag_handle_begin(*ctx, &loop_end_drag_);
            loop_end_orig_ = eff_le;
        } else if (in_brace) {
            ui_drag_handle_begin(*ctx, &loop_body_drag_);
            loop_body_ls_orig_ = eff_ls;
            loop_body_le_orig_ = eff_le;
        } else if (fade_in_n > 0.0f && aed::hit_handle_x(mouse.x, fade_in_sx, ed::kHandleGrab)) {
            ui_drag_handle_begin(*ctx, &fade_in_drag_);
            fade_in_orig_ms_ = p_fade_in_ms;
        } else if (fade_out_n > 0.0f && aed::hit_handle_x(mouse.x, fade_out_sx, ed::kHandleGrab)) {
            ui_drag_handle_begin(*ctx, &fade_out_drag_);
            fade_out_orig_ms_ = p_fade_out_ms;
        } else if (p_warp && wf) {
            // Check for warp marker hit; Shift+click adds a new marker
            int hit_warp = -1;
            for (int wi = 0; wi < static_cast<int>(wf->warp_markers.size()); ++wi) {
                const float wx = ns2x(static_cast<float>(wf->warp_markers[wi].source_sample) * ppsn);
                if (aed::hit_handle_x(mouse.x, wx, ed::kHandleGrab + 2.0f)) {
                    hit_warp = wi;
                    break;
                }
            }
            if (hit_warp >= 0) {
                warp_drag_idx_         = hit_warp;
                warp_drag_start_x_     = mouse.x;
                warp_drag_orig_sample_ = wf->warp_markers[hit_warp].source_sample;
                warp_drag_cur_sample_  = warp_drag_orig_sample_;
            } else if (mouse.shift_down) {
                // Add new warp marker at this sample position
                const float n = timeline_vp_.screen_to_world(mouse.x);
                const uint32_t sample = static_cast<uint32_t>(
                    std::max(0.0f, std::min(n, 1.0f)) * static_cast<float>(wf->frame_count));
                const double beat = (wf->file_sample_rate > 0 && p_fbpm > 0.0f)
                    ? static_cast<double>(sample) * p_fbpm / (wf->file_sample_rate * 60.0) : 0.0;
                auto pts = std::vector<audio_clip_ed::WarpPoint>(
                    wf->warp_markers.begin(), wf->warp_markers.end());
                pts.push_back({sample, beat});
                const std::string s = audio_clip_ed::serialize_warp_points(pts);
                set_sp("warp_points", s.c_str());
            }
        } else if (p_slice_mode != 0 && wf && !wf->slice_regions.empty()) {
            // Click inside a slice region to select it
            const float n = timeline_vp_.screen_to_world(mouse.x);
            const uint32_t sample = static_cast<uint32_t>(
                std::max(0.0f, std::min(n, 1.0f)) * static_cast<float>(wf->frame_count));
            for (int si = 0; si < static_cast<int>(wf->slice_regions.size()); ++si) {
                if (sample >= wf->slice_regions[si].start && sample < wf->slice_regions[si].end) {
                    set_p("slice_index", static_cast<float>(si));
                    break;
                }
            }
        }
    }

    // ---- Right-click handling (warp delete, transient delete) ----
    if (mouse.right_clicked && mouse_in_wave && wf) {
        if (p_warp) {
            for (int wi = 0; wi < static_cast<int>(wf->warp_markers.size()); ++wi) {
                const float wx = ns2x(static_cast<float>(wf->warp_markers[wi].source_sample) * ppsn);
                if (aed::hit_handle_x(mouse.x, wx, ed::kHandleGrab + 4.0f)) {
                    if (wi == 0 || wi + 1 == static_cast<int>(wf->warp_markers.size())) break;
                    auto pts = std::vector<audio_clip_ed::WarpPoint>(
                        wf->warp_markers.begin(), wf->warp_markers.end());
                    pts.erase(pts.begin() + wi);
                    const std::string s = audio_clip_ed::serialize_warp_points(pts);
                    set_sp("warp_points", s.c_str());
                    break;
                }
            }
        }
        if (p_show_trans) {
            for (int ti = 0; ti < static_cast<int>(wf->transient_markers.size()); ++ti) {
                const float tx = ns2x(static_cast<float>(wf->transient_markers[ti].source_sample) * ppsn);
                if (aed::hit_handle_x(mouse.x, tx, ed::kHandleGrab)) {
                    auto pts = std::vector<audio_clip_ed::TransientPoint>(
                        wf->transient_markers.begin(), wf->transient_markers.end());
                    pts.erase(pts.begin() + ti);
                    const std::string s = audio_clip_ed::serialize_transient_points(pts);
                    set_sp("transient_points", s.c_str());
                    break;
                }
            }
        }
    }

    // ---- Update drag handles + emit params ----
    // Convert screen-pixel deltas to normalized [0,1] units at the current zoom level.
    const float ww = std::max(1.0f, wave_area.w);
    auto delta_norm = [&](float dx) {
        return aed::pixel_delta_to_norm(dx, static_cast<float>(timeline_vp_.view_size), ww);
    };
    {
        auto r = ui_drag_handle_update(*ctx, &clip_start_drag_);
        if (r.dragging) {
            set_p("clip_start", aed::drag_clip_start(clip_start_orig_, delta_norm(r.dx), p_ce));
        }
    }
    {
        auto r = ui_drag_handle_update(*ctx, &clip_end_drag_);
        if (r.dragging) {
            set_p("clip_end", aed::drag_clip_end(clip_end_orig_, delta_norm(r.dx), p_cs));
        }
    }
    {
        auto r = ui_drag_handle_update(*ctx, &loop_start_drag_);
        if (r.dragging) {
            set_p("loop_start",
                  aed::drag_loop_start(loop_start_orig_, delta_norm(r.dx), p_cs, eff_le));
        }
    }
    {
        auto r = ui_drag_handle_update(*ctx, &loop_end_drag_);
        if (r.dragging) {
            set_p("loop_end",
                  aed::drag_loop_end(loop_end_orig_, delta_norm(r.dx), eff_ls, p_ce));
        }
    }
    {
        auto r = ui_drag_handle_update(*ctx, &loop_body_drag_);
        if (r.dragging) {
            const auto moved = aed::drag_loop_body(loop_body_ls_orig_, loop_body_le_orig_,
                                                   delta_norm(r.dx), p_cs, p_ce);
            set_p("loop_start", moved.start);
            set_p("loop_end",   moved.end);
        }
    }
    {
        auto r = ui_drag_handle_update(*ctx, &fade_in_drag_);
        if (r.dragging && wf && wf->file_sample_rate > 0) {
            const float clip_ms = static_cast<float>(wf->duration_sec * 1000.0)
                                  * std::max(0.0001f, p_ce - p_cs);
            const float delta_ms = delta_norm(r.dx) * clip_ms;
            set_p("fade_in_ms",
                  std::max(0.0f, std::min(fade_in_orig_ms_ + delta_ms, 500.0f)));
        }
    }
    {
        auto r = ui_drag_handle_update(*ctx, &fade_out_drag_);
        if (r.dragging && wf && wf->file_sample_rate > 0) {
            const float clip_ms = static_cast<float>(wf->duration_sec * 1000.0)
                                  * std::max(0.0001f, p_ce - p_cs);
            const float delta_ms = delta_norm(r.dx) * clip_ms;
            // Fade-out handle drags left to increase fade: negate delta
            set_p("fade_out_ms",
                  std::max(0.0f, std::min(fade_out_orig_ms_ - delta_ms, 500.0f)));
        }
    }

    // Warp marker drag — moves the source_sample, keeping beat fixed
    if (warp_drag_idx_ >= 0 && wf) {
        if (mouse.left_down) {
            const int idx = warp_drag_idx_;
            const uint32_t min_sample = (idx > 0)
                ? wf->warp_markers[idx - 1].source_sample + 1u : 0u;
            const uint32_t max_sample = (idx + 1 < static_cast<int>(wf->warp_markers.size()))
                ? wf->warp_markers[idx + 1].source_sample - 1u
                : (wf->frame_count > 0 ? wf->frame_count - 1u : 0u);
            const double moved = static_cast<double>(warp_drag_orig_sample_) +
                                 static_cast<double>(delta_norm(mouse.x - warp_drag_start_x_)) *
                                     static_cast<double>(wf->frame_count);
            warp_drag_cur_sample_ = static_cast<uint32_t>(
                std::clamp(std::llround(moved),
                           static_cast<long long>(min_sample),
                           static_cast<long long>(std::max(min_sample, max_sample))));
        } else {
            // Drag ended — write serialized warp points
            auto pts = std::vector<audio_clip_ed::WarpPoint>(
                wf->warp_markers.begin(), wf->warp_markers.end());
            if (warp_drag_idx_ < static_cast<int>(pts.size()))
                pts[warp_drag_idx_].source_sample = warp_drag_cur_sample_;
            const std::string s = audio_clip_ed::serialize_warp_points(pts);
            set_sp("warp_points", s.c_str());
            warp_drag_idx_ = -1;
        }
    }

    // Update hover indices (for cursor + visual feedback)
    warp_hover_idx_      = -1;
    transient_hover_idx_ = -1;
    slice_hover_idx_     = -1;
    if (mouse_in_wave && wf && ppsn > 0.0f) {
        if (p_warp) {
            for (int wi = 0; wi < static_cast<int>(wf->warp_markers.size()); ++wi) {
                const float wx = ns2x(static_cast<float>(wf->warp_markers[wi].source_sample) * ppsn);
                if (aed::hit_handle_x(mouse.x, wx, ed::kHandleGrab + 2.0f)) {
                    warp_hover_idx_ = wi; break;
                }
            }
        }
        if (p_show_trans && warp_hover_idx_ < 0) {
            for (int ti = 0; ti < static_cast<int>(wf->transient_markers.size()); ++ti) {
                const float tx = ns2x(static_cast<float>(wf->transient_markers[ti].source_sample) * ppsn);
                if (aed::hit_handle_x(mouse.x, tx, ed::kHandleGrab)) {
                    transient_hover_idx_ = ti; break;
                }
            }
        }
        if (p_slice_mode != 0 && warp_hover_idx_ < 0 && transient_hover_idx_ < 0) {
            const float n = timeline_vp_.screen_to_world(mouse.x);
            const uint32_t sample = static_cast<uint32_t>(
                std::max(0.0f, std::min(n, 1.0f)) * static_cast<float>(wf->frame_count));
            for (int si = 0; si < static_cast<int>(wf->slice_regions.size()); ++si) {
                if (sample >= wf->slice_regions[si].start && sample < wf->slice_regions[si].end) {
                    slice_hover_idx_ = si; break;
                }
            }
        }
    }

    // ---- Cursor shape ----
    const bool any_dragging =
        clip_start_drag_.dragging || clip_end_drag_.dragging ||
        loop_start_drag_.dragging || loop_end_drag_.dragging ||
        loop_body_drag_.dragging ||
        fade_in_drag_.dragging || fade_out_drag_.dragging ||
        warp_drag_idx_ >= 0;
    const bool in_loop_brace = p_loop && mouse_in_wave &&
        mouse.x >= ls_sx && mouse.x <= le_sx &&
        mouse.y >= wave_area.y && mouse.y < wave_area.y + ed::kBraceH + 4.0f;
    const bool hovering_handle =
        aed::hit_handle_x(mouse.x, cs_sx, ed::kHandleGrab) ||
        aed::hit_handle_x(mouse.x, ce_sx, ed::kHandleGrab) ||
        (p_loop && aed::hit_handle_x(mouse.x, ls_sx, ed::kHandleGrab)) ||
        (p_loop && aed::hit_handle_x(mouse.x, le_sx, ed::kHandleGrab)) ||
        (fade_in_n  > 0.0f && aed::hit_handle_x(mouse.x, fade_in_sx,  ed::kHandleGrab)) ||
        (fade_out_n > 0.0f && aed::hit_handle_x(mouse.x, fade_out_sx, ed::kHandleGrab)) ||
        warp_hover_idx_ >= 0 ||
        in_loop_brace;
    if ((any_dragging || hovering_handle) && mouse_in_wave &&
        ctx->host.set_cursor && ctx->host.opaque) {
        ctx->host.set_cursor(ctx->host.opaque, VIVID_CURSOR_RESIZE_H);
    }

    // ---- Handle drag labels (time position near cursor) ----
    if (wf && wf->duration_sec > 0.0 && d.draw_text) {
        char label[32] = {};
        bool show_label = false;
        float label_x = mouse.x + 5.0f;
        float label_norm = -1.0f;
        if (clip_start_drag_.dragging) {
            label_norm = p_cs; show_label = true;
        } else if (clip_end_drag_.dragging) {
            label_norm = p_ce; show_label = true;
        } else if (loop_start_drag_.dragging) {
            label_norm = eff_ls; show_label = true;
        } else if (loop_end_drag_.dragging) {
            label_norm = eff_le; show_label = true;
        } else if (loop_body_drag_.dragging) {
            label_norm = eff_ls; show_label = true;
        } else if (fade_in_drag_.dragging) {
            std::snprintf(label, sizeof(label), "%.0f ms", p_fade_in_ms);
            show_label = true;
        } else if (fade_out_drag_.dragging) {
            std::snprintf(label, sizeof(label), "%.0f ms", p_fade_out_ms);
            show_label = true;
        }
        if (show_label) {
            if (label_norm >= 0.0f)
                aed::format_time(label, sizeof(label), label_norm * wf->duration_sec);
            const float ly = wave_area.y + 6.0f;
            d.draw_rect(o, label_x - 2.0f, ly - 2.0f, 68.0f, 16.0f,
                        {0.0f, 0.0f, 0.0f, 0.65f});
            d.draw_text(o, label_x, ly, label,
                        {th.bright_text.r, th.bright_text.g, th.bright_text.b, 0.95f}, 0.85f);
        }
    }

    // ==== INFO ROW ====
    {
        char buf[320];
        if (wf && wf->actual_bins > 0) {
            char dur_buf[32];
            aed::format_time(dur_buf, sizeof(dur_buf), wf->duration_sec);
            std::snprintf(buf, sizeof(buf), "%s   %s   %u Hz",
                          fname, dur_buf, wf->file_sample_rate);
            if (p_fbpm > 0.0f) {
                char bpm_buf[32];
                std::snprintf(bpm_buf, sizeof(bpm_buf), "   %.1f BPM", p_fbpm);
                std::strncat(buf, bpm_buf, sizeof(buf) - std::strlen(buf) - 1);
            } else if (detected_bpm_ > 0.0f) {
                char bpm_buf[32];
                std::snprintf(bpm_buf, sizeof(bpm_buf), "   %.0f BPM (auto)", detected_bpm_);
                std::strncat(buf, bpm_buf, sizeof(buf) - std::strlen(buf) - 1);
            } else if (p_rmode == 2) {
                std::strncat(buf, "   — set file BPM to sync",
                             sizeof(buf) - std::strlen(buf) - 1);
            }
        } else {
            std::snprintf(buf, sizeof(buf), "No file loaded — drop a WAV file");
        }
        if (d.draw_text) {
            d.draw_text(o, top_row.x + 6.0f, top_row.y + 5.0f, buf,
                        {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.9f}, 1.0f);
        }
    }

    // ==== WAVEFORM AREA ====
    if (d.draw_rect)
        d.draw_rect(o, wave_area.x, wave_area.y, wave_area.w, wave_area.h,
                    {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 1.0f});

    if (!wf || wf->actual_bins == 0) {
        if (d.draw_text) {
            const float tx = wave_area.x + wave_area.w * 0.5f - 60.0f;
            const float ty = wave_area.y + wave_area.h * 0.5f - 8.0f;
            d.draw_text(o, tx, ty, "Drop a WAV file",
                        {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.45f}, 1.0f);
        }
    } else {
        // ---- Draw waveform bins ----
        const uint32_t nb  = wf->actual_bins;
        const float ch     = wave_area.h * 0.5f;   // height per channel
        const float ch_amp = ch * 0.46f;            // amplitude pixel scale

        for (uint32_t b = 0; b < nb; ++b) {
            const float n0 = static_cast<float>(b)   / static_cast<float>(nb);
            const float n1 = static_cast<float>(b+1) / static_cast<float>(nb);
            const float sx = timeline_vp_.world_to_screen(n0);
            const float ex = timeline_vp_.world_to_screen(n1);
            if (ex <= wave_area.x || sx >= wave_area.x + wave_area.w) continue;
            const float px = std::max(sx, wave_area.x);
            const float pw = std::max(0.5f, std::min(ex, wave_area.x + wave_area.w) - px);

            // Dim bins outside the clip region
            const bool in_clip = (n1 > p_cs) && (n0 < p_ce);
            const VividColor col = in_clip
                ? VividColor{th.accent.r, th.accent.g, th.accent.b, 0.72f}
                : VividColor{th.accent.r * 0.35f, th.accent.g * 0.35f, th.accent.b * 0.35f, 0.45f};

            const auto& bin = wf->bins[b];
            if (d.draw_rect) {
                // Left channel (top half)
                const float lcy = wave_area.y + ch * 0.5f;
                const float lyt = lcy - bin.max_L * ch_amp;
                const float lyb = lcy - bin.min_L * ch_amp;
                d.draw_rect(o, px, lyt, pw, std::max(1.0f, lyb - lyt), col);
                // Right channel (bottom half)
                const float rcy = wave_area.y + ch + ch * 0.5f;
                const float ryt = rcy - bin.max_R * ch_amp;
                const float ryb = rcy - bin.min_R * ch_amp;
                d.draw_rect(o, px, ryt, pw, std::max(1.0f, ryb - ryt), col);
            }
        }

        // Channel divider
        if (d.draw_rect) {
            d.draw_rect(o, wave_area.x, wave_area.y + ch, wave_area.w, 1.0f,
                        {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.12f});
        }

        // ---- Dim overlay outside clip region ----
        if (d.draw_rect) {
            const VividColor dim{0.0f, 0.0f, 0.0f, 0.30f};
            const float csx = std::max(cs_sx, wave_area.x);
            const float cex = std::min(ce_sx, wave_area.x + wave_area.w);
            if (csx > wave_area.x)
                d.draw_rect(o, wave_area.x, wave_area.y,
                            csx - wave_area.x, wave_area.h, dim);
            if (cex < wave_area.x + wave_area.w)
                d.draw_rect(o, cex, wave_area.y,
                            wave_area.x + wave_area.w - cex, wave_area.h, dim);
        }

        // ---- Clip handles (bold vertical lines) ----
        if (d.draw_line) {
            const VividColor cc{th.bright_text.r, th.bright_text.g, th.bright_text.b, 0.95f};
            if (cs_sx >= wave_area.x - 2.0f && cs_sx <= wave_area.x + wave_area.w + 2.0f)
                d.draw_line(o, cs_sx, wave_area.y, cs_sx, wave_area.y + wave_area.h, 2.5f, cc);
            if (ce_sx >= wave_area.x - 2.0f && ce_sx <= wave_area.x + wave_area.w + 2.0f)
                d.draw_line(o, ce_sx, wave_area.y, ce_sx, wave_area.y + wave_area.h, 2.5f, cc);
        }

        // ---- Loop brace and handles ----
        if (p_loop && d.draw_rect && d.draw_line) {
            const VividColor lc{th.accent.r, th.accent.g, th.accent.b, 1.0f};
            const float bx = std::max(ls_sx, wave_area.x);
            const float bw = std::max(0.0f, std::min(le_sx, wave_area.x + wave_area.w) - bx);
            // Top brace bar
            d.draw_rect(o, bx, wave_area.y, bw, 2.5f, lc);
            // Loop start handle
            if (ls_sx >= wave_area.x - 2.0f && ls_sx <= wave_area.x + wave_area.w + 2.0f)
                d.draw_line(o, ls_sx, wave_area.y, ls_sx, wave_area.y + wave_area.h, 1.5f, lc);
            // Loop end handle
            if (le_sx >= wave_area.x - 2.0f && le_sx <= wave_area.x + wave_area.w + 2.0f)
                d.draw_line(o, le_sx, wave_area.y, le_sx, wave_area.y + wave_area.h, 1.5f, lc);
        }

        // ---- Ableton-style overlays: fades, warp, transients, slices ----
        if (d.draw_rect && ppsn > 0.0f) {
            if (fade_in_n > 0.0f) {
                const float fx0 = ns2x(p_cs);
                const float fx1 = ns2x(p_cs + fade_in_n);
                d.draw_rect(o, std::max(fx0, wave_area.x), wave_area.y,
                            std::max(0.0f, std::min(fx1, wave_area.x + wave_area.w) - std::max(fx0, wave_area.x)),
                            wave_area.h, {th.bright_text.r, th.bright_text.g, th.bright_text.b, 0.15f});
                // Always-visible thin edge line; bright on hover/drag
                const bool fi_active = fade_in_drag_.dragging || aed::hit_handle_x(mouse.x, fade_in_sx, ed::kHandleGrab);
                d.draw_rect(o, fade_in_sx - 1.0f, wave_area.y, fi_active ? 3.0f : 1.0f, wave_area.h,
                            {th.bright_text.r, th.bright_text.g, th.bright_text.b, fi_active ? 0.80f : 0.35f});
            }
            if (fade_out_n > 0.0f) {
                const float fx0 = ns2x(p_ce - fade_out_n);
                const float fx1 = ns2x(p_ce);
                d.draw_rect(o, std::max(fx0, wave_area.x), wave_area.y,
                            std::max(0.0f, std::min(fx1, wave_area.x + wave_area.w) - std::max(fx0, wave_area.x)),
                            wave_area.h, {th.bright_text.r, th.bright_text.g, th.bright_text.b, 0.15f});
                const bool fo_active = fade_out_drag_.dragging || aed::hit_handle_x(mouse.x, fade_out_sx, ed::kHandleGrab);
                d.draw_rect(o, fade_out_sx - 1.0f, wave_area.y, fo_active ? 3.0f : 1.0f, wave_area.h,
                            {th.bright_text.r, th.bright_text.g, th.bright_text.b, fo_active ? 0.80f : 0.35f});
            }

            if (p_slice_mode != 0) {
                // Active slice fill (behind boundary lines)
                if (active_slice >= 0 && active_slice < static_cast<int>(wf->slice_regions.size())) {
                    const auto& asl = wf->slice_regions[active_slice];
                    const float ax0 = ns2x(static_cast<float>(asl.start) * ppsn);
                    const float ax1 = ns2x(static_cast<float>(asl.end) * ppsn);
                    d.draw_rect(o, std::max(ax0, wave_area.x), wave_area.y,
                                std::max(0.0f, std::min(ax1, wave_area.x + wave_area.w) - std::max(ax0, wave_area.x)),
                                wave_area.h, {0.95f, 0.78f, 0.25f, 0.13f});
                }
                // Boundary lines
                int si = 0;
                for (const auto& sl : wf->slice_regions) {
                    const float sn = static_cast<float>(sl.start) * ppsn;
                    const float sx = ns2x(sn);
                    const bool hov = (si == slice_hover_idx_);
                    const bool act = (si == active_slice);
                    if (sx >= wave_area.x && sx <= wave_area.x + wave_area.w)
                        d.draw_rect(o, sx, wave_area.y, (hov || act) ? 2.5f : 1.5f, wave_area.h,
                                    {0.95f, 0.78f, 0.25f, (hov || act) ? 0.90f : 0.50f});
                    ++si;
                }
            }
            if (p_show_trans) {
                int ti = 0;
                for (const auto& tr : wf->transient_markers) {
                    const float tx = ns2x(static_cast<float>(tr.source_sample) * ppsn);
                    const bool hov = (ti == transient_hover_idx_);
                    if (tx >= wave_area.x && tx <= wave_area.x + wave_area.w)
                        d.draw_rect(o, tx, wave_area.y + wave_area.h * 0.08f, hov ? 2.0f : 1.0f,
                                    wave_area.h * 0.84f, {1.0f, 0.85f, 0.30f, hov ? 0.85f : 0.45f});
                    ++ti;
                }
            }
            if (p_warp) {
                int wi = 0;
                for (const auto& wp : wf->warp_markers) {
                    // During drag show live sample position; beat label stays fixed
                    const float draw_sample = static_cast<float>(
                        (wi == warp_drag_idx_) ? warp_drag_cur_sample_ : wp.source_sample);
                    const float wx = ns2x(draw_sample * ppsn);
                    const bool hov = (wi == warp_hover_idx_) || (wi == warp_drag_idx_);
                    if (wx >= wave_area.x && wx <= wave_area.x + wave_area.w) {
                        d.draw_rect(o, wx - (hov ? 2.0f : 1.0f), wave_area.y,
                                    hov ? 4.0f : 2.0f, wave_area.h,
                                    {0.55f, 0.78f, 1.0f, hov ? 0.95f : 0.70f});
                        if (d.draw_text) {
                            char beat_buf[16];
                            std::snprintf(beat_buf, sizeof(beat_buf), "%.1f", wp.beat);
                            d.draw_text(o, wx + 3.0f, wave_area.y + 3.0f, beat_buf,
                                        {0.55f, 0.78f, 1.0f, hov ? 1.0f : 0.65f}, 0.75f);
                        }
                    }
                    ++wi;
                }
            }
            if (p_loop && p_xfade_ms > 0.0f) {
                const float xfade_n = std::min(eff_le - eff_ls,
                    p_xfade_ms * static_cast<float>(wf->file_sample_rate) / 1000.0f * ppsn);
                const float x0 = ns2x(std::max(eff_ls, eff_le - xfade_n));
                const float x1 = ns2x(eff_le);
                d.draw_rect(o, std::max(x0, wave_area.x), wave_area.y,
                            std::max(0.0f, std::min(x1, wave_area.x + wave_area.w) - std::max(x0, wave_area.x)),
                            ed::kBraceH, {th.accent.r, th.accent.g, th.accent.b, 0.22f});
            }
        }

        // ---- Playhead ----
        if (d.draw_line) {
            const float clip_span = std::max(0.0001f, p_ce - p_cs);
            const float abs_pos   = p_cs + position * clip_span;
            const float ph_sx     = ns2x(abs_pos);
            if (ph_sx >= wave_area.x && ph_sx <= wave_area.x + wave_area.w) {
                d.draw_line(o, ph_sx, wave_area.y, ph_sx, wave_area.y + wave_area.h,
                            1.5f, {1.0f, 1.0f, 1.0f, 0.7f});
            }
        }
    }

    // ==== RULER ====
    if (d.draw_rect) {
        d.draw_rect(o, ruler_row.x, ruler_row.y, ruler_row.w, ruler_row.h,
                    {th.dark_bg.r * 0.80f, th.dark_bg.g * 0.80f, th.dark_bg.b * 0.80f, 1.0f});
    }
    if (wf && wf->duration_sec > 0.0 && d.draw_rect && d.draw_text) {
        const double total_dur = wf->duration_sec;
        const double view_dur  = timeline_vp_.view_size * total_dur;

        // Pick a tick interval that gives readable spacing
        double tick_sec = 0.1;
        if (view_dur > 2.0)   tick_sec = 0.5;
        if (view_dur > 10.0)  tick_sec = 1.0;
        if (view_dur > 30.0)  tick_sec = 2.0;
        if (view_dur > 90.0)  tick_sec = 5.0;
        if (view_dur > 200.0) tick_sec = 10.0;

        const double start_t   = timeline_vp_.view_start * total_dur;
        const double end_t     = (timeline_vp_.view_start + timeline_vp_.view_size) * total_dur;
        const double first_tick = std::floor(start_t / tick_sec) * tick_sec;

        for (double t = first_tick; t <= end_t + 1e-9; t += tick_sec) {
            const float norm = static_cast<float>(t / total_dur);
            const float tx   = ns2x(norm);
            if (tx < ruler_row.x || tx > ruler_row.x + ruler_row.w) continue;
            d.draw_rect(o, tx, ruler_row.y, 1.0f, ruler_row.h,
                        {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.35f});
            char tbuf[16];
            if (tick_sec >= 1.0)
                std::snprintf(tbuf, sizeof(tbuf), "%.0fs", t);
            else
                std::snprintf(tbuf, sizeof(tbuf), "%.1fs", t);
            d.draw_text(o, tx + 2.0f, ruler_row.y + 3.0f, tbuf,
                        {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.65f}, 0.8f);
        }
    }

    // ==== SIDE PANEL ====
    if (d.draw_rect)
        d.draw_rect(o, side.x, side.y, side.w, side.h,
                    {th.dark_bg.r * 0.88f, th.dark_bg.g * 0.88f, th.dark_bg.b * 0.88f, 1.0f});

    {
        auto lc = ui_layout(side, ed::kPad, ed::kGap);

        auto row = [&]() { return ui_row(lc, ed::kRowH); };
        auto label_row = [&](const char* title) {
            Rect r = row();
            if (d.draw_text)
                d.draw_text(o, r.x, r.y + 2.0f, title,
                            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.55f}, 0.82f);
        };

        // ---- CLIP REGION ----
        label_row("CLIP");
        {
            auto r = ui_slider_h(*ctx, row(), "start",
                                 p_cs, 0.0f, 1.0f, &side_sliders_[0]);
                if (r.changed) set_p("clip_start", aed::drag_clip_start(r.value, 0.0f, p_ce));
        }
        {
            auto r = ui_slider_h(*ctx, row(), "end",
                                 p_ce, 0.0f, 1.0f, &side_sliders_[1]);
                if (r.changed) set_p("clip_end", aed::drag_clip_end(r.value, 0.0f, p_cs));
        }

        // ---- LOOP ----
        // In sync mode the clip region is always the loop; hide loop controls.
        if (p_rmode != 2) {
            label_row("LOOP");
            {
                Rect r = row();
                auto tr = ui_toggle(*ctx, r, "loop", p_loop);
                if (tr.clicked) set_p("loop", tr.value ? 1.0f : 0.0f);
            }
            {
                auto r = ui_slider_h(*ctx, row(), "start",
                                     eff_ls, 0.0f, 1.0f, &side_sliders_[2]);
                if (r.changed) set_p("loop_start",
                                     aed::drag_loop_start(r.value, 0.0f, p_cs, eff_le));
            }
            {
                auto r = ui_slider_h(*ctx, row(), "end",
                                     eff_le, 0.0f, 1.0f, &side_sliders_[3]);
                if (r.changed) set_p("loop_end",
                                     aed::drag_loop_end(r.value, 0.0f, eff_ls, p_ce));
            }
        }

        // ---- PLAYBACK ----
        label_row("PLAYBACK");
        {
            // rate_mode radio
            Rect r = row();
            auto rr = ui_radio(*ctx, r, ed::kRateModeLabels, 3, p_rmode);
            if (rr.clicked) set_p("rate_mode", static_cast<float>(rr.value));
        }
        {
            Rect r = row();
            auto tr = ui_toggle(*ctx, r, "stretch", p_stretch);
            if (tr.clicked) set_p("stretch", tr.value ? 1.0f : 0.0f);
        }
        if (p_rmode != 2) {
            auto r = ui_slider_h(*ctx, row(), "speed",
                                 p_speed, 0.1f, 8.0f, &side_sliders_[4]);
            if (r.changed) set_p("speed", r.value);
        }
        {
            auto r = ui_slider_h(*ctx, row(), "pitch",
                                 p_pitch, -24.0f, 24.0f, &side_sliders_[5]);
            if (r.changed) set_p("pitch", r.value);
        }
        {
            auto r = ui_slider_h(*ctx, row(), "file bpm",
                                 p_fbpm, 0.0f, 300.0f, &side_sliders_[6]);
            if (r.changed) set_p("file_bpm", r.value);
        }

        label_row("LAUNCH");
        {
            Rect r = row();
            auto rr = ui_radio(*ctx, r, ed::kLaunchModeLabels, 4, p_launch_mode);
            if (rr.clicked) set_p("launch_mode", static_cast<float>(rr.value));
        }
        {
            Rect r = row();
            auto rr = ui_radio(*ctx, r, ed::kLaunchQuantizeLabels, 4, p_launch_quant);
            if (rr.clicked) set_p("launch_quantize", static_cast<float>(rr.value));
        }
        if (launch_pending) label_row("pending");

        label_row("WARP");
        {
            Rect r = row();
            auto tr = ui_toggle(*ctx, r, "warp", p_warp);
            if (tr.clicked) set_p("warp_enabled", tr.value ? 1.0f : 0.0f);
        }
        {
            Rect r = row();
            auto rr = ui_radio(*ctx, r, ed::kWarpModeLabels, 3, p_wmode);
            if (rr.clicked) {
                set_p("warp_mode", static_cast<float>(rr.value));
                if (rr.value == 2) set_p("stretch", 0.0f);
            }
        }
        {
            Rect r = row();
            auto tr = ui_toggle(*ctx, r, "reverse", p_reverse);
            if (tr.clicked) set_p("reverse", tr.value ? 1.0f : 0.0f);
        }

        label_row("FADES");
        {
            auto r = ui_slider_h(*ctx, row(), "in ms",
                                 p_fade_in_ms, 0.0f, 500.0f, &side_sliders_[7]);
            if (r.changed) set_p("fade_in_ms", r.value);
        }
        {
            auto r = ui_slider_h(*ctx, row(), "out ms",
                                 p_fade_out_ms, 0.0f, 500.0f, &side_sliders_[8]);
            if (r.changed) set_p("fade_out_ms", r.value);
        }
        {
            auto r = ui_slider_h(*ctx, row(), "loop x",
                                 p_xfade_ms, 0.0f, 200.0f, &side_sliders_[9]);
            if (r.changed) set_p("loop_crossfade_ms", r.value);
        }

        label_row("SLICES");
        {
            Rect r = row();
            auto rr = ui_radio(*ctx, r, ed::kSliceModeLabels, 4, p_slice_mode);
            if (rr.clicked) set_p("slice_mode", static_cast<float>(rr.value));
        }
        {
            Rect r = row();
            auto tr = ui_toggle(*ctx, r, "transients", p_show_trans);
            if (tr.clicked) set_p("show_transients", tr.value ? 1.0f : 0.0f);
        }
        {
            auto r = ui_slider_h(*ctx, row(), "sensitivity",
                                 p_trans_sens, 0.0f, 1.0f, &side_sliders_[10]);
            if (r.changed) set_p("transient_sensitivity", r.value);
        }
        {
            auto br = ui_button(*ctx, row(), "reset transients");
            if (br.clicked) set_sp("transient_points", "");
        }
    }
}
