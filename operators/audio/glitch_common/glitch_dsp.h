#ifndef VIVID_GLITCH_DSP_H
#define VIVID_GLITCH_DSP_H

#include <cmath>
#include <cstdint>
#include <vector>
#include "operator_api/audio_dsp.h"

namespace glitch {

using audio_dsp::WhiteNoise;
using audio_dsp::detect_trigger;

// ---------------------------------------------------------------------------
// Crossfade coefficient: 0->1->1->0 envelope for click-free edges
// ---------------------------------------------------------------------------
inline float crossfade_coeff(uint32_t pos, uint32_t length, uint32_t fade_samples) {
    if (length == 0 || fade_samples == 0) return 1.0f;
    if (pos < fade_samples)
        return static_cast<float>(pos) / static_cast<float>(fade_samples);
    if (pos > length - fade_samples)
        return static_cast<float>(length - pos) / static_cast<float>(fade_samples);
    return 1.0f;
}

// ---------------------------------------------------------------------------
// CircularBuffer: mono ring buffer with lazy allocation
//
// write() records input continuously.
// read(double) reads at fractional position with linear interpolation.
// read_reverse() reads backwards from a start position.
// get_read_pos() computes absolute position N frames before write head.
// ---------------------------------------------------------------------------
struct CircularBuffer {
    std::vector<float> buffer;
    uint32_t size      = 0;
    uint32_t write_pos = 0;
    bool     ready     = false;
    uint32_t init_rate = 0;

    void init(uint32_t sample_rate, float seconds = 4.0f) {
        if (ready && init_rate == sample_rate) return;
        size = static_cast<uint32_t>(sample_rate * seconds) + 1;
        if (buffer.size() < size) {
            buffer.assign(size, 0.0f);  // first init or upward rate change
        } else {
            std::fill_n(buffer.data(), size, 0.0f);  // reuse existing capacity
        }
        write_pos = 0;
        ready     = true;
        init_rate = sample_rate;
    }

    void write(float sample) {
        buffer[write_pos] = sample;
        if (++write_pos >= size) write_pos = 0;
    }

    // Read at fractional position (absolute index into buffer) with linear interpolation
    float read(double frame_pos) const {
        double wrapped = std::fmod(frame_pos, static_cast<double>(size));
        if (wrapped < 0.0) wrapped += size;

        uint32_t idx0 = static_cast<uint32_t>(wrapped);
        uint32_t idx1 = idx0 + 1;
        if (idx1 >= size) idx1 = 0;

        float frac = static_cast<float>(wrapped - idx0);
        return buffer[idx0] * (1.0f - frac) + buffer[idx1] * frac;
    }

    // Read backwards: start_pos is the beginning of the slice, offset goes backward
    float read_reverse(uint32_t start_pos, double offset) const {
        double pos = static_cast<double>(start_pos) - offset;
        if (pos < 0.0) pos += size;
        return read(pos);
    }

    // Get absolute buffer position N frames before current write head
    uint32_t get_read_pos(uint32_t frames_back) const {
        if (frames_back >= size) frames_back = size - 1;
        int32_t pos = static_cast<int32_t>(write_pos) - static_cast<int32_t>(frames_back);
        if (pos < 0) pos += static_cast<int32_t>(size);
        return static_cast<uint32_t>(pos);
    }

    void clear() {
        std::fill(buffer.begin(), buffer.end(), 0.0f);
        write_pos = 0;
    }
};

} // namespace glitch

#endif // VIVID_GLITCH_DSP_H
