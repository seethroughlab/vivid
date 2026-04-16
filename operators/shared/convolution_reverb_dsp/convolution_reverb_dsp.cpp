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

// Non-uniform partitioning tunables. See docs/plans/core-audio-optimization-roadmap.md
// "ConvolutionReverb Non-Uniform Partition Pass" for the doubling-zone design.
// kEarlyLenTarget matches the original engine's direct-convolution length so the
// O(early_len * block_size) inner loop cost does not regress. The correctness
// invariant for each zone is `ir_offset - partition_size + block_size >= 0`,
// which holds trivially regardless of early_len (zone 1 has ir_offset=early_len
// and partition_size=block_size, so the write offset equals early_len).
constexpr uint32_t kEarlyLenTarget = 256;
constexpr uint32_t kZoneSizeCap = 4096;
constexpr uint32_t kPartitionsPerZone = 4;

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

void Engine::destroy_accel_setups() {
#if VIVID_ACCELERATE_ENABLED
    for (auto& s : fft_setups_) {
        if (s.setup) vDSP_destroy_fftsetup(s.setup);
    }
    fft_setups_.clear();
#endif
}

Engine::~Engine() {
    destroy_accel_setups();
}

void Engine::reset() {
    plan_ = {};
    has_plan_ = false;
    plan_rebuild_count_ = 0;
    last_stats_ = {};
    destroy_accel_setups();
#if VIVID_ACCELERATE_ENABLED
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

    // Early-block direct convolution covers IR[0, early_len). Zone 1's
    // ir_offset equals early_len and its partition_size equals block_size,
    // giving write_offset = early_len >= 0 independent of block_size — so we
    // keep early_len small to bound the O(early_len * block_size) direct loop.
    uint32_t early_len = std::min<uint32_t>(kEarlyLenTarget, ir_frames);
    next.early_len = early_len;

    auto copy_early = [&](std::vector<float>& dst, const std::vector<float>& src) {
        dst.assign(std::max<uint32_t>(1, early_len), 0.0f);
        for (uint32_t i = 0; i < early_len; ++i)
            dst[i] = src[i];
    };
    copy_early(next.early_ll, ir.ll);
    copy_early(next.early_lr, ir.lr);
    copy_early(next.early_rl, ir.rl);
    copy_early(next.early_rr, ir.rr);

    // Zone schedule: doubling partition sizes capped at kZoneSizeCap, with
    // kPartitionsPerZone partitions per zone until the last zone absorbs the
    // remaining IR samples. Invariant upheld by construction:
    //   ir_offset_k >= partition_size_k  for every zone
    // which guarantees each zone's IFFT output lands at non-negative tail_accum
    // offsets (no output-side latency introduced).
    const uint32_t tail_frames = ir_frames > early_len ? ir_frames - early_len : 0;
    uint32_t ir_offset = early_len;
    uint32_t remaining = tail_frames;
    uint32_t current_size = frames;

    while (remaining > 0) {
        Zone zone{};
        zone.partition_size = current_size;
        zone.fft_size = next_pow2(current_size * 2u);
        zone.ir_offset = ir_offset;
        const uint32_t max_parts = (remaining + current_size - 1u) / current_size;
        // Once we reach the zone-size cap there's no further doubling win, so
        // absorb the entire remainder into one zone. Earlier zones keep the
        // small fixed partition count so the input/output FFT overhead per
        // fire stays bounded and zone cadence stays fine-grained.
        zone.partition_count = current_size >= kZoneSizeCap
            ? max_parts
            : std::min<uint32_t>(kPartitionsPerZone, max_parts);

        auto make_partitions = [&](const std::vector<float>& src) {
            std::vector<std::vector<Complex>> partitions(zone.partition_count);
            for (uint32_t p = 0; p < zone.partition_count; ++p) {
                partitions[p].assign(zone.fft_size, {});
                const uint32_t src_offset = zone.ir_offset + p * zone.partition_size;
                if (src_offset >= src.size()) break;
                const uint32_t copy_count = std::min<uint32_t>(
                    zone.partition_size,
                    static_cast<uint32_t>(src.size()) - src_offset);
                for (uint32_t i = 0; i < copy_count; ++i)
                    partitions[p][i].re = src[src_offset + i];
                fft(partitions[p].data(), zone.fft_size, false, backend);
            }
            return partitions;
        };

        zone.tail_ll = make_partitions(ir.ll);
        zone.tail_lr = make_partitions(ir.lr);
        zone.tail_rl = make_partitions(ir.rl);
        zone.tail_rr = make_partitions(ir.rr);

        zone.input_history_l.assign(zone.partition_count, std::vector<Complex>(zone.fft_size));
        zone.input_history_r.assign(zone.partition_count, std::vector<Complex>(zone.fft_size));
        zone.input_accum_l.assign(zone.partition_size, 0.0f);
        zone.input_accum_r.assign(zone.partition_size, 0.0f);
        zone.fft_l.assign(zone.fft_size, {});
        zone.fft_r.assign(zone.fft_size, {});
        zone.fft_sum_l.assign(zone.fft_size, {});
        zone.fft_sum_r.assign(zone.fft_size, {});

        next.max_fft_size = std::max(next.max_fft_size, zone.fft_size);
        next.total_partition_count += zone.partition_count;
        next.latency_samples = std::max(next.latency_samples, zone.partition_size);

        const uint32_t consumed = zone.partition_count * zone.partition_size;
        ir_offset += consumed;
        remaining = remaining > consumed ? remaining - consumed : 0;
        next.zones.push_back(std::move(zone));

        if (current_size < kZoneSizeCap)
            current_size = std::min(current_size * 2u, kZoneSizeCap);
    }

    // Shared tail accumulator. Every zone overlap-adds into this buffer at
    // offset `ir_offset - partition_size + block_size`, and the head `block_size`
    // samples are emitted every block before a left-shift. Size must cover the
    // farthest write a zone can make, which is (ir_offset_k + partition_size_k)
    // for the last zone, plus block_size headroom for the per-block shift.
    uint32_t tail_accum_size = frames;
    for (const auto& z : next.zones) {
        const uint32_t max_end = z.ir_offset + z.partition_size + frames;
        tail_accum_size = std::max(tail_accum_size, max_end);
    }
    next.tail_accum_size = tail_accum_size;
    next.tail_accum_l.assign(tail_accum_size, 0.0f);
    next.tail_accum_r.assign(tail_accum_size, 0.0f);

    next.prev_l.assign(std::max<uint32_t>(1, early_len), 0.0f);
    next.prev_r.assign(std::max<uint32_t>(1, early_len), 0.0f);
    next.wet_l.assign(frames, 0.0f);
    next.wet_r.assign(frames, 0.0f);

    plan_ = std::move(next);
    has_plan_ = true;
    ++plan_rebuild_count_;

#if VIVID_ACCELERATE_ENABLED
    // Create one Accelerate FFT setup per distinct log2(fft_size) across zones.
    destroy_accel_setups();
    for (const auto& zone : plan_.zones) {
        const int log2n = log2_size(zone.fft_size);
        bool have = false;
        for (const auto& s : fft_setups_) if (s.log2n == log2n) { have = true; break; }
        if (have) continue;
        AccelSetup s{};
        s.log2n = log2n;
        s.setup = vDSP_create_fftsetup(static_cast<vDSP_Length>(log2n), kFFTRadix2);
        fft_setups_.push_back(s);
    }
    accel_real_.assign(plan_.max_fft_size, 0.0f);
    accel_imag_.assign(plan_.max_fft_size, 0.0f);
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
    FFTSetup setup = nullptr;
    for (const auto& s : fft_setups_) {
        if (s.log2n == log2n) { setup = s.setup; break; }
    }
    if (!setup) return false;
    if (accel_real_.size() < n) {
        accel_real_.assign(n, 0.0f);
        accel_imag_.assign(n, 0.0f);
    }
    for (uint32_t i = 0; i < n; ++i) {
        accel_real_[i] = data[i].re;
        accel_imag_[i] = data[i].im;
    }
    DSPSplitComplex split{accel_real_.data(), accel_imag_.data()};
    vDSP_fft_zip(setup, &split, 1, static_cast<vDSP_Length>(log2n),
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
    if (plan_.zones.empty()) return;

    // Feed this block's input into every zone. Zones with partition_size == block_size
    // fire every block; larger zones fire every (partition_size / block_size) blocks.
    for (auto& zone : plan_.zones) {
        uint32_t consumed = 0;
        while (consumed < frames) {
            const uint32_t room = zone.partition_size - zone.input_fill;
            const uint32_t n = std::min(room, frames - consumed);
            std::copy(in_l + consumed, in_l + consumed + n,
                      zone.input_accum_l.begin() + zone.input_fill);
            std::copy(in_r + consumed, in_r + consumed + n,
                      zone.input_accum_r.begin() + zone.input_fill);
            zone.input_fill += n;
            consumed += n;
            if (zone.input_fill == zone.partition_size)
                submit_zone_partition(zone, backend);
        }
    }

    // Emit head of tail_accum into wet, then shift left by block_size.
    for (uint32_t i = 0; i < frames; ++i) {
        plan_.wet_l[i] += plan_.tail_accum_l[i];
        plan_.wet_r[i] += plan_.tail_accum_r[i];
    }
    std::move(plan_.tail_accum_l.begin() + frames, plan_.tail_accum_l.end(),
              plan_.tail_accum_l.begin());
    std::fill(plan_.tail_accum_l.end() - frames, plan_.tail_accum_l.end(), 0.0f);
    std::move(plan_.tail_accum_r.begin() + frames, plan_.tail_accum_r.end(),
              plan_.tail_accum_r.begin());
    std::fill(plan_.tail_accum_r.end() - frames, plan_.tail_accum_r.end(), 0.0f);
}

void Engine::submit_zone_partition(Zone& zone, Backend backend) {
    if (zone.partition_count == 0) return;

    // FFT the newly-filled input_accum (zero-padded to fft_size) and store in
    // the zone's frequency-delay-line history ring.
    std::fill(zone.fft_l.begin(), zone.fft_l.end(), Complex{});
    std::fill(zone.fft_r.begin(), zone.fft_r.end(), Complex{});
    for (uint32_t i = 0; i < zone.partition_size; ++i) {
        zone.fft_l[i].re = zone.input_accum_l[i];
        zone.fft_r[i].re = zone.input_accum_r[i];
    }
    fft(zone.fft_l.data(), zone.fft_size, false, backend);
    fft(zone.fft_r.data(), zone.fft_size, false, backend);

    zone.input_history_l[zone.history_pos] = zone.fft_l;
    zone.input_history_r[zone.history_pos] = zone.fft_r;

    std::fill(zone.fft_sum_l.begin(), zone.fft_sum_l.end(), Complex{});
    std::fill(zone.fft_sum_r.begin(), zone.fft_sum_r.end(), Complex{});

    auto madd = [](Complex x, Complex h, Complex& y) {
        y.re += x.re * h.re - x.im * h.im;
        y.im += x.re * h.im + x.im * h.re;
    };

    for (uint32_t p = 0; p < zone.partition_count; ++p) {
        const uint32_t hist = (zone.history_pos + zone.partition_count - p) % zone.partition_count;
        const auto& xl = zone.input_history_l[hist];
        const auto& xr = zone.input_history_r[hist];
        const auto& hll = zone.tail_ll[p];
        const auto& hlr = zone.tail_lr[p];
        const auto& hrl = zone.tail_rl[p];
        const auto& hrr = zone.tail_rr[p];
        for (uint32_t i = 0; i < zone.fft_size; ++i) {
            madd(xl[i], hll[i], zone.fft_sum_l[i]);
            madd(xr[i], hrl[i], zone.fft_sum_l[i]);
            madd(xl[i], hlr[i], zone.fft_sum_r[i]);
            madd(xr[i], hrr[i], zone.fft_sum_r[i]);
        }
    }

    fft(zone.fft_sum_l.data(), zone.fft_size, true, backend);
    fft(zone.fft_sum_r.data(), zone.fft_size, true, backend);

    // Overlap-add the IFFT output into the shared tail_accum. Derivation:
    //   tail_accum[i] at emit time of host block k represents the deferred
    //   contribution to output at absolute time kB + i (invariant preserved by
    //   the per-block left-shift of tail_accum). A zone firing at block k with
    //   partition_size N contributes output at time kB + ir_offset + n + (B - N)
    //   for n in [0, 2N-1], so the zone writes at offset (ir_offset - N + B).
    //   With ir_offset >= N (enforced by the zone schedule), the write offset
    //   is always >= 0.
    const uint32_t write_offset = zone.ir_offset + plan_.block_size - zone.partition_size;
    for (uint32_t i = 0; i < zone.fft_size; ++i) {
        plan_.tail_accum_l[write_offset + i] += zone.fft_sum_l[i].re;
        plan_.tail_accum_r[write_offset + i] += zone.fft_sum_r[i].re;
    }

    zone.history_pos = (zone.history_pos + 1u) % zone.partition_count;
    zone.input_fill = 0;
    std::fill(zone.input_accum_l.begin(), zone.input_accum_l.end(), 0.0f);
    std::fill(zone.input_accum_r.begin(), zone.input_accum_r.end(), 0.0f);
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
    uint32_t ir_frames_total = plan_.early_len;
    for (const auto& z : plan_.zones)
        ir_frames_total += z.partition_count * z.partition_size;
    last_stats_.ir_frames = ir_frames_total;
    last_stats_.partition_count = plan_.total_partition_count;
    last_stats_.zone_count = static_cast<uint32_t>(plan_.zones.size());
    last_stats_.latency_samples = plan_.latency_samples;
    last_stats_.plan_rebuild_count = plan_rebuild_count_;
}

} // namespace vivid::convolution_reverb_dsp
