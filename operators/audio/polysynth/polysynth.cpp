#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "operator_api/adsr.h"
#include <cmath>
#include <cstring>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static constexpr double TWO_PI = 2.0 * M_PI;

namespace adsr = vivid::adsr;

struct Polysynth : vivid::OperatorBase {
    static constexpr const char* kName   = "Polysynth";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = true;

    vivid::Param<int>   waveform         {"waveform",         1,        {"sine", "saw", "square", "triangle"}};
    vivid::Param<float> attack           {"attack",           0.01f,    0.001f, 5.0f};
    vivid::Param<float> decay            {"decay",            0.1f,     0.001f, 5.0f};
    vivid::Param<float> sustain          {"sustain",          0.7f,     0.0f,   1.0f};
    vivid::Param<float> release          {"release",          0.3f,     0.001f, 10.0f};
    vivid::Param<float> amplitude        {"amplitude",        0.3f,     0.0f,   1.0f};
    vivid::Param<float> detune           {"detune",           0.0f,     0.0f,   50.0f};
    vivid::Param<float> filter_cutoff    {"filter_cutoff",    20000.0f, 20.0f,  20000.0f};
    vivid::Param<float> filter_env_depth {"filter_env_depth", 0.0f,     0.0f,   8.0f};
    vivid::Param<bool>  env_bypass       {"env_bypass",       false};

    static constexpr int kMaxVoices = 16;

    struct Voice {
        float note = 0;
        float velocity = 0;
        double phase = 0;
        uint64_t note_id = 0;
        int gate_slot = -1;
        float filter_state = 0.0f;
        double cached_freq = 0.0;
        adsr::State env;

        bool is_active() const { return env.is_active(); }
    };

    Voice voices_[kMaxVoices] = {};
    uint64_t note_counter_ = 0;

    // Previous spread inputs for gate edge detection
    float prev_gates_[kMaxVoices] = {};
    float prev_notes_[kMaxVoices] = {};
    uint32_t prev_spread_len_ = 0;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&waveform);
        out.push_back(&attack);
        out.push_back(&decay);
        out.push_back(&sustain);
        out.push_back(&release);
        out.push_back(&amplitude);
        out.push_back(&detune);
        out.push_back(&filter_cutoff);
        out.push_back(&filter_env_depth);
        out.push_back(&env_bypass);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"notes",      VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // 0
        out.push_back({"velocities", VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // 1
        out.push_back({"gates",      VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // 2
        out.push_back({"filter_env", VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // 3
        out.push_back({"pitch_mod",  VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // 4
        out.push_back({"amp_mod",    VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});   // 5
        out.push_back({"output",     VIVID_PORT_AUDIO_FLOAT,    VIVID_PORT_OUTPUT});  // out 0
        out.push_back({"envelopes",  VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT}); // out 1
    }

    // --- DSP helpers ---

    static float cents_to_ratio(float cents) {
        return std::pow(2.0f, cents / 1200.0f);
    }

    static float read_spread_slot(const VividSpreadPort* sp, int slot, float fallback = 0.0f) {
        if (sp && sp->data && slot >= 0 && static_cast<uint32_t>(slot) < sp->length)
            return sp->data[slot];
        return fallback;
    }

    static double generate_sample(double phase, int wave) {
        switch (wave) {
            case 0: // Sine
                return std::sin(phase);
            case 1: { // Saw (centered, bandlimited-ish)
                double t = phase / TWO_PI;
                return 2.0 * (t - std::floor(t + 0.5));
            }
            case 2: // Square
                return phase < M_PI ? 1.0 : -1.0;
            case 3: { // Triangle
                double t = phase / TWO_PI;
                return 4.0 * std::abs(t - std::floor(t + 0.5)) - 1.0;
            }
            default:
                return 0.0;
        }
    }

    int find_free_voice() const {
        for (int i = 0; i < kMaxVoices; ++i) {
            if (!voices_[i].is_active()) return i;
        }
        return -1;
    }

    int find_voice_to_steal() const {
        int steal_idx = -1;
        uint64_t oldest_id = UINT64_MAX;
        for (int i = 0; i < kMaxVoices; ++i) {
            if (voices_[i].is_active() && voices_[i].note_id < oldest_id) {
                oldest_id = voices_[i].note_id;
                steal_idx = i;
            }
        }
        return steal_idx;
    }

    int find_voice_by_note(float note) const {
        for (int i = 0; i < kMaxVoices; ++i) {
            if (voices_[i].is_active() && voices_[i].env.stage != adsr::RELEASE &&
                std::abs(voices_[i].note - note) < 0.5f) {
                return i;
            }
        }
        return -1;
    }

    void trigger_note_on(float note, float vel, int slot) {
        int vi = find_voice_by_note(note);
        if (vi < 0) {
            vi = find_free_voice();
            if (vi < 0) vi = find_voice_to_steal();
        }
        if (vi < 0) return;

        Voice& v = voices_[vi];
        v.note = note;
        v.velocity = vel;
        v.phase = 0.0;
        adsr::gate_on(v.env);
        v.note_id = ++note_counter_;
        v.gate_slot = slot;
        v.filter_state = 0.0f;
    }

    void trigger_note_off(float note) {
        int vi = find_voice_by_note(note);
        if (vi >= 0)
            adsr::gate_off(voices_[vi].env);
    }

    void update_gates(const VividProcessContext* ctx) {
        if (!ctx->input_spreads) return;

        const auto& notes_sp = ctx->input_spreads[0];
        const auto& vel_sp   = ctx->input_spreads[1];
        const auto& gates_sp = ctx->input_spreads[2];

        uint32_t len = gates_sp.length;
        if (len > static_cast<uint32_t>(kMaxVoices)) len = kMaxVoices;

        for (uint32_t i = 0; i < len; ++i) {
            float cur_gate = read_spread_slot(&gates_sp, static_cast<int>(i));
            float cur_note = read_spread_slot(&notes_sp, static_cast<int>(i));
            float cur_vel  = read_spread_slot(&vel_sp,   static_cast<int>(i), 0.8f);

            float prev_gate = (i < prev_spread_len_) ? prev_gates_[i] : 0.0f;
            float prev_note = (i < prev_spread_len_) ? prev_notes_[i] : 0.0f;

            bool on        = (cur_gate > 0.5f) && (prev_gate <= 0.5f);
            bool off       = (cur_gate <= 0.5f) && (prev_gate > 0.5f);
            bool retrigger = (cur_gate > 0.5f) && (prev_gate > 0.5f) &&
                             (std::abs(cur_note - prev_note) > 0.5f);

            if (on || retrigger)
                trigger_note_on(cur_note, cur_vel, static_cast<int>(i));
            else if (off)
                trigger_note_off(prev_note);

            prev_gates_[i] = cur_gate;
            prev_notes_[i] = cur_note;
        }

        // Handle slots that disappeared (spread got shorter)
        for (uint32_t i = len; i < prev_spread_len_; ++i) {
            if (prev_gates_[i] > 0.5f)
                trigger_note_off(prev_notes_[i]);
            prev_gates_[i] = 0.0f;
            prev_notes_[i] = 0.0f;
        }

        prev_spread_len_ = len;
    }

    void process(const VividProcessContext* ctx) override {
        auto* audio = vivid_audio(ctx);
        if (!audio) return;

        float* out = audio->output_buffers[0];
        uint32_t frames = audio->buffer_size;
        double sample_rate = static_cast<double>(audio->sample_rate);
        float dt = 1.0f / static_cast<float>(audio->sample_rate);

        int   wave           = waveform.int_value();
        float att            = attack.value;
        float dec            = decay.value;
        float sus            = sustain.value;
        float rel            = release.value;
        float amp            = amplitude.value;
        float det_cents      = detune.value;
        float filt_cutoff    = filter_cutoff.value;
        float filt_env_depth = filter_env_depth.value;

        // Read modulation spread inputs
        const VividSpreadPort* filter_env_sp = ctx->input_spreads ? &ctx->input_spreads[3] : nullptr;
        const VividSpreadPort* pitch_mod_sp  = ctx->input_spreads ? &ctx->input_spreads[4] : nullptr;
        const VividSpreadPort* amp_mod_sp    = ctx->input_spreads ? &ctx->input_spreads[5] : nullptr;

        bool bypass = env_bypass.value > 0.5f;

        update_gates(ctx);

        bool filter_active = (filt_cutoff < 19999.0f) || (filt_env_depth > 0.001f);
        float norm = 1.0f / std::sqrt(static_cast<float>(kMaxVoices));
        float detune_ratio = cents_to_ratio(det_cents);

        // Cache per-voice frequency (avoid std::pow per sample)
        for (int vi = 0; vi < kMaxVoices; ++vi) {
            Voice& v = voices_[vi];
            if (!v.is_active()) continue;
            float pitch_offset = read_spread_slot(pitch_mod_sp, v.gate_slot);
            double note_val = static_cast<double>(v.note + pitch_offset);
            v.cached_freq = 440.0 * std::pow(2.0, (note_val - 69.0) / 12.0)
                            * static_cast<double>(detune_ratio);
        }

        std::memset(out, 0, frames * sizeof(float));

        for (uint32_t s = 0; s < frames; ++s) {
            float mix = 0.0f;

            for (int vi = 0; vi < kMaxVoices; ++vi) {
                Voice& v = voices_[vi];
                if (!v.is_active()) continue;

                adsr::advance(v.env, dt, att, dec, sus, rel);
                if (!v.is_active()) continue;

                float sig = static_cast<float>(generate_sample(v.phase, wave));
                sig *= (bypass ? 1.0f : v.env.env_value) * v.velocity;
                sig *= read_spread_slot(amp_mod_sp, v.gate_slot, 1.0f);

                // Per-voice one-pole lowpass filter
                if (filter_active) {
                    float filt_env_val = read_spread_slot(filter_env_sp, v.gate_slot);
                    float mod_cutoff = filt_cutoff * std::pow(2.0f, filt_env_val * filt_env_depth);
                    if (mod_cutoff > static_cast<float>(audio->sample_rate) * 0.5f)
                        mod_cutoff = static_cast<float>(audio->sample_rate) * 0.5f;
                    float alpha = 2.0f * static_cast<float>(M_PI) * mod_cutoff
                                  / static_cast<float>(audio->sample_rate);
                    if (alpha > 1.0f) alpha = 1.0f;
                    if (alpha < 0.0f) alpha = 0.0f;
                    v.filter_state += alpha * (sig - v.filter_state);
                    sig = v.filter_state;
                }

                mix += sig;

                v.phase += v.cached_freq * TWO_PI / sample_rate;
                if (v.phase >= TWO_PI) v.phase -= TWO_PI;
            }

            out[s] = mix * amp * norm;
        }

        // Write per-voice envelope values to output spread
        if (ctx->output_spreads) {
            auto& env_sp = ctx->output_spreads[1]; // envelopes is output port 1
            uint32_t active_count = 0;
            for (int vi = 0; vi < kMaxVoices; ++vi) {
                if (voices_[vi].is_active()) {
                    if (active_count < env_sp.capacity)
                        env_sp.data[active_count] = voices_[vi].env.env_value;
                    active_count++;
                }
            }
            env_sp.length = std::min(active_count, env_sp.capacity);
        }
    }
};

VIVID_REGISTER(Polysynth)
