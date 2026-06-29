#include "audio/audio_callback.h"

#include "app/app_state.h"
#include "transport.h"
#include "audio/vst3_host.h"

#include <algorithm>
#include <cmath>

namespace { constexpr double kPi = 3.14159265358979323846; }

// Real-time audio callback: render the hosted instrument's arpeggio (or a test
// tone if no plugin), advance the transport, and publish a block RMS level.
void audio_callback(ma_device* device, void* out, const void* /*in*/, ma_uint32 frames) {
    auto* st = static_cast<AudioState*>(device->pUserData);
    auto* fout = static_cast<float*>(out);
    const double sr = device->sampleRate;

    const double beats = st->transport ? st->transport->beats.load(std::memory_order_relaxed) : 0.0;
    const double bpm   = st->transport ? st->transport->bpm.load(std::memory_order_relaxed) : 120.0;

    bool rendered = false;
    if (st->session)
        rendered = vivid_poc::session_process(st->session, fout, frames,
                                              static_cast<uint32_t>(sr), bpm, beats, 4);
    if (!rendered) {
        const double inc = 2.0 * kPi * st->tone_hz / sr;
        for (ma_uint32 i = 0; i < frames; ++i) {
            float s = 0.05f * static_cast<float>(std::sin(st->phase));
            st->phase += inc;
            if (st->phase > 2.0 * kPi) st->phase -= 2.0 * kPi;
            fout[i * 2 + 0] = s;
            fout[i * 2 + 1] = s;
        }
    }

    if (st->transport) {
        st->transport->advance(frames, sr);
        const float a_lo = 1.f - std::exp(-6.2832f * 200.f / static_cast<float>(sr));
        const float a_hi = 1.f - std::exp(-6.2832f * 2000.f / static_cast<float>(sr));
        double sum_sq = 0.0, slo = 0.0, smi = 0.0, shi = 0.0;
        for (ma_uint32 i = 0; i < frames; ++i) {
            const float l = fout[i * 2];
            sum_sq += static_cast<double>(l) * l;
            st->m_flt_lo += (l - st->m_flt_lo) * a_lo;
            st->m_flt_hi += (l - st->m_flt_hi) * a_hi;
            const float lo = st->m_flt_lo, mi = st->m_flt_hi - st->m_flt_lo, hi = l - st->m_flt_hi;
            slo += static_cast<double>(lo) * lo; smi += static_cast<double>(mi) * mi; shi += static_cast<double>(hi) * hi;
        }
        const double inv = 1.0 / (frames > 0 ? frames : 1);
        const float rms = static_cast<float>(std::sqrt(sum_sq * inv));
        st->transport->level.store(rms, std::memory_order_relaxed);
        st->transport->band_low.store(static_cast<float>(std::sqrt(slo * inv)), std::memory_order_relaxed);
        st->transport->band_mid.store(static_cast<float>(std::sqrt(smi * inv)), std::memory_order_relaxed);
        st->transport->band_high.store(static_cast<float>(std::sqrt(shi * inv)), std::memory_order_relaxed);
        const float tr = std::max(0.0f, (rms - st->tr_baseline) * 6.0f);
        st->tr_baseline += (rms - st->tr_baseline) * 0.04f;
        st->transport->transient.store(std::min(1.0f, tr), std::memory_order_relaxed);
    }
}
