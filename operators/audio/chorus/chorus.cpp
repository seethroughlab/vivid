#include "operator_api/metronome_sync.h"
#include "operator_api/operator.h"
#include "operator_api/audio_dsp.h"
#include "runtime/simd/simd_config.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

// ---------------------------------------------------------------------------
// Chorus — multi-voice fractional delay lines with phase-offset LFOs (mono)
//
// Hot path: one shared delay history (not per-voice), six concurrent
// fractional reads per block. On macOS the per-voice reads batch into a
// single vDSP_vlint call each; scalar fallback preserved for non-Apple.
// The shared-history layout also eliminates 5× redundant per-sample pushes
// that the prior per-voice design needed for seamless voice-count changes.
// ---------------------------------------------------------------------------

static constexpr int   kMaxVoices   = 6;
static constexpr float kCenterDelay = 0.007f;  // 7ms
static constexpr float kMaxDepth    = 0.005f;  // 5ms
static constexpr float kMaxDelay    = 0.015f;  // 15ms

/**
 * @brief Multi-voice chorus effect with modulated fractional delay.
 *
 * Mixes delayed copies of the input with phase-offset LFO modulation.
 * Each voice uses a slightly different delay time to create width and
 * movement. Center delay is 7ms with +/-5ms variation.
 *
 * @tip Increase voices for a thicker, more ensemble-like sound.
 * @see Flanger, Phaser, Delay
 */
struct Chorus : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Chorus";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> rate  {"rate",   0.5f, 0.05f, 5.0f};
    vivid::Param<int>   rate_mode{"rate_mode", 0, vivid::rate_mode_labels()};
    vivid::Param<int>   sync_division{"sync_division", 2, vivid::metronome_division_labels()};
    vivid::Param<float> depth {"depth",  0.5f, 0.0f,  1.0f};
    vivid::Param<int>   voices{"voices", 3, 1, 6};
    vivid::Param<float> mix   {"mix",    0.5f, 0.0f,  1.0f};

    // Shared delay history: oldest samples at index 0, newest at tail.
    // Sized ring_samples + block_headroom so that after each block's tail
    // write the last valid sample sits at delay_history_.size()-1.
    std::vector<float> delay_history_;
    int ring_samples_ = 0;
    int block_headroom_ = 0;

    // Per-block scratch (resized as needed — off the audio hot path only on
    // block-size change).
    std::vector<float> indices_scratch_;       // frames
    std::vector<float> voice_out_scratch_;     // frames
    std::vector<float> wet_scratch_;           // frames

    double   phase_       = 0.0;
    float    prev_external_phase_ = 0.0f;
    int      external_beat_count_ = 0;
    bool     initialized_ = false;
    uint32_t init_rate_   = 0;

    Chorus() {
        vivid::semantic_tag(rate, "frequency_hz");
        vivid::semantic_shape(rate, "scalar");
        vivid::semantic_unit(rate, "Hz");
        vivid::display_hint(rate, VIVID_DISPLAY_KNOB);
        vivid::description(rate, "LFO modulation speed in Hz");
        vivid::description(rate_mode, "Free runs internally, follows an external beat_phase input, or locks to the graph metronome");
        vivid::description(sync_division, "Musical note length used when the chorus rate follows a clock");

        vivid::semantic_tag(depth, "probability_01");
        vivid::semantic_shape(depth, "scalar");
        vivid::display_hint(depth, VIVID_DISPLAY_KNOB);
        vivid::description(depth, "Amount of delay-time modulation (0 = none, 1 = full)");

        vivid::semantic_tag(voices, "count");
        vivid::semantic_shape(voices, "scalar");
        vivid::display_hint(voices, VIVID_DISPLAY_KNOB);
        vivid::description(voices, "Number of chorus voices (more = thicker)");

        vivid::semantic_tag(mix, "probability_01");
        vivid::semantic_shape(mix, "scalar");
        vivid::semantic_intent(mix, "wet_mix");
        vivid::display_hint(mix, VIVID_DISPLAY_KNOB);
        vivid::description(mix, "Dry/wet blend (0 = dry, 1 = fully chorused)");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&rate);
        out.push_back(&rate_mode);
        out.push_back(&sync_division);
        out.push_back(&depth);
        out.push_back(&voices);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",   VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"rate_cv", VIVID_PORT_SCALAR, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"beat_phase", VIVID_PORT_SCALAR, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        vivid::append_analysis_ports(out);
    }

    void lazy_init(uint32_t sr, uint32_t frames) {
        const int needed_ring = static_cast<int>(kMaxDelay * static_cast<float>(sr)) + 2;
        const int needed_headroom = static_cast<int>(frames) + 2;
        const bool rate_changed = !initialized_ || init_rate_ != sr;
        const bool buffer_too_small =
            ring_samples_ != needed_ring ||
            block_headroom_ < needed_headroom;

        if (rate_changed || buffer_too_small) {
            ring_samples_ = needed_ring;
            block_headroom_ = std::max(needed_headroom, block_headroom_);
            // Ensure at least the current block fits, with some slack for
            // future slightly-larger blocks (grow-only to avoid reallocs).
            if (block_headroom_ < needed_headroom) block_headroom_ = needed_headroom;
            delay_history_.assign(ring_samples_ + block_headroom_, 0.0f);
        }

        if (rate_changed) {
            phase_ = 0.0;
            prev_external_phase_ = 0.0f;
            external_beat_count_ = 0;
            initialized_ = true;
            init_rate_ = sr;
        }
    }

    // Shift history left by `frames` and append the new block at the tail.
    // After this call, delay_history_[hist_size - 1] is the newest sample.
    void push_block(const float* in, uint32_t frames) {
        const size_t hist_size = delay_history_.size();
        if (hist_size == 0 || frames == 0) return;
        if (frames >= hist_size) {
            std::memcpy(delay_history_.data(), in + (frames - hist_size),
                        hist_size * sizeof(float));
            return;
        }
        std::memmove(delay_history_.data(),
                     delay_history_.data() + frames,
                     (hist_size - frames) * sizeof(float));
        std::memcpy(delay_history_.data() + hist_size - frames,
                    in, frames * sizeof(float));
    }

    void process_audio(const VividAudioContext* ctx) override {
        const uint32_t frames = ctx->buffer_size;
        if (frames == 0) return;
        lazy_init(ctx->sample_rate, frames);

        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];

        float rate_cv_val = ctx->input_buffers[1] ? ctx->input_buffers[1][0] : 0.0f;
        const int mode = rate_mode.int_value();
        float mod_rate = rate.value + rate_cv_val;
        if (mod_rate < 0.05f) mod_rate = 0.05f;
        if (mod_rate > 5.0f)  mod_rate = 5.0f;

        int   voice_count = voices.int_value();
        if (voice_count < 1) voice_count = 1;
        if (voice_count > kMaxVoices) voice_count = kMaxVoices;
        float wet = mix.value;
        float dry = 1.0f - wet;
        float d   = depth.value;
        float sr  = static_cast<float>(ctx->sample_rate);
        double inv_sr = 1.0 / static_cast<double>(ctx->sample_rate);
        const vivid::MetronomeTransport metronome = vivid::metronome_transport(ctx);
        const double metronome_beats_per_sample = (metronome.bpm > 0.0f)
            ? (static_cast<double>(metronome.bpm) / 60.0) / static_cast<double>(ctx->sample_rate)
            : 0.0;

        const float center_samples = kCenterDelay * sr;
        const float depth_samples  = kMaxDepth * d * sr;
        const float max_delay_clamp = static_cast<float>(ring_samples_ - 2);

        push_block(in, frames);

        if (indices_scratch_.size() < frames)   indices_scratch_.assign(frames, 0.0f);
        if (voice_out_scratch_.size() < frames) voice_out_scratch_.assign(frames, 0.0f);
        if (wet_scratch_.size() < frames)       wet_scratch_.assign(frames, 0.0f);

        // The tail of delay_history_ corresponds to the last sample of this
        // block. Sample i of the block is at absolute history index
        //     tail_base + i,  where tail_base = hist_size - frames.
        const uint32_t hist_size = static_cast<uint32_t>(delay_history_.size());
        const uint32_t tail_base = hist_size - frames;
        const float tail_base_f = static_cast<float>(tail_base);

        std::fill_n(wet_scratch_.data(), frames, 0.0f);

        // Capture phase state so Free mode advances only once across the
        // block (matches the prior per-sample increment but expressed up
        // front so we can read it per-voice).
        double phase_start = phase_;
        const double phase_inc = static_cast<double>(mod_rate) * inv_sr;
        const float inv_voice_count = 1.0f / static_cast<float>(voice_count);

        for (int v = 0; v < voice_count; ++v) {
            // Fill indices_scratch_ with absolute history indices for this
            // voice across the block. Scalar phase/LFO/delay computation —
            // the three rate modes keep the loop branchy, and the work is
            // tiny compared to the downstream vectorized read.
            const double voice_offset = static_cast<double>(v) * static_cast<double>(inv_voice_count);
            double phase = phase_start;
            for (uint32_t i = 0; i < frames; ++i) {
                double base_phase;
                if (mode == vivid::kRateModeMetronome) {
                    base_phase = vivid::cycle_phase_from_total_beats(
                        metronome.beats_elapsed + static_cast<double>(i) * metronome_beats_per_sample,
                        sync_division.int_value());
                } else if (mode == vivid::kRateModeExternal) {
                    const float external_phase = ctx->input_buffers[2]
                        ? ctx->input_buffers[2][i] : 0.0f;
                    // advance_external_total_beats is stateful; only the last
                    // voice's iteration should mutate the instance state, so
                    // snapshot counters into locals and restore state at end.
                    float local_prev = prev_external_phase_;
                    int   local_count = external_beat_count_;
                    const double total_beats = vivid::advance_external_total_beats(
                        external_phase, local_prev, local_count);
                    if (v == voice_count - 1 && i == frames - 1) {
                        prev_external_phase_ = local_prev;
                        external_beat_count_ = local_count;
                    }
                    base_phase = vivid::cycle_phase_from_total_beats(
                        total_beats, sync_division.int_value());
                } else { // Free
                    base_phase = phase;
                    phase += phase_inc;
                    if (phase >= 1.0) phase -= 1.0;
                }

                double voice_phase = base_phase + voice_offset;
                if (voice_phase >= 1.0) voice_phase -= 1.0;

                const float lfo = static_cast<float>(audio_dsp::waveform(voice_phase, 0)) * 0.5f + 0.5f;
                float delay_samples = center_samples + depth_samples * lfo;
                if (delay_samples < 1.0f) delay_samples = 1.0f;
                if (delay_samples >= max_delay_clamp) delay_samples = max_delay_clamp;

                // Absolute index into delay_history_: current-sample position
                // is (tail_base + i); go back by delay_samples. With
                // delay_samples >= 1 and delay_samples <= ring_samples_ - 2,
                // the result is always in [tail_base - ring_samples_ + 2,
                // tail_base + i - 1], well inside [0, hist_size - 1].
                indices_scratch_[i] = tail_base_f + static_cast<float>(i) - delay_samples;
            }

#if VIVID_ACCELERATE_ENABLED
            vDSP_vlint(delay_history_.data(),
                       indices_scratch_.data(), 1,
                       voice_out_scratch_.data(), 1,
                       static_cast<vDSP_Length>(frames),
                       static_cast<vDSP_Length>(hist_size));
            vDSP_vadd(voice_out_scratch_.data(), 1,
                      wet_scratch_.data(), 1,
                      wet_scratch_.data(), 1,
                      static_cast<vDSP_Length>(frames));
#else
            for (uint32_t i = 0; i < frames; ++i) {
                const float idx_f = indices_scratch_[i];
                int i0 = static_cast<int>(idx_f);
                int i1 = i0 + 1;
                const float frac = idx_f - static_cast<float>(i0);
                if (i0 < 0) i0 = 0;
                if (i1 < 0) i1 = 0;
                if (i0 >= static_cast<int>(hist_size)) i0 = static_cast<int>(hist_size) - 1;
                if (i1 >= static_cast<int>(hist_size)) i1 = static_cast<int>(hist_size) - 1;
                wet_scratch_[i] += delay_history_[i0] * (1.0f - frac)
                                 + delay_history_[i1] * frac;
            }
#endif
        }

        // Advance Free-mode phase once across the block (matches pre-refactor
        // behavior where phase_ advanced by phase_inc per sample).
        if (mode == vivid::kRateModeFree) {
            double p = phase_start + phase_inc * static_cast<double>(frames);
            p -= std::floor(p);
            phase_ = p;
        }

        // Mix: out = in * dry + (wet_scratch / voice_count) * wet_mix.
#if VIVID_ACCELERATE_ENABLED
        const float wet_scale = wet * inv_voice_count;
        vDSP_vsmul(in, 1, &dry, out, 1, static_cast<vDSP_Length>(frames));
        vDSP_vsma(wet_scratch_.data(), 1, &wet_scale,
                  out, 1, out, 1, static_cast<vDSP_Length>(frames));
#else
        const float wet_scale = wet * inv_voice_count;
        for (uint32_t i = 0; i < frames; ++i) {
            out[i] = in[i] * dry + wet_scratch_[i] * wet_scale;
        }
#endif
    }
};

VIVID_REGISTER(Chorus)
