#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include <cmath>
#include <cstring>
#include <algorithm>

struct SpreadADSR : vivid::OperatorBase {
    static constexpr const char* kName   = "SpreadADSR";
    static constexpr VividDomain kDomain = VIVID_DOMAIN_AUDIO;
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> attack  {"attack",   0.01f,  0.001f, 2.0f};
    vivid::Param<float> decay   {"decay",    0.2f,   0.01f,  2.0f};
    vivid::Param<float> sustain {"sustain",  0.7f,   0.0f,   1.0f};
    vivid::Param<float> release {"release",  0.3f,   0.001f, 10.0f};

    static constexpr int kMaxSlots = 16;

    enum Stage { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };

    struct SlotState {
        Stage stage = IDLE;
        float env_value = 0.0f;
        float env_progress = 0.0f;
        float release_start = 0.0f;
    };

    SlotState slots_[kMaxSlots] = {};
    float prev_gates_[kMaxSlots] = {};
    uint32_t prev_len_ = 0;

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&attack);
        out.push_back(&decay);
        out.push_back(&sustain);
        out.push_back(&release);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"gates",     VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_INPUT});
        out.push_back({"envelopes", VIVID_PORT_CONTROL_SPREAD, VIVID_PORT_OUTPUT});
    }

    static float compute_envelope(const SlotState& s, float sustain_level) {
        switch (s.stage) {
            case ATTACK:
                return s.env_progress;
            case DECAY:
                return 1.0f - s.env_progress * (1.0f - sustain_level);
            case SUSTAIN:
                return sustain_level;
            case RELEASE:
                return s.release_start * (1.0f - s.env_progress);
            case IDLE:
            default:
                return 0.0f;
        }
    }

    static void advance_envelope(SlotState& s, float dt, float attack_t, float decay_t,
                                 float sustain_level, float release_t) {
        if (s.stage == IDLE) return;

        switch (s.stage) {
            case ATTACK: {
                s.env_progress += dt / std::max(0.001f, attack_t);
                if (s.env_progress >= 1.0f) {
                    s.env_progress = 0.0f;
                    s.stage = DECAY;
                }
                break;
            }
            case DECAY: {
                s.env_progress += dt / std::max(0.001f, decay_t);
                if (s.env_progress >= 1.0f) {
                    s.env_progress = 0.0f;
                    s.stage = SUSTAIN;
                }
                break;
            }
            case SUSTAIN:
                break;
            case RELEASE: {
                s.env_progress += dt / std::max(0.001f, release_t);
                if (s.env_progress >= 1.0f) {
                    s.stage = IDLE;
                    s.env_value = 0.0f;
                }
                break;
            }
            default:
                break;
        }

        s.env_value = compute_envelope(s, sustain_level);
    }

    void process(const VividProcessContext* ctx) override {
        auto* audio = vivid_audio(ctx);
        if (!audio) return;

        float att = attack.value;
        float dec = decay.value;
        float sus = sustain.value;
        float rel = release.value;
        float dt = 1.0f / static_cast<float>(audio->sample_rate);
        uint32_t frames = audio->buffer_size;

        // Read gates spread input
        uint32_t len = 0;
        const float* gate_data = nullptr;
        if (ctx->input_spreads) {
            const auto& gates_sp = ctx->input_spreads[0];
            len = std::min(gates_sp.length, static_cast<uint32_t>(kMaxSlots));
            gate_data = gates_sp.data;
        }

        // Detect gate edges
        for (uint32_t i = 0; i < len; ++i) {
            float cur_gate = gate_data ? gate_data[i] : 0.0f;
            float prev_gate = (i < prev_len_) ? prev_gates_[i] : 0.0f;

            bool gate_on  = (cur_gate > 0.5f) && (prev_gate <= 0.5f);
            bool gate_off = (cur_gate <= 0.5f) && (prev_gate > 0.5f);

            if (gate_on) {
                slots_[i].stage = ATTACK;
                slots_[i].env_progress = 0.0f;
            } else if (gate_off) {
                if (slots_[i].stage != IDLE) {
                    slots_[i].release_start = slots_[i].env_value;
                    slots_[i].stage = RELEASE;
                    slots_[i].env_progress = 0.0f;
                }
            }

            prev_gates_[i] = cur_gate;
        }

        // Handle disappeared slots (spread shrank → release those slots)
        for (uint32_t i = len; i < prev_len_; ++i) {
            if (prev_gates_[i] > 0.5f && slots_[i].stage != IDLE && slots_[i].stage != RELEASE) {
                slots_[i].release_start = slots_[i].env_value;
                slots_[i].stage = RELEASE;
                slots_[i].env_progress = 0.0f;
            }
            prev_gates_[i] = 0.0f;
        }

        prev_len_ = len;

        // Advance envelopes per-sample for all active slots
        uint32_t max_slot = std::max(len, prev_len_);
        if (max_slot > static_cast<uint32_t>(kMaxSlots)) max_slot = kMaxSlots;

        for (uint32_t s = 0; s < frames; ++s) {
            for (uint32_t i = 0; i < static_cast<uint32_t>(kMaxSlots); ++i) {
                advance_envelope(slots_[i], dt, att, dec, sus, rel);
            }
        }

        // Write output spread: envelope values for active slots
        // Output length matches gate input length (even released slots beyond len
        // will still be included if they are active)
        if (ctx->output_spreads) {
            auto& env_sp = ctx->output_spreads[0];
            // Include all slots that are either gated or still active (releasing)
            uint32_t out_len = len;
            for (uint32_t i = len; i < static_cast<uint32_t>(kMaxSlots); ++i) {
                if (slots_[i].stage != IDLE) out_len = i + 1;
            }
            out_len = std::min(out_len, env_sp.capacity);
            env_sp.length = out_len;
            for (uint32_t i = 0; i < out_len; ++i) {
                env_sp.data[i] = slots_[i].env_value;
            }
        }
    }
};

VIVID_REGISTER(SpreadADSR)
