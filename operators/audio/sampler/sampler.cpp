#include "operator_api/operator.h"
#include "operator_api/adsr.h"
#include "operator_api/adsr_inspector.h"
#include "operator_api/midi_types.h"
#include "operator_api/type_id.h"
#include "sample_bank.h"
#include "voice.h"
#include <atomic>
#include <cmath>

using namespace vivid_sampler;

struct Sampler : vivid::AudioOperatorBase {
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

    Voice voices_[kMaxVoices];
    GateTracker gate_tracker_;
    std::atomic<SampleBank*> bank_{nullptr};
    SampleBank* deferred_delete_ = nullptr;
    std::string last_path_;
    uint64_t frame_counter_ = 0;

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
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"gates",      VIVID_PORT_SPREAD, VIVID_PORT_INPUT});
        out.push_back({"notes",      VIVID_PORT_SPREAD, VIVID_PORT_INPUT});
        out.push_back({"velocities", VIVID_PORT_SPREAD, VIVID_PORT_INPUT});
        out.push_back(VIVID_CUSTOM_REF_PORT("midi_in", VIVID_PORT_INPUT, VividMidiBuffer));
        out.push_back({"output",     VIVID_PORT_AUDIO,  VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2});
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

        // Read spread inputs
        SpreadInput gates_in = read_spread_input(ctx->input_spreads, 0);
        SpreadInput notes_in = read_spread_input(ctx->input_spreads, 1);
        SpreadInput vels_in  = read_spread_input(ctx->input_spreads, 2);

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

        // Process MIDI input
        if (ctx->custom_inputs && ctx->custom_input_count > 0 && ctx->custom_inputs[0]) {
            auto* midi = static_cast<const VividMidiBuffer*>(ctx->custom_inputs[0]);
            for (uint32_t m = 0; m < midi->count; ++m) {
                const auto& msg = midi->messages[m];
                uint8_t status = msg.status & 0xF0;

                if (status == 0x90 && msg.data2 > 0) {
                    int note = msg.data1;
                    float vel = msg.data2 / 127.0f;

                    const SampleRegion* region = find_region(active_group, note, vel);
                    if (!region || !region->data) {
                        region = find_nearest_region(active_group, note);
                        if (!region || !region->data) continue;
                    }

                    int vi = -1;
                    for (int j = 0; j < max_voices; ++j) {
                        if (voices_[j].active && voices_[j].note == note) {
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
                } else if (status == 0x80 || (status == 0x90 && msg.data2 == 0)) {
                    int note = msg.data1;
                    for (int j = 0; j < max_voices; ++j) {
                        if (voices_[j].active && voices_[j].note == note) {
                            voice_note_off(voices_[j]);
                            break;
                        }
                    }
                }
            }
        }

        // Process gate edges
        uint32_t num_slots = std::min(gates_in.length, static_cast<uint32_t>(kMaxVoices));
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

                const SampleRegion* region = find_region(active_group, note, vel);
                if (!region || !region->data) {
                    region = find_nearest_region(active_group, note);
                    if (!region || !region->data) continue;
                }

                // Find voice: reuse one already playing this note, or allocate
                int vi = -1;
                for (int j = 0; j < max_voices; ++j) {
                    if (voices_[j].active && voices_[j].note == note) {
                        vi = j;
                        break;
                    }
                }
                if (vi < 0) vi = find_free_voice(voices_, max_voices);
                if (vi < 0) vi = steal_oldest_voice(voices_, max_voices);

                // Pitch interpolation
                double semitone_diff = static_cast<double>(note - region->root_note) +
                                       (region->tune_cents / 100.0);
                double pitch_rate = std::pow(2.0, semitone_diff / 12.0);
                double rate = pitch_rate * (static_cast<double>(region->data->sample_rate) /
                                            static_cast<double>(ctx->sample_rate));

                voice_note_on(voices_[vi], note, vel, region, rate,
                              frame_counter_, false);
            } else if (edge == -1) {
                // Falling edge — note off (always trigger release)
                int note = 60;
                if (slot < notes_in.length && notes_in.data)
                    note = static_cast<int>(notes_in.data[slot]);

                for (int j = 0; j < max_voices; ++j) {
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

            for (int v = 0; v < max_voices; ++v) {
                if (!voices_[v].active) continue;
                voice_render_frame(voices_[v], out_L, out_R, dt,
                                   env_attack, env_decay, env_sustain, env_release);
            }

            out_L *= p_volume;
            out_R *= p_volume;

            ctx->output_buffers[0][s]                      = out_L;
            ctx->output_buffers[0][ctx->buffer_size + s]   = out_R;

            frame_counter_++;
        }
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

VIVID_REGISTER(Sampler)
VIVID_FILE_DROP(kSamplerFileDrops)
VIVID_INSPECTOR(Sampler)
