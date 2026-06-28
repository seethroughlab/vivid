#include "audio/sampler.h"
#include "miniaudio.h"
#include <cstdio>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace vivid_poc {

static size_t bar_samples(uint32_t sr, double bpm) {
    return static_cast<size_t>(static_cast<double>(sr) * 4.0 * 60.0 / (bpm > 0 ? bpm : 120.0));
}

Sampler gen_sub_pulse(uint32_t sr, double bpm) {
    Sampler s; s.name = "Sub Pulse"; s.loop_beats = 4.0;
    const double spb = 60.0 / bpm;
    const size_t total = bar_samples(sr, bpm);
    s.L.resize(total);
    for (size_t i = 0; i < total; ++i) {
        const double t = static_cast<double>(i) / sr;
        const double within = std::fmod(t / spb, 1.0);     // position within a beat
        const double env = std::exp(-within * 6.0);
        s.L[i] = static_cast<float>(std::sin(2.0 * M_PI * 55.0 * t) * env * 0.7);
    }
    return s;
}

Sampler gen_noise_sweep(uint32_t sr, double bpm) {
    Sampler s; s.name = "Noise Sweep"; s.loop_beats = 4.0;
    const size_t total = bar_samples(sr, bpm);
    s.L.resize(total);
    uint32_t rng = 2246822519u; float lp = 0.f;
    for (size_t i = 0; i < total; ++i) {
        rng = rng * 1664525u + 1013904223u;
        const float w = static_cast<float>((rng >> 9)) / 8388608.0f - 1.0f;  // -1..1
        const double prog = static_cast<double>(i) / total;
        const double cut = 0.02 + 0.4 * (0.5 - 0.5 * std::cos(prog * 2.0 * M_PI));  // sweep
        lp += static_cast<float>(cut) * (w - lp);
        s.L[i] = lp * 0.5f;
    }
    return s;
}

Sampler gen_bell_loop(uint32_t sr, double bpm) {
    Sampler s; s.name = "Bell Loop"; s.loop_beats = 4.0;
    const double spb = 60.0 / bpm;
    const size_t total = bar_samples(sr, bpm);
    s.L.assign(total, 0.f);
    const double strikes[] = { 0.0, 2.5 };
    const double partials[] = { 1.0, 2.76, 5.40 };
    for (double st : strikes) {
        const size_t s0 = static_cast<size_t>(st * spb * sr);
        for (size_t i = s0; i < total; ++i) {
            const double t = static_cast<double>(i - s0) / sr;
            const double env = std::exp(-t * 3.0);
            if (env < 0.001) break;
            double v = 0.0;
            for (double p : partials) v += std::sin(2.0 * M_PI * 440.0 * p * t);
            s.L[i] += static_cast<float>(v / 3.0 * env * 0.4);
        }
    }
    return s;
}

bool sampler_load_wav(const std::string& path, uint32_t sr_hint, double bpm, Sampler& out) {
    ma_decoder_config cfg = ma_decoder_config_init(ma_format_f32, 2, sr_hint);
    ma_decoder dec;
    if (ma_decoder_init_file(path.c_str(), &cfg, &dec) != MA_SUCCESS) {
        std::fprintf(stderr, "[Sampler] failed to decode %s\n", path.c_str());
        return false;
    }
    ma_uint64 frames = 0;
    ma_decoder_get_length_in_pcm_frames(&dec, &frames);
    if (frames == 0) { ma_decoder_uninit(&dec); return false; }

    std::vector<float> inter(static_cast<size_t>(frames) * 2);
    ma_uint64 read = 0;
    ma_decoder_read_pcm_frames(&dec, inter.data(), frames, &read);
    const ma_uint32 sr = dec.outputSampleRate;
    ma_decoder_uninit(&dec);

    out.L.resize(static_cast<size_t>(read));
    out.R.resize(static_cast<size_t>(read));
    for (size_t i = 0; i < read; ++i) { out.L[i] = inter[2 * i]; out.R[i] = inter[2 * i + 1]; }

    const double secs = static_cast<double>(read) / (sr > 0 ? sr : 44100);
    const double natural_beats = secs * bpm / 60.0;
    const double bars = std::max(1.0, std::round(natural_beats / 4.0));
    out.loop_beats = bars * 4.0;
    out.name = path.substr(path.find_last_of('/') + 1);
    std::fprintf(stderr, "[Sampler] loaded %s (%.2fs, %.0f beats)\n", out.name.c_str(), secs, out.loop_beats);
    return true;
}

}  // namespace vivid_poc
