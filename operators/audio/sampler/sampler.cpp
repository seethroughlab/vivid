#include "operator_api/operator.h"
#include "operator_api/adsr.h"
#include "operator_api/adsr_inspector.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "sample_bank.h"
#include "voice.h"
#include "voice_breakouts.h"
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

using namespace vivid_sampler;

/**
 * @brief Multi-group polyphonic sample player with ADSR and velocity control.
 *
 * Loads audio files organized into groups, each with configurable ADSR,
 * volume, and voice count. Supports velocity-sensitive playback and
 * keytracking across up to 16 simultaneous voices.
 *
 * @param group Selects which sample group to play from the loaded bank.
 * @param voices Maximum polyphony (1-16).
 * @see Slicer, SP404, MidiInput
 */
struct Sampler : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName = "Sampler";
    static constexpr bool kTimeDependent = false;
    static constexpr int kMaxVoices = 16;

    vivid::Param<vivid::FilePath> file   {"file"};
    vivid::Param<float> attack  {"attack",  0.0f, 0.0f, 2.0f};
    vivid::Param<float> decay   {"decay",   0.0f, 0.0f, 2.0f};
    vivid::Param<float> sustain {"sustain", 0.0f, 0.0f, 1.0f};
    vivid::Param<float> release {"release", 0.0f, 0.0f, 10.0f};
    vivid::Param<float> volume  {"volume",  1.0f, 0.0f, 2.0f};
    vivid::Param<int>   voices  {"voices",  8, 1, 16};
    vivid::Param<int>   group   {"group",   0, 0, 31};
    // Per-note expression depth (Phase 5). Pressure scales per-voice
    // amplitude; timbre detunes per-voice playback rate (signed semitones).
    // Default depth=12 semitones at full timbre = ±1 octave glide.
    vivid::Param<float> pressure_to_amp     {"pressure_to_amp",     0.5f,  0.0f, 1.0f};
    vivid::Param<float> timbre_to_pitch     {"timbre_to_pitch",    12.0f, -24.0f, 24.0f};

    Voice voices_[kMaxVoices];
    std::atomic<SampleBank*> bank_{nullptr};
    SampleBank* deferred_delete_ = nullptr;
    std::string last_path_;
    uint64_t frame_counter_ = 0;

    Sampler() {
        vivid::description(file, "Audio file or sample bank to load");
        vivid::description(attack, "Envelope attack time in seconds (0 = use sample default)");
        vivid::description(decay, "Envelope decay time in seconds (0 = use sample default)");
        vivid::description(sustain, "Envelope sustain level (0 = use sample default)");
        vivid::description(release, "Envelope release time in seconds (0 = use sample default)");
        vivid::description(volume, "Master output volume, can boost up to 2x");
        vivid::description(voices, "Maximum number of simultaneous notes (1-16)");
        vivid::description(group, "Which sample group to play from the loaded bank");
    }

    ~Sampler() {
        delete bank_.load(std::memory_order_relaxed);
        delete deferred_delete_;
    }

    void draw_inspector(VividInspectorContext* ctx) override {
        // Param order: file=0, attack=1, decay=2, sustain=3, release=4
        float a = (ctx->param_count > 1) ? ctx->param_values[1] : 0.0f;
        float d = (ctx->param_count > 2) ? ctx->param_values[2] : 0.0f;
        float s = (ctx->param_count > 3) ? ctx->param_values[3] : 0.0f;
        float r = (ctx->param_count > 4) ? ctx->param_values[4] : 0.0f;
        bool bypassed = (a == 0.0f && d == 0.0f && s == 0.0f && r == 0.0f);
        vivid::adsr_inspector::draw(ctx, a, d, s, r, bypassed);
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&file);
        out.push_back(&attack);
        out.push_back(&decay);
        out.push_back(&sustain);
        out.push_back(&release);
        out.push_back(&volume);
        out.push_back(&voices);
        out.push_back(&group);
        out.push_back(&pressure_to_amp);
        out.push_back(&timbre_to_pitch);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_in", VIVID_PORT_INPUT, VividNoteBuffer));
        out.push_back({"output",     VIVID_PORT_AUDIO_BUFFER,  VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2});
        // Per-voice advanced breakouts. voices_out is mono-per-voice
        // (kMaxVoices channels) populated in active-note order sorted by
        // note_id; the four control lanes share the same ordering.
        out.push_back({"voices_out",       VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT,
                       VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr,
                       16, 0.0f}); // kMaxVoices channels
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

    void prepare_instance_assets() override {
        refresh_sample_bank();
    }

    void main_thread_update(double /*time*/) override {
        refresh_sample_bank();
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

        // Read params
        float p_attack  = attack.value;
        float p_decay   = decay.value;
        float p_sustain = sustain.value;
        float p_release = release.value;
        float p_volume  = volume.value;
        int   p_voices  = voices.int_value();
        int   p_group   = group.int_value();
        float dt        = 1.0f / static_cast<float>(ctx->sample_rate);

        // Select group
        int group_idx = std::max(0, std::min(p_group, static_cast<int>(bank->groups.size()) - 1));
        const SampleGroup& active_group = bank->groups[group_idx];

        // ADSR override: 0 means use the group's value
        float env_attack  = (p_attack  > 0.0f) ? p_attack  : active_group.attack;
        float env_decay   = (p_decay   > 0.0f) ? p_decay   : active_group.decay;
        float env_sustain = (p_sustain > 0.0f) ? p_sustain : active_group.sustain;
        float env_release = (p_release > 0.0f) ? p_release : active_group.release;

        // Configurable polyphony
        int max_voices = std::max(1, std::min(p_voices, kMaxVoices));

        // Process native note input. Voice slot lookup is by note_id (not by
        // note number) so same-pitch overlapping notes get distinct slots.
        if (ctx->custom_inputs && ctx->custom_input_count > 0 && ctx->custom_inputs[0]) {
            auto* notes = static_cast<const VividNoteBuffer*>(ctx->custom_inputs[0]);
            for (uint32_t m = 0; m < notes->count; ++m) {
                const auto& ev = notes->events[m];
                if (ev.note_id == 0) continue;  // global stream

                if (ev.type == VIVID_NOTE_ON) {
                    int note = ev.note_number;
                    float vel = ev.value;

                    const SampleRegion* region = find_region(active_group, note, vel);
                    if (!region || !region->data) {
                        region = find_nearest_region(active_group, note);
                        if (!region || !region->data) continue;
                    }

                    // Find a slot already holding this note_id (only happens
                    // on emitter bugs); otherwise allocate free or steal.
                    int vi = -1;
                    for (int j = 0; j < max_voices; ++j) {
                        if (voices_[j].active && voices_[j].note_id == ev.note_id) {
                            vi = j; break;
                        }
                    }
                    if (vi < 0) vi = find_free_voice(voices_, max_voices);
                    if (vi < 0) vi = steal_oldest_voice(voices_, max_voices);

                    double semitone_diff = static_cast<double>(note - region->root_note) +
                                           (region->tune_cents / 100.0);
                    double pitch_rate = std::pow(2.0, semitone_diff / 12.0);
                    double rate = pitch_rate * (static_cast<double>(region->data->sample_rate) /
                                                static_cast<double>(ctx->sample_rate));

                    voice_note_on(voices_[vi], note, vel, region, rate,
                                  frame_counter_, false);
                    voices_[vi].note_id = ev.note_id;
                    voices_[vi].pitch_bend_semis = 0.0f;
                    voices_[vi].pressure         = 0.0f;
                    voices_[vi].timbre           = 0.0f;
                } else if (ev.type == VIVID_NOTE_OFF) {
                    for (int j = 0; j < max_voices; ++j) {
                        if (voices_[j].active && voices_[j].note_id == ev.note_id) {
                            voice_note_off(voices_[j]);
                            break;
                        }
                    }
                } else if (ev.type == VIVID_NOTE_PITCH_BEND) {
                    for (int j = 0; j < max_voices; ++j) {
                        if (voices_[j].active && voices_[j].note_id == ev.note_id) {
                            voices_[j].pitch_bend_semis = ev.value;
                            break;
                        }
                    }
                } else if (ev.type == VIVID_NOTE_PRESSURE) {
                    for (int j = 0; j < max_voices; ++j) {
                        if (voices_[j].active && voices_[j].note_id == ev.note_id) {
                            voices_[j].pressure = ev.value;
                            break;
                        }
                    }
                } else if (ev.type == VIVID_NOTE_TIMBRE) {
                    for (int j = 0; j < max_voices; ++j) {
                        if (voices_[j].active && voices_[j].note_id == ev.note_id) {
                            voices_[j].timbre = ev.value;
                            break;
                        }
                    }
                }
            }
        }

        // Active-voice ordering for the breakout surface: sort active slots
        // by note_id. voices_out channel `pos` mirrors the voice at sorted
        // rank `pos`; the four voice_* lanes use the same ordering. Cap at
        // 64 (no synth has more — emit_voice_breakouts has the same bound).
        constexpr int kMaxSortedVoices = 64;
        int slot_to_pos[kMaxVoices];
        int sorted[kMaxSortedVoices];
        int active_count = 0;
        for (int v = 0; v < kMaxVoices; ++v) {
            slot_to_pos[v] = -1;
            if (v < kMaxSortedVoices && voices_[v].active)
                sorted[active_count++] = v;
        }
        std::sort(sorted, sorted + active_count,
                  [this](int a, int b) {
                      return voices_[a].note_id < voices_[b].note_id;
                  });
        for (int i = 0; i < active_count; ++i) slot_to_pos[sorted[i]] = i;

        // Zero voices_out (port 1) so unused channels are silent.
        const uint32_t frames = ctx->buffer_size;
        float* voices_out_buf = (ctx->output_buffers && ctx->output_buffers[1])
                                ? ctx->output_buffers[1] : nullptr;
        if (voices_out_buf) {
            std::memset(voices_out_buf, 0,
                        static_cast<size_t>(kMaxVoices) * frames * sizeof(float));
        }

        // Per-note expression (Phase 5): pressure scales per-voice gain;
        // timbre detunes playback rate by ±timbre_to_pitch semitones.
        const float p_amp_depth     = pressure_to_amp.value;
        const float t_pitch_semis   = timbre_to_pitch.value;

        // Render audio
        for (uint32_t s = 0; s < frames; ++s) {
            float out_L = 0.0f;
            float out_R = 0.0f;

            for (int v = 0; v < max_voices; ++v) {
                if (!voices_[v].active) continue;
                const auto& slot = voices_[v];
                const float gain_scale = 1.0f + p_amp_depth * slot.pressure;
                const float rate_scale = std::pow(
                    2.0f, (t_pitch_semis * slot.timbre) / 12.0f);
                float voice_L = 0.0f;
                float voice_R = 0.0f;
                voice_render_frame(voices_[v], voice_L, voice_R, dt,
                                   env_attack, env_decay, env_sustain, env_release,
                                   rate_scale, gain_scale);
                out_L += voice_L;
                out_R += voice_R;

                // Mirror to voices_out as a mono mix of L+R, applying the
                // master volume so breakout audio matches the stereo mix.
                if (voices_out_buf && slot_to_pos[v] >= 0) {
                    const int pos = slot_to_pos[v];
                    voices_out_buf[pos * frames + s] = (voice_L + voice_R) * 0.5f * p_volume;
                }
            }

            out_L *= p_volume;
            out_R *= p_volume;

            ctx->output_buffers[0][s]                      = out_L;
            ctx->output_buffers[0][frames + s]             = out_R;

            frame_counter_++;
        }

        // Emit voice_*/voices_out aligned to active-note-by-note_id order.
        // ctx->value_outputs[] is indexed by overall OUTPUT port position.
        // Output port order: output(0), voices_out(1), voice_ids(2),
        // voice_gates(3), voice_velocities(4), voice_freqs(5).
        //
        // Inlined the value-API equivalent of
        // vivid_sequencers::emit_voice_breakouts_from_sorted: the shared
        // helper is still lane-API-only (VividLaneOutput), so we replicate its
        // four-lane emission here over the same sorted/active_count ordering to
        // keep behavior byte-identical.
        if (ctx->value_outputs) {
            const uint32_t n = static_cast<uint32_t>(active_count);
            auto emit_lane = [&](int port, auto value_for_slot) {
                VividValueOutput* out = &ctx->value_outputs[port];
                float* buf = vivid_value_output_floats(out, n);
                if (buf) {
                    for (int i = 0; i < active_count; ++i) {
                        buf[i] = value_for_slot(
                            static_cast<const vivid::VoiceSlot&>(
                                voices_[sorted[i]]));
                    }
                }
                vivid_value_output_commit(out, n);
            };
            emit_lane(2, [](const vivid::VoiceSlot& s) {
                return static_cast<float>(s.note_id);
            });
            emit_lane(3, [](const vivid::VoiceSlot& s) {
                return s.gate ? 1.0f : 0.0f;
            });
            emit_lane(4, [](const vivid::VoiceSlot& s) {
                return s.velocity;
            });
            emit_lane(5, [](const vivid::VoiceSlot& s) {
                return vivid_sequencers::voice_freq_hz(s);
            });
        }
    }

    void refresh_sample_bank() {
        // Old banks are always retired on the caller's thread. During async
        // prepare_instance_assets() there is no live audio thread yet, and
        // main_thread_update() preserves the existing safe handoff behavior.
        delete deferred_delete_;
        deferred_delete_ = nullptr;

        const std::string& path = file.str_value;
        if (path == last_path_) return;
        last_path_ = path;

        SampleBank* new_bank = path.empty() ? nullptr : load_sample_bank(path);
        SampleBank* old = bank_.exchange(new_bank, std::memory_order_acq_rel);
        deferred_delete_ = old;
    }
};

static const char* kSamplerDropExts[] = {".wav"};
static const VividFileDropHandlerDescriptor kSamplerFileDrops[] = {{
    "Load Sample",
    kSamplerDropExts,
    1,
    "file",
    50,
    "Create a Sampler node from a dropped WAV file.",
}};

VIVID_DEFINE_OP(Sampler) {
}

VIVID_FILE_DROP(kSamplerFileDrops)
VIVID_INSPECTOR(Sampler)
