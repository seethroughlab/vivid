#pragma once
// Click-free ADSR amplitude envelope. Ported verbatim from vivid-classic
// (src/operator_api/adsr.h) — a tiny, dependency-free state machine shared by
// the sample-playback voice engine (see voice.h).
#include <algorithm>

namespace vivid {
namespace adsr {

enum Stage { IDLE, ATTACK, DECAY, SUSTAIN, RELEASE };

struct State {
    Stage stage        = IDLE;
    float env_value    = 0.0f;
    float env_progress = 0.0f;
    float release_start = 0.0f;

    bool is_active() const { return stage != IDLE; }
};

inline float compute(const State& s, float sustain) {
    switch (s.stage) {
        case ATTACK:  return s.env_progress;
        case DECAY:   return 1.0f - s.env_progress * (1.0f - sustain);
        case SUSTAIN: return sustain;
        case RELEASE: return s.release_start * (1.0f - s.env_progress);
        case IDLE:
        default:      return 0.0f;
    }
}

inline void advance(State& s, float dt, float attack, float decay,
                    float sustain, float release) {
    if (s.stage == IDLE) return;

    switch (s.stage) {
        case ATTACK:
            s.env_progress += dt / std::max(0.001f, attack);
            if (s.env_progress >= 1.0f) { s.env_progress = 0.0f; s.stage = DECAY; }
            break;
        case DECAY:
            s.env_progress += dt / std::max(0.001f, decay);
            if (s.env_progress >= 1.0f) { s.env_progress = 0.0f; s.stage = SUSTAIN; }
            break;
        case SUSTAIN:
            break;
        case RELEASE:
            s.env_progress += dt / std::max(0.001f, release);
            if (s.env_progress >= 1.0f) { s.stage = IDLE; s.env_value = 0.0f; }
            break;
        default:
            break;
    }

    s.env_value = compute(s, sustain);
}

inline void gate_on(State& s) {
    // Retrigger from the current env_value instead of jumping to 0. During ATTACK,
    // compute() returns env_progress, so seeding env_progress with env_value makes
    // the new attack ramp begin at the current level and reach 1.0 with the same
    // slope as a fresh attack — no discontinuity, no click on retrigger.
    s.stage = ATTACK;
    s.env_progress = s.env_value;
    s.release_start = 0.0f;
}

inline void gate_off(State& s) {
    if (s.stage != IDLE && s.stage != RELEASE) {
        s.release_start = s.env_value;
        s.stage = RELEASE;
        s.env_progress = 0.0f;
    }
}

} // namespace adsr
} // namespace vivid
