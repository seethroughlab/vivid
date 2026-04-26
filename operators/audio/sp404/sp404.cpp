#include "operator_api/operator.h"
#include "operator_api/adsr.h"
#include "operator_api/note_types.h"
#include "operator_api/type_id.h"
#include "sample_bank.h"
#include "voice.h"
#include <atomic>

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

    Voice voices_[kMaxPads];
    GateTracker gate_tracker_;
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
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"gates",      VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});
        out.push_back({"notes",      VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});
        out.push_back({"velocities", VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT});
        out.push_back(VIVID_CUSTOM_REF_PORT("notes_in", VIVID_PORT_INPUT, VividNoteBuffer));
        out.push_back({"output",     VIVID_PORT_AUDIO_BUFFER,  VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2});
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
        LaneInput gates_in = read_lane_input(ctx->input_lanes, 0);
        LaneInput notes_in = read_lane_input(ctx->input_lanes, 1);
        LaneInput vels_in  = read_lane_input(ctx->input_lanes, 2);

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

        // Process gate edges
        uint32_t num_slots = std::min(gates_in.length, static_cast<uint32_t>(kMaxPads));
        for (uint32_t slot = 0; slot < num_slots; ++slot) {
            float gate = gates_in.data[slot];
            int edge = gate_tracker_.detect(static_cast<int>(slot), gate);

            if (edge == 1) {
                // Rising edge — note on
                int note = 60;
                if (slot < notes_in.length && notes_in.data)
                    note = static_cast<int>(notes_in.data[slot]);
                float vel = 1.0f;
                if (slot < vels_in.length && vels_in.data)
                    vel = vels_in.data[slot];

                const SampleRegion* region = find_region(group, note, vel);
                if (!region || !region->data) {
                    region = find_nearest_region(group, note);
                    if (!region || !region->data) continue;
                }

                // Find voice: reuse one already playing this note, or allocate
                int vi = -1;
                for (int j = 0; j < kMaxPads; ++j) {
                    if (voices_[j].active && voices_[j].note == note) {
                        vi = j;
                        break;
                    }
                }
                if (vi < 0) vi = find_free_voice(voices_, kMaxPads);
                if (vi < 0) vi = steal_oldest_voice(voices_, kMaxPads);

                double rate = static_cast<double>(region->data->sample_rate) /
                              static_cast<double>(ctx->sample_rate);
                bool one_shot = (p_mode == 0);

                voice_note_on(voices_[vi], note, vel, region, rate,
                              frame_counter_, one_shot);
            } else if (edge == -1) {
                // Falling edge — note off
                int note = 60;
                if (slot < notes_in.length && notes_in.data)
                    note = static_cast<int>(notes_in.data[slot]);

                for (int j = 0; j < kMaxPads; ++j) {
                    if (voices_[j].active && voices_[j].note == note) {
                        voice_note_off(voices_[j]);
                        break;
                    }
                }
            }
        }

        gate_tracker_.update(gates_in.data, gates_in.length);

        // Render audio
        for (uint32_t s = 0; s < ctx->buffer_size; ++s) {
            float out_L = 0.0f;
            float out_R = 0.0f;

            for (int v = 0; v < kMaxPads; ++v) {
                if (!voices_[v].active) continue;
                voice_render_frame(voices_[v], out_L, out_R, dt,
                                   p_attack, p_decay, p_sustain, p_release);

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
            ctx->output_buffers[0][ctx->buffer_size + s]   = out_R;  // channel 1

            frame_counter_++;
        }
    }
};

VIVID_REGISTER(SP404)
