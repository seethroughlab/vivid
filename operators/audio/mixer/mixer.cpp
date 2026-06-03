#include "operator_api/operator.h"
#include "operator_api/thumbnail.h"
#include "operator_api/draw_ui_helpers.h"
#include "operator_api/draw_plot_helpers.h"
#include "operator_api/editor_ui.h"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>

static constexpr int kMaxInputs = 16;

/**
 * @brief Stereo summing mixer with per-input gain + pan for up to 16 audio inputs.
 *
 * Sums connected audio inputs into a stereo bus. Each input has its own
 * gain (0..2, default 1) and pan (-1..+1, default 0 = center) using an
 * equal-power pan law. Mono inputs fan to both legs; stereo inputs have
 * their existing image preserved (pan rotates the L/R balance).
 * Disconnected inputs contribute silence. Uses repeat-group ports
 * for grow-on-connect UI behavior.
 *
 * The input ports are named `input_0` .. `input_15`; per-input controls are
 * `gain_<n>` (0..2, 1 = unity) and `pan_<n>` (-1..+1). Connect an audio
 * source with connect(src/output, mixer/input_0), then ramp gain_0.
 *
 * @pitfall The ports are `input_0`..`input_15`, NOT a single `input`.
 *   Connecting to a non-existent port name (e.g. "input") returns ok but
 *   silently drops the wire -- it carries no audio. Run get_graph_errors
 *   after wiring to catch dropped connections.
 * @input input_0 First audio input (mono or stereo); input_1..input_15 follow.
 * @output output Stereo sum of all connected inputs after gain + pan.
 * @see Gain, Composite, StereoPanWidth
 */
struct Mixer : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Mixer";
    static constexpr bool kTimeDependent = false;

    // Static-name params in separate arrays so the codegen can generate
    // the descriptor from the initializer list (nullptr names are opaque
    // to static analysis and produce an empty param table).
    std::array<vivid::Param<float>, 16> gain_ = {{
        {"gain_0",  1.0f, 0.0f, 2.0f}, {"gain_1",  1.0f, 0.0f, 2.0f},
        {"gain_2",  1.0f, 0.0f, 2.0f}, {"gain_3",  1.0f, 0.0f, 2.0f},
        {"gain_4",  1.0f, 0.0f, 2.0f}, {"gain_5",  1.0f, 0.0f, 2.0f},
        {"gain_6",  1.0f, 0.0f, 2.0f}, {"gain_7",  1.0f, 0.0f, 2.0f},
        {"gain_8",  1.0f, 0.0f, 2.0f}, {"gain_9",  1.0f, 0.0f, 2.0f},
        {"gain_10", 1.0f, 0.0f, 2.0f}, {"gain_11", 1.0f, 0.0f, 2.0f},
        {"gain_12", 1.0f, 0.0f, 2.0f}, {"gain_13", 1.0f, 0.0f, 2.0f},
        {"gain_14", 1.0f, 0.0f, 2.0f}, {"gain_15", 1.0f, 0.0f, 2.0f},
    }};
    std::array<vivid::Param<float>, 16> pan_ = {{
        {"pan_0",  0.0f, -1.0f, 1.0f}, {"pan_1",  0.0f, -1.0f, 1.0f},
        {"pan_2",  0.0f, -1.0f, 1.0f}, {"pan_3",  0.0f, -1.0f, 1.0f},
        {"pan_4",  0.0f, -1.0f, 1.0f}, {"pan_5",  0.0f, -1.0f, 1.0f},
        {"pan_6",  0.0f, -1.0f, 1.0f}, {"pan_7",  0.0f, -1.0f, 1.0f},
        {"pan_8",  0.0f, -1.0f, 1.0f}, {"pan_9",  0.0f, -1.0f, 1.0f},
        {"pan_10", 0.0f, -1.0f, 1.0f}, {"pan_11", 0.0f, -1.0f, 1.0f},
        {"pan_12", 0.0f, -1.0f, 1.0f}, {"pan_13", 0.0f, -1.0f, 1.0f},
        {"pan_14", 0.0f, -1.0f, 1.0f}, {"pan_15", 0.0f, -1.0f, 1.0f},
    }};

    // Mute / solo are console-driven, persisted, MCP-accessible, but hidden
    // from the standard inspector (VIVID_DISPLAY_EDITOR). Appended AFTER
    // gain/pan in collect_params so existing gain/pan indices are unchanged.
    std::array<vivid::Param<float>, 16> mute_ = {{
        {"mute_0",  0.0f, 0.0f, 1.0f}, {"mute_1",  0.0f, 0.0f, 1.0f},
        {"mute_2",  0.0f, 0.0f, 1.0f}, {"mute_3",  0.0f, 0.0f, 1.0f},
        {"mute_4",  0.0f, 0.0f, 1.0f}, {"mute_5",  0.0f, 0.0f, 1.0f},
        {"mute_6",  0.0f, 0.0f, 1.0f}, {"mute_7",  0.0f, 0.0f, 1.0f},
        {"mute_8",  0.0f, 0.0f, 1.0f}, {"mute_9",  0.0f, 0.0f, 1.0f},
        {"mute_10", 0.0f, 0.0f, 1.0f}, {"mute_11", 0.0f, 0.0f, 1.0f},
        {"mute_12", 0.0f, 0.0f, 1.0f}, {"mute_13", 0.0f, 0.0f, 1.0f},
        {"mute_14", 0.0f, 0.0f, 1.0f}, {"mute_15", 0.0f, 0.0f, 1.0f},
    }};
    std::array<vivid::Param<float>, 16> solo_ = {{
        {"solo_0",  0.0f, 0.0f, 1.0f}, {"solo_1",  0.0f, 0.0f, 1.0f},
        {"solo_2",  0.0f, 0.0f, 1.0f}, {"solo_3",  0.0f, 0.0f, 1.0f},
        {"solo_4",  0.0f, 0.0f, 1.0f}, {"solo_5",  0.0f, 0.0f, 1.0f},
        {"solo_6",  0.0f, 0.0f, 1.0f}, {"solo_7",  0.0f, 0.0f, 1.0f},
        {"solo_8",  0.0f, 0.0f, 1.0f}, {"solo_9",  0.0f, 0.0f, 1.0f},
        {"solo_10", 0.0f, 0.0f, 1.0f}, {"solo_11", 0.0f, 0.0f, 1.0f},
        {"solo_12", 0.0f, 0.0f, 1.0f}, {"solo_13", 0.0f, 0.0f, 1.0f},
        {"solo_14", 0.0f, 0.0f, 1.0f}, {"solo_15", 0.0f, 0.0f, 1.0f},
    }};

    char gain_desc_[kMaxInputs][80];
    char pan_desc_[kMaxInputs][80];

    // --- Inspector interaction state ---
    int insp_drag_ch_   = -1;  // channel currently being dragged (-1 = none)
    int insp_drag_kind_ =  0;  // 0 = none, 1 = gain, 2 = pan

    // --- Editor (console window) interaction state ---
    vivid::ui::SliderState ed_fader_[kMaxInputs];  // per-channel vertical gain fader
    int ed_pan_drag_ch_ = -1;                      // channel whose pan is being dragged

    // --- Per-input metering (pre-gain peak) ---
    // Written on the audio thread, read on the UI thread for the inspector's
    // level meters. Plain float read of an atomic is fine for a meter display;
    // meter_env_ is the audio-thread-private envelope-follower state.
    std::atomic<float> in_meter_[kMaxInputs];
    float              meter_env_[kMaxInputs] = {};

    // Post-mix output peak, written on the audio thread and read on the UI
    // thread by the live thumbnail (see draw_thumbnail). out_env_ is the
    // audio-thread-private envelope-follower scratch. (Which inputs are
    // connected is supplied to the thumbnail by the host via
    // VividThumbnailContext::connected_input_mask — not derivable here, since
    // disconnected ports still receive zero-filled buffers.)
    std::atomic<float>    out_meter_{0.0f};
    float                 out_env_ = 0.0f;

    Mixer() {
        for (int i = 0; i < kMaxInputs; ++i) {
            in_meter_[i].store(0.0f, std::memory_order_relaxed);

            std::snprintf(gain_desc_[i], sizeof(gain_desc_[i]),
                          "Level multiplier for input %d (0 = silent, 1 = unity, 2 = double)", i);
            std::snprintf(pan_desc_[i], sizeof(pan_desc_[i]),
                          "Pan for input %d (-1 = hard left, 0 = center, +1 = hard right)", i);

            vivid::display_hint(gain_[i], VIVID_DISPLAY_KNOB);
            vivid::layout_row(gain_[i], 2, 0);
            vivid::semantic_tag(gain_[i], "amplitude_linear");
            vivid::semantic_shape(gain_[i], "scalar");
            vivid::description(gain_[i], gain_desc_[i]);
            vivid::repeat_group(gain_[i], "input", static_cast<uint16_t>(i));

            vivid::display_hint(pan_[i], VIVID_DISPLAY_KNOB);
            vivid::layout_row(pan_[i], 2, 1);
            vivid::semantic_tag(pan_[i], "pan");
            vivid::semantic_shape(pan_[i], "scalar");
            vivid::description(pan_[i], pan_desc_[i]);
            vivid::repeat_group(pan_[i], "input", static_cast<uint16_t>(i));

            // Hidden from the standard inspector; driven from the console.
            vivid::display_hint(mute_[i], VIVID_DISPLAY_EDITOR);
            vivid::description(mute_[i], "Mute this input (console)");
            vivid::display_hint(solo_[i], VIVID_DISPLAY_EDITOR);
            vivid::description(solo_[i], "Solo this input (console)");
        }
    }

    // Range-for loops let the codegen expand the array initializer into
    // a static descriptor. Order: all gain params then all pan params,
    // matching the descriptor index → collect_params index alignment
    // required by _vivid_sync_params.
    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        for (auto& p : gain_) out.push_back(&p);
        for (auto& p : pan_) out.push_back(&p);
        for (auto& p : mute_) out.push_back(&p);
        for (auto& p : solo_) out.push_back(&p);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        // Fields: name, type, dir, transport, payload_size, type_name, channels,
        //         default_value, stable_type_id, semantic_tag, semantic_shape,
        //         semantic_intent, description, display_hint, repeat_group, repeat_group_idx
        out.push_back({"input_0",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  0});
        out.push_back({"input_1",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  1});
        out.push_back({"input_2",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  2});
        out.push_back({"input_3",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  3});
        out.push_back({"input_4",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  4});
        out.push_back({"input_5",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  5});
        out.push_back({"input_6",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  6});
        out.push_back({"input_7",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  7});
        out.push_back({"input_8",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  8});
        out.push_back({"input_9",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input",  9});
        out.push_back({"input_10", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 10});
        out.push_back({"input_11", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 11});
        out.push_back({"input_12", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 12});
        out.push_back({"input_13", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 13});
        out.push_back({"input_14", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 14});
        out.push_back({"input_15", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2, 0.0f, nullptr, nullptr, nullptr, nullptr, nullptr, 0, "input", 15});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2});
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        const uint32_t n = ctx->buffer_size;
        float* out_l = ctx->output_buffers[0];
        float* out_r = ctx->output_buffers[0] + n;  // planar stereo

        std::memset(out_l, 0, 2 * n * sizeof(float));

        static constexpr float kPiOver4 = 3.14159265358979f * 0.25f;

        // Solo is exclusive: if ANY input is soloed, only soloed inputs play.
        bool solo_active = false;
        for (int i = 0; i < kMaxInputs; ++i)
            if (solo_[i].value > 0.5f) { solo_active = true; break; }

        for (int i = 0; i < kMaxInputs; ++i) {
            const float* in = ctx->input_buffers[i];

            const uint32_t in_ch = ctx->input_channel_counts
                                       ? ctx->input_channel_counts[i]
                                       : 2u;

            // Pre-gain peak meter: measure every connected input (even at
            // gain 0) so the inspector shows arriving signal, then apply a
            // simple per-block falloff. Disconnected inputs decay toward 0.
            float blk_peak = 0.0f;
            if (in) {
                const uint32_t scan = (in_ch >= 2) ? (2u * n) : n;
                for (uint32_t s = 0; s < scan; ++s) {
                    const float m = std::fabs(in[s]);
                    if (m > blk_peak) blk_peak = m;
                }
            }
            float env = meter_env_[i] * 0.80f;
            if (blk_peak > env) env = blk_peak;
            meter_env_[i] = env;
            in_meter_[i].store(env, std::memory_order_relaxed);

            const float g = gain_[i].value;
            const bool muted  = mute_[i].value > 0.5f;
            const bool soloed = solo_[i].value > 0.5f;
            const bool audible = !muted && (!solo_active || soloed);
            if (!in || g == 0.0f || !audible) continue;

            // Equal-power pan law: pan ∈ [-1, +1] → angle ∈ [0, π/2].
            // Mirrors operators/audio/stereo_pan_width/stereo_pan_width.cpp:86-90.
            const float angle = (pan_[i].value + 1.0f) * kPiOver4;
            const float gl = g * std::cos(angle);
            const float gr = g * std::sin(angle);

            if (in_ch >= 2) {
                const float* in_l = in;
                const float* in_r = in + n;
                for (uint32_t s = 0; s < n; ++s) out_l[s] += in_l[s] * gl;
                for (uint32_t s = 0; s < n; ++s) out_r[s] += in_r[s] * gr;
            } else {
                for (uint32_t s = 0; s < n; ++s) {
                    out_l[s] += in[s] * gl;
                    out_r[s] += in[s] * gr;
                }
            }
        }

        // Post-mix output peak, same per-block envelope falloff as the inputs.
        float out_peak = 0.0f;
        for (uint32_t s = 0; s < 2 * n; ++s) {
            const float m = std::fabs(out_l[s]);
            if (m > out_peak) out_peak = m;
        }
        float oenv = out_env_ * 0.80f;
        if (out_peak > oenv) oenv = out_peak;
        out_env_ = oenv;
        out_meter_.store(oenv, std::memory_order_relaxed);
    }

    // Channel-strip inspector: one row per CONNECTED input, labeled by the
    // upstream node's display name (resolved by the host into
    // ctx->input_connections — the audio context never sees this). Each strip
    // carries a draggable gain fader (0..2, unity tick at center) and a pan
    // slider (-1..+1, center-detented visually). Unconnected inputs are hidden,
    // so the panel grows with the patch rather than always showing 16 rows.
    void draw_inspector(VividInspectorContext* ctx) override {
        using namespace vivid::draw_ui;
        auto& d = ctx->draw;
        void* o = d.opaque;
        const auto& th = ctx->theme;

        const float x = ctx->content_x;
        float y = ctx->content_y;
        const float w = ctx->content_width;
        const float lh = line_height_or(d, o, 12.0f);

        const VividColor bright = th.bright_text;
        const VividColor dim    = th.dim_text;
        const VividColor accent = th.accent;
        const VividColor track  = th.slider_track;
        const VividColor fill   = th.slider_fill;

        char hdr[48];
        std::snprintf(hdr, sizeof(hdr), "%u channel%s",
                      ctx->input_connection_count,
                      ctx->input_connection_count == 1 ? "" : "s");
        draw_section_header(d, o, x, y, w, hdr, dim, th.separator, 0.8f);
        y += lh + 8.0f;

        if (ctx->input_connection_count == 0) {
            draw_text_aligned(d, o, x, y, w,
                              "Connect audio sources to the inputs.",
                              dim, 0.85f, 0.0f);
            ctx->consumed_height = (y + lh) - ctx->content_y;
            return;
        }

        const bool mdown = ctx->mouse.left_down != 0;
        if (!mdown) { insp_drag_ch_ = -1; insp_drag_kind_ = 0; }

        auto in_rect = [](const VividInspectorMouse& m,
                          float rx, float ry, float rw, float rh) {
            return m.x >= rx && m.x <= rx + rw && m.y >= ry && m.y <= ry + rh;
        };

        constexpr float kStripH = 58.0f;

        for (uint32_t k = 0; k < ctx->input_connection_count; ++k) {
            const VividInputConnection& ic = ctx->input_connections[k];

            // Channel index from the "input_<n>" port name (robust to any
            // future reordering of the descriptor's port list).
            int ch = -1;
            if (ic.port_name) {
                const char* us = std::strrchr(ic.port_name, '_');
                if (us) ch = std::atoi(us + 1);
            }
            if (ch < 0 || ch >= kMaxInputs) continue;

            const float gain = (ctx->param_count > static_cast<uint32_t>(ch))
                                   ? ctx->param_values[ch] : 1.0f;
            const float pan  = (ctx->param_count > static_cast<uint32_t>(kMaxInputs + ch))
                                   ? ctx->param_values[kMaxInputs + ch] : 0.0f;

            const float sy = y;
            draw_panel(d, o, x, sy, w, kStripH - 6.0f,
                       with_alpha(th.dark_bg, 0.6f), {0, 0, 0, 0}, th.corner_radius);

            // Channel label = the connected NODE's id (its identity), not the
            // operator type — so multiple nodes of the same type are distinct.
            const char* name = (ic.source_node_id && ic.source_node_id[0])
                                   ? ic.source_node_id : ic.source_label;
            draw_clipped_text_box(d, o, x + 6.0f, sy + 4.0f, w * 0.60f, lh,
                                  name, {0, 0, 0, 0}, bright, 0.0f, 0.85f, 0.0f, 0.0f);
            char gtxt[16];
            if (gain <= 0.0001f) std::snprintf(gtxt, sizeof(gtxt), "-inf");
            else std::snprintf(gtxt, sizeof(gtxt), "%+.1f dB",
                               20.0f * std::log10(gain));
            draw_text_aligned(d, o, x + w * 0.60f, sy + 4.0f, w * 0.40f - 6.0f,
                              gtxt, dim, 0.8f, 1.0f);

            // Gain fader (0..2), unity tick at the midpoint
            const float fx = x + 6.0f, fw = w - 12.0f;
            const float fy = sy + 4.0f + lh + 3.0f;
            draw_meter(d, o, fx, fy, fw, 7.0f, gain * 0.5f, fill, track,
                       MeterOrientation::Horizontal, 2.0f);
            if (d.draw_rect)
                d.draw_rect(o, fx + fw * 0.5f, fy - 1.0f, 1.0f, 9.0f,
                            with_alpha(bright, 0.4f));

            // Live pre-gain input meter (thin bar directly under the fader)
            const float lvl = in_meter_[ch].load(std::memory_order_relaxed);
            const float my = fy + 7.0f + 1.0f;
            draw_meter(d, o, fx, my, fw, 3.0f, lvl,
                       with_alpha(accent, 0.9f), with_alpha(track, 0.5f),
                       MeterOrientation::Horizontal, 1.5f);

            // Pan slider (-1..+1) with a center mark and a handle
            const float py2 = my + 8.0f;
            draw_panel(d, o, fx, py2 + 2.0f, fw, 3.0f, track, {0, 0, 0, 0}, 1.5f);
            const float pan_frac = (pan + 1.0f) * 0.5f;
            if (d.draw_rect) {
                d.draw_rect(o, fx + fw * 0.5f, py2, 1.0f, 7.0f, with_alpha(dim, 0.6f));
                d.draw_rect(o, fx + fw * pan_frac - 1.5f, py2 - 1.0f, 3.0f, 9.0f, accent);
            }

            // Interaction — begin drag on click inside either control's band,
            // then track the held pointer (even if it leaves the row).
            if (ctx->mouse.left_clicked) {
                if (in_rect(ctx->mouse, fx, fy - 3.0f, fw, 13.0f)) {
                    insp_drag_ch_ = ch; insp_drag_kind_ = 1;
                } else if (in_rect(ctx->mouse, fx, py2 - 3.0f, fw, 13.0f)) {
                    insp_drag_ch_ = ch; insp_drag_kind_ = 2;
                }
            }
            if (mdown && insp_drag_ch_ == ch && insp_drag_kind_ != 0) {
                const float rel = std::clamp((ctx->mouse.x - fx) / fw, 0.0f, 1.0f);
                char nm[16];
                if (insp_drag_kind_ == 1) {
                    std::snprintf(nm, sizeof(nm), "gain_%d", ch);
                    ctx->commands.set_param(ctx->commands.opaque, nm, rel * 2.0f);
                } else {
                    std::snprintf(nm, sizeof(nm), "pan_%d", ch);
                    ctx->commands.set_param(ctx->commands.opaque, nm, rel * 2.0f - 1.0f);
                }
            }

            y += kStripH;
        }

        ctx->consumed_height = y - ctx->content_y;
    }

    // ----- Tier-3 editor: full mixing console (dedicated window) -----
    // The inspector stays the quick per-channel adjuster; this console is the
    // larger surface — vertical faders, live meters, pan, dB readouts — one
    // column per CONNECTED input, labeled by its source node (resolved by the
    // host into ctx->input_connections).
    static VividEditorMetadata editor_metadata() {
        VividEditorMetadata m{};
        m.default_width  = 760;
        m.default_height = 460;
        m.min_width      = 360;
        m.min_height     = 300;
        m.title_suffix   = "Console";
        return m;
    }

    void draw_editor(VividEditorContext* ctx) {
        if (!ctx) return;
        namespace ui = ::vivid::ui;
        using namespace vivid::draw_ui;
        auto& d = ctx->draw;
        void* o = d.opaque;
        const auto& th = ctx->theme;

        const float W = ctx->surface_width;
        const float H = ctx->surface_height;
        const float lh = line_height_or(d, o, 12.0f);

        const VividColor bright = th.bright_text;
        const VividColor dim    = th.dim_text;
        const VividColor accent = th.accent;
        const VividColor track  = th.slider_track;
        const VividColor fill   = th.slider_fill;

        // Background
        draw_panel(d, o, 0, 0, W, H, th.bg);

        // Title + channel count
        const uint32_t nconn = ctx->input_connection_count;
        char title[48];
        std::snprintf(title, sizeof(title), "MIXER  \xC2\xB7  %u channel%s",
                      nconn, nconn == 1 ? "" : "s");
        draw_text_aligned(d, o, 14.0f, 12.0f, W - 28.0f, title, bright, 1.0f, 0.0f);
        if (d.draw_rect) d.draw_rect(o, 14.0f, 30.0f, W - 28.0f, 1.0f,
                                     with_alpha(th.separator, 1.0f));

        if (nconn == 0) {
            draw_text_aligned(d, o, 14.0f, 48.0f, W - 28.0f,
                              "Connect audio sources to the mixer's inputs.",
                              dim, 0.9f, 0.0f);
            return;
        }

        // Strip column geometry
        const float left = 16.0f, right = 16.0f;
        const float top = 44.0f, bottom = 14.0f;
        const float avail = std::max(1.0f, W - left - right);
        const float col_w = std::clamp(avail / static_cast<float>(nconn), 56.0f, 132.0f);

        const float name_y   = top;
        const float fader_top = top + lh + 6.0f;
        const float db_h      = lh + 2.0f;
        const float pan_h     = 14.0f;
        const float btn_h     = 16.0f;
        const float fader_bot = H - bottom - db_h - pan_h - btn_h - 14.0f;
        const float fader_h   = std::max(40.0f, fader_bot - fader_top);

        const bool mdown = ctx->mouse.left_down != 0;
        if (!mdown) ed_pan_drag_ch_ = -1;

        for (uint32_t k = 0; k < nconn; ++k) {
            const VividInputConnection& ic = ctx->input_connections[k];
            const int ch = (ic.port_index < static_cast<uint32_t>(kMaxInputs))
                               ? static_cast<int>(ic.port_index) : -1;
            if (ch < 0) continue;

            const float cx = left + static_cast<float>(k) * col_w;
            const float gain = (ctx->param_count > static_cast<uint32_t>(ch))
                                   ? ctx->param_values[ch] : 1.0f;
            const float pan  = (ctx->param_count > static_cast<uint32_t>(kMaxInputs + ch))
                                   ? ctx->param_values[kMaxInputs + ch] : 0.0f;

            // Column background
            draw_panel(d, o, cx + 2.0f, name_y - 2.0f, col_w - 4.0f,
                       (H - bottom) - (name_y - 2.0f),
                       with_alpha(th.dark_bg, 0.5f), {0, 0, 0, 0}, th.corner_radius);

            // Column label = the connected NODE's id (its identity), not the
            // operator type. Left-aligned so the start of the id reads clearly
            // and the distinguishing part stays visible if a long id clips.
            const char* name = (ic.source_node_id && ic.source_node_id[0])
                                   ? ic.source_node_id : ic.source_label;
            draw_clipped_text_box(d, o, cx + 5.0f, name_y, col_w - 10.0f, lh,
                                  name, {0, 0, 0, 0}, bright, 0.0f, 0.8f, 0.0f, 0.0f);

            // Vertical gain fader (0..2) + a live meter column beside it.
            const float fader_w = 16.0f;
            const float meter_w = 6.0f;
            const float group_w = fader_w + 6.0f + meter_w;
            const float fader_x = cx + (col_w - group_w) * 0.5f;
            const float meter_x = fader_x + fader_w + 6.0f;

            ui::Rect fr{fader_x, fader_top, fader_w, fader_h};
            ui::SliderResult sr = ui::ui_slider_v(*ctx, fr, gain, 0.0f, 2.0f,
                                                  &ed_fader_[ch], fill, track);
            if (sr.changed) {
                char nm[16];
                std::snprintf(nm, sizeof(nm), "gain_%d", ch);
                ctx->commands.set_param(ctx->commands.opaque, nm, sr.value);
            }
            // Unity gridline (gain == 1.0 -> halfway up the fader)
            if (d.draw_rect)
                d.draw_rect(o, fader_x - 2.0f, fader_top + fader_h * 0.5f,
                            fader_w + 4.0f, 1.0f, with_alpha(bright, 0.35f));

            // Live pre-gain meter
            const float lvl = in_meter_[ch].load(std::memory_order_relaxed);
            draw_meter(d, o, meter_x, fader_top, meter_w, fader_h,
                       std::clamp(lvl, 0.0f, 1.0f), with_alpha(accent, 0.9f),
                       with_alpha(track, 0.6f), MeterOrientation::Vertical, 1.5f);

            // dB readout
            char gtxt[16];
            if (gain <= 0.0001f) std::snprintf(gtxt, sizeof(gtxt), "-inf");
            else std::snprintf(gtxt, sizeof(gtxt), "%+.1f", 20.0f * std::log10(gain));
            draw_text_aligned(d, o, cx + 4.0f, fader_bot + 3.0f, col_w - 8.0f,
                              gtxt, dim, 0.8f, 0.5f);

            // Pan slider (-1..+1), compact horizontal with center detent.
            const float pan_x = cx + 8.0f, pan_w = col_w - 16.0f;
            const float pan_y = fader_bot + 3.0f + db_h;
            draw_panel(d, o, pan_x, pan_y + 5.0f, pan_w, 3.0f, track, {0, 0, 0, 0}, 1.5f);
            const float pan_frac = (pan + 1.0f) * 0.5f;
            if (d.draw_rect) {
                d.draw_rect(o, pan_x + pan_w * 0.5f, pan_y + 2.0f, 1.0f, 9.0f,
                            with_alpha(dim, 0.6f));
                d.draw_rect(o, pan_x + pan_w * pan_frac - 1.5f, pan_y + 1.0f,
                            3.0f, 11.0f, accent);
            }
            const auto& m = ctx->mouse;
            const bool in_pan = m.x >= pan_x - 2.0f && m.x <= pan_x + pan_w + 2.0f &&
                                m.y >= pan_y - 2.0f && m.y <= pan_y + 14.0f;
            if (m.left_clicked && in_pan) ed_pan_drag_ch_ = ch;
            if (mdown && ed_pan_drag_ch_ == ch) {
                const float rel = std::clamp((m.x - pan_x) / std::max(1.0f, pan_w),
                                             0.0f, 1.0f);
                char nm[16];
                std::snprintf(nm, sizeof(nm), "pan_%d", ch);
                ctx->commands.set_param(ctx->commands.opaque, nm, rel * 2.0f - 1.0f);
            }

            // Mute / Solo toggles (persisted params at 2N+ch / 3N+ch).
            const uint32_t mute_idx = 2u * kMaxInputs + ch;
            const uint32_t solo_idx = 3u * kMaxInputs + ch;
            const bool muted  = ctx->param_count > mute_idx &&
                                ctx->param_values[mute_idx] > 0.5f;
            const bool soloed = ctx->param_count > solo_idx &&
                                ctx->param_values[solo_idx] > 0.5f;
            const float btn_y = pan_y + pan_h + 4.0f;
            const float bw = (pan_w - 6.0f) * 0.5f;
            ui::Rect mr{pan_x, btn_y, bw, btn_h};
            ui::Rect sr2{pan_x + bw + 6.0f, btn_y, bw, btn_h};
            const VividColor mute_on{0.82f, 0.30f, 0.30f, 1.0f};
            const VividColor solo_on{0.92f, 0.76f, 0.22f, 1.0f};
            if (ui::ui_toggle(*ctx, mr, "M", muted, {}, mute_on).clicked) {
                char nm[16]; std::snprintf(nm, sizeof(nm), "mute_%d", ch);
                ctx->commands.set_param(ctx->commands.opaque, nm, muted ? 0.0f : 1.0f);
            }
            if (ui::ui_toggle(*ctx, sr2, "S", soloed, {}, solo_on).clicked) {
                char nm[16]; std::snprintf(nm, sizeof(nm), "solo_%d", ch);
                ctx->commands.set_param(ctx->commands.opaque, nm, soloed ? 0.0f : 1.0f);
            }
        }
    }

    // Live thumbnail: one mini level meter per CONNECTED input (lit by the
    // pre-gain peak the audio thread publishes into in_meter_), plus a brighter
    // OUT meter for the summed bus. The strip adapts to the connected count, so
    // the node face reads like a tiny mixing console rather than a fixed icon.
    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx || !ctx->draw.opaque) return;
        auto& d = const_cast<VividDrawAPI&>(ctx->draw);
        void* o = d.opaque;

        const float w = static_cast<float>(ctx->thumbnail_logical_width
                                               ? ctx->thumbnail_logical_width  : ctx->thumbnail_width);
        const float h = static_cast<float>(ctx->thumbnail_logical_height
                                               ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

        vivid::draw_plot::draw_thumb_background(d, o, w, h);

        // Connected channels (ascending), from the host-resolved graph topology.
        const uint32_t mask = ctx->connected_input_mask;
        int chans[kMaxInputs];
        int n = 0;
        for (int i = 0; i < kMaxInputs; ++i)
            if (mask & (1u << i)) chans[n++] = i;

        if (n == 0) {
            vivid::draw_plot::draw_thumb_label(d, o, 6.0f, 4.0f, "no inputs",
                                               {0.45f, 0.50f, 0.58f, 0.9f}, 0.8f);
            return;
        }

        char hdr[16];
        std::snprintf(hdr, sizeof(hdr), "%d ch", n);
        vivid::draw_plot::draw_thumb_label(d, o, 6.0f, 4.0f, hdr,
                                           {0.45f, 0.55f, 0.65f, 0.95f}, 0.8f);

        const float pad     = 6.0f;
        const float bar_top = 18.0f;
        const float bar_h   = std::max(4.0f, (h - pad) - bar_top);

        // Reserve a wider right-hand slot for the OUT (sum) meter; the input
        // strip fills the remainder, one column per connected channel.
        const float out_w       = std::max(10.0f, w * 0.16f);
        const float out_x       = w - pad - out_w;
        const float arrow_w     = 8.0f;
        const float strip_left  = pad;
        const float strip_right = out_x - arrow_w;
        const float avail       = std::max(1.0f, strip_right - strip_left);
        const float col_w       = avail / static_cast<float>(n);

        const VividColor track {0.16f, 0.16f, 0.19f, 0.8f};
        const VividColor low   {0.31f, 0.75f, 0.39f, 0.86f};
        const VividColor high  {0.86f, 0.31f, 0.24f, 0.86f};

        for (int k = 0; k < n; ++k) {
            const float lvl = std::clamp(
                in_meter_[chans[k]].load(std::memory_order_relaxed), 0.0f, 1.0f);
            const float cx = strip_left + static_cast<float>(k) * col_w;
            const float bw = std::max(2.0f, col_w - 2.0f);
            vivid::draw_plot::draw_scalar_meter(d, o, cx, bar_top, bw, bar_h,
                                                lvl, track, low, high, 1.5f);
        }

        // Convergence cue: a small triangle pointing into the OUT meter.
        if (d.draw_tri) {
            const float ay = bar_top + bar_h * 0.5f;
            d.draw_tri(o, strip_right + 1.0f, ay - 4.0f,
                       strip_right + 1.0f, ay + 4.0f,
                       out_x - 1.0f, ay, {0.55f, 0.62f, 0.72f, 0.8f});
        }

        // OUT (sum) meter — brighter palette than the per-input strips.
        const float out_lvl = std::clamp(
            out_meter_.load(std::memory_order_relaxed), 0.0f, 1.0f);
        const VividColor out_low {0.36f, 0.66f, 0.95f, 0.92f};
        const VividColor out_high{0.92f, 0.42f, 0.95f, 0.92f};
        vivid::draw_plot::draw_scalar_meter(d, o, out_x, bar_top, out_w, bar_h,
                                            out_lvl, track, out_low, out_high, 1.5f);
    }
};

VIVID_DEFINE_OP(Mixer) {
}

VIVID_THUMBNAIL(Mixer)
// Full mode: the channel strips ARE the mixer's UI, so suppress the redundant
// auto-generated gain_*/pan_* knob grid (which only covers connected inputs
// via the strips anyway). See node_graph_draw_inspector.cpp — params render
// only in VIVID_INSPECTOR_STANDARD mode.
VIVID_INSPECTOR_FULL_MODE(Mixer)
// Deliberate two-surface split (departs from the one-tier convention in
// operators/CLAUDE.md): the inspector is the quick per-channel adjuster; this
// dedicated console window is the fuller mixing surface. The runtime adds an
// "Open Editor" button to the inspector header automatically (gated on
// has_editor), so both coexist.
VIVID_EDITOR(Mixer)
