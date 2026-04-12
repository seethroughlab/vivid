#include "shared/convolution_reverb_dsp/convolution_reverb_dsp.h"

#include "miniaudio.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace vivid::convolution_reverb_dsp {
namespace {

static float clamp(float v, float lo, float hi) {
    return std::max(lo, std::min(hi, v));
}

static uint32_t next_pow2(uint32_t v) {
    uint32_t out = 1;
    while (out < v) out <<= 1u;
    return out;
}

static int log2_size(uint32_t n) {
    int out = 0;
    while (n > 1) {
        n >>= 1u;
        ++out;
    }
    return out;
}

static float db_to_gain(float db) {
    return std::pow(10.0f, db / 20.0f);
}

static float generated_noise(uint32_t i, uint32_t seed) {
    uint32_t x = i * 1664525u + seed * 1013904223u + 0x9e3779b9u;
    x ^= x >> 16;
    x *= 2246822519u;
    x ^= x >> 13;
    return static_cast<float>(static_cast<int32_t>(x)) / 2147483648.0f;
}

struct RawIr {
    std::vector<float> channels[4];
    uint32_t sample_rate = 0;
    uint32_t channel_count = 0;
};

static RawIr load_wav_ir(const std::string& path) {
    RawIr out{};
    if (path.empty()) return out;

    ma_decoder_config config = ma_decoder_config_init(ma_format_f32, 0, 0);
    ma_decoder decoder;
    if (ma_decoder_init_file(path.c_str(), &config, &decoder) != MA_SUCCESS) {
        std::fprintf(stderr, "[convolution_reverb] Failed to decode IR: %s\n", path.c_str());
        return out;
    }

    ma_uint64 total_frames = 0;
    ma_decoder_get_length_in_pcm_frames(&decoder, &total_frames);
    const uint32_t channels = std::min<uint32_t>(decoder.outputChannels, 4);
    if (total_frames == 0 || channels == 0) {
        ma_decoder_uninit(&decoder);
        return out;
    }

    std::vector<float> interleaved(static_cast<size_t>(total_frames) * decoder.outputChannels);
    ma_uint64 frames_read = 0;
    ma_decoder_read_pcm_frames(&decoder, interleaved.data(), total_frames, &frames_read);

    out.sample_rate = decoder.outputSampleRate;
    out.channel_count = channels;
    for (uint32_t ch = 0; ch < channels; ++ch)
        out.channels[ch].resize(static_cast<size_t>(frames_read));

    for (size_t i = 0; i < static_cast<size_t>(frames_read); ++i) {
        for (uint32_t ch = 0; ch < channels; ++ch)
            out.channels[ch][i] = interleaved[i * decoder.outputChannels + ch];
    }

    ma_decoder_uninit(&decoder);
    return out;
}

static std::vector<float> resample_linear(const std::vector<float>& in,
                                          uint32_t in_rate,
                                          uint32_t out_rate) {
    if (in.empty()) return {};
    if (in_rate == 0 || out_rate == 0 || in_rate == out_rate) return in;

    const double ratio = static_cast<double>(out_rate) / static_cast<double>(in_rate);
    const size_t out_count = std::max<size_t>(1, static_cast<size_t>(std::ceil(in.size() * ratio)));
    std::vector<float> out(out_count, 0.0f);
    for (size_t i = 0; i < out.size(); ++i) {
        const double src = static_cast<double>(i) / ratio;
        const size_t i0 = static_cast<size_t>(std::floor(src));
        const size_t i1 = std::min(i0 + 1, in.size() - 1);
        const float frac = static_cast<float>(src - static_cast<double>(i0));
        out[i] = in[i0] + (in[i1] - in[i0]) * frac;
    }
    return out;
}

static RawIr generate_builtin_ir(int preset, uint32_t sample_rate) {
    struct Shape {
        float seconds;
        float brightness;
        float density;
        float stereo;
    };
    static constexpr Shape shapes[] = {
        {0.65f, 0.55f, 0.62f, 0.55f}, // room
        {1.35f, 0.82f, 0.76f, 0.70f}, // plate
        {3.20f, 0.45f, 0.88f, 0.90f}, // hall
        {6.00f, 0.30f, 0.96f, 1.00f}, // cathedral
    };

    const Shape shape = shapes[std::clamp(preset, 0, static_cast<int>(std::size(shapes)) - 1)];
    const uint32_t frames = std::max<uint32_t>(1, static_cast<uint32_t>(shape.seconds * sample_rate));
    RawIr raw{};
    raw.sample_rate = sample_rate;
    raw.channel_count = 4;
    for (auto& ch : raw.channels)
        ch.assign(frames, 0.0f);

    const float sr = static_cast<float>(sample_rate);
    for (uint32_t i = 0; i < frames; ++i) {
        const float t = static_cast<float>(i) / sr;
        const float env = std::exp(-6.0f * t / shape.seconds);
        const float high = std::exp(-2.0f * t * (1.0f - shape.brightness));
        const float late = generated_noise(i, 11u + static_cast<uint32_t>(preset) * 37u);
        const float mod_a = 0.65f + 0.35f * std::sin(2.0f * static_cast<float>(M_PI) * (0.37f * t + 0.013f));
        const float mod_b = 0.65f + 0.35f * std::sin(2.0f * static_cast<float>(M_PI) * (0.43f * t + 0.19f));
        const float body = late * env * high * shape.density * 0.18f;

        raw.channels[0][i] = body * mod_a;
        raw.channels[3][i] = body * mod_b;
        raw.channels[1][i] = body * (0.18f + 0.12f * shape.stereo)
                            * std::sin(2.0f * static_cast<float>(M_PI) * 0.73f * t + 0.5f);
        raw.channels[2][i] = body * (0.16f + 0.14f * shape.stereo)
                            * std::sin(2.0f * static_cast<float>(M_PI) * 0.67f * t + 1.7f);
    }

    const uint32_t reflection_count = preset == 0 ? 9u : 18u;
    for (uint32_t r = 0; r < reflection_count; ++r) {
        const float ms = 4.0f + static_cast<float>(r * r + 3u * r) * (preset == 0 ? 1.2f : 2.6f);
        const uint32_t idx = static_cast<uint32_t>(ms * 0.001f * sr);
        if (idx >= frames) continue;
        const float amp = 0.35f * std::exp(-static_cast<float>(r) / 8.0f);
        raw.channels[0][idx] += amp;
        raw.channels[3][idx] += amp * (0.82f + 0.05f * static_cast<float>(r % 3));
        raw.channels[1][idx] += amp * 0.28f * shape.stereo;
        raw.channels[2][idx] -= amp * 0.24f * shape.stereo;
    }

    return raw;
}

static void copy_with_delay_and_trim(std::vector<float>& dst,
                                     const std::vector<float>& src,
                                     uint32_t pre_delay,
                                     uint32_t max_frames,
                                     float gain) {
    dst.assign(std::max<uint32_t>(1, max_frames), 0.0f);
    if (pre_delay >= max_frames || src.empty()) return;
    const uint32_t copy_count = std::min<uint32_t>(static_cast<uint32_t>(src.size()), max_frames - pre_delay);
    for (uint32_t i = 0; i < copy_count; ++i)
        dst[pre_delay + i] = src[i] * gain;
}

} // namespace

const char* backend_name(Backend backend) {
    switch (backend) {
        case Backend::Accelerate: return "accelerate";
        case Backend::Scalar:
        default:                  return "scalar";
    }
}

Backend preferred_backend() {
#if VIVID_ACCELERATE_ENABLED
    return Backend::Accelerate;
#else
    return Backend::Scalar;
#endif
}

ImpulseResponse build_impulse_response(const IrConfig& config) {
    const uint32_t sample_rate = config.sample_rate == 0 ? 48000u : config.sample_rate;
    RawIr raw = load_wav_ir(config.file_path);
    if (raw.channel_count == 0)
        raw = generate_builtin_ir(config.preset, sample_rate);

    for (uint32_t ch = 0; ch < raw.channel_count; ++ch)
        raw.channels[ch] = resample_linear(raw.channels[ch], raw.sample_rate, sample_rate);

    const uint32_t pre_delay = static_cast<uint32_t>(
        std::max(0.0f, config.pre_delay_ms) * 0.001f * static_cast<float>(sample_rate) + 0.5f);
    const uint32_t tail_frames = std::max<uint32_t>(
        1, static_cast<uint32_t>(clamp(config.tail_seconds, 0.05f, 30.0f) * sample_rate));
    const float gain = db_to_gain(clamp(config.gain_db, -36.0f, 24.0f));

    ImpulseResponse ir{};
    ir.sample_rate = sample_rate;
    if (raw.channel_count == 1) {
        ir.layout = IrLayout::Mono;
        std::vector<float> mono;
        copy_with_delay_and_trim(mono, raw.channels[0], pre_delay, tail_frames, gain * 0.5f);
        ir.ll = mono;
        ir.lr = mono;
        ir.rl = mono;
        ir.rr = mono;
    } else if (raw.channel_count == 2) {
        ir.layout = IrLayout::Stereo;
        copy_with_delay_and_trim(ir.ll, raw.channels[0], pre_delay, tail_frames, gain);
        ir.lr.assign(ir.ll.size(), 0.0f);
        ir.rl.assign(ir.ll.size(), 0.0f);
        copy_with_delay_and_trim(ir.rr, raw.channels[1], pre_delay, tail_frames, gain);
    } else {
        ir.layout = IrLayout::TrueStereo;
        copy_with_delay_and_trim(ir.ll, raw.channels[0], pre_delay, tail_frames, gain);
        copy_with_delay_and_trim(ir.lr, raw.channels[1], pre_delay, tail_frames, gain);
        copy_with_delay_and_trim(ir.rl, raw.channels[2], pre_delay, tail_frames, gain);
        copy_with_delay_and_trim(ir.rr, raw.channels[3], pre_delay, tail_frames, gain);
    }
    return ir;
}

Engine::~Engine() {
#if VIVID_ACCELERATE_ENABLED
    if (fft_setup_) {
        vDSP_destroy_fftsetup(fft_setup_);
        fft_setup_ = nullptr;
    }
#endif
}

void Engine::reset() {
    plan_ = {};
    has_plan_ = false;
    plan_rebuild_count_ = 0;
    last_stats_ = {};
#if VIVID_ACCELERATE_ENABLED
    if (fft_setup_) {
        vDSP_destroy_fftsetup(fft_setup_);
        fft_setup_ = nullptr;
    }
    fft_log2_ = 0;
    accel_real_.clear();
    accel_imag_.clear();
#endif
}

bool Engine::plan_matches(uint32_t frames,
                          uint32_t sample_rate,
                          const ProcessParams& params,
                          Backend backend) const {
    const std::string file_path = params.ir_file ? params.ir_file : "";
    return has_plan_
        && plan_.block_size == frames
        && plan_.sample_rate == sample_rate
        && plan_.backend == backend
        && plan_.preset == params.ir_preset
        && plan_.file_path == file_path
        && std::fabs(plan_.tail_seconds - params.tail_seconds) < 0.001f
        && std::fabs(plan_.pre_delay_ms - params.pre_delay_ms) < 0.001f
        && std::fabs(plan_.ir_gain_db - params.ir_gain_db) < 0.001f;
}

void Engine::rebuild_plan(uint32_t frames,
                          uint32_t sample_rate,
                          const ProcessParams& params,
                          Backend backend) {
    Plan next{};
    next.sample_rate = sample_rate;
    next.block_size = frames;
    next.tail_partition_size = frames;
    next.fft_size = next_pow2(next.tail_partition_size * 2u);
    next.early_len = std::min<uint32_t>(frames, 256u);
    next.backend = backend;
    next.file_path = params.ir_file ? params.ir_file : "";
    next.preset = params.ir_preset;
    next.tail_seconds = params.tail_seconds;
    next.pre_delay_ms = params.pre_delay_ms;
    next.ir_gain_db = params.ir_gain_db;

    IrConfig ir_config{};
    ir_config.preset = params.ir_preset;
    ir_config.file_path = next.file_path;
    ir_config.sample_rate = sample_rate;
    ir_config.tail_seconds = params.tail_seconds;
    ir_config.pre_delay_ms = params.pre_delay_ms;
    ir_config.gain_db = params.ir_gain_db;
    const ImpulseResponse ir = build_impulse_response(ir_config);
    next.layout = ir.layout;

    const uint32_t ir_frames = ir.frames();
    next.early_len = std::min(next.early_len, ir_frames);
    auto copy_early = [&](std::vector<float>& dst, const std::vector<float>& src) {
        dst.assign(next.early_len, 0.0f);
        for (uint32_t i = 0; i < next.early_len; ++i)
            dst[i] = src[i];
    };
    copy_early(next.early_ll, ir.ll);
    copy_early(next.early_lr, ir.lr);
    copy_early(next.early_rl, ir.rl);
    copy_early(next.early_rr, ir.rr);

    const uint32_t tail_frames = ir_frames > next.early_len ? ir_frames - next.early_len : 0;
    next.partition_count = tail_frames == 0 ? 0 : (tail_frames + next.tail_partition_size - 1u) / next.tail_partition_size;

    auto make_partitions = [&](const std::vector<float>& src) {
        std::vector<std::vector<Complex>> partitions(next.partition_count);
        for (uint32_t p = 0; p < next.partition_count; ++p) {
            partitions[p].assign(next.fft_size, {});
            const uint32_t src_offset = next.early_len + p * next.tail_partition_size;
            const uint32_t copy_count = std::min<uint32_t>(next.tail_partition_size,
                                                           static_cast<uint32_t>(src.size()) - src_offset);
            for (uint32_t i = 0; i < copy_count; ++i)
                partitions[p][i].re = src[src_offset + i];
            fft(partitions[p].data(), next.fft_size, false, backend);
        }
        return partitions;
    };

    next.tail_ll = make_partitions(ir.ll);
    next.tail_lr = make_partitions(ir.lr);
    next.tail_rl = make_partitions(ir.rl);
    next.tail_rr = make_partitions(ir.rr);

    next.input_history_l.assign(std::max<uint32_t>(1, next.partition_count), std::vector<Complex>(next.fft_size));
    next.input_history_r.assign(std::max<uint32_t>(1, next.partition_count), std::vector<Complex>(next.fft_size));
    next.prev_l.assign(std::max<uint32_t>(1, next.early_len), 0.0f);
    next.prev_r.assign(std::max<uint32_t>(1, next.early_len), 0.0f);
    next.input_accum_l.assign(next.tail_partition_size, 0.0f);
    next.input_accum_r.assign(next.tail_partition_size, 0.0f);
    next.tail_accum_l.assign(next.fft_size + next.early_len + frames, 0.0f);
    next.tail_accum_r.assign(next.fft_size + next.early_len + frames, 0.0f);
    next.fft_l.assign(next.fft_size, {});
    next.fft_r.assign(next.fft_size, {});
    next.fft_sum_l.assign(next.fft_size, {});
    next.fft_sum_r.assign(next.fft_size, {});
    next.wet_l.assign(frames, 0.0f);
    next.wet_r.assign(frames, 0.0f);

    plan_ = std::move(next);
    has_plan_ = true;
    ++plan_rebuild_count_;

#if VIVID_ACCELERATE_ENABLED
    const int log2n = log2_size(plan_.fft_size);
    if (fft_setup_ && fft_log2_ != log2n) {
        vDSP_destroy_fftsetup(fft_setup_);
        fft_setup_ = nullptr;
    }
    if (!fft_setup_) {
        fft_setup_ = vDSP_create_fftsetup(static_cast<vDSP_Length>(log2n), kFFTRadix2);
    }
    fft_log2_ = log2n;
    accel_real_.assign(plan_.fft_size, 0.0f);
    accel_imag_.assign(plan_.fft_size, 0.0f);
#endif
}

void Engine::fft(Complex* data, uint32_t n, bool inverse, Backend backend) {
    if (backend == Backend::Accelerate && fft_accelerate(data, n, inverse))
        return;
    fft_scalar(data, n, inverse);
}

void Engine::fft_scalar(Complex* data, uint32_t n, bool inverse) {
    const int log2n = log2_size(n);
    for (uint32_t i = 0; i < n; ++i) {
        uint32_t j = 0;
        for (int b = 0; b < log2n; ++b)
            j |= ((i >> b) & 1u) << (log2n - 1 - b);
        if (j > i) std::swap(data[i], data[j]);
    }

    for (uint32_t len = 2; len <= n; len <<= 1u) {
        const float angle = (inverse ? 2.0f : -2.0f) * static_cast<float>(M_PI) / static_cast<float>(len);
        const float wlen_re = std::cos(angle);
        const float wlen_im = std::sin(angle);
        for (uint32_t i = 0; i < n; i += len) {
            float w_re = 1.0f;
            float w_im = 0.0f;
            for (uint32_t j = 0; j < len / 2; ++j) {
                Complex& u = data[i + j];
                Complex& v_src = data[i + j + len / 2];
                const Complex v{v_src.re * w_re - v_src.im * w_im,
                                v_src.re * w_im + v_src.im * w_re};
                v_src = {u.re - v.re, u.im - v.im};
                u = {u.re + v.re, u.im + v.im};
                const float next_re = w_re * wlen_re - w_im * wlen_im;
                w_im = w_re * wlen_im + w_im * wlen_re;
                w_re = next_re;
            }
        }
    }

    if (inverse) {
        const float inv = 1.0f / static_cast<float>(n);
        for (uint32_t i = 0; i < n; ++i) {
            data[i].re *= inv;
            data[i].im *= inv;
        }
    }
}

bool Engine::fft_accelerate(Complex* data, uint32_t n, bool inverse) {
#if VIVID_ACCELERATE_ENABLED
    const int log2n = log2_size(n);
    if (!fft_setup_ || fft_log2_ != log2n) return false;
    if (accel_real_.size() < n) {
        accel_real_.assign(n, 0.0f);
        accel_imag_.assign(n, 0.0f);
    }
    for (uint32_t i = 0; i < n; ++i) {
        accel_real_[i] = data[i].re;
        accel_imag_[i] = data[i].im;
    }
    DSPSplitComplex split{accel_real_.data(), accel_imag_.data()};
    vDSP_fft_zip(fft_setup_, &split, 1, static_cast<vDSP_Length>(log2n),
                 inverse ? FFT_INVERSE : FFT_FORWARD);
    const float scale = inverse ? (1.0f / static_cast<float>(n)) : 1.0f;
    for (uint32_t i = 0; i < n; ++i) {
        data[i].re = accel_real_[i] * scale;
        data[i].im = accel_imag_[i] * scale;
    }
    return true;
#else
    (void)data;
    (void)n;
    (void)inverse;
    return false;
#endif
}

void Engine::render_direct(const float* in_l, const float* in_r, uint32_t frames) {
    std::fill(plan_.wet_l.begin(), plan_.wet_l.end(), 0.0f);
    std::fill(plan_.wet_r.begin(), plan_.wet_r.end(), 0.0f);
    if (plan_.early_len == 0) return;

    for (uint32_t i = 0; i < frames; ++i) {
        float wet_l = 0.0f;
        float wet_r = 0.0f;
        for (uint32_t k = 0; k < plan_.early_len; ++k) {
            const float xl = k <= i ? in_l[i - k] : plan_.prev_l[plan_.prev_l.size() + i - k];
            const float xr = k <= i ? in_r[i - k] : plan_.prev_r[plan_.prev_r.size() + i - k];
            wet_l += plan_.early_ll[k] * xl + plan_.early_rl[k] * xr;
            wet_r += plan_.early_lr[k] * xl + plan_.early_rr[k] * xr;
        }
        plan_.wet_l[i] = wet_l;
        plan_.wet_r[i] = wet_r;
    }
}

void Engine::render_tail(const float* in_l, const float* in_r, uint32_t frames, Backend backend) {
    if (plan_.partition_count == 0) return;

    uint32_t consumed = 0;
    while (consumed < frames) {
        const uint32_t room = plan_.tail_partition_size - plan_.input_fill;
        const uint32_t n = std::min(room, frames - consumed);
        std::copy(in_l + consumed, in_l + consumed + n, plan_.input_accum_l.begin() + plan_.input_fill);
        std::copy(in_r + consumed, in_r + consumed + n, plan_.input_accum_r.begin() + plan_.input_fill);
        plan_.input_fill += n;
        consumed += n;
        if (plan_.input_fill == plan_.tail_partition_size)
            submit_tail_partition(backend);
    }

    for (uint32_t i = 0; i < frames; ++i) {
        plan_.wet_l[i] += plan_.tail_accum_l[i];
        plan_.wet_r[i] += plan_.tail_accum_r[i];
    }

    std::move(plan_.tail_accum_l.begin() + frames, plan_.tail_accum_l.end(), plan_.tail_accum_l.begin());
    std::fill(plan_.tail_accum_l.end() - frames, plan_.tail_accum_l.end(), 0.0f);
    std::move(plan_.tail_accum_r.begin() + frames, plan_.tail_accum_r.end(), plan_.tail_accum_r.begin());
    std::fill(plan_.tail_accum_r.end() - frames, plan_.tail_accum_r.end(), 0.0f);
}

void Engine::submit_tail_partition(Backend backend) {
    if (plan_.partition_count == 0) return;

    std::fill(plan_.fft_l.begin(), plan_.fft_l.end(), Complex{});
    std::fill(plan_.fft_r.begin(), plan_.fft_r.end(), Complex{});
    for (uint32_t i = 0; i < plan_.tail_partition_size; ++i) {
        plan_.fft_l[i].re = plan_.input_accum_l[i];
        plan_.fft_r[i].re = plan_.input_accum_r[i];
    }
    fft(plan_.fft_l.data(), plan_.fft_size, false, backend);
    fft(plan_.fft_r.data(), plan_.fft_size, false, backend);

    plan_.input_history_l[plan_.history_pos] = plan_.fft_l;
    plan_.input_history_r[plan_.history_pos] = plan_.fft_r;

    std::fill(plan_.fft_sum_l.begin(), plan_.fft_sum_l.end(), Complex{});
    std::fill(plan_.fft_sum_r.begin(), plan_.fft_sum_r.end(), Complex{});

    auto madd = [](Complex x, Complex h, Complex& y) {
        y.re += x.re * h.re - x.im * h.im;
        y.im += x.re * h.im + x.im * h.re;
    };

    for (uint32_t p = 0; p < plan_.partition_count; ++p) {
        const uint32_t hist = (plan_.history_pos + plan_.partition_count - p) % plan_.partition_count;
        const auto& xl = plan_.input_history_l[hist];
        const auto& xr = plan_.input_history_r[hist];
        const auto& hll = plan_.tail_ll[p];
        const auto& hlr = plan_.tail_lr[p];
        const auto& hrl = plan_.tail_rl[p];
        const auto& hrr = plan_.tail_rr[p];
        for (uint32_t i = 0; i < plan_.fft_size; ++i) {
            madd(xl[i], hll[i], plan_.fft_sum_l[i]);
            madd(xr[i], hrl[i], plan_.fft_sum_l[i]);
            madd(xl[i], hlr[i], plan_.fft_sum_r[i]);
            madd(xr[i], hrr[i], plan_.fft_sum_r[i]);
        }
    }

    fft(plan_.fft_sum_l.data(), plan_.fft_size, true, backend);
    fft(plan_.fft_sum_r.data(), plan_.fft_size, true, backend);

    for (uint32_t i = 0; i < plan_.fft_size; ++i) {
        plan_.tail_accum_l[plan_.early_len + i] += plan_.fft_sum_l[i].re;
        plan_.tail_accum_r[plan_.early_len + i] += plan_.fft_sum_r[i].re;
    }

    plan_.history_pos = (plan_.history_pos + 1u) % plan_.partition_count;
    plan_.input_fill = 0;
    std::fill(plan_.input_accum_l.begin(), plan_.input_accum_l.end(), 0.0f);
    std::fill(plan_.input_accum_r.begin(), plan_.input_accum_r.end(), 0.0f);
}

void Engine::update_previous_input(const float* in_l, const float* in_r, uint32_t frames) {
    if (plan_.early_len == 0) return;
    if (frames >= plan_.early_len) {
        std::copy(in_l + frames - plan_.early_len, in_l + frames, plan_.prev_l.begin());
        std::copy(in_r + frames - plan_.early_len, in_r + frames, plan_.prev_r.begin());
        return;
    }
    std::move(plan_.prev_l.begin() + frames, plan_.prev_l.end(), plan_.prev_l.begin());
    std::copy(in_l, in_l + frames, plan_.prev_l.end() - frames);
    std::move(plan_.prev_r.begin() + frames, plan_.prev_r.end(), plan_.prev_r.begin());
    std::copy(in_r, in_r + frames, plan_.prev_r.end() - frames);
}

void Engine::process(const float* input_stereo,
                     float* output_stereo,
                     uint32_t frames,
                     uint32_t sample_rate,
                     const ProcessParams& params,
                     Backend backend) {
    if (!output_stereo || frames == 0 || sample_rate == 0) return;
    const float* in_l = input_stereo;
    const float* in_r = input_stereo ? input_stereo + frames : nullptr;
    float* out_l = output_stereo;
    float* out_r = output_stereo + frames;
    if (!in_l || !in_r) {
        std::fill(out_l, out_l + frames * 2u, 0.0f);
        return;
    }

    if (!plan_matches(frames, sample_rate, params, backend))
        rebuild_plan(frames, sample_rate, params, backend);

    render_direct(in_l, in_r, frames);
    render_tail(in_l, in_r, frames, backend);

    const float mix = clamp(params.mix, 0.0f, 1.0f);
    const float dry = 1.0f - mix;
    const float width = clamp(params.width, 0.0f, 2.0f);
    for (uint32_t i = 0; i < frames; ++i) {
        const float mid = 0.5f * (plan_.wet_l[i] + plan_.wet_r[i]);
        const float side = 0.5f * (plan_.wet_l[i] - plan_.wet_r[i]) * width;
        const float wet_l = mid + side;
        const float wet_r = mid - side;
        out_l[i] = in_l[i] * dry + wet_l * mix;
        out_r[i] = in_r[i] * dry + wet_r * mix;
    }
    update_previous_input(in_l, in_r, frames);

    last_stats_.backend = backend == Backend::Accelerate && preferred_backend() == Backend::Accelerate
        ? Backend::Accelerate
        : (backend == Backend::Accelerate ? plan_.backend : backend);
#if !VIVID_ACCELERATE_ENABLED
    if (last_stats_.backend == Backend::Accelerate)
        last_stats_.backend = Backend::Scalar;
#endif
    last_stats_.layout = plan_.layout;
    last_stats_.sample_rate = sample_rate;
    last_stats_.block_size = frames;
    last_stats_.ir_frames = static_cast<uint32_t>(plan_.early_ll.size() + plan_.partition_count * plan_.tail_partition_size);
    last_stats_.partition_count = plan_.partition_count;
    last_stats_.plan_rebuild_count = plan_rebuild_count_;
}

} // namespace vivid::convolution_reverb_dsp
