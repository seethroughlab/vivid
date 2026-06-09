#include "operator_api/operator.h"
#include "operator_api/adsr.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "operator_api/draw_ui_helpers.h"
#include "operator_api/editor_keys.h"
#include "operator_api/editor_ui.h"
#include "sample_bank.h"
#include "voice.h"
#include "voice_breakouts.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

using namespace vivid_sampler;

struct SP404 : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "SP404";
    static constexpr bool kTimeDependent = false;
    static constexpr int kMaxPads = 16;

    // ---- Per-pad sample files (pad N = MIDI note 36+N) ----
    vivid::Param<vivid::FilePath> pad_file_0 {"pad_file_0"};
    vivid::Param<vivid::FilePath> pad_file_1 {"pad_file_1"};
    vivid::Param<vivid::FilePath> pad_file_2 {"pad_file_2"};
    vivid::Param<vivid::FilePath> pad_file_3 {"pad_file_3"};
    vivid::Param<vivid::FilePath> pad_file_4 {"pad_file_4"};
    vivid::Param<vivid::FilePath> pad_file_5 {"pad_file_5"};
    vivid::Param<vivid::FilePath> pad_file_6 {"pad_file_6"};
    vivid::Param<vivid::FilePath> pad_file_7 {"pad_file_7"};
    vivid::Param<vivid::FilePath> pad_file_8 {"pad_file_8"};
    vivid::Param<vivid::FilePath> pad_file_9 {"pad_file_9"};
    vivid::Param<vivid::FilePath> pad_file_10{"pad_file_10"};
    vivid::Param<vivid::FilePath> pad_file_11{"pad_file_11"};
    vivid::Param<vivid::FilePath> pad_file_12{"pad_file_12"};
    vivid::Param<vivid::FilePath> pad_file_13{"pad_file_13"};
    vivid::Param<vivid::FilePath> pad_file_14{"pad_file_14"};
    vivid::Param<vivid::FilePath> pad_file_15{"pad_file_15"};

    // ---- Global playback params ----
    vivid::Param<int>   mode    {"mode",    0, {"one_shot", "loop", "gate"}};
    vivid::Param<float> attack  {"attack",  0.001f, 0.001f, 2.0f};
    vivid::Param<float> decay   {"decay",   0.1f,   0.01f,  2.0f};
    vivid::Param<float> sustain {"sustain", 1.0f,   0.0f,   1.0f};
    vivid::Param<float> release {"release", 0.05f,  0.001f, 10.0f};
    vivid::Param<float> volume  {"volume",  1.0f,   0.0f,   2.0f};
    vivid::Param<float> pressure_to_amp {"pressure_to_amp", 0.5f,  0.0f, 1.0f};
    vivid::Param<float> timbre_to_pitch {"timbre_to_pitch", 12.0f, -24.0f, 24.0f};
    vivid::Param<int>   reverse {"reverse", 0, {"off", "on"}};
    vivid::Param<float> detune_semitones {"detune_semitones", 0.0f, -24.0f, 24.0f};
    vivid::Param<float> loop_crossfade_ms {"loop_crossfade_ms", 0.0f, 0.0f, 100.0f};

    Voice voices_[kMaxPads];
    std::atomic<SampleBank*> bank_{nullptr};
    SampleBank* deferred_delete_ = nullptr;
    std::string last_pad_paths_[16];
    uint64_t frame_counter_ = 0;

    // ---- Main-thread display state (read by draw_editor, same thread) ----
    std::string pad_display_names_[16];
    int selected_pad_ = 0;

    // ---- Editor slider drag state ----
    vivid::ui::SliderState sp_attack_drag_{};
    vivid::ui::SliderState sp_decay_drag_{};
    vivid::ui::SliderState sp_sustain_drag_{};
    vivid::ui::SliderState sp_release_drag_{};
    vivid::ui::SliderState sp_volume_drag_{};
    vivid::ui::SliderState sp_detune_drag_{};
    vivid::ui::SliderState sp_xfade_drag_{};

    SP404() {
        vivid::description(mode,              "Playback mode: one_shot plays once, loop repeats, gate sustains while held");
        vivid::description(attack,            "Envelope attack time in seconds");
        vivid::description(decay,             "Envelope decay time in seconds");
        vivid::description(sustain,           "Envelope sustain level (0-1)");
        vivid::description(release,           "Envelope release time in seconds");
        vivid::description(volume,            "Master output volume, can boost up to 2x");
        vivid::description(pressure_to_amp,   "Pressure modulation depth for per-voice amplitude");
        vivid::description(timbre_to_pitch,   "Timbre modulation range in semitones for per-voice pitch");
        vivid::description(reverse,           "Play pads backwards from the sample or loop end");
        vivid::description(detune_semitones,  "Static tape-style pitch offset in semitones (all pads)");
        vivid::description(loop_crossfade_ms, "Equal-power crossfade at loop wraps in loop mode");
    }

    ~SP404() {
        delete bank_.load(std::memory_order_relaxed);
        delete deferred_delete_;
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        // Indices 0-15: per-pad file paths
        out.push_back(&pad_file_0);  out.push_back(&pad_file_1);
        out.push_back(&pad_file_2);  out.push_back(&pad_file_3);
        out.push_back(&pad_file_4);  out.push_back(&pad_file_5);
        out.push_back(&pad_file_6);  out.push_back(&pad_file_7);
        out.push_back(&pad_file_8);  out.push_back(&pad_file_9);
        out.push_back(&pad_file_10); out.push_back(&pad_file_11);
        out.push_back(&pad_file_12); out.push_back(&pad_file_13);
        out.push_back(&pad_file_14); out.push_back(&pad_file_15);
        // Indices 16-26: global params
        out.push_back(&mode);
        out.push_back(&attack);
        out.push_back(&decay);
        out.push_back(&sustain);
        out.push_back(&release);
        out.push_back(&volume);
        out.push_back(&pressure_to_amp);
        out.push_back(&timbre_to_pitch);
        out.push_back(&reverse);
        out.push_back(&detune_semitones);
        out.push_back(&loop_crossfade_ms);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_in", VIVID_PORT_INPUT, VividNoteBuffer));
        out.push_back({"output",     VIVID_PORT_AUDIO_BUFFER,  VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2});
        out.push_back({"voices_out", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT,
                       VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 16, 0.0f});
        vivid::advanced_breakout(out.back());
        out.push_back({.name="voice_ids",        .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_OUTPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        vivid::advanced_breakout(out.back());
        out.push_back({.name="voice_gates",      .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_OUTPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        vivid::advanced_breakout(out.back());
        out.push_back({.name="voice_velocities", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_OUTPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        vivid::advanced_breakout(out.back());
        out.push_back({.name="voice_freqs",      .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_OUTPUT, .multiplicity=VIVID_MULTIPLICITY_MANY});
        vivid::advanced_breakout(out.back());
        vivid::append_analysis_ports(out);
    }

    // Pointer array for iteration over the 16 pad params.
    vivid::Param<vivid::FilePath>* pad_param(int i) {
        vivid::Param<vivid::FilePath>* pads[16] = {
            &pad_file_0,  &pad_file_1,  &pad_file_2,  &pad_file_3,
            &pad_file_4,  &pad_file_5,  &pad_file_6,  &pad_file_7,
            &pad_file_8,  &pad_file_9,  &pad_file_10, &pad_file_11,
            &pad_file_12, &pad_file_13, &pad_file_14, &pad_file_15
        };
        return pads[i];
    }

    void main_thread_update(double /*time*/) override {
        delete deferred_delete_;
        deferred_delete_ = nullptr;

        // Check whether any pad path changed.
        bool any_changed = false;
        for (int i = 0; i < 16; ++i) {
            if (pad_param(i)->str_value != last_pad_paths_[i]) {
                any_changed = true;
                break;
            }
        }
        if (!any_changed) return;

        auto* new_bank = new SampleBank();
        new_bank->groups.resize(1);
        SampleGroup& g = new_bank->groups[0];
        g.name = "pads";

        for (int i = 0; i < 16; ++i) {
            const std::string& path = pad_param(i)->str_value;
            last_pad_paths_[i] = path;

            if (path.empty()) {
                pad_display_names_[i].clear();
                continue;
            }

            auto data = decode_wav(path);
            if (!data) {
                pad_display_names_[i].clear();
                continue;
            }

            SampleRegion region;
            region.root_note = region.lo_note = region.hi_note = 36 + i;
            region.data = data;
            g.regions.push_back(region);

            // Derive display name: basename without extension.
            size_t slash = path.find_last_of("/\\");
            std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
            size_t dot = name.rfind('.');
            if (dot != std::string::npos) name.resize(dot);
            pad_display_names_[i] = std::move(name);
        }

        SampleBank* old = bank_.exchange(new_bank, std::memory_order_acq_rel);
        deferred_delete_ = old;
    }

    void process_audio(const VividAudioContext* ctx) override {
        SampleBank* bank = bank_.load(std::memory_order_acquire);
        if (!bank || bank->groups.empty()) {
            for (uint32_t i = 0; i < ctx->buffer_size; ++i) {
                ctx->output_buffers[0][i] = 0.0f;
                ctx->output_buffers[0][ctx->buffer_size + i] = 0.0f;
            }
            return;
        }

        int   p_mode    = mode.int_value();
        float p_attack  = attack.value;
        float p_decay   = decay.value;
        float p_sustain = sustain.value;
        float p_release = release.value;
        float p_volume  = volume.value;
        bool  p_reverse = reverse.int_value() != 0;
        float p_detune  = detune_semitones.value;
        float dt        = 1.0f / static_cast<float>(ctx->sample_rate);
        const uint32_t p_loop_crossfade_frames = static_cast<uint32_t>(
            std::min(loop_crossfade_ms.value, 100.0f) *
            static_cast<float>(ctx->sample_rate) / 1000.0f);

        const SampleGroup& group = bank->groups[0];

        if (ctx->custom_inputs && ctx->custom_input_count > 0 && ctx->custom_inputs[0]) {
            auto* notes = static_cast<const VividNoteBuffer*>(ctx->custom_inputs[0]);
            for (uint32_t m = 0; m < notes->count; ++m) {
                const auto& ev = notes->events[m];
                if (ev.note_id == 0) continue;

                if (ev.type == VIVID_NOTE_ON) {
                    int note = ev.note_number;
                    float vel = ev.value;

                    const SampleRegion* region = find_region(group, note, vel);
                    if (!region || !region->data) {
                        region = find_nearest_region(group, note);
                        if (!region || !region->data) continue;
                    }

                    int vi = -1;
                    for (int j = 0; j < kMaxPads; ++j) {
                        if (voices_[j].active && voices_[j].note_id == ev.note_id) {
                            vi = j; break;
                        }
                    }
                    if (vi < 0) vi = find_free_voice(voices_, kMaxPads);
                    if (vi < 0) vi = steal_oldest_voice(voices_, kMaxPads);

                    double rate = static_cast<double>(region->data->sample_rate) /
                                  static_cast<double>(ctx->sample_rate);
                    rate *= std::pow(2.0, static_cast<double>(p_detune) / 12.0);
                    bool one_shot = (p_mode == 0);

                    voice_note_on(voices_[vi], note, vel, region, rate,
                                  frame_counter_, one_shot, p_reverse);
                    voices_[vi].note_id          = ev.note_id;
                    voices_[vi].pitch_bend_semis = 0.0f;
                    voices_[vi].pressure         = 0.0f;
                    voices_[vi].timbre           = 0.0f;
                } else if (ev.type == VIVID_NOTE_OFF) {
                    for (int j = 0; j < kMaxPads; ++j) {
                        if (voices_[j].active && voices_[j].note_id == ev.note_id) {
                            voice_note_off(voices_[j]);
                            break;
                        }
                    }
                } else if (ev.type == VIVID_NOTE_PITCH_BEND) {
                    for (int j = 0; j < kMaxPads; ++j) {
                        if (voices_[j].active && voices_[j].note_id == ev.note_id) {
                            voices_[j].pitch_bend_semis = ev.value;
                            break;
                        }
                    }
                }
            }
        }

        int slot_to_pos[kMaxPads];
        int sorted[kMaxPads];
        int active_count = 0;
        for (int v = 0; v < kMaxPads; ++v) {
            slot_to_pos[v] = -1;
            if (voices_[v].active) sorted[active_count++] = v;
        }
        std::sort(sorted, sorted + active_count,
                  [this](int a, int b) {
                      return voices_[a].note_id < voices_[b].note_id;
                  });
        for (int i = 0; i < active_count; ++i) slot_to_pos[sorted[i]] = i;

        const uint32_t frames = ctx->buffer_size;
        float* voices_out_buf = (ctx->output_buffers && ctx->output_buffers[1])
                                ? ctx->output_buffers[1] : nullptr;
        if (voices_out_buf) {
            std::memset(voices_out_buf, 0,
                        static_cast<size_t>(kMaxPads) * frames * sizeof(float));
        }

        const float p_amp_depth   = pressure_to_amp.value;
        const float t_pitch_semis = timbre_to_pitch.value;

        for (uint32_t s = 0; s < frames; ++s) {
            float out_L = 0.0f;
            float out_R = 0.0f;

            for (int v = 0; v < kMaxPads; ++v) {
                if (!voices_[v].active) continue;
                const auto& slot = voices_[v];
                const float gain_scale = 1.0f + p_amp_depth * slot.pressure;
                const float rate_scale = std::pow(2.0f, (t_pitch_semis * slot.timbre) / 12.0f);
                float voice_L = 0.0f;
                float voice_R = 0.0f;
                VoiceRenderOptions render_options{};
                render_options.reverse = p_reverse;
                render_options.force_loop = (p_mode == 1);
                render_options.loop_crossfade_frames = (p_mode == 1)
                    ? p_loop_crossfade_frames : 0u;
                voice_render_frame(voices_[v], voice_L, voice_R, dt,
                                   p_attack, p_decay, p_sustain, p_release,
                                   rate_scale, gain_scale, render_options);
                out_L += voice_L;
                out_R += voice_R;

                if (voices_out_buf && slot_to_pos[v] >= 0) {
                    const int pos = slot_to_pos[v];
                    voices_out_buf[pos * frames + s] = (voice_L + voice_R) * 0.5f * p_volume;
                }
            }

            out_L *= p_volume;
            out_R *= p_volume;
            ctx->output_buffers[0][s]        = out_L;
            ctx->output_buffers[0][frames + s] = out_R;
            frame_counter_++;
        }

        if (ctx->value_outputs) {
            // Voice breakouts emitted to value_outputs[2..5] (ids/gates/
            // velocities/freqs). Output ports 0=output, 1=voices_out (audio)
            // precede the first voice_* lane port, so the lane ports start at
            // ordinal 2 — matching the previous lane-output slice at [2..5].
            auto emit_breakout = [&](int port, auto value_for_slot) {
                VividValueOutput* out = &ctx->value_outputs[port];
                float* buf = vivid_value_output_floats(
                    out, static_cast<uint32_t>(active_count));
                if (buf) {
                    for (int i = 0; i < active_count; ++i) {
                        buf[i] = value_for_slot(
                            static_cast<const vivid::VoiceSlot&>(
                                voices_[sorted[i]]));
                    }
                }
                vivid_value_output_commit(
                    out, static_cast<uint32_t>(active_count));
            };
            emit_breakout(2, [](const vivid::VoiceSlot& s) {
                return static_cast<float>(s.note_id);
            });
            emit_breakout(3, [](const vivid::VoiceSlot& s) {
                return s.gate ? 1.0f : 0.0f;
            });
            emit_breakout(4, [](const vivid::VoiceSlot& s) {
                return s.velocity;
            });
            emit_breakout(5, [](const vivid::VoiceSlot& s) {
                return vivid_sequencers::voice_freq_hz(s);
            });
        }
    }

    // ---- Editor ----
    static VividEditorMetadata editor_metadata();
    void draw_editor(VividEditorContext* ctx);
};

// ---------------------------------------------------------------------------
// Editor
// ---------------------------------------------------------------------------

VividEditorMetadata SP404::editor_metadata() {
    VividEditorMetadata m{};
    m.default_width  = 820;
    m.default_height = 560;
    m.min_width      = 640;
    m.min_height     = 420;
    m.title_suffix   = "SP-404";
    return m;
}

void SP404::draw_editor(VividEditorContext* ctx) {
    if (!ctx) return;

    auto& d = ctx->draw;
    void* o = d.opaque;
    const auto& th = ctx->theme;

    // Param index constants (must match collect_params order).
    // 0-15: pad_file_N; 16=mode, 17=attack, 18=decay, 19=sustain, 20=release
    // 21=volume, 22=pressure_to_amp, 23=timbre_to_pitch
    // 24=reverse, 25=detune_semitones, 26=loop_crossfade_ms
    auto get_param = [&](int idx, float fallback) -> float {
        if (idx < 0 || static_cast<uint32_t>(idx) >= ctx->param_count) return fallback;
        return ctx->param_values[idx];
    };
    auto set_named = [&](const char* name, float v) {
        if (ctx->commands.set_param)
            ctx->commands.set_param(ctx->commands.opaque, name, v);
    };

    const int   p_mode    = static_cast<int>(get_param(16, 0.0f) + 0.5f);
    const float p_attack  = get_param(17, 0.001f);
    const float p_decay   = get_param(18, 0.1f);
    const float p_sustain = get_param(19, 1.0f);
    const float p_release = get_param(20, 0.05f);
    const float p_volume  = get_param(21, 1.0f);
    const float p_detune  = get_param(25, 0.0f);
    const float p_xfade   = get_param(26, 0.0f);
    const bool  p_reverse = get_param(24, 0.0f) > 0.5f;

    constexpr float kInset      = 10.0f;
    constexpr float kTopBarH    = 36.0f;
    constexpr float kSidePanelW = 240.0f;

    const float surf_w = ctx->surface_width;
    const float surf_h = ctx->surface_height;

    const float top_x = kInset;
    const float top_y = kInset;
    const float top_w = surf_w - 2.0f * kInset;

    const float grid_x = kInset;
    const float grid_y = top_y + kTopBarH + kInset;
    const float grid_w = std::max(0.0f, surf_w - 3.0f * kInset - kSidePanelW);
    const float grid_h = std::max(0.0f, surf_h - grid_y - kInset);

    const float side_x = grid_x + grid_w + kInset;
    const float side_y = grid_y;
    const float side_w = kSidePanelW;
    const float side_h = grid_h;

    // --- Top bar ---
    vivid::draw_ui::draw_panel(d, o, top_x, top_y, top_w, kTopBarH,
        {th.dark_bg.r * 0.7f, th.dark_bg.g * 0.7f, th.dark_bg.b * 0.7f, 0.95f});
    if (d.draw_text) {
        d.draw_text(o, top_x + 10.0f, top_y + 9.0f, "SP-404",
            {th.bright_text.r, th.bright_text.g, th.bright_text.b, 0.85f}, 1.1f);
    }
    {
        static constexpr const char* kModeLabels[3] = {"one-shot", "loop", "gate"};
        const vivid::ui::Rect mode_r{top_x + 76.0f, top_y + 5.0f, 210.0f, kTopBarH - 10.0f};
        auto r = vivid::ui::ui_radio(*ctx, mode_r, kModeLabels, 3, p_mode);
        if (r.clicked) set_named("mode", static_cast<float>(r.value));
    }

    // --- Pad grid ---
    vivid::draw_ui::draw_panel(d, o, grid_x, grid_y, grid_w, grid_h,
        {th.dark_bg.r * 0.85f, th.dark_bg.g * 0.85f, th.dark_bg.b * 0.85f, 0.9f});

    // Row colors inspired by hardware SP-404 MK2.
    constexpr float kPadColors[4][3] = {
        {0.9f, 0.45f, 0.2f},  // A — warm orange
        {0.2f, 0.65f, 0.7f},  // B — teal
        {0.4f, 0.75f, 0.3f},  // C — lime
        {0.6f, 0.3f,  0.85f}, // D — violet
    };
    constexpr const char* kRowLabels[4] = {"A", "B", "C", "D"};

    constexpr float kPadGap = 6.0f;
    const float pad_w = std::max(0.0f, (grid_w - 5.0f * kPadGap) / 4.0f);
    const float pad_h = std::max(0.0f, (grid_h - 5.0f * kPadGap) / 4.0f);

    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 4; ++col) {
            const int idx = row * 4 + col;
            const float px = grid_x + kPadGap + col * (pad_w + kPadGap);
            const float py = grid_y + kPadGap + row * (pad_h + kPadGap);
            const bool has_file = !pad_display_names_[idx].empty();
            const bool sel = (selected_pad_ == idx);

            const float alpha = has_file ? 0.65f : 0.18f;
            const VividColor fill{kPadColors[row][0], kPadColors[row][1],
                                  kPadColors[row][2], alpha};
            const VividColor border = sel
                ? VividColor{th.bright_text.r, th.bright_text.g, th.bright_text.b, 0.95f}
                : VividColor{0.0f, 0.0f, 0.0f, 0.0f};
            vivid::draw_ui::draw_panel(d, o, px, py, pad_w, pad_h,
                fill, border, 5.0f, sel ? 2.0f : 0.0f);

            if (d.draw_text) {
                char label[4];
                std::snprintf(label, sizeof(label), "%s%d", kRowLabels[row], col + 1);
                d.draw_text(o, px + 5.0f, py + 4.0f, label,
                    {th.bright_text.r, th.bright_text.g, th.bright_text.b,
                     has_file ? 0.85f : 0.35f}, 0.85f);

                const std::string& name = pad_display_names_[idx];
                if (has_file) {
                    // Truncate sample name to fit
                    const char* n = name.c_str();
                    char trunc[20];
                    if (name.size() > 13) {
                        std::snprintf(trunc, sizeof(trunc), "%.12s…", n);
                        n = trunc;
                    }
                    d.draw_text(o, px + 5.0f, py + pad_h * 0.45f, n,
                        {th.bright_text.r, th.bright_text.g, th.bright_text.b, 0.8f}, 0.78f);
                } else {
                    d.draw_text(o, px + 5.0f, py + pad_h * 0.45f, "—",
                        {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.3f}, 0.85f);
                }
            }

            // Click to select
            const bool in_pad = ctx->mouse.x >= px && ctx->mouse.x < px + pad_w &&
                                 ctx->mouse.y >= py && ctx->mouse.y < py + pad_h;
            if (ctx->host.set_cursor && in_pad)
                ctx->host.set_cursor(ctx->host.opaque, VIVID_CURSOR_HAND);
            if (ctx->mouse.left_clicked && in_pad)
                selected_pad_ = idx;
        }
    }

    // --- Side panel ---
    constexpr float kSpPad = 10.0f;
    vivid::draw_ui::draw_panel(d, o, side_x, side_y, side_w, side_h,
        {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.9f},
        {th.separator.r, th.separator.g, th.separator.b, 0.5f}, 4.0f, 1.0f);

    // Selected pad header
    if (d.draw_text) {
        const int row = selected_pad_ / 4;
        const int col = selected_pad_ % 4;
        char hdr[8];
        std::snprintf(hdr, sizeof(hdr), "%s%d", kRowLabels[row], col + 1);
        d.draw_text(o, side_x + kSpPad, side_y + kSpPad, hdr,
            {th.bright_text.r, th.bright_text.g, th.bright_text.b, 0.95f}, 1.1f);

        const std::string& name = pad_display_names_[selected_pad_];
        d.draw_text(o, side_x + kSpPad, side_y + kSpPad + 18.0f,
            name.empty() ? "(no file)" : name.c_str(),
            name.empty()
                ? VividColor{th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.35f}
                : VividColor{th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.8f},
            0.85f);
    }
    if (d.draw_rect) {
        d.draw_rect(o, side_x + kSpPad, side_y + 52.0f,
            side_w - 2.0f * kSpPad, 1.0f,
            {th.separator.r, th.separator.g, th.separator.b, 0.5f});
    }

    // Param sliders
    auto sp_cur = vivid::ui::ui_layout(
        vivid::ui::Rect{side_x, side_y + 56.0f, side_w, side_h - 56.0f}, kSpPad, 4.0f);

    {
        auto row = vivid::ui::ui_row(sp_cur, 22.0f);
        auto r = vivid::ui::ui_slider_h(*ctx, row, "Attack",
            p_attack, 0.001f, 2.0f, &sp_attack_drag_);
        if (r.changed) set_named("attack", r.value);
    }
    {
        auto row = vivid::ui::ui_row(sp_cur, 22.0f);
        auto r = vivid::ui::ui_slider_h(*ctx, row, "Decay",
            p_decay, 0.01f, 2.0f, &sp_decay_drag_);
        if (r.changed) set_named("decay", r.value);
    }
    {
        auto row = vivid::ui::ui_row(sp_cur, 22.0f);
        auto r = vivid::ui::ui_slider_h(*ctx, row, "Sustain",
            p_sustain, 0.0f, 1.0f, &sp_sustain_drag_);
        if (r.changed) set_named("sustain", r.value);
    }
    {
        auto row = vivid::ui::ui_row(sp_cur, 22.0f);
        auto r = vivid::ui::ui_slider_h(*ctx, row, "Release",
            p_release, 0.001f, 10.0f, &sp_release_drag_);
        if (r.changed) set_named("release", r.value);
    }

    vivid::ui::ui_row(sp_cur, 6.0f); // spacer

    {
        auto row = vivid::ui::ui_row(sp_cur, 22.0f);
        auto r = vivid::ui::ui_slider_h(*ctx, row, "Volume",
            p_volume, 0.0f, 2.0f, &sp_volume_drag_);
        if (r.changed) set_named("volume", r.value);
    }
    {
        auto row = vivid::ui::ui_row(sp_cur, 22.0f);
        auto r = vivid::ui::ui_slider_h(*ctx, row, "Detune st",
            p_detune, -24.0f, 24.0f, &sp_detune_drag_);
        if (r.changed) set_named("detune_semitones", r.value);
    }
    {
        auto row = vivid::ui::ui_row(sp_cur, 22.0f);
        auto r = vivid::ui::ui_slider_h(*ctx, row, "Xfade ms",
            p_xfade, 0.0f, 100.0f, &sp_xfade_drag_);
        if (r.changed) set_named("loop_crossfade_ms", r.value);
    }

    vivid::ui::ui_row(sp_cur, 6.0f); // spacer

    {
        auto row = vivid::ui::ui_row(sp_cur, 26.0f);
        const VividColor fill_off{0.20f, 0.20f, 0.23f, 1.0f};
        const VividColor fill_on{0.75f, 0.35f, 0.22f, 1.0f};
        auto t = vivid::ui::ui_toggle(*ctx, row, "Reverse", p_reverse, fill_off, fill_on);
        if (t.clicked) set_named("reverse", p_reverse ? 0.0f : 1.0f);
    }

    // Hint at bottom
    if (d.draw_text) {
        d.draw_text(o, side_x + kSpPad, side_y + side_h - 18.0f,
            "Assign files via the inspector",
            {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.35f}, 0.75f);
    }
}

VIVID_DEFINE_OP(SP404) {
    display_name = "SP404";
    keywords     = {"sampler", "drum", "pad", "sample", "groovebox", "percussion", "sp404"};
    summary      = "16-pad sample-based drum machine with per-pad audio files and a dedicated grid editor.";
}

VIVID_EDITOR(SP404)
