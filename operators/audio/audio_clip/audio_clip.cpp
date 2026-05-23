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

    inline float interp_sample(const std::vector<float>& samples, double pos,
                               uint32_t start, uint32_t end) {
        if (samples.empty() || end <= start) return 0.0f;
        pos = std::clamp(pos, static_cast<double>(start),
                         static_cast<double>(end > 0 ? end - 1 : 0));
        const auto p0 = static_cast<uint32_t>(pos);
        const float frac = static_cast<float>(pos - p0);
        const uint32_t p1 = (p0 + 1 < end) ? p0 + 1 : p0;
        return samples[p0] + frac * (samples[p1] - samples[p0]);
    }
}

// Full definition of ClipState — not visible to the editor, which only uses
// the embedded AudioClipWaveform via display_waveform_.
struct AudioClip::ClipState {
    std::vector<float>  samples_L;
    std::vector<float>  samples_R;
    uint32_t            frame_count      = 0;
    uint32_t            file_sample_rate = 0;  // always == device rate after load
    float               detected_bpm     = 0.0f;
    std::vector<audio_clip_ed::WarpPoint> warp_points;
    std::vector<audio_clip_ed::TransientPoint> transient_points;
    std::vector<audio_clip_ed::SliceRegion> slice_regions;
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
    s->waveform.warp_markers = s->warp_points;
    s->waveform.transient_markers = s->transient_points;
    s->waveform.slice_regions = s->slice_regions;
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
    out.push_back(&warp_enabled);
    out.push_back(&warp_mode);
    vivid::display_hint(transient_points, VIVID_DISPLAY_HIDDEN);
    out.push_back(&transient_points);
    out.push_back(&show_transients);
    out.push_back(&transient_sensitivity);
    out.push_back(&launch_mode);
    out.push_back(&launch_quantize);
    out.push_back(&reverse);
    out.push_back(&fade_in_ms);
    out.push_back(&fade_out_ms);
    out.push_back(&loop_crossfade_ms);
    out.push_back(&slice_mode);
    vivid::display_hint(slice_points, VIVID_DISPLAY_HIDDEN);
    out.push_back(&slice_points);
    vivid::display_hint(slice_index, VIVID_DISPLAY_HIDDEN);
    vivid::editor_only(slice_index);
    out.push_back(&slice_index);
}

void AudioClip::collect_ports(std::vector<VividPortDescriptor>& out) {
    // Input port indices: play=0, stop=1, beat_phase=2, slice_index=3
    // Output port indices: audio_out=0, position_out=1, done_out=2,
    // launch_pending_out=3, slice_count_out=4, active_slice_out=5, analysis=6+
    out.push_back({"play",       VIVID_PORT_SCALAR,       VIVID_PORT_INPUT,
                   VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "trigger"});
    out.push_back({"stop",       VIVID_PORT_SCALAR,       VIVID_PORT_INPUT,
                   VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, "trigger"});
    out.push_back({"beat_phase", VIVID_PORT_SCALAR,       VIVID_PORT_INPUT,
                   VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
    out.push_back({"slice_index", VIVID_PORT_SCALAR,      VIVID_PORT_INPUT,
                   VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
    out.push_back({"audio_out",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT,
                   VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 2});
    out.push_back({"position_out", VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT,
                   VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
    out.push_back({"done_out",     VIVID_PORT_SCALAR,     VIVID_PORT_OUTPUT,
                   VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
    out.push_back({"launch_pending_out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT,
                   VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
    out.push_back({"slice_count_out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT,
                   VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
    out.push_back({"active_slice_out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT,
                   VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, -1.0f});
    vivid::append_analysis_ports(out);
}

// -------------------------------------------------------------------------
// File loading — main thread only
// -------------------------------------------------------------------------
void AudioClip::main_thread_update(double /*time*/) {
    delete deferred_delete_;
    deferred_delete_ = nullptr;

    const std::string& path = file.str_value;
    const std::string state_key =
        path + "|" + warp_points.str_value + "|" + transient_points.str_value + "|" +
        slice_points.str_value + "|" + std::to_string(clip_start.value) + "|" +
        std::to_string(clip_end.value) + "|" + std::to_string(transient_sensitivity.value) +
        "|" + std::to_string(slice_mode.int_value()) + "|" + std::to_string(file_bpm.value);
    if (state_key == last_state_key_) return;

    if (path.empty()) {
        last_path_        = path;
        last_state_key_   = state_key;
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
        last_state_key_   = state_key;
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

    const float cs_n = std::clamp(clip_start.value, 0.0f, 0.9999f);
    const float ce_n = std::max(cs_n + 1e-4f, std::clamp(clip_end.value, 0.0f, 1.0f));
    const uint32_t cs = static_cast<uint32_t>(cs_n * state->frame_count);
    const uint32_t ce = std::min(
        static_cast<uint32_t>(ce_n * state->frame_count),
        state->frame_count);
    const float source_bpm_for_warp = file_bpm.value > 0.0f ? file_bpm.value : state->detected_bpm;
    const double fallback_beats = (source_bpm_for_warp > 0.0f && state->file_sample_rate > 0)
        ? source_bpm_for_warp * static_cast<double>(std::max(1u, ce - cs)) /
              (static_cast<double>(state->file_sample_rate) * 60.0)
        : 4.0;

    const auto parsed_warp = audio_clip_ed::parse_warp_points(warp_points.str_value);
    state->warp_points = audio_clip_ed::compile_warp_points(parsed_warp, cs, ce, fallback_beats);

    const auto authored_transients = audio_clip_ed::parse_transient_points(transient_points.str_value);
    state->transient_points = authored_transients.empty()
        ? audio_clip_ed::detect_transients(state->samples_L, state->samples_R,
                                           state->file_sample_rate, transient_sensitivity.value)
        : authored_transients;
    const auto manual_slices = audio_clip_ed::parse_sample_points(slice_points.str_value);
    state->slice_regions = audio_clip_ed::compile_slices(slice_mode.int_value(),
                                                         state->transient_points,
                                                         manual_slices, cs, ce);

    state->stretcher.presetCheaper(2, static_cast<double>(sr));
    std::memset(state->scratch_in_L, 0, sizeof(state->scratch_in_L));
    std::memset(state->scratch_in_R, 0, sizeof(state->scratch_in_R));

    build_waveform_bins(state);

    last_path_       = path;
    last_state_key_  = state_key;
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
        fill_extra_outputs(ctx, N, 0.0f, 0.0f, -1.0f);
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
    const int     p_wmode  = warp_mode.int_value();
    const bool    p_stretch= (stretch.int_value() != 0) && p_wmode != 2;
    const bool    p_warp   = warp_enabled.int_value() != 0 && state->warp_points.size() >= 2;
    const bool    p_reverse = reverse.int_value() != 0;
    const int     p_launch_mode = launch_mode.int_value();
    const int     p_launch_quantize = launch_quantize.int_value();
    const int     p_slice_mode = slice_mode.int_value();

    // Clip region
    const float   p_cs_n   = std::max(0.0f, std::min(clip_start.value, 0.9999f));
    const float   p_ce_n   = std::max(p_cs_n + 1e-4f, std::min(clip_end.value, 1.0f));
    uint32_t cs      = static_cast<uint32_t>(p_cs_n * state->frame_count);
    uint32_t ce      = std::min(static_cast<uint32_t>(p_ce_n * state->frame_count),
                                state->frame_count);

    const float* slice_idx_buf = ctx->input_buffers[3];
    const float slice_idx_in = slice_idx_buf ? slice_idx_buf[0] : slice_index.value;
    const int slice_count = (p_slice_mode != 0) ? static_cast<int>(state->slice_regions.size()) : 0;

    // Loop region — clamped inside clip region
    float   p_ls_n   = std::max(p_cs_n, std::min(loop_start.value, p_ce_n - 1e-4f));
    float   p_le_n   = std::max(p_ls_n + 1e-4f, std::min(loop_end.value, p_ce_n));
    uint32_t ls      = static_cast<uint32_t>(p_ls_n * state->frame_count);
    uint32_t le      = std::min(static_cast<uint32_t>(p_le_n * state->frame_count), ce);

    uint32_t clip_region = ce > cs ? ce - cs : 1;

    // Detect new clip state: reset playback, auto-play if enabled
    if (state != last_clip_seen_) {
        last_clip_seen_    = state;
        done_pulse_        = false;
        last_pitch_        = 999.0f;
        drain_frames_left_ = 0;
        prev_sync_phase_   = -1.0;
        launch_pending_    = false;
        active_slice_idx_   = -1;
        state->stretcher.reset();
        playback_pos_ = p_reverse ? static_cast<double>(ce > 0 ? ce - 1 : cs)
                                  : static_cast<double>(cs);
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
        launch_pending_    = false;
    }
    const bool gate_held = play_buf && play_buf[N - 1] > 0.5f;
    if (p_launch_mode == 1 && !gate_held) {
        is_playing_ = false;
    }

    auto select_slice = [&]() {
        if (slice_count <= 0) {
            active_slice_idx_ = -1;
            return;
        }
        active_slice_idx_ = std::clamp(static_cast<int>(std::floor(slice_idx_in)),
                                       0, slice_count - 1);
    };
    auto apply_active_slice = [&]() {
        if (active_slice_idx_ >= 0 && active_slice_idx_ < slice_count) {
            cs = state->slice_regions[active_slice_idx_].start;
            ce = state->slice_regions[active_slice_idx_].end;
            clip_region = ce > cs ? ce - cs : 1;
        }
        p_ls_n = std::max(static_cast<float>(cs) / state->frame_count,
                          std::min(loop_start.value, static_cast<float>(ce) / state->frame_count - 1e-4f));
        p_le_n = std::max(p_ls_n + 1e-4f,
                          std::min(loop_end.value, static_cast<float>(ce) / state->frame_count));
        ls = std::max(cs, static_cast<uint32_t>(p_ls_n * state->frame_count));
        le = std::min(static_cast<uint32_t>(p_le_n * state->frame_count), ce);
    };
    auto start_playback = [&]() {
        apply_active_slice();
        is_playing_        = true;
        done_pulse_        = false;
        drain_frames_left_ = 0;
        launch_pending_    = false;
        playback_pos_      = p_reverse ? static_cast<double>(ce > 0 ? ce - 1 : cs)
                                       : static_cast<double>(cs);
        last_pitch_        = 999.0f;
        state->stretcher.reset();
    };
    auto queue_or_start = [&]() {
        select_slice();
        if (p_launch_quantize <= 0) {
            start_playback();
            return;
        }
        const auto metro = vivid::metronome_transport(ctx);
        launch_target_beat_ = audio_clip_ed::next_quantized_beat(
            metro.beats_elapsed, metro.beats_per_bar, p_launch_quantize);
        launch_pending_ = true;
    };

    if (play_edge) {
        if (p_launch_mode == 2) {
            if (is_playing_ || launch_pending_) {
                is_playing_ = false;
                launch_pending_ = false;
            } else {
                queue_or_start();
            }
        } else {
            queue_or_start();
        }
    }
    if (p_launch_mode == 1 && gate_held && !is_playing_ && !launch_pending_) {
        queue_or_start();
    }
    if (launch_pending_) {
        const auto metro = vivid::metronome_transport(ctx);
        if (metro.beats_elapsed + 1e-9 >= launch_target_beat_)
            start_playback();
    }
    apply_active_slice();
    const uint32_t fade_in_frames = static_cast<uint32_t>(
        std::min(fade_in_ms.value, 500.0f) * static_cast<float>(sr) / 1000.0f);
    const uint32_t fade_out_frames = static_cast<uint32_t>(
        std::min(fade_out_ms.value, 500.0f) * static_cast<float>(sr) / 1000.0f);
    const uint32_t crossfade_frames = static_cast<uint32_t>(
        std::min(loop_crossfade_ms.value, 200.0f) * static_cast<float>(sr) / 1000.0f);

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
        fill_extra_outputs(ctx, N, launch_pending_ ? 1.0f : 0.0f,
                           static_cast<float>(slice_count),
                           static_cast<float>(active_slice_idx_));
        done_pulse_ = false;
        return;
    }

    // ---- Sync mode: phase-locked to metronome, loop length intrinsic to clip ----
    if (p_rmode == vivid::kRateModeMetronome) {
        auto silence = [&]() {
            std::memset(out_L, 0, N * sizeof(float));
            std::memset(out_R, 0, N * sizeof(float));
            emit_scalars(ctx, N, 0.0f, 0.0f);
            fill_extra_outputs(ctx, N, launch_pending_ ? 1.0f : 0.0f,
                               static_cast<float>(slice_count),
                               static_cast<float>(active_slice_idx_));
        };

        const vivid::MetronomeTransport metro = vivid::metronome_transport(ctx);
        if (metro.bpm <= 0.0f) { silence(); return; }

        // Loop length in beats at file's native tempo, spanning the clip region
        const double clip_frames = static_cast<double>(ce - cs);
        if (!p_warp && p_fbpm <= 0.0f) { silence(); return; }
        const double loop_beats  = p_warp
            ? audio_clip_ed::warp_total_beats(state->warp_points)
            : p_fbpm * clip_frames / (state->file_sample_rate * 60.0);
        if (loop_beats <= 0.0) { silence(); return; }

        auto source_from_phase = [&](double phase) {
            double src = p_warp
                ? audio_clip_ed::source_for_warp_beat(state->warp_points, phase * loop_beats)
                : audio_clip_ed::source_for_normalized_phase(phase, cs, ce, false);
            if (p_reverse) src = audio_clip_ed::reverse_source_position(src, cs, ce > 0 ? ce - 1 : cs);
            return src;
        };

        // Derive loop phase from global beat clock
        const double loop_phase = std::fmod(metro.beats_elapsed, loop_beats) / loop_beats;

        // Detect loop wrap; force pitch re-apply on next buffer
        if (prev_sync_phase_ >= 0.0 && loop_phase < prev_sync_phase_ - 0.5) {
            last_pitch_ = 999.0f;
        }
        prev_sync_phase_ = loop_phase;

        // Position is always derived from the clock, not accumulated
        playback_pos_ = source_from_phase(loop_phase);

        if (p_stretch) {
            if (p_fbpm <= 0.0f) { silence(); return; }
            if (std::fabs(p_pitch - last_pitch_) > 0.001f) {
                state->stretcher.setTransposeSemitones(p_pitch);
                last_pitch_ = p_pitch;
            }
            const float eff_speed = std::max(0.1f, std::min(
                static_cast<float>(metro.bpm / p_fbpm), static_cast<float>(kMaxSpeedRatio)));
            uint32_t input_frames = static_cast<uint32_t>(N * eff_speed + 0.5f);
            input_frames = std::max(1u, std::min(input_frames, kMaxInputFrames));
            if (p_wmode == 1) {
                const double pos_end = playback_pos_ + (p_reverse ? -1.0 : 1.0) * input_frames;
                const double lo = std::min(playback_pos_, pos_end);
                const double hi = std::max(playback_pos_, pos_end);
                for (const auto& tp : state->transient_points) {
                    if (tp.source_sample > lo && tp.source_sample <= hi) {
                        state->stretcher.reset();
                        last_pitch_ = 999.0f;
                        break;
                    }
                }
            }
            gather_source(state, cs, ce, cs, ce, true, p_reverse,
                          fade_in_frames, fade_out_frames, crossfade_frames, input_frames);
            const float* fi[2] = { state->scratch_in_L, state->scratch_in_R };
            float*       fo[2] = { out_L, out_R };
            state->stretcher.process(fi, input_frames, fo, N);
        } else {
            // Per-sample scrubbing — tape-style (pitch follows tempo)
            const double beats_per_sample = metro.bpm / 60.0 / static_cast<double>(sr);
            for (uint32_t s = 0; s < N; ++s) {
                const double sb  = metro.beats_elapsed + static_cast<double>(s) * beats_per_sample;
                const double sp  = std::fmod(sb, loop_beats) / loop_beats;
                const double pd  = source_from_phase(sp);
                out_L[s] = interp_sample(state->samples_L, pd, cs, ce);
                out_R[s] = interp_sample(state->samples_R, pd, cs, ce);
                if (pos_out) pos_out[s] = p_reverse ? 1.0f - static_cast<float>(sp)
                                                    : static_cast<float>(sp);
            }
            const double last_b = metro.beats_elapsed + static_cast<double>(N - 1) * beats_per_sample;
            const double last_p = std::fmod(last_b, loop_beats) / loop_beats;
            playback_pos_ = source_from_phase(last_p);
        }

        for (uint32_t s = 0; s < N; ++s) {
            out_L[s] *= p_volume;
            out_R[s] *= p_volume;
        }

        const double final_b = metro.beats_elapsed +
            static_cast<double>(N - 1) * metro.bpm / 60.0 / static_cast<double>(sr);
        const double final_p = std::fmod(final_b, loop_beats) / loop_beats;
        if (p_stretch) {
            emit_scalars(ctx, N, p_reverse ? 1.0f - static_cast<float>(final_p)
                                           : static_cast<float>(final_p), 0.0f);
        } else {
            if (ctx->output_buffers[2]) std::memset(ctx->output_buffers[2], 0, N * sizeof(float));
        }
        fill_extra_outputs(ctx, N, launch_pending_ ? 1.0f : 0.0f,
                           static_cast<float>(slice_count),
                           static_cast<float>(active_slice_idx_));
        return;
    }

    if (!is_playing_) {
        std::memset(out_L, 0, N * sizeof(float));
        std::memset(out_R, 0, N * sizeof(float));
        emit_scalars(ctx, N, 0.0f, 0.0f);
        fill_extra_outputs(ctx, N, launch_pending_ ? 1.0f : 0.0f,
                           static_cast<float>(slice_count),
                           static_cast<float>(active_slice_idx_));
        return;
    }

    // External mode: beat_phase[s] scrubs the clip region per sample
    if (p_rmode == vivid::kRateModeExternal) {
        const float* bp = ctx->input_buffers[2];
        if (ctx->output_buffers[2]) std::memset(ctx->output_buffers[2], 0, N * sizeof(float));
        const double total_beats = p_warp ? audio_clip_ed::warp_total_beats(state->warp_points) : 1.0;
        for (uint32_t s = 0; s < N; ++s) {
            const float phase = bp ? std::max(0.0f, std::min(bp[s], 1.0f)) : 0.0f;
            double pos_d = p_warp
                ? audio_clip_ed::source_for_warp_beat(state->warp_points, phase * total_beats)
                : audio_clip_ed::source_for_normalized_phase(phase, cs, ce, false);
            if (p_reverse) pos_d = audio_clip_ed::reverse_source_position(pos_d, cs, ce > 0 ? ce - 1 : cs);
            out_L[s] = interp_sample(state->samples_L, pos_d, cs, ce) * p_volume;
            out_R[s] = interp_sample(state->samples_R, pos_d, cs, ce) * p_volume;
            if (pos_out) pos_out[s] = p_reverse ? 1.0f - phase : phase;
        }
        const float last_phase = bp ? std::max(0.0f, std::min(bp[N - 1], 1.0f)) : 0.0f;
        playback_pos_ = p_warp
            ? audio_clip_ed::source_for_warp_beat(state->warp_points, last_phase * total_beats)
            : audio_clip_ed::source_for_normalized_phase(last_phase, cs, ce, false);
        if (p_reverse) playback_pos_ = audio_clip_ed::reverse_source_position(playback_pos_, cs, ce > 0 ? ce - 1 : cs);
        fill_extra_outputs(ctx, N, launch_pending_ ? 1.0f : 0.0f,
                           static_cast<float>(slice_count),
                           static_cast<float>(active_slice_idx_));
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
        if (p_wmode == 1) {
            const double pos_end = playback_pos_ + (p_reverse ? -1.0 : 1.0) * input_frames;
            const double lo = std::min(playback_pos_, pos_end);
            const double hi = std::max(playback_pos_, pos_end);
            for (const auto& tp : state->transient_points) {
                if (tp.source_sample > lo && tp.source_sample <= hi) {
                    state->stretcher.reset();
                    last_pitch_ = 999.0f;
                    break;
                }
            }
        }
        hit_end = gather_source(state, cs, ce, ls, le, p_loop, p_reverse,
                                fade_in_frames, fade_out_frames, crossfade_frames, input_frames);
        const float* fi[2] = { state->scratch_in_L, state->scratch_in_R };
        float*       fo[2] = { out_L, out_R };
        state->stretcher.process(fi, input_frames, fo, N);
    } else {
        hit_end = simple_render(state, out_L, out_R, N, cs, ce, ls, le, p_loop, eff_speed,
                                p_reverse, fade_in_frames, fade_out_frames,
                                crossfade_frames, pos_out);
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

    const float position = p_reverse
        ? std::max(0.0f, std::min(
              static_cast<float>((ce > 0 ? ce - 1 : cs) - playback_pos_) /
                  static_cast<float>(clip_region), 1.0f))
        : std::max(0.0f, std::min(
              static_cast<float>(playback_pos_ - cs) / static_cast<float>(clip_region), 1.0f));
    const float done_val = done_pulse_ ? 1.0f : 0.0f;
    if (p_stretch) {
        emit_scalars(ctx, N, position, done_val);
    } else if (ctx->output_buffers[2]) {
        std::memset(ctx->output_buffers[2], 0, N * sizeof(float));
        ctx->output_buffers[2][N - 1] = done_val;
    }
    fill_extra_outputs(ctx, N, launch_pending_ ? 1.0f : 0.0f,
                       static_cast<float>(slice_count),
                       static_cast<float>(active_slice_idx_));
    done_pulse_ = false;
}

// -------------------------------------------------------------------------
bool AudioClip::gather_source(ClipState* state, uint32_t cs, uint32_t ce,
                               uint32_t ls, uint32_t le,
                               bool p_loop, bool p_reverse,
                               uint32_t fade_in_frames, uint32_t fade_out_frames,
                               uint32_t crossfade_frames, uint32_t count) {
    uint32_t filled  = 0;
    bool     hit_end = false;
    const uint32_t loop_len = le > ls ? le - ls : 1;
    const uint32_t xfade = p_loop ? std::min(crossfade_frames, loop_len / 2) : 0;

    auto gain_at = [&](double pos) {
        float g = 1.0f;
        if (fade_in_frames > 0 && pos < cs + fade_in_frames)
            g *= audio_clip_ed::equal_power_fade_in(static_cast<float>((pos - cs) / fade_in_frames));
        if (fade_out_frames > 0 && pos > ce - fade_out_frames)
            g *= audio_clip_ed::equal_power_fade_out(static_cast<float>((pos - (ce - fade_out_frames)) / fade_out_frames));
        return g;
    };

    while (filled < count) {
        if (p_loop && !p_reverse && playback_pos_ >= le) {
            playback_pos_ = static_cast<double>(ls);
        } else if (p_loop && p_reverse && playback_pos_ < ls) {
            playback_pos_ = static_cast<double>(le > 0 ? le - 1 : ls);
        } else if (!p_loop && !p_reverse && playback_pos_ >= ce) {
            hit_end = true;
            break;
        } else if (!p_loop && p_reverse && playback_pos_ < cs) {
            hit_end = true;
            break;
        }

        double pos = playback_pos_;
        float l = interp_sample(state->samples_L, pos, cs, ce);
        float r = interp_sample(state->samples_R, pos, cs, ce);
        if (xfade > 0 && !p_reverse && pos >= static_cast<double>(le - xfade) && pos < le) {
            const float t = static_cast<float>((pos - (le - xfade)) / static_cast<double>(xfade));
            const double wrap_pos = static_cast<double>(ls) + (pos - (le - xfade));
            const float a = audio_clip_ed::equal_power_fade_out(t);
            const float b = audio_clip_ed::equal_power_fade_in(t);
            l = l * a + interp_sample(state->samples_L, wrap_pos, cs, ce) * b;
            r = r * a + interp_sample(state->samples_R, wrap_pos, cs, ce) * b;
        } else if (xfade > 0 && p_reverse && pos < static_cast<double>(ls + xfade) && pos >= ls) {
            const float t = static_cast<float>((ls + xfade - pos) / static_cast<double>(xfade));
            const double wrap_pos = static_cast<double>(le > 0 ? le - 1 : ls) - (ls + xfade - pos);
            const float a = audio_clip_ed::equal_power_fade_out(t);
            const float b = audio_clip_ed::equal_power_fade_in(t);
            l = l * a + interp_sample(state->samples_L, wrap_pos, cs, ce) * b;
            r = r * a + interp_sample(state->samples_R, wrap_pos, cs, ce) * b;
        }
        const float g = gain_at(pos);
        state->scratch_in_L[filled] = l * g;
        state->scratch_in_R[filled] = r * g;
        ++filled;
        playback_pos_ += p_reverse ? -1.0 : 1.0;
    }
    std::memset(state->scratch_in_L + filled, 0, (count - filled) * sizeof(float));
    std::memset(state->scratch_in_R + filled, 0, (count - filled) * sizeof(float));
    return hit_end;
}

bool AudioClip::simple_render(ClipState* state, float* out_L, float* out_R,
                               uint32_t N, uint32_t cs, uint32_t ce,
                               uint32_t ls, uint32_t le,
                               bool p_loop, float advance, bool p_reverse,
                               uint32_t fade_in_frames, uint32_t fade_out_frames,
                               uint32_t crossfade_frames, float* pos_out) {
    bool hit_end = false;
    const uint32_t clip_region = ce > cs ? ce - cs : 1;
    const uint32_t loop_len = le > ls ? le - ls : 1;
    const uint32_t xfade = p_loop ? std::min(crossfade_frames, loop_len / 2) : 0;

    auto gain_at = [&](double pos) {
        float g = 1.0f;
        if (fade_in_frames > 0 && pos < cs + fade_in_frames)
            g *= audio_clip_ed::equal_power_fade_in(static_cast<float>((pos - cs) / fade_in_frames));
        if (fade_out_frames > 0 && pos > ce - fade_out_frames)
            g *= audio_clip_ed::equal_power_fade_out(static_cast<float>((pos - (ce - fade_out_frames)) / fade_out_frames));
        return g;
    };

    for (uint32_t s = 0; s < N; ++s) {
        if (p_loop && !p_reverse && playback_pos_ >= le) {
            playback_pos_ = static_cast<double>(ls);
        } else if (p_loop && p_reverse && playback_pos_ < ls) {
            playback_pos_ = static_cast<double>(le > 0 ? le - 1 : ls);
        } else if (!p_loop && !p_reverse && playback_pos_ >= ce) {
            hit_end = true;
            out_L[s] = 0.0f;
            out_R[s] = 0.0f;
            if (pos_out) pos_out[s] = 1.0f;
            continue;
        } else if (!p_loop && p_reverse && playback_pos_ < cs) {
            hit_end = true;
            out_L[s] = 0.0f;
            out_R[s] = 0.0f;
            if (pos_out) pos_out[s] = 1.0f;
            continue;
        }

        double pos = playback_pos_;
        float l = interp_sample(state->samples_L, pos, cs, ce);
        float r = interp_sample(state->samples_R, pos, cs, ce);
        if (xfade > 0 && !p_reverse && pos >= static_cast<double>(le - xfade) && pos < le) {
            const float t = static_cast<float>((pos - (le - xfade)) / static_cast<double>(xfade));
            const double wrap_pos = static_cast<double>(ls) + (pos - (le - xfade));
            const float a = audio_clip_ed::equal_power_fade_out(t);
            const float b = audio_clip_ed::equal_power_fade_in(t);
            l = l * a + interp_sample(state->samples_L, wrap_pos, cs, ce) * b;
            r = r * a + interp_sample(state->samples_R, wrap_pos, cs, ce) * b;
        } else if (xfade > 0 && p_reverse && pos < static_cast<double>(ls + xfade) && pos >= ls) {
            const float t = static_cast<float>((ls + xfade - pos) / static_cast<double>(xfade));
            const double wrap_pos = static_cast<double>(le > 0 ? le - 1 : ls) - (ls + xfade - pos);
            const float a = audio_clip_ed::equal_power_fade_out(t);
            const float b = audio_clip_ed::equal_power_fade_in(t);
            l = l * a + interp_sample(state->samples_L, wrap_pos, cs, ce) * b;
            r = r * a + interp_sample(state->samples_R, wrap_pos, cs, ce) * b;
        }
        const float g = gain_at(pos);
        out_L[s] = l * g;
        out_R[s] = r * g;
        if (pos_out) {
            pos_out[s] = p_reverse
                ? std::max(0.0f, std::min(static_cast<float>((ce > 0 ? ce - 1 : cs) - pos) /
                                               static_cast<float>(clip_region), 1.0f))
                : std::max(0.0f, std::min(static_cast<float>(pos - cs) /
                                               static_cast<float>(clip_region), 1.0f));
        }
        playback_pos_ += p_reverse ? -advance : advance;
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

void AudioClip::fill_extra_outputs(const VividAudioContext* ctx, uint32_t N, float pending,
                                   float slice_count, float active_slice) {
    if (ctx->output_buffers[3])
        std::fill(ctx->output_buffers[3], ctx->output_buffers[3] + N, pending);
    if (ctx->output_buffers[4])
        std::fill(ctx->output_buffers[4], ctx->output_buffers[4] + N, slice_count);
    if (ctx->output_buffers[5])
        std::fill(ctx->output_buffers[5], ctx->output_buffers[5] + N, active_slice);
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
