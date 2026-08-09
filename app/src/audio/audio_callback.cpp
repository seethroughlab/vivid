#include "audio/audio_callback.h"

#include "app/app.h"
#include "transport.h"
#include "audio/vst3_host.h"
#include "operator_api/movie_audio.h"   // vivid_movie_audio_set_playing (gates the movie-audio bus)
#include "audio/movie_audio_bus.h"      // movie_audio_begin_block / movie_audio_mix_master
#include "audio/audio_health.h"         // ADR-0031 §3: RT health counters + RtScope
#include "audio/audio_budgets.h"        // ADR-0031 §6: callback budget (over-budget threshold)

#include <algorithm>
#include <chrono>
#include <cmath>

namespace { constexpr double kPi = 3.14159265358979323846; }

// Real-time audio callback: render the hosted instrument's arpeggio (or a test
// tone if no plugin), advance the transport, and publish a block RMS level.
// device->pUserData is the shared App (never a Window).
void audio_callback(ma_device* device, void* out, const void* /*in*/, ma_uint32 frames) {
    auto* a = static_cast<vivid::App*>(device->pUserData);
    auto* fout = static_cast<float*>(out);
    const double sr = device->sampleRate;

    // ADR-0031 §3: mark the realtime scope (gates in-session_process handoff-skip counting so the
    // offline bounce never ticks it) and time the whole callback for the over-budget counter.
    vivid::audio::health::RtScope rt_scope;
    const auto cb_t0 = std::chrono::steady_clock::now();
    vivid::audio::health::g_callbacks.fetch_add(1, std::memory_order_relaxed);
    // An oversized block is the one place session_process fills silence and bails (frames > max supported
    // block); count it as the render-bail-to-silence event before we hand it over.
    if (frames > vivid::audio::audio_budgets().max_block_frames)
        vivid::audio::health::g_render_bailouts.fetch_add(1, std::memory_order_relaxed);

    const double beats = a->transport ? a->transport->beats.load(std::memory_order_relaxed) : 0.0;
    const double bpm   = a->transport ? a->transport->bpm.load(std::memory_order_relaxed) : 120.0;
    const bool playing = a->transport ? a->transport->is_playing() : true;
    static bool was_playing = true;                    // audio thread only (single device)
    const bool release_all = was_playing && !playing;  // play->stop edge: flush held notes
    was_playing = playing;

    // Gate the movie-audio bus on transport state BEFORE the graph runs: a MovieAudio op's pull()
    // only drains (and advances its movie's master A/V clock) while playing, so paused freezes both
    // the movie sound and — via the shared clock — the video frame, in sync.
    vivid_movie_audio_set_playing(playing ? 1 : 0);
    vivid_movie_audio_set_device_rate(static_cast<float>(sr));   // the Video op must decode audio at this rate
    vivid::movie_audio_begin_block();                            // reset per-block MovieAudio-drained flags

    bool rendered = false;
    if (a->session)
        rendered = vivid::session::session_process(a->session, fout, frames,
                                              static_cast<uint32_t>(sr), bpm, beats, 4, playing, release_all);
    if (!rendered) {
        // Nothing to render (an empty session — no instruments) => SILENCE, not the old 110 Hz test
        // tone. The metronome click below still mixes in over the cleared buffer if it's enabled.
        std::fill(fout, fout + static_cast<size_t>(frames) * 2, 0.f);
    }

    // A lone Video node (no MovieAudio op) still plays its movie's audio: mix any movie channel the
    // graph didn't already drain straight into the master. Adding a MovieAudio node reroutes that
    // channel through the graph (and its effects) instead — this fallback then leaves it alone.
    vivid::movie_audio_mix_master(fout, frames, playing);

    // Metronome click (M6.3): a short decaying sine on each beat while enabled. Mixed into
    // the master before metering. The downbeat (every 4th beat) is accented higher.
    if (a->session && playing && bpm > 0.0 && vivid::session::session_get_metronome(a->session)) {
        const double bps = bpm / 60.0;
        const double end = beats + frames * bps / sr;
        const long long nb = static_cast<long long>(std::floor(beats)) + 1;   // next integer beat
        int trigger_off = -1;
        if (static_cast<double>(nb) <= end && nb != a->click_last_beat) {
            trigger_off = static_cast<int>((static_cast<double>(nb) - beats) / bps * sr);
            trigger_off = std::clamp(trigger_off, 0, static_cast<int>(frames) - 1);
            a->click_last_beat = nb;
            a->click_freq = (nb % 4 == 0) ? 1600.f : 1000.f;   // accent the downbeat
        }
        const float decay = std::exp(-1.f / (0.03f * static_cast<float>(sr)));   // ~30 ms tail
        double inc = 2.0 * kPi * a->click_freq / sr;
        for (ma_uint32 i = 0; i < frames; ++i) {
            if (static_cast<int>(i) == trigger_off) { a->click_amp = 0.5f; a->click_phase = 0.0; inc = 2.0 * kPi * a->click_freq / sr; }
            if (a->click_amp > 1e-4f) {
                const float s = a->click_amp * static_cast<float>(std::sin(a->click_phase));
                fout[i * 2 + 0] += s; fout[i * 2 + 1] += s;
                a->click_phase += inc;
                if (a->click_phase > 2.0 * kPi) a->click_phase -= 2.0 * kPi;
                a->click_amp *= decay;
            }
        }
    }

    if (a->transport) {
        a->transport->capture_write_interleaved(fout, frames, static_cast<uint32_t>(sr));
        a->transport->recording_tap_write(fout, frames);   // video export: drain the live master (lock-free, no-op when idle)
        a->transport->advance(frames, sr);
        const float a_lo = 1.f - std::exp(-6.2832f * 200.f / static_cast<float>(sr));
        const float a_hi = 1.f - std::exp(-6.2832f * 2000.f / static_cast<float>(sr));
        double sum_sq = 0.0, slo = 0.0, smi = 0.0, shi = 0.0;
        for (ma_uint32 i = 0; i < frames; ++i) {
            const float l = fout[i * 2];
            sum_sq += static_cast<double>(l) * l;
            a->m_flt_lo += (l - a->m_flt_lo) * a_lo;
            a->m_flt_hi += (l - a->m_flt_hi) * a_hi;
            const float lo = a->m_flt_lo, mi = a->m_flt_hi - a->m_flt_lo, hi = l - a->m_flt_hi;
            slo += static_cast<double>(lo) * lo; smi += static_cast<double>(mi) * mi; shi += static_cast<double>(hi) * hi;
        }
        const double inv = 1.0 / (frames > 0 ? frames : 1);
        const float rms = static_cast<float>(std::sqrt(sum_sq * inv));
        a->transport->level.store(rms, std::memory_order_relaxed);
        a->transport->band_low.store(static_cast<float>(std::sqrt(slo * inv)), std::memory_order_relaxed);
        a->transport->band_mid.store(static_cast<float>(std::sqrt(smi * inv)), std::memory_order_relaxed);
        a->transport->band_high.store(static_cast<float>(std::sqrt(shi * inv)), std::memory_order_relaxed);
        const float tr = std::max(0.0f, (rms - a->tr_baseline) * 6.0f);
        a->tr_baseline += (rms - a->tr_baseline) * 0.04f;
        a->transport->transient.store(std::min(1.0f, tr), std::memory_order_relaxed);
    }

    // ADR-0031 §3: publish this callback's wall time + credit an over-budget block if it exceeded the
    // block's realtime duration × the configured multiplier.
    const auto cb_us = std::chrono::duration_cast<std::chrono::microseconds>(
                           std::chrono::steady_clock::now() - cb_t0).count();
    vivid::audio::health::note_callback_us(static_cast<uint32_t>(cb_us), frames, sr,
                                           vivid::audio::audio_budgets().callback_budget_mult);
}
