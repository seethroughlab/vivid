#include "audio_clip.h"
#include "sample_bank.h"
#include "signalsmith-stretch.h"
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace vivid_sampler;

namespace {
    constexpr uint32_t kMaxBlockSize   = 512;
    constexpr uint32_t kMaxSpeedRatio  = 8;
    constexpr uint32_t kMaxInputFrames = kMaxBlockSize * kMaxSpeedRatio + 64;
}

// Full definition of ClipState — not visible to the editor, which only uses
// the embedded AudioClipWaveform via display_waveform_.
struct AudioClip::ClipState {
    std::vector<float>  samples_L;
    std::vector<float>  samples_R;
    uint32_t            frame_count      = 0;
    uint32_t            file_sample_rate = 0;  // always == device rate after load
    float               detected_bpm     = 0.0f;
    signalsmith::stretch::SignalsmithStretch<float> stretcher;
    float scratch_in_L[kMaxInputFrames];
    float scratch_in_R[kMaxInputFrames];
    AudioClipWaveform   waveform;
};

void AudioClip::build_waveform_bins(ClipState* s) {
    const uint32_t n    = s->frame_count;
    const uint32_t bins = std::min(n, AudioClipWaveform::kBins);
    s->waveform.actual_bins      = bins;
    s->waveform.frame_count      = n;
    s->waveform.file_sample_rate = s->file_sample_rate;
    s->waveform.duration_sec     = (s->file_sample_rate > 0)
        ? static_cast<double>(n) / s->file_sample_rate : 0.0;

    for (uint32_t b = 0; b < bins; ++b) {
        const uint32_t f0 = static_cast<uint64_t>(b)   * n / bins;
        const uint32_t f1 = static_cast<uint64_t>(b+1) * n / bins;
        float mnL =  1.f, mxL = -1.f, mnR =  1.f, mxR = -1.f;
        for (uint32_t f = f0; f < f1; ++f) {
            if (s->samples_L[f] < mnL) mnL = s->samples_L[f];
            if (s->samples_L[f] > mxL) mxL = s->samples_L[f];
            if (s->samples_R[f] < mnR) mnR = s->samples_R[f];
            if (s->samples_R[f] > mxR) mxR = s->samples_R[f];
        }
        s->waveform.bins[b] = {mnL, mxL, mnR, mxR};
    }
}

// -------------------------------------------------------------------------
AudioClip::~AudioClip() {
    delete clip_.load(std::memory_order_relaxed);
    delete deferred_delete_;
}

void AudioClip::collect_params(std::vector<vivid::ParamBase*>& out) {
    out.push_back(&file);
    out.push_back(&auto_play);
    out.push_back(&loop);
    out.push_back(&loop_start);
    out.push_back(&loop_end);
    out.push_back(&volume);
    out.push_back(&speed);
    out.push_back(&pitch);
    out.push_back(&file_bpm);
    out.push_back(&rate_mode);
    out.push_back(&stretch);
    out.push_back(&clip_start);
    out.push_back(&clip_end);
    vivid::display_hint(warp_points, VIVID_DISPLAY_HIDDEN);
    out.push_back(&warp_points);
}

void AudioClip::collect_ports(std::vector<VividPortDescriptor>& out) {
    // Input port indices: play=0, stop=1, beat_phase=2
    // Output port indices: audio_out=0, position_out=1, done_out=2, analysis=3+
    out.push_back({"play",       VIVID_PORT_SCALAR,       VIVID_PORT_INPUT,
                   VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "trigger"});
    out.push_back({"stop",       VIVID_PORT_SCALAR,       VIVID_PORT_INPUT,
                   VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "trigger"});
    out.push_back({"beat_phase", VIVID_PORT_SCALAR,       VIVID_PORT_INPUT,
                   VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
    out.push_back({"audio_out",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT,
                   VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2});
    out.push_back({"position_out", VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT,
                   VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
    out.push_back({"done_out",     VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT,
                   VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
    vivid::append_analysis_ports(out);
}

// -------------------------------------------------------------------------
// File loading — main thread only
// -------------------------------------------------------------------------
void AudioClip::main_thread_update(double /*time*/) {
    delete deferred_delete_;
    deferred_delete_ = nullptr;

    const std::string& path = file.str_value;
    if (path == last_path_) return;

    if (path.empty()) {
        last_path_        = path;
        display_waveform_ = nullptr;
        detected_bpm_     = 0.0f;
        deferred_delete_  = clip_.exchange(nullptr, std::memory_order_acq_rel);
        return;
    }

    // Wait until the audio thread reports its sample rate; retry next frame.
    const uint32_t sr = known_sr_.load(std::memory_order_relaxed);
    if (sr == 0) return;

    auto sd = decode_wav(path);
    if (!sd || sd->samples_L.empty()) {
        last_path_        = path;
        display_waveform_ = nullptr;
        detected_bpm_     = 0.0f;
        deferred_delete_  = clip_.exchange(nullptr, std::memory_order_acq_rel);
        return;
    }

    auto* state = new ClipState();
    state->samples_L        = std::move(sd->samples_L);
    state->frame_count      = static_cast<uint32_t>(state->samples_L.size());
    state->file_sample_rate = sd->sample_rate;
    state->detected_bpm     = sd->tempo_bpm;
    state->samples_R        = sd->stereo ? std::move(sd->samples_R) : state->samples_L;
    detected_bpm_           = sd->tempo_bpm;

    // Resample to device rate so pitch is correct regardless of file sample rate.
    if (state->file_sample_rate != sr && state->frame_count > 0) {
        const float    ratio     = static_cast<float>(state->file_sample_rate) / static_cast<float>(sr);
        const uint32_t new_count = static_cast<uint32_t>(state->frame_count / ratio + 0.5f);
        std::vector<float> rL(new_count), rR(new_count);
        for (uint32_t i = 0; i < new_count; ++i) {
            const float  src  = i * ratio;
            const uint32_t p0 = static_cast<uint32_t>(src);
            const float  frac = src - static_cast<float>(p0);
            const uint32_t p1 = (p0 + 1 < state->frame_count) ? p0 + 1 : p0;
            rL[i] = state->samples_L[p0] + frac * (state->samples_L[p1] - state->samples_L[p0]);
            rR[i] = state->samples_R[p0] + frac * (state->samples_R[p1] - state->samples_R[p0]);
        }
        state->samples_L        = std::move(rL);
        state->samples_R        = std::move(rR);
        state->frame_count      = new_count;
        state->file_sample_rate = sr;
    }

    state->stretcher.presetCheaper(2, static_cast<double>(sr));
    std::memset(state->scratch_in_L, 0, sizeof(state->scratch_in_L));
    std::memset(state->scratch_in_R, 0, sizeof(state->scratch_in_R));

    build_waveform_bins(state);

    last_path_       = path;
    deferred_delete_ = clip_.exchange(state, std::memory_order_acq_rel);
    display_waveform_ = &state->waveform;
}

// -------------------------------------------------------------------------
// Audio processing
// -------------------------------------------------------------------------
void AudioClip::process_audio(const VividAudioContext* ctx) {
    const uint32_t N  = ctx->buffer_size;
    const uint32_t sr = ctx->sample_rate;
    known_sr_.store(sr, std::memory_order_relaxed);

    float* out_L = ctx->output_buffers[0];
    float* out_R = ctx->output_buffers[0] + N;
    float* pos_out = ctx->output_buffers[1];

    ClipState* state = clip_.load(std::memory_order_acquire);

    // No file loaded
    if (!state || state->frame_count == 0) {
        std::memset(out_L, 0, N * sizeof(float));
        std::memset(out_R, 0, N * sizeof(float));
        emit_scalars(ctx, N, 0.0f, 0.0f);
        if (state != last_clip_seen_) {
            last_clip_seen_    = state;
            is_playing_        = false;
            drain_frames_left_ = 0;
        }
        return;
    }

    // Snapshot params
    const bool    p_auto   = auto_play.int_value() != 0;
    const bool    p_loop   = loop.int_value() != 0;
    const float   p_volume = volume.value;
    const float   p_speed  = speed.value;
    const float   p_pitch  = pitch.value;
    const float   p_fbpm   = (file_bpm.value > 0.0f) ? file_bpm.value : state->detected_bpm;
    const int     p_rmode  = rate_mode.int_value();
    const bool    p_stretch= stretch.int_value() != 0;

    // Clip region
    const float   p_cs_n   = std::max(0.0f, std::min(clip_start.value, 0.9999f));
    const float   p_ce_n   = std::max(p_cs_n + 1e-4f, std::min(clip_end.value, 1.0f));
    const uint32_t cs      = static_cast<uint32_t>(p_cs_n * state->frame_count);
    const uint32_t ce      = std::min(static_cast<uint32_t>(p_ce_n * state->frame_count),
                                      state->frame_count);

    // Loop region — clamped inside clip region
    const float   p_ls_n   = std::max(p_cs_n, std::min(loop_start.value, p_ce_n - 1e-4f));
    const float   p_le_n   = std::max(p_ls_n + 1e-4f, std::min(loop_end.value, p_ce_n));
    const uint32_t ls      = static_cast<uint32_t>(p_ls_n * state->frame_count);
    const uint32_t le      = std::min(static_cast<uint32_t>(p_le_n * state->frame_count), ce);

    const uint32_t clip_region = ce > cs ? ce - cs : 1;

    // Detect new clip state: reset playback, auto-play if enabled
    if (state != last_clip_seen_) {
        last_clip_seen_    = state;
        done_pulse_        = false;
        last_pitch_        = 999.0f;
        drain_frames_left_ = 0;
        prev_sync_phase_   = -1.0;
        state->stretcher.reset();
        playback_pos_ = static_cast<double>(cs);
        is_playing_   = p_auto;
    }

    // Per-sample trigger scan
    const float* play_buf = ctx->input_buffers[0];
    const float* stop_buf = ctx->input_buffers[1];
    bool play_edge = false, stop_edge = false;
    for (uint32_t s = 0; s < N; ++s) {
        const float pv = play_buf ? play_buf[s] : 0.0f;
        const float sv = stop_buf ? stop_buf[s] : 0.0f;
        if (sv > 0.5f && prev_stop_ <= 0.5f) stop_edge = true;
        if (pv > 0.5f && prev_play_ <= 0.5f) play_edge = true;
        prev_play_ = pv;
        prev_stop_ = sv;
    }
    if (stop_edge) {
        is_playing_        = false;
        done_pulse_        = false;
        drain_frames_left_ = 0;
    }
    if (play_edge) {
        is_playing_        = true;
        done_pulse_        = false;
        drain_frames_left_ = 0;
        playback_pos_      = static_cast<double>(cs);
        last_pitch_        = 999.0f;
        state->stretcher.reset();
    }

    // Drain mode: flush stretcher look-ahead tail after a non-looping clip ends
    if (drain_frames_left_ > 0) {
        std::memset(state->scratch_in_L, 0, N * sizeof(float));
        std::memset(state->scratch_in_R, 0, N * sizeof(float));
        const float* fi[2] = { state->scratch_in_L, state->scratch_in_R };
        float*       fo[2] = { out_L, out_R };
        state->stretcher.process(fi, N, fo, N);
        for (uint32_t s = 0; s < N; ++s) {
            out_L[s] *= p_volume;
            out_R[s] *= p_volume;
        }
        drain_frames_left_ = drain_frames_left_ > N ? drain_frames_left_ - N : 0;
        if (drain_frames_left_ == 0) {
            is_playing_ = false;
            done_pulse_ = true;
        }
        const float done_val = done_pulse_ ? 1.0f : 0.0f;
        emit_scalars(ctx, N, 1.0f, done_val);
        done_pulse_ = false;
        return;
    }

    // ---- Sync mode: phase-locked to metronome, loop length intrinsic to clip ----
    if (p_rmode == vivid::kRateModeMetronome) {
        auto silence = [&]() {
            std::memset(out_L, 0, N * sizeof(float));
            std::memset(out_R, 0, N * sizeof(float));
            emit_scalars(ctx, N, 0.0f, 0.0f);
        };

        if (p_fbpm <= 0.0f) { silence(); return; }

        const vivid::MetronomeTransport metro = vivid::metronome_transport(ctx);
        if (metro.bpm <= 0.0f) { silence(); return; }

        // Loop length in beats at file's native tempo, spanning the clip region
        const double clip_frames = static_cast<double>(ce - cs);
        const double loop_beats  = p_fbpm * clip_frames / (state->file_sample_rate * 60.0);
        if (loop_beats <= 0.0) { silence(); return; }

        // Derive loop phase from global beat clock
        const double loop_phase = std::fmod(metro.beats_elapsed, loop_beats) / loop_beats;

        // Detect loop wrap; force pitch re-apply on next buffer
        if (prev_sync_phase_ >= 0.0 && loop_phase < prev_sync_phase_ - 0.5) {
            last_pitch_ = 999.0f;
        }
        prev_sync_phase_ = loop_phase;

        // Position is always derived from the clock, not accumulated
        playback_pos_ = static_cast<double>(cs) + loop_phase * clip_frames;

        if (p_stretch) {
            if (std::fabs(p_pitch - last_pitch_) > 0.001f) {
                state->stretcher.setTransposeSemitones(p_pitch);
                last_pitch_ = p_pitch;
            }
            const float eff_speed = std::max(0.1f, std::min(
                static_cast<float>(metro.bpm / p_fbpm), static_cast<float>(kMaxSpeedRatio)));
            uint32_t input_frames = static_cast<uint32_t>(N * eff_speed + 0.5f);
            input_frames = std::max(1u, std::min(input_frames, kMaxInputFrames));
            gather_source(state, cs, ce, cs, ce, true, input_frames);
            const float* fi[2] = { state->scratch_in_L, state->scratch_in_R };
            float*       fo[2] = { out_L, out_R };
            state->stretcher.process(fi, input_frames, fo, N);
        } else {
            // Per-sample scrubbing — tape-style (pitch follows tempo)
            const double beats_per_sample = metro.bpm / 60.0 / static_cast<double>(sr);
            for (uint32_t s = 0; s < N; ++s) {
                const double sb  = metro.beats_elapsed + static_cast<double>(s) * beats_per_sample;
                const double sp  = std::fmod(sb, loop_beats) / loop_beats;
                const double pd  = static_cast<double>(cs) + sp * clip_frames;
                const auto   p0  = static_cast<uint32_t>(pd);
                const float  frc = static_cast<float>(pd - p0);
                const uint32_t p0c = (p0 < ce) ? p0 : (ce > 0 ? ce - 1 : 0);
                const uint32_t p1  = (p0c + 1 < ce) ? p0c + 1 : p0c;
                out_L[s] = state->samples_L[p0c] + frc * (state->samples_L[p1] - state->samples_L[p0c]);
                out_R[s] = state->samples_R[p0c] + frc * (state->samples_R[p1] - state->samples_R[p0c]);
                if (pos_out) pos_out[s] = static_cast<float>(sp);
            }
            const double last_b = metro.beats_elapsed + static_cast<double>(N - 1) * beats_per_sample;
            const double last_p = std::fmod(last_b, loop_beats) / loop_beats;
            playback_pos_ = static_cast<double>(cs) + last_p * clip_frames;
        }

        for (uint32_t s = 0; s < N; ++s) {
            out_L[s] *= p_volume;
            out_R[s] *= p_volume;
        }

        const double final_b = metro.beats_elapsed +
            static_cast<double>(N - 1) * metro.bpm / 60.0 / static_cast<double>(sr);
        const double final_p = std::fmod(final_b, loop_beats) / loop_beats;
        if (p_stretch) {
            emit_scalars(ctx, N, static_cast<float>(final_p), 0.0f);
        } else {
            if (ctx->output_buffers[2]) std::memset(ctx->output_buffers[2], 0, N * sizeof(float));
        }
        return;
    }

    if (!is_playing_) {
        std::memset(out_L, 0, N * sizeof(float));
        std::memset(out_R, 0, N * sizeof(float));
        emit_scalars(ctx, N, 0.0f, 0.0f);
        return;
    }

    // External mode: beat_phase[s] scrubs the clip region per sample
    if (p_rmode == vivid::kRateModeExternal) {
        const float* bp = ctx->input_buffers[2];
        if (ctx->output_buffers[2]) std::memset(ctx->output_buffers[2], 0, N * sizeof(float));
        for (uint32_t s = 0; s < N; ++s) {
            const float phase = bp ? std::max(0.0f, std::min(bp[s], 1.0f)) : 0.0f;
            const double pos_d = cs + static_cast<double>(phase * clip_region);
            const auto   p0   = static_cast<uint32_t>(pos_d);
            const float  frac = static_cast<float>(pos_d - p0);
            const uint32_t p0c = (p0 < ce) ? p0 : (ce > 0 ? ce - 1 : 0);
            const uint32_t p1  = (p0c + 1 < ce) ? p0c + 1 : p0c;
            out_L[s] = (state->samples_L[p0c] + frac * (state->samples_L[p1] - state->samples_L[p0c])) * p_volume;
            out_R[s] = (state->samples_R[p0c] + frac * (state->samples_R[p1] - state->samples_R[p0c])) * p_volume;
            if (pos_out) pos_out[s] = phase;
        }
        const float last_phase = bp ? std::max(0.0f, std::min(bp[N - 1], 1.0f)) : 0.0f;
        playback_pos_ = cs + static_cast<double>(last_phase * clip_region);
        return;
    }

    // ---- Free mode ----
    float eff_speed = p_speed;
    eff_speed = std::max(0.1f, std::min(eff_speed, static_cast<float>(kMaxSpeedRatio)));

    bool hit_end = false;

    if (p_stretch) {
        if (std::fabs(p_pitch - last_pitch_) > 0.001f) {
            state->stretcher.setTransposeSemitones(p_pitch);
            last_pitch_ = p_pitch;
        }
        uint32_t input_frames = static_cast<uint32_t>(N * eff_speed + 0.5f);
        input_frames = std::max(1u, std::min(input_frames, kMaxInputFrames));
        hit_end = gather_source(state, cs, ce, ls, le, p_loop, input_frames);
        const float* fi[2] = { state->scratch_in_L, state->scratch_in_R };
        float*       fo[2] = { out_L, out_R };
        state->stretcher.process(fi, input_frames, fo, N);
    } else {
        hit_end = simple_render(state, out_L, out_R, N, cs, ce, ls, le, p_loop, eff_speed, pos_out);
    }

    for (uint32_t s = 0; s < N; ++s) {
        out_L[s] *= p_volume;
        out_R[s] *= p_volume;
    }

    if (hit_end && !p_loop) {
        if (p_stretch) {
            drain_frames_left_ = static_cast<uint32_t>(state->stretcher.inputLatency());
            if (drain_frames_left_ == 0) {
                is_playing_ = false;
                done_pulse_ = true;
            }
        } else {
            is_playing_ = false;
            done_pulse_ = true;
        }
    }

    const float position = std::max(0.0f, std::min(
        static_cast<float>(playback_pos_ - cs) / static_cast<float>(clip_region), 1.0f));
    const float done_val = done_pulse_ ? 1.0f : 0.0f;
    if (p_stretch) {
        emit_scalars(ctx, N, position, done_val);
    } else if (ctx->output_buffers[2]) {
        std::memset(ctx->output_buffers[2], 0, N * sizeof(float));
        ctx->output_buffers[2][N - 1] = done_val;
    }
    done_pulse_ = false;
}

// -------------------------------------------------------------------------
bool AudioClip::gather_source(ClipState* state, uint32_t cs, uint32_t ce,
                               uint32_t ls, uint32_t le,
                               bool p_loop, uint32_t count) {
    uint32_t cur     = static_cast<uint32_t>(playback_pos_);
    uint32_t filled  = 0;
    bool     hit_end = false;

    while (filled < count) {
        if (p_loop) {
            if (cur >= le) {
                cur = ls;
                playback_pos_ = static_cast<double>(ls);
            }
            if (le <= cur) break;  // safety: degenerate loop region
            const uint32_t chunk = std::min(le - cur, count - filled);
            std::memcpy(state->scratch_in_L + filled,
                        state->samples_L.data() + cur, chunk * sizeof(float));
            std::memcpy(state->scratch_in_R + filled,
                        state->samples_R.data() + cur, chunk * sizeof(float));
            filled += chunk;
            cur    += chunk;
        } else {
            if (cur >= ce) { hit_end = true; break; }
            const uint32_t chunk = std::min(ce - cur, count - filled);
            std::memcpy(state->scratch_in_L + filled,
                        state->samples_L.data() + cur, chunk * sizeof(float));
            std::memcpy(state->scratch_in_R + filled,
                        state->samples_R.data() + cur, chunk * sizeof(float));
            filled += chunk;
            cur    += chunk;
        }
    }
    playback_pos_ = static_cast<double>(cur);
    // Zero-pad tail so the stretcher sees clean silence when we've hit the end
    std::memset(state->scratch_in_L + filled, 0, (count - filled) * sizeof(float));
    std::memset(state->scratch_in_R + filled, 0, (count - filled) * sizeof(float));
    return hit_end;
}

bool AudioClip::simple_render(ClipState* state, float* out_L, float* out_R,
                               uint32_t N, uint32_t cs, uint32_t ce,
                               uint32_t ls, uint32_t le,
                               bool p_loop, float advance, float* pos_out) {
    bool hit_end = false;
    const uint32_t clip_region = ce > cs ? ce - cs : 1;
    for (uint32_t s = 0; s < N; ++s) {
        if (p_loop && static_cast<uint32_t>(playback_pos_) >= le) {
            playback_pos_ = static_cast<double>(ls);
        } else if (!p_loop && static_cast<uint32_t>(playback_pos_) >= ce) {
            hit_end = true;
            out_L[s] = 0.0f;
            out_R[s] = 0.0f;
            if (pos_out) pos_out[s] = 1.0f;
            continue;
        }
        const auto   p0   = static_cast<uint32_t>(playback_pos_);
        const float  frac = static_cast<float>(playback_pos_ - p0);
        const uint32_t p1 = p_loop ? ((p0 + 1 < le) ? p0 + 1 : ls)
                                   : ((p0 + 1 < ce) ? p0 + 1 : p0);
        out_L[s] = state->samples_L[p0] + frac * (state->samples_L[p1] - state->samples_L[p0]);
        out_R[s] = state->samples_R[p0] + frac * (state->samples_R[p1] - state->samples_R[p0]);
        if (pos_out) {
            pos_out[s] = std::max(0.0f, std::min(
                static_cast<float>(playback_pos_ - cs) / static_cast<float>(clip_region), 1.0f));
        }
        playback_pos_ += advance;
    }
    return hit_end;
}

void AudioClip::emit_scalars(const VividAudioContext* ctx, uint32_t N,
                              float position, float done) {
    if (ctx->output_buffers[1]) {
        std::fill(ctx->output_buffers[1], ctx->output_buffers[1] + N, position);
    }
    if (ctx->output_buffers[2]) {
        std::memset(ctx->output_buffers[2], 0, N * sizeof(float));
        ctx->output_buffers[2][N - 1] = done;
    }
}

VIVID_DEFINE_OP(AudioClip) {
}

VIVID_EDITOR(AudioClip)

static const char* kAudioClipDropExts[] = {".wav"};
static const VividFileDropHandlerDescriptor kAudioClipFileDrops[] = {{
    "Audio Clip",
    kAudioClipDropExts,
    1,
    "file",
    90,
    "Create an Audio Clip node — linear playback with time stretch and BPM sync.",
}};

VIVID_FILE_DROP(kAudioClipFileDrops)
