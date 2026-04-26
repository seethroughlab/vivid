#include "operator_api/operator.h"
#include "operator_api/audio_dsp.h"

#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// Tape — retro tape / VHS character: warm saturation, wow + flutter pitch
// drift, head-bump high-frequency rolloff, optional sample-rate decimation,
// and tape hiss. Inspired by Baby Audio Super VHS and TAIP.
// ---------------------------------------------------------------------------

namespace {

// LFO rates are fixed (not user-exposed). These match the perceptual targets
// used by reference plugins: wow is the slow warble, flutter is the
// faster jitter that adds liveliness.
constexpr double kWowHz     = 0.40;
constexpr double kFlutterHz = 5.5;
constexpr float  kTwoPi     = 6.28318530717958647692f;

// Mean delay-line read offset. Wow + flutter modulate around this center.
// Keeping it small (~3ms) avoids audible Haas-style doubling at zero
// modulation — the dry signal stays in roughly the same time as the wet.
constexpr float kCenterDelayMs = 3.0f;

// Maximum total modulation depth (samples) — sets buffer size and clamps
// how much wow + flutter can push the read pointer.
constexpr float kMaxDepthMs = 22.0f;
constexpr float kBufferMs   = kCenterDelayMs + kMaxDepthMs + 4.0f;

// Pre/de-emphasis frequency for the tape EQ trick. Both shelves are
// hardcoded — the user-facing "tone" param drives the post-stage low-pass
// instead.
constexpr float kEmphasisHz = 5000.0f;

struct OnePoleLP {
    float a  = 1.0f;
    float b  = 0.0f;
    float z1 = 0.0f;

    void set_cutoff(float freq, float sr) {
        if (freq <= 0.0f || sr <= 0.0f) { a = 1.0f; b = 0.0f; return; }
        float x = std::exp(-2.0f * static_cast<float>(M_PI) * freq / sr);
        a = 1.0f - x;
        b = x;
    }

    float process(float input) {
        z1 = input * a + z1 * b;
        return z1;
    }
};

// First-order high-shelf using the LP/HP-complementary trick.
// shelf_db: positive = boost highs, negative = cut highs.
struct OnePoleShelf {
    OnePoleLP lp;
    float gain_high = 1.0f;

    void set(float freq, float shelf_db, float sr) {
        lp.set_cutoff(freq, sr);
        gain_high = std::pow(10.0f, shelf_db / 20.0f);
    }

    float process(float x) {
        float low  = lp.process(x);
        float high = x - low;
        return low + high * gain_high;
    }
};

struct FractionalDelayLine {
    std::vector<float> buffer;
    int size  = 0;
    int write = 0;

    void init(int max_samples) {
        size  = max_samples;
        write = 0;
        if (static_cast<int>(buffer.size()) < size) {
            buffer.assign(size, 0.0f);
        } else {
            std::fill_n(buffer.data(), size, 0.0f);
        }
    }

    void push(float v) {
        buffer[write] = v;
        if (++write >= size) write = 0;
    }

    float read(float delay_samples) const {
        if (size <= 0) return 0.0f;
        float idx_f = static_cast<float>(write) - delay_samples;
        while (idx_f < 0.0f)              idx_f += static_cast<float>(size);
        while (idx_f >= static_cast<float>(size)) idx_f -= static_cast<float>(size);
        int idx0 = static_cast<int>(idx_f);
        int idx1 = idx0 + 1;
        if (idx1 >= size) idx1 = 0;
        float frac = idx_f - std::floor(idx_f);
        return buffer[idx0] * (1.0f - frac) + buffer[idx1] * frac;
    }
};

} // namespace

/**
 * @brief Retro tape / VHS character: saturation, pitch drift, hiss, and
 * head-bump rolloff in one node.
 *
 * Combines the canonical tape sound primitives — wow + flutter modulated
 * delay, asymmetric tanh saturation flanked by pre- and de-emphasis EQ
 * (concentrating compression on highs the way real tape does), a tone-
 * controlled high-frequency rolloff, optional sample-rate decimation for
 * a VHS digitization vibe, and a tape-hiss noise floor.
 *
 * @tip Try the `Warm Cassette` factory preset on a clean drum bus or pad
 * for instant analog-leaning glue. Crank `wow` and `flutter` to dial in
 * how unstable the playback should feel.
 * @recipe drumkit/output -> Tape{preset=Warm Cassette}/input -> mix
 * @see Bitcrush, Distortion, Chorus
 * @family voice_shaper
 */
struct Tape : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "Tape";
    static constexpr bool kTimeDependent = false;
    static constexpr VividLaneBehavior kLaneBehavior = VIVID_LANE_POINTWISE;

    vivid::Param<float> drive          {"drive",          0.30f,  0.0f, 1.0f};
    vivid::Param<float> wow            {"wow",            0.20f,  0.0f, 1.0f};
    vivid::Param<float> flutter        {"flutter",        0.15f,  0.0f, 1.0f};
    vivid::Param<float> tone           {"tone",           0.70f,  0.0f, 1.0f};
    vivid::Param<float> rate_reduction {"rate_reduction", 0.0f,   0.0f, 1.0f};
    vivid::Param<float> hiss           {"hiss",           0.05f,  0.0f, 1.0f};
    vivid::Param<float> bias           {"bias",           0.0f,  -1.0f, 1.0f};
    vivid::Param<float> mix            {"mix",            1.0f,   0.0f, 1.0f};

    Tape() {
        vivid::semantic_tag(drive, "amplitude_linear");
        vivid::semantic_shape(drive, "scalar");
        vivid::semantic_intent(drive, "pre_gain");
        vivid::description(drive,
            "Tape saturation amount. Maps to a soft tanh curve with EQ "
            "pre/de-emphasis baked in, so highs compress before lows.");

        vivid::semantic_tag(wow, "amplitude_linear");
        vivid::semantic_shape(wow, "scalar");
        vivid::description(wow,
            "Slow pitch drift depth (modulates a delay line at ~0.4 Hz). "
            "Higher = more cassette-like warble.");

        vivid::semantic_tag(flutter, "amplitude_linear");
        vivid::semantic_shape(flutter, "scalar");
        vivid::description(flutter,
            "Fast pitch jitter depth (~5.5 Hz). Adds liveliness on top "
            "of wow.");

        vivid::semantic_tag(tone, "probability_01");
        vivid::semantic_shape(tone, "scalar");
        vivid::description(tone,
            "Post-saturation brightness. 1 = full bandwidth, 0 = aggressive "
            "head-bump rolloff (~1.5 kHz).");

        vivid::semantic_tag(rate_reduction, "probability_01");
        vivid::semantic_shape(rate_reduction, "scalar");
        vivid::description(rate_reduction,
            "Sample-rate decimation. 0 = full SR, 1 ≈ 6 kHz target. "
            "Cranks the VHS dropout vibe.");

        vivid::semantic_tag(hiss, "amplitude_linear");
        vivid::semantic_shape(hiss, "scalar");
        vivid::description(hiss,
            "Tape noise floor. Adds white-ish hiss; max ≈ -32 dB.");

        vivid::semantic_tag(bias, "amplitude_linear");
        vivid::semantic_shape(bias, "scalar");
        vivid::description(bias,
            "Saturation asymmetry. Positive values bias toward 2nd-harmonic "
            "warmth; negative reverses the curve.");

        vivid::semantic_tag(mix, "probability_01");
        vivid::semantic_shape(mix, "scalar");
        vivid::semantic_intent(mix, "wet_mix");
        vivid::description(mix, "Dry/wet blend (0 = bypass, 1 = full tape).");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&drive);
        out.push_back(&wow);
        out.push_back(&flutter);
        out.push_back(&tone);
        out.push_back(&rate_reduction);
        out.push_back(&hiss);
        out.push_back(&bias);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,
                       VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT,
                       VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        vivid::append_analysis_ports(out);
    }

    struct LaneState {
        FractionalDelayLine delay;
        OnePoleShelf pre_emph;
        OnePoleShelf de_emph;
        OnePoleLP    tone_lp;
        double  wow_phase     = 0.0;
        double  flutter_phase = 0.0;
        audio_dsp::WhiteNoise hiss_gen;
        int     rate_counter      = 0;
        float   rate_held_sample  = 0.0f;
        float   dc_x1 = 0.0f, dc_y1 = 0.0f;
        bool    initialized       = false;
        uint32_t init_rate        = 0;

        void ensure_init(uint32_t sr, std::uint32_t lane_id) {
            if (initialized && init_rate == sr) return;
            int max_samples = static_cast<int>((kBufferMs / 1000.0f) * sr) + 4;
            delay.init(max_samples);
            pre_emph.set(kEmphasisHz, +3.0f, static_cast<float>(sr));
            de_emph.set(kEmphasisHz, -3.0f, static_cast<float>(sr));
            tone_lp.set_cutoff(18000.0f, static_cast<float>(sr));
            // Stagger LFO phase per lane for stereo decorrelation.
            wow_phase     = static_cast<double>((lane_id * 0x9E37u) & 0xFFFFu)
                            / 65535.0 * kTwoPi;
            flutter_phase = static_cast<double>((lane_id * 0x517Bu) & 0xFFFFu)
                            / 65535.0 * kTwoPi;
            // Pre-seed hiss generator differently per lane so stereo hiss
            // isn't a perfect mono signal.
            hiss_gen.state = 12345u + lane_id * 2654435761u;
            initialized = true;
            init_rate   = sr;
        }
    };

    LaneState scalar_state_;

    void process_audio(const VividAudioContext* ctx) override {
        LaneState& s = ctx->lane_state_fn
            ? *vivid_lane_state(ctx, ctx->lane_id, LaneState)
            : scalar_state_;
        s.ensure_init(ctx->sample_rate, ctx->lane_id);

        const float* in  = ctx->input_buffers[0];
        float*       out = ctx->output_buffers[0];
        const uint32_t frames = ctx->buffer_size;
        const float    sr     = static_cast<float>(ctx->sample_rate);

        // --- Param-driven coefficients (recomputed once per block) ---
        // drive 0..1 → tanh pre-gain 1..6×, log-shaped so low values feel
        // gentler.
        const float drive_amt = drive.value;
        const float pre_gain  = 1.0f + drive_amt * drive_amt * 5.0f;
        const float post_gain = 1.0f / (1.0f + drive_amt * 0.4f); // makeup
        const float bias_v    = bias.value * 0.7f;                // ~±0.7 max
        const float bias_offset = std::tanh(bias_v * pre_gain);

        // tone 0..1 → cutoff 1.5 kHz .. 18 kHz, exponential.
        const float tone_cut = 1500.0f * std::pow(12.0f, tone.value);
        s.tone_lp.set_cutoff(tone_cut, sr);

        // rate_reduction 0..1 → target sample rate 48 kHz .. 6 kHz.
        const float target_sr = sr - rate_reduction.value * (sr - 6000.0f);
        const int   rate_step = std::max(1,
            static_cast<int>(std::round(sr / std::max(1.0f, target_sr))));

        // hiss 0..1 → linear amplitude up to ~0.025 (≈ -32 dB).
        const float hiss_amp = hiss.value * hiss.value * 0.025f;

        // wow / flutter depths in samples.
        const float wow_depth_samp     = wow.value     * 6.0f * (sr / 48000.0f);
        const float flutter_depth_samp = flutter.value * 1.5f * (sr / 48000.0f);
        const float center_delay_samp  = (kCenterDelayMs / 1000.0f) * sr;

        const double wow_inc     = kWowHz     * 2.0 * M_PI / static_cast<double>(sr);
        const double flutter_inc = kFlutterHz * 2.0 * M_PI / static_cast<double>(sr);

        const float wet = mix.value;
        const float dry = 1.0f - wet;

        for (uint32_t i = 0; i < frames; ++i) {
            const float x = in[i];

            // --- Wow + flutter delay read ---
            s.delay.push(x);
            const float lfo_off =
                wow_depth_samp     * static_cast<float>(std::sin(s.wow_phase)) +
                flutter_depth_samp * static_cast<float>(std::sin(s.flutter_phase));
            s.wow_phase     += wow_inc;
            s.flutter_phase += flutter_inc;
            if (s.wow_phase     > kTwoPi) s.wow_phase     -= kTwoPi;
            if (s.flutter_phase > kTwoPi) s.flutter_phase -= kTwoPi;
            float read_delay = center_delay_samp + lfo_off;
            // Clamp to safe range inside the buffer.
            if (read_delay < 1.0f) read_delay = 1.0f;
            if (read_delay > static_cast<float>(s.delay.size - 2))
                read_delay = static_cast<float>(s.delay.size - 2);
            float y = s.delay.read(read_delay);

            // --- Pre-emphasis → tanh (with bias) → de-emphasis ---
            y = s.pre_emph.process(y);
            float driven = y * pre_gain + bias_v * pre_gain;
            float saturated = std::tanh(driven) - bias_offset;
            y = saturated * post_gain;
            y = s.de_emph.process(y);

            // --- Tone-controlled head-bump LP ---
            y = s.tone_lp.process(y);

            // --- Sample-rate decimation (sample-and-hold) ---
            if (rate_step > 1) {
                if (s.rate_counter == 0) s.rate_held_sample = y;
                ++s.rate_counter;
                if (s.rate_counter >= rate_step) s.rate_counter = 0;
                y = s.rate_held_sample;
            } else {
                s.rate_counter = 0;
            }

            // --- Hiss ---
            if (hiss_amp > 0.0f) {
                // WhiteNoise::next() returns 0..1; recenter to ±0.5 then scale.
                float n = s.hiss_gen.next() - 0.5f;
                y += n * 2.0f * hiss_amp;
            }

            // --- DC blocker (one-pole high-pass at ~5 Hz, R = 0.999) ---
            constexpr float dc_R = 0.999f;
            float dc_y = y - s.dc_x1 + dc_R * s.dc_y1;
            s.dc_x1 = y;
            s.dc_y1 = dc_y;
            y = dc_y;

            out[i] = x * dry + y * wet;
        }
    }
};

VIVID_REGISTER(Tape)
