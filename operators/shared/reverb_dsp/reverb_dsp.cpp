#include "shared/reverb_dsp/reverb_dsp.h"

#include <algorithm>

namespace vivid::reverb_dsp {
namespace {

static constexpr int kCombLengths[kCombCount] = {1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617};
static constexpr int kAllPassLengths[kAllPassCount] = {556, 441, 341, 225};

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

void Engine::CombFilter::init(int len) {
    size = len;
    idx = 0;
    filterstore = 0.0f;
    if (static_cast<int>(buffer.size()) < size) {
        buffer.assign(size, 0.0f);
    } else {
        std::fill_n(buffer.data(), size, 0.0f);
    }
}

float Engine::CombFilter::process(float input, float feedback, float damp1, float damp2) {
    const float out = buffer[idx];
    filterstore = out * damp2 + filterstore * damp1;
    buffer[idx] = input + filterstore * feedback;
    if (++idx >= size) idx = 0;
    return out;
}

void Engine::AllPassDelay::init(int len) {
    size = len;
    idx = 0;
    if (static_cast<int>(buffer.size()) < size) {
        buffer.assign(size, 0.0f);
    } else {
        std::fill_n(buffer.data(), size, 0.0f);
    }
}

float Engine::AllPassDelay::process(float input) {
    const float bufout = buffer[idx];
    const float out = bufout - input;
    buffer[idx] = input + bufout * 0.5f;
    if (++idx >= size) idx = 0;
    return out;
}

void Engine::reset() {
    for (auto& comb : combs_)
        comb = {};
    for (auto& allpass : allpasses_)
        allpass = {};
    initialized_ = false;
    init_rate_ = 0;
    initialization_count_ = 0;
    last_stats_ = {};
}

void Engine::lazy_init(uint32_t sample_rate) {
    if (initialized_ && init_rate_ == sample_rate) return;

    const double scale = static_cast<double>(sample_rate) / 44100.0;
    for (int i = 0; i < kCombCount; ++i) {
        const int len = static_cast<int>(kCombLengths[i] * scale);
        combs_[i].init(len);
        last_stats_.comb_sizes[i] = len;
    }
    for (int i = 0; i < kAllPassCount; ++i) {
        const int len = static_cast<int>(kAllPassLengths[i] * scale);
        allpasses_[i].init(len);
        last_stats_.allpass_sizes[i] = len;
    }
    initialized_ = true;
    init_rate_ = sample_rate;
    ++initialization_count_;
}

void Engine::process(const float* in,
                     float* out,
                     uint32_t frames,
                     uint32_t sample_rate,
                     const ProcessParams& params,
                     Backend backend) {
    if (!in || !out || frames == 0 || sample_rate == 0) return;
    lazy_init(sample_rate);

    const float fb = params.room_size * 0.28f + 0.7f;
    const float damp1 = params.damping;
    const float damp2 = 1.0f - damp1;
    const float wet = params.mix;
    const float dry = 1.0f - wet;

    for (uint32_t i = 0; i < frames; ++i) {
        const float inp = in[i] * 0.125f;
        float sum = 0.0f;

        for (int c = 0; c < kCombCount; ++c)
            sum += combs_[c].process(inp, fb, damp1, damp2);

        for (int a = 0; a < kAllPassCount; ++a)
            sum = allpasses_[a].process(sum);

        out[i] = in[i] * dry + sum * wet;
    }

    last_stats_.backend = backend;
    last_stats_.sample_rate = sample_rate;
    last_stats_.initialization_count = initialization_count_;
}

} // namespace vivid::reverb_dsp
