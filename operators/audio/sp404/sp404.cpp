#include "operator_api/operator.h"
#include "operator_api/adsr.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "sample_bank.h"
#include "voice.h"
#include "voice_breakouts.h"
#include <algorithm>
#include <atomic>
#include <cstring>

using namespace vivid_sampler;

/**
 * @brief Single-pad sampler with one-shot, loop, and gate playback.
 *
 * Plays the first group from a sample bank file with configurable ADSR
 * and play mode. Designed for simple trigger-and-play use cases like
 * drum pads.
 *
 * @see Sampler, Slicer
 */
struct SP404 : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "SP404";
    static constexpr bool kTimeDependent = false;
    static constexpr int kMaxPads = 16;

    vivid::Param<vivid::FilePath> file   {"file"};
    vivid::Param<int>   mode    {"mode",    0, {"one_shot", "loop", "gate"}};
    vivid::Param<float> attack  {"attack",  0.001f, 0.001f, 2.0f};
    vivid::Param<float> decay   {"decay",   0.1f,   0.01f,  2.0f};
    vivid::Param<float> sustain {"sustain", 1.0f,   0.0f,   1.0f};
    vivid::Param<float> release {"release", 0.05f,  0.001f, 10.0f};
    vivid::Param<float> volume  {"volume",  1.0f,   0.0f,   2.0f};
    // Per-note expression depth (Phase 5).
    vivid::Param<float> pressure_to_amp {"pressure_to_amp", 0.5f,  0.0f, 1.0f};
    vivid::Param<float> timbre_to_pitch {"timbre_to_pitch", 12.0f, -24.0f, 24.0f};

    Voice voices_[kMaxPads];
    std::atomic<SampleBank*> bank_{nullptr};
    SampleBank* deferred_delete_ = nullptr;
    std::string last_path_;
    uint64_t frame_counter_ = 0;

    SP404() {
        vivid::description(file, "Sample bank file to load");
        vivid::description(mode, "Playback mode: one_shot plays once, loop repeats, gate sustains while held");
        vivid::description(attack, "Envelope attack time in seconds");
        vivid::description(decay, "Envelope decay time in seconds");
        vivid::description(sustain, "Envelope sustain level (0-1)");
        vivid::description(release, "Envelope release time in seconds");
        vivid::description(volume, "Master output volume, can boost up to 2x");
    }

    ~SP404() {
        delete bank_.load(std::memory_order_relaxed);
        delete deferred_delete_;
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&file);
        out.push_back(&mode);
        out.push_back(&attack);
        out.push_back(&decay);
        out.push_back(&sustain);
        out.push_back(&release);
        out.push_back(&volume);
        out.push_back(&pressure_to_amp);
        out.push_back(&timbre_to_pitch);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_in", VIVID_PORT_INPUT, VividNoteBuffer));
        out.push_back({"output",     VIVID_PORT_AUDIO_BUFFER,  VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2});
        // Per-voice advanced breakouts (kMaxPads channels mono each).
        out.push_back({"voices_out",       VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT,
                       VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr,
                       16, 0.0f}); // kMaxPads channels
        vivid::advanced_breakout(out.back());
        out.push_back({"voice_ids",        VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
        vivid::advanced_breakout(out.back());
        out.push_back({"voice_gates",      VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
        vivid::advanced_breakout(out.back());
        out.push_back({"voice_velocities", VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
        vivid::advanced_breakout(out.back());
        out.push_back({"voice_freqs",      VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT});
        vivid::advanced_breakout(out.back());
        vivid::append_analysis_ports(out);
    }

    void main_thread_update(double /*time*/) override {
        // Deferred delete of old bank (safe — audio thread has moved on)
        delete deferred_delete_;
        deferred_delete_ = nullptr;

        const std::string& path = file.str_value;
        if (path == last_path_) return;
        last_path_ = path;

        if (path.empty()) {
            SampleBank* old = bank_.exchange(nullptr, std::memory_order_acq_rel);
            deferred_delete_ = old;
            return;
        }

        SampleBank* new_bank = load_sample_bank(path);
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

        // Read lane inputs

        // Read params
        int   p_mode    = mode.int_value();
        float p_attack  = attack.value;
        float p_decay   = decay.value;
        float p_sustain = sustain.value;
        float p_release = release.value;
        float p_volume  = volume.value;
        float dt        = 1.0f / static_cast<float>(ctx->sample_rate);

        const SampleGroup& group = bank->groups[0];

        // Process native note input. Voice slot lookup is by note_id.
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
                    bool one_shot = (p_mode == 0);

                    voice_note_on(voices_[vi], note, vel, region, rate,
                                  frame_counter_, one_shot);
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
                // SP404 doesn't currently route pressure or timbre — ignore.
            }
        }

        // Build active-voice ordering for the breakout surface.
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

        // Per-note expression (Phase 5): pressure scales gain;
        // timbre detunes playback rate by ±timbre_to_pitch semitones.
        const float p_amp_depth   = pressure_to_amp.value;
        const float t_pitch_semis = timbre_to_pitch.value;

        // Render audio
        for (uint32_t s = 0; s < frames; ++s) {
            float out_L = 0.0f;
            float out_R = 0.0f;

            for (int v = 0; v < kMaxPads; ++v) {
                if (!voices_[v].active) continue;
                const auto& slot = voices_[v];
                const float gain_scale = 1.0f + p_amp_depth * slot.pressure;
                const float rate_scale = std::pow(
                    2.0f, (t_pitch_semis * slot.timbre) / 12.0f);
                float voice_L = 0.0f;
                float voice_R = 0.0f;
                voice_render_frame(voices_[v], voice_L, voice_R, dt,
                                   p_attack, p_decay, p_sustain, p_release,
                                   rate_scale, gain_scale);
                out_L += voice_L;
                out_R += voice_R;

                if (voices_out_buf && slot_to_pos[v] >= 0) {
                    const int pos = slot_to_pos[v];
                    voices_out_buf[pos * frames + s] = (voice_L + voice_R) * 0.5f * p_volume;
                }

                // Loop mode: if voice just deactivated due to end-of-sample
                // but envelope is still active, wrap playback to start
                if (p_mode == 1 && !voices_[v].active &&
                    voices_[v].envelope.stage != vivid::adsr::IDLE) {
                    voices_[v].active = true;
                    voices_[v].playback_pos = 0.0;
                }
            }

            out_L *= p_volume;
            out_R *= p_volume;

            ctx->output_buffers[0][s]                      = out_L;  // channel 0
            ctx->output_buffers[0][frames + s]             = out_R;  // channel 1

            frame_counter_++;
        }

        // Emit voice_*/voices_out aligned to active-note-by-note_id order.
        // ctx->output_lanes[] is indexed by overall OUTPUT port position.
        // Output port order: output(0), voices_out(1), voice_ids(2),
        // voice_gates(3), voice_velocities(4), voice_freqs(5).
        if (ctx->output_lanes) {
            VividLaneOutput lanes[vivid_sequencers::kVoiceBreakoutLaneCount] = {
                ctx->output_lanes[2], ctx->output_lanes[3],
                ctx->output_lanes[4], ctx->output_lanes[5],
            };
            vivid_sequencers::emit_voice_breakouts_from_sorted(
                voices_, sorted, active_count, lanes);
        }
    }
};

VIVID_DEFINE_OP(SP404) {
}

VIVID_REGISTER(SP404)
