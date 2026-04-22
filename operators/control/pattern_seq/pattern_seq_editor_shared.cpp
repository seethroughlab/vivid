#include "pattern_seq_editor_shared.h"

namespace vivid::pattern_seq_editor {

std::string param_name_for(int step) {
    return std::string("val_") + std::to_string(step);
}

namespace {

std::uint32_t xorshift32(std::uint32_t state) {
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

} // namespace

void fill_ramp_up(float* out, int num_steps) {
    if (!out || num_steps <= 0) return;
    if (num_steps == 1) { out[0] = 0.0f; return; }
    for (int i = 0; i < num_steps; ++i) {
        const float t = static_cast<float>(i) /
                        static_cast<float>(num_steps - 1);  // 0..1
        out[i] = kValueMin + t * (kValueMax - kValueMin);
    }
}

void fill_ramp_down(float* out, int num_steps) {
    if (!out || num_steps <= 0) return;
    if (num_steps == 1) { out[0] = 0.0f; return; }
    for (int i = 0; i < num_steps; ++i) {
        const float t = static_cast<float>(i) /
                        static_cast<float>(num_steps - 1);
        out[i] = kValueMax - t * (kValueMax - kValueMin);
    }
}

void fill_zero(float* out, int num_steps) {
    if (!out || num_steps <= 0) return;
    for (int i = 0; i < num_steps; ++i) out[i] = 0.0f;
}

void fill_random(float* out, int num_steps, std::uint32_t seed) {
    if (!out || num_steps <= 0) return;
    if (seed == 0) seed = 0xA5C3CAFEu;
    for (int i = 0; i < num_steps; ++i) {
        seed = xorshift32(seed);
        const float norm = static_cast<float>(seed) /
                           static_cast<float>(0xFFFFFFFFu);   // 0..1
        const float bipolar = norm * 2.0f - 1.0f;             // -1..+1
        out[i] = bipolar * kValueMax;
    }
}

} // namespace vivid::pattern_seq_editor
