#include "shared/granular_dsp/granular_dsp.h"

#include <algorithm>
#include <cmath>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace vivid::granular_dsp {
namespace {

float clampf(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

float compute_window(float phase, int type) {
    switch (type) {
        default:
        case 0:
            return 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * phase));
        case 1:
            return 0.54f - 0.46f * std::cos(2.0f * static_cast<float>(M_PI) * phase);
        case 2:
            return 0.42f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * phase)
                         + 0.08f * std::cos(4.0f * static_cast<float>(M_PI) * phase);
        case 3:
            return 1.0f - std::fabs(2.0f * phase - 1.0f);
    }
}

} // namespace

const char* backend_name(Backend backend) {
    switch (backend) {
        case Backend::Scalar:
        default: return "scalar";
    }
}

Backend preferred_backend() {
    return Backend::Scalar;
}

void CaptureBuffer::init(int max_samples) {
    size = max_samples;
    write = 0;
    if (static_cast<int>(buffer.size()) < size) {
        buffer.assign(size, 0.0f);
    } else {
        std::fill_n(buffer.data(), size, 0.0f);
    }
}

void CaptureBuffer::push(float v) {
    if (size <= 0) return;
    buffer[write] = v;
    if (++write >= size) write = 0;
}

float CaptureBuffer::read_linear(float abs_pos) const {
    if (size <= 1) return 0.0f;

    const float size_f = static_cast<float>(size);
    float idx_f = abs_pos;
    if (idx_f >= size_f || idx_f < 0.0f) {
        idx_f -= std::floor(idx_f / size_f) * size_f;
    }

    int idx0 = static_cast<int>(idx_f);
    if (idx0 >= size) idx0 = 0;
    if (idx0 < 0) idx0 += size;
    int idx1 = idx0 + 1;
    if (idx1 >= size) idx1 = 0;

    const float frac = idx_f - static_cast<float>(idx0);
    return buffer[idx0] * (1.0f - frac) + buffer[idx1] * frac;
}

Engine::Engine() {
    init_window_tables();
}

void Engine::reset() {
    capture_.buffer.clear();
    capture_.size = 0;
    capture_.write = 0;
    for (auto& grain : grains_)
        grain = {};
    sched_phase_ = 0.0;
    initialized_ = false;
    init_rate_ = 0;
    last_stats_ = {};
}

void Engine::init_window_tables() {
    if (window_tables_initialized_) return;
    for (int type = 0; type < 4; ++type) {
        for (int i = 0; i <= kWindowTableSize; ++i) {
            const float phase = static_cast<float>(i) / static_cast<float>(kWindowTableSize);
            window_tables_[type][i] = compute_window(phase, type);
        }
    }
    window_tables_initialized_ = true;
}

void Engine::lazy_init(uint32_t sample_rate) {
    init_window_tables();
    if (initialized_ && init_rate_ == sample_rate) return;

    const int cap_samples = static_cast<int>(kMaxCaptureSec * static_cast<float>(sample_rate)) + 2;
    capture_.init(cap_samples);
    for (auto& grain : grains_)
        grain = {};
    sched_phase_ = 0.0;
    initialized_ = true;
    init_rate_ = sample_rate;
    last_stats_ = {};
}

int Engine::spawn_grain(float sample_rate,
                        float position,
                        float pitch,
                        float grain_size_ms,
                        float randomize,
                        int window_type) {
    int slot = -1;
    for (int g = 0; g < kMaxGrains; ++g) {
        if (!grains_[g].active) {
            slot = g;
            break;
        }
    }
    if (slot < 0) return -1;

    Grain& grain = grains_[slot];

    const float pos_jitter = randomize * 0.1f * rng_.next();
    const float pos = clampf(position + pos_jitter, 0.0f, 1.0f);
    const float delay_samples = pos * static_cast<float>(capture_.size);
    grain.start_pos = static_cast<float>(capture_.write) - delay_samples + static_cast<float>(capture_.size);
    if (grain.start_pos >= static_cast<float>(capture_.size))
        grain.start_pos -= static_cast<float>(capture_.size);

    const float size_jitter = 1.0f + randomize * 0.25f * rng_.next();
    const float grain_ms = grain_size_ms * std::max(0.25f, size_jitter);
    grain.length = std::max(1, static_cast<int>(grain_ms * 0.001f * sample_rate));

    const float pitch_jitter = randomize * rng_.next();
    grain.playback_rate = std::pow(2.0f, (pitch + pitch_jitter) / 12.0f);

    grain.cursor = 0.0f;
    grain.window_type = std::max(0, std::min(3, window_type));
    grain.active = true;
    return slot;
}

int Engine::compact_active(std::array<int, kMaxGrains>& active) const {
    int count = 0;
    for (int g = 0; g < kMaxGrains; ++g) {
        if (grains_[g].active)
            active[count++] = g;
    }
    return count;
}

float Engine::window_lookup(float phase, int type) const {
    if (phase <= 0.0f) return window_tables_[std::max(0, std::min(3, type))][0];
    if (phase >= 1.0f) return window_tables_[std::max(0, std::min(3, type))][kWindowTableSize];

    const int wt = std::max(0, std::min(3, type));
    const float pos = phase * static_cast<float>(kWindowTableSize);
    const int idx = static_cast<int>(pos);
    const float frac = pos - static_cast<float>(idx);
    const auto& table = window_tables_[wt];
    return table[idx] + (table[idx + 1] - table[idx]) * frac;
}

void Engine::process(const float* in,
                     float* out,
                     uint32_t frames,
                     uint32_t sample_rate,
                     const ProcessParams& params,
                     Backend backend) {
    if (!in || !out || frames == 0) return;
    lazy_init(sample_rate);

    const float mod_position = clampf(params.position, 0.0f, 1.0f);
    const float mod_pitch = clampf(params.pitch, -24.0f, 24.0f);
    const float mod_density = clampf(params.density, 0.5f, 60.0f);
    const float wet = clampf(params.mix, 0.0f, 1.0f);
    const float dry = 1.0f - wet;
    const float grain_size_ms = std::max(1.0f, params.grain_size_ms);
    const float randomize = clampf(params.randomize, 0.0f, 1.0f);
    const int window_type = std::max(0, std::min(3, params.window_type));
    const float sample_rate_f = static_cast<float>(sample_rate);
    const double inv_sr = 1.0 / static_cast<double>(sample_rate);

    std::array<int, kMaxGrains> active_indices{};
    int active_count = compact_active(active_indices);
    int max_active_this_block = active_count;

    for (uint32_t i = 0; i < frames; ++i) {
        capture_.push(in[i]);

        sched_phase_ += mod_density * inv_sr;
        if (sched_phase_ >= 1.0) {
            sched_phase_ -= 1.0;
            const int slot = spawn_grain(sample_rate_f, mod_position, mod_pitch,
                                         grain_size_ms, randomize, window_type);
            if (slot >= 0 && active_count < kMaxGrains) {
                active_indices[active_count++] = slot;
                max_active_this_block = std::max(max_active_this_block, active_count);
            }
        }

        float grain_sum = 0.0f;
        int live_count = 0;
        for (int ai = 0; ai < active_count; ++ai) {
            Grain& grain = grains_[active_indices[ai]];
            if (!grain.active) continue;

            const float phase = grain.cursor / static_cast<float>(grain.length);
            if (phase >= 1.0f) {
                grain.active = false;
                active_indices[ai] = active_indices[--active_count];
                --ai;
                continue;
            }

            const float env = window_lookup(phase, grain.window_type);
            const float read_pos = grain.start_pos + grain.cursor * grain.playback_rate;
            grain_sum += capture_.read_linear(read_pos) * env;
            ++live_count;
            grain.cursor += 1.0f;
        }

        if (live_count > 0)
            grain_sum *= 1.0f / std::sqrt(static_cast<float>(live_count));

        out[i] = in[i] * dry + grain_sum * wet;
    }

    last_stats_.backend = backend;
    last_stats_.active_grains = max_active_this_block;
}

void Engine::fill_inspector_snapshot(InspectorSnapshot& snap,
                                     float position_norm,
                                     int window_type) const {
    if (capture_.size > 0) {
        const float samples_per_bin = static_cast<float>(capture_.size) / static_cast<float>(kWaveformBins);
        for (int b = 0; b < kWaveformBins; ++b) {
            const float start_f = static_cast<float>(b) * samples_per_bin;
            const float end_f = start_f + samples_per_bin;
            const int start_i = static_cast<int>(start_f);
            const int end_i = std::min(static_cast<int>(end_f), capture_.size);

            float lo = 1e30f;
            float hi = -1e30f;
            for (int s = start_i; s < end_i; ++s) {
                const int idx = (capture_.write + s) % capture_.size;
                const float v = capture_.buffer[idx];
                if (v < lo) lo = v;
                if (v > hi) hi = v;
            }
            if (lo > hi) {
                lo = 0.0f;
                hi = 0.0f;
            }
            snap.bins[b].min_val = lo;
            snap.bins[b].max_val = hi;
        }
    }

    int active = 0;
    const float buf_size_f = static_cast<float>(capture_.size);
    const float bin_scale = buf_size_f > 0.0f ? static_cast<float>(kWaveformBins) / buf_size_f : 0.0f;

    for (int g = 0; g < kMaxGrains; ++g) {
        const auto& grain = grains_[g];
        auto& gs = snap.grains[g];
        gs.active = grain.active;
        if (!grain.active) continue;

        const float delay = std::fmod(
            static_cast<float>(capture_.write) - grain.start_pos + buf_size_f,
            buf_size_f);
        const float bin_pos = static_cast<float>(kWaveformBins) - delay * bin_scale;
        gs.bin_start = bin_pos;
        gs.bin_width = static_cast<float>(grain.length) * bin_scale;
        gs.phase = grain.length > 0 ? grain.cursor / static_cast<float>(grain.length) : 0.0f;
        ++active;
    }

    snap.active_count = active;
    snap.window_type = std::max(0, std::min(3, window_type));
    snap.position_norm = clampf(position_norm, 0.0f, 1.0f);
}

} // namespace vivid::granular_dsp
