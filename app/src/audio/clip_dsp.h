#pragma once
#include "audio/audio_clip.h"              // AudioClip + WarpMode
#include "audio/audio_clip_shared.h"    // source_for_warp_beat
#include "signalsmith-stretch.h"        // pitch-preserving time-stretch
#include <cstring>
#include <cmath>
#include <algorithm>

// Per-clip DSP state for the pitch-preserving warp modes (Complex/Beats). Non-copyable
// (owns a streaming stretcher); one is kept per live audio-clip slot (Track::aud_dsp) as a
// unique_ptr so its large scratch arrays have a stable address. init() runs on the UI/main
// thread — presetCheaper + a warm-up process() force all internal allocations there, so the
// audio thread's process() is allocation-free.
namespace vivid::session {

struct ClipDsp {
    static constexpr uint32_t kMaxBlock = 4096;             // >= any audio period
    static constexpr uint32_t kMaxIn    = kMaxBlock * 4 + 64;  // up to 4x playback speed

    signalsmith::stretch::SignalsmithStretch<float> st;
    float    scratchL[kMaxIn];
    float    scratchR[kMaxIn];
    float    last_pitch = 999.f;   // sentinel forces setTransposeSemitones on first block
    bool     ready = false;

    void init(double sr) {
        st.presetCheaper(2, static_cast<float>(sr));
        st.reset();
        std::memset(scratchL, 0, sizeof scratchL);
        std::memset(scratchR, 0, sizeof scratchR);
        float warmL[kMaxBlock] = {0}, warmR[kMaxBlock] = {0};
        float* ins[2]  = { scratchL, scratchR };
        float* outs[2] = { warmL, warmR };
        st.process(ins, static_cast<int>(kMaxIn), outs, static_cast<int>(kMaxBlock));  // force alloc
        st.reset();
        last_pitch = 999.f;
        ready = true;
    }
    void reset() { st.reset(); last_pitch = 999.f; }
};

// Render one block of `frames`. Complex/Beats warp modes go through the stretcher (pitch
// preserved, tempo follows the transport); everything else (non-warp, Repitch) uses the
// Varispeed render(). Allocation-free on the audio thread.
inline void process_clip(const AudioClip& c, ClipDsp& dsp,
                         double block_start_beats, double delta, uint32_t frames, uint32_t /*sr*/,
                         float* outL, float* outR, float trim0, float trim1) {
    const bool stretch = c.warp_enabled && (c.warp_mode == WarpMode::Complex || c.warp_mode == WarpMode::Beats);
    if (!stretch || !dsp.ready || frames > ClipDsp::kMaxBlock || c.L.empty() || c.loop_beats <= 0.0) {
        c.render(block_start_beats, delta, frames, outL, outR, trim0, trim1);   // varispeed path
        return;
    }
    const double N = static_cast<double>(c.L.size());
    auto interp = [&](const std::vector<float>& buf, double s) -> float {
        if (buf.empty()) return 0.f;
        double m = std::fmod(s, N); if (m < 0) m += N;
        const size_t i0 = static_cast<size_t>(m);
        const double fr = m - static_cast<double>(i0);
        const size_t i1 = (i0 + 1 < buf.size()) ? i0 + 1 : 0;
        return static_cast<float>(buf[i0] * (1.0 - fr) + buf[i1] * fr);
    };
    auto src_at = [&](double beat) -> double {
        double ph = std::fmod(beat, c.loop_beats); if (ph < 0) ph += c.loop_beats;
        if (!c.warp_points.empty()) return audio_clip_ed::source_for_warp_beat(c.warp_points, ph);
        return ph / c.loop_beats * N;   // no markers: whole buffer maps linearly across the loop
    };
    const double p0 = src_at(block_start_beats);
    double advance = src_at(block_start_beats + delta) - p0;
    if (advance <= 0.0) advance += N;   // loop wrap
    const uint32_t inN = static_cast<uint32_t>(std::clamp(std::round(advance), 1.0, static_cast<double>(ClipDsp::kMaxIn)));

    const std::vector<float>& src2 = c.R.empty() ? c.L : c.R;
    const double dir = c.reverse ? -1.0 : 1.0;
    for (uint32_t i = 0; i < inN; ++i) {
        const double s = p0 + dir * static_cast<double>(i);
        dsp.scratchL[i] = interp(c.L, s);
        dsp.scratchR[i] = interp(src2, s);
    }
    // Beats mode: reset the stretcher when a transient starts inside this block (tight attacks).
    if (c.warp_mode == WarpMode::Beats) {
        for (const auto& t : c.transients) {
            const double ts = static_cast<double>(t.source_sample);
            if (ts >= p0 && ts < p0 + advance) { dsp.reset(); break; }
        }
    }
    const float semis = c.pitch_semitones + c.detune_cents * 0.01f;
    if (std::fabs(semis - dsp.last_pitch) > 1e-3f) { dsp.st.setTransposeSemitones(semis); dsp.last_pitch = semis; }

    float* ins[2]  = { dsp.scratchL, dsp.scratchR };
    float* outs[2] = { outL, outR };
    dsp.st.process(ins, static_cast<int>(inN), outs, static_cast<int>(frames));
    if (c.gain != 1.0f)
        for (uint32_t i = 0; i < frames; ++i) { outL[i] *= c.gain; outR[i] *= c.gain; }
}

}  // namespace vivid::session
