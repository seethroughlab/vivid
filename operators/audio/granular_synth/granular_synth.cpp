#include "operator_api/operator.h"
#include "operator_api/audio_dsp.h"

#include <atomic>
#include <cmath>
#include <vector>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// ---------------------------------------------------------------------------
// Granular Synth — grain cloud engine with capture buffer (mono)
// ---------------------------------------------------------------------------

static constexpr int   kMaxGrains      = 32;
static constexpr float kMaxCaptureSec  = 4.0f;

// Inspector display constants
static constexpr int   kWaveformBins   = 280;
static constexpr float kWaveformH      = 80.0f;
static constexpr float kInfoBarH       = 18.0f;
static constexpr float kInspPad        = 4.0f;

struct CaptureBuffer {
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

    // Read with linear interpolation at fractional sample index (absolute)
    float read_linear(float abs_pos) const {
        // Wrap into valid range
        float idx_f = std::fmod(abs_pos, static_cast<float>(size));
        if (idx_f < 0.0f) idx_f += static_cast<float>(size);
        int idx0 = static_cast<int>(idx_f);
        int idx1 = idx0 + 1;
        if (idx0 >= size) idx0 -= size;
        if (idx1 >= size) idx1 -= size;
        if (idx0 < 0) idx0 += size;
        if (idx1 < 0) idx1 += size;
        float frac = idx_f - std::floor(idx_f);
        return buffer[idx0] * (1.0f - frac) + buffer[idx1] * frac;
    }
};

struct Grain {
    bool  active        = false;
    float start_pos     = 0.0f; // absolute sample position in capture buffer
    int   length        = 0;    // grain length in samples
    float cursor        = 0.0f; // playback position within grain (samples)
    float playback_rate = 1.0f;
    int   window_type   = 0;    // 0=Hann, 1=Hamming, 2=Blackman, 3=Triangle
};

static float grain_window(float phase, int type) {
    // phase in [0, 1]
    switch (type) {
        default:
        case 0: // Hann
            return 0.5f * (1.0f - std::cos(2.0f * static_cast<float>(M_PI) * phase));
        case 1: // Hamming
            return 0.54f - 0.46f * std::cos(2.0f * static_cast<float>(M_PI) * phase);
        case 2: // Blackman
            return 0.42f - 0.5f * std::cos(2.0f * static_cast<float>(M_PI) * phase)
                         + 0.08f * std::cos(4.0f * static_cast<float>(M_PI) * phase);
        case 3: // Triangle
            return 1.0f - std::fabs(2.0f * phase - 1.0f);
    }
}

// ---------------------------------------------------------------------------
// Inspector snapshot — lock-free audio→UI data transfer
// ---------------------------------------------------------------------------

struct WaveformBin {
    float min_val = 0.0f;
    float max_val = 0.0f;
};

struct GrainSnapshot {
    bool  active   = false;
    float bin_start = 0.0f; // start position in bin coordinates [0, kWaveformBins)
    float bin_width = 0.0f; // width in bins
    float phase    = 0.0f;  // playback progress [0, 1]
};

struct InspectorSnapshot {
    WaveformBin bins[kWaveformBins] = {};
    GrainSnapshot grains[kMaxGrains] = {};
    int   active_count = 0;
    int   window_type  = 0;
    float position_norm = 0.0f; // [0, 1] — the position parameter value
};

struct DoubleBufferedSnapshot {
    InspectorSnapshot slots[2];
    std::atomic<int> read_idx{0};

    void write(const CaptureBuffer& cap, const Grain* grains, int grain_count,
               float position_param, int win_type) {
        int wi = 1 - read_idx.load(std::memory_order_relaxed);
        auto& snap = slots[wi];

        // Decimate capture buffer into bins (min/max)
        if (cap.size > 0) {
            float samples_per_bin = static_cast<float>(cap.size) / static_cast<float>(kWaveformBins);
            for (int b = 0; b < kWaveformBins; ++b) {
                // Map bin to buffer position: bin 0 = oldest (write_head), bin N-1 = newest
                float start_f = static_cast<float>(b) * samples_per_bin;
                float end_f   = start_f + samples_per_bin;
                int start_i = static_cast<int>(start_f);
                int end_i   = std::min(static_cast<int>(end_f), cap.size);

                float lo =  1e30f;
                float hi = -1e30f;
                for (int s = start_i; s < end_i; ++s) {
                    int idx = (cap.write + s) % cap.size;
                    float v = cap.buffer[idx];
                    if (v < lo) lo = v;
                    if (v > hi) hi = v;
                }
                if (lo > hi) { lo = 0.0f; hi = 0.0f; }
                snap.bins[b].min_val = lo;
                snap.bins[b].max_val = hi;
            }
        }

        // Snapshot grain states
        int active = 0;
        float buf_size_f = static_cast<float>(cap.size);
        float bin_scale = (buf_size_f > 0.0f)
            ? static_cast<float>(kWaveformBins) / buf_size_f : 0.0f;

        for (int g = 0; g < grain_count && g < kMaxGrains; ++g) {
            const auto& grain = grains[g];
            auto& gs = snap.grains[g];
            gs.active = grain.active;
            if (!grain.active) continue;

            // Convert grain start to delay-from-write-head, then to bin coordinate
            float delay = std::fmod(
                static_cast<float>(cap.write) - grain.start_pos + buf_size_f,
                buf_size_f);
            // bin 0 = oldest = largest delay, bin N-1 = newest = 0 delay
            float bin_pos = static_cast<float>(kWaveformBins) - delay * bin_scale;
            gs.bin_start = bin_pos;
            gs.bin_width = static_cast<float>(grain.length) * bin_scale;
            gs.phase = (grain.length > 0)
                ? grain.cursor / static_cast<float>(grain.length) : 0.0f;
            active++;
        }
        // Clear remaining slots
        for (int g = grain_count; g < kMaxGrains; ++g)
            snap.grains[g].active = false;

        snap.active_count = active;
        snap.window_type = win_type;
        snap.position_norm = position_param;

        read_idx.store(wi, std::memory_order_release);
    }

    const InspectorSnapshot& read() const {
        return slots[read_idx.load(std::memory_order_acquire)];
    }
};

// ---------------------------------------------------------------------------
/**
 * @brief Granular synthesis engine with up to 32 simultaneous grains.
 *
 * Captures incoming audio into a buffer and replays it as overlapping
 * grains with configurable size, density, position, and pitch. Includes
 * a custom waveform inspector showing the capture buffer and active
 * grain positions.
 *
 * @tip Freeze a texture by setting position manually and disconnecting input.
 * @param position Playback position in the capture buffer (0-1).
 * @param randomize Random scatter applied to grain position, pitch, and timing.
 * @param window Grain envelope shape. Hann is smooth, Triangle is percussive.
 * @see SpectralFreeze, Sampler
 */
struct GranularSynth : vivid::OperatorBase, vivid::AudioProcessable {
    static constexpr const char* kName   = "GranularSynth";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> grain_size{"grain_size", 80.0f,  5.0f,  500.0f};
    vivid::Param<float> density   {"density",    10.0f,  0.5f,  60.0f};
    vivid::Param<float> position  {"position",   0.8f,   0.0f,  1.0f};
    vivid::Param<float> pitch     {"pitch",      0.0f,  -24.0f, 24.0f};
    vivid::Param<float> randomize {"randomize",  0.1f,   0.0f,  1.0f};
    vivid::Param<int>   window    {"window",     0, {"Hann", "Hamming", "Blackman", "Triangle"}};
    vivid::Param<float> mix       {"mix",        1.0f,   0.0f,  1.0f};

    CaptureBuffer  capture_;
    Grain          grains_[kMaxGrains];
    double         sched_phase_ = 0.0;
    audio_dsp::WhiteNoise rng_;
    bool           initialized_ = false;
    uint32_t       init_rate_   = 0;

    // Inspector state
    DoubleBufferedSnapshot insp_snapshot_;
    bool insp_dragging_ = false;

    GranularSynth() {
        vivid::semantic_tag(grain_size, "time_milliseconds");
        vivid::semantic_shape(grain_size, "scalar");
        vivid::semantic_unit(grain_size, "ms");
        vivid::display_hint(grain_size, VIVID_DISPLAY_KNOB);
        vivid::description(grain_size, "Duration of each grain in milliseconds");

        vivid::semantic_tag(density, "frequency_hz");
        vivid::semantic_shape(density, "scalar");
        vivid::semantic_unit(density, "Hz");
        vivid::display_hint(density, VIVID_DISPLAY_KNOB);
        vivid::description(density, "Rate of grain emission in grains per second");

        vivid::semantic_tag(position, "probability_01");
        vivid::semantic_shape(position, "scalar");
        vivid::display_hint(position, VIVID_DISPLAY_KNOB);
        vivid::description(position, "Playback position in the capture buffer (0 = newest, 1 = oldest)");

        vivid::semantic_tag(pitch, "semitones");
        vivid::semantic_shape(pitch, "scalar");
        vivid::semantic_unit(pitch, "st");
        vivid::display_hint(pitch, VIVID_DISPLAY_KNOB);
        vivid::description(pitch, "Pitch shift applied to grains in semitones");

        vivid::semantic_tag(randomize, "probability_01");
        vivid::semantic_shape(randomize, "scalar");
        vivid::display_hint(randomize, VIVID_DISPLAY_KNOB);
        vivid::description(randomize, "Random scatter applied to grain position, pitch, and timing");

        vivid::description(window, "Grain envelope shape: Hann, Hamming, Blackman, or Triangle");

        vivid::semantic_tag(mix, "probability_01");
        vivid::semantic_shape(mix, "scalar");
        vivid::semantic_intent(mix, "wet_mix");
        vivid::display_hint(mix, VIVID_DISPLAY_KNOB);
        vivid::description(mix, "Blend between dry input and granular output");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&grain_size);
        out.push_back(&density);
        out.push_back(&position);
        out.push_back(&pitch);
        out.push_back(&randomize);
        out.push_back(&window);
        out.push_back(&mix);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",      VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output",     VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"position_cv", VIVID_PORT_SCALAR, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"pitch_cv",    VIVID_PORT_SCALAR, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"density_cv",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        vivid::append_analysis_ports(out);
    }

    void lazy_init(uint32_t sr) {
        if (initialized_ && init_rate_ == sr) return;
        int cap_samples = static_cast<int>(kMaxCaptureSec * sr) + 2;
        capture_.init(cap_samples);
        for (int g = 0; g < kMaxGrains; g++)
            grains_[g].active = false;
        sched_phase_ = 0.0;
        initialized_ = true;
        init_rate_   = sr;
    }

    void spawn_grain(float sr, float pos_param, float pitch_param, float size_ms,
                     float rand_amt, int win_type) {
        // Find inactive slot
        int slot = -1;
        for (int g = 0; g < kMaxGrains; g++) {
            if (!grains_[g].active) { slot = g; break; }
        }
        if (slot < 0) return; // all slots busy

        Grain& grain = grains_[slot];

        // Jitter position: up to 10% of buffer
        float pos_jitter = rand_amt * 0.1f * rng_.next();
        float pos = pos_param + pos_jitter;
        pos = std::fmax(0.0f, std::fmin(1.0f, pos));

        // Start position: how far back from write head
        float delay_samples = pos * static_cast<float>(capture_.size);
        grain.start_pos = std::fmod(
            static_cast<float>(capture_.write) - delay_samples + static_cast<float>(capture_.size),
            static_cast<float>(capture_.size));

        // Grain size with jitter (up to +/-25%)
        float size_jitter = 1.0f + rand_amt * 0.25f * rng_.next();
        float grain_ms = size_ms * std::fmax(0.25f, size_jitter);
        grain.length = std::max(1, static_cast<int>(grain_ms * 0.001f * sr));

        // Pitch with jitter (up to +/-1 semitone)
        float pitch_jitter = rand_amt * rng_.next(); // +/- 1 semitone
        float total_pitch = pitch_param + pitch_jitter;
        grain.playback_rate = std::pow(2.0f, total_pitch / 12.0f);

        grain.cursor = 0.0f;
        grain.window_type = win_type;
        grain.active = true;
    }

    void process_audio(const VividAudioContext* ctx) override {
        lazy_init(ctx->sample_rate);

        float* in  = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];
        uint32_t frames = ctx->buffer_size;

        float pos_cv = ctx->input_float_values ? ctx->input_float_values[0] : 0.0f;
        float pitch_cv = ctx->input_float_values ? ctx->input_float_values[1] : 0.0f;
        float density_cv = ctx->input_float_values ? ctx->input_float_values[2] : 0.0f;

        float mod_position = std::fmax(0.0f, std::fmin(1.0f, position.value + pos_cv));
        float mod_pitch    = std::fmax(-24.0f, std::fmin(24.0f, pitch.value + pitch_cv));
        float mod_density  = std::fmax(0.5f, std::fmin(60.0f, density.value + density_cv));

        float sr       = static_cast<float>(ctx->sample_rate);
        double inv_sr  = 1.0 / static_cast<double>(ctx->sample_rate);
        float wet      = mix.value;
        float dry      = 1.0f - wet;
        float size_ms  = grain_size.value;
        float rand_amt = randomize.value;
        int   win_type = window.int_value();

        for (uint32_t i = 0; i < frames; i++) {
            // 1. Push input into capture buffer
            capture_.push(in[i]);

            // 2. Advance scheduler
            double prev_phase = sched_phase_;
            sched_phase_ += mod_density * inv_sr;
            if (sched_phase_ >= 1.0) {
                sched_phase_ -= 1.0;
                spawn_grain(sr, mod_position, mod_pitch, size_ms, rand_amt, win_type);
            }

            // 3. Sum active grains
            float grain_sum = 0.0f;
            int active_count = 0;
            for (int g = 0; g < kMaxGrains; g++) {
                Grain& grain = grains_[g];
                if (!grain.active) continue;

                float phase = grain.cursor / static_cast<float>(grain.length);
                if (phase >= 1.0f) {
                    grain.active = false;
                    continue;
                }

                float env = grain_window(phase, grain.window_type);
                float read_pos = grain.start_pos + grain.cursor * grain.playback_rate;
                float sample = capture_.read_linear(read_pos);
                grain_sum += sample * env;
                active_count++;

                grain.cursor += 1.0f;
            }

            // 4. Normalize by 1/sqrt(active_count)
            if (active_count > 0) {
                grain_sum *= 1.0f / std::sqrt(static_cast<float>(active_count));
            }

            // 5. Mix
            out[i] = in[i] * dry + grain_sum * wet;
        }

        // Update inspector snapshot
        insp_snapshot_.write(capture_, grains_, kMaxGrains, mod_position, win_type);
    }

    // ---------------------------------------------------------------------------
    // Custom inspector
    // ---------------------------------------------------------------------------

    void draw_inspector(VividInspectorContext* ctx) override {
        auto& d = ctx->draw;
        void* o = d.opaque;
        const auto& th = ctx->theme;

        const float px = ctx->content_x;
        float py = ctx->content_y + kInspPad;
        const float w = ctx->content_width;

        const auto& snap = insp_snapshot_.read();

        // --- Waveform display ---
        const float wave_x = px;
        const float wave_y = py;
        const float wave_w = w;
        const float wave_h = kWaveformH;

        // Background
        d.draw_rounded_rect(o, wave_x, wave_y, wave_w, wave_h, th.corner_radius,
                            {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.95f});

        // Clip to waveform area
        d.push_clip_rect(o, wave_x, wave_y, wave_w, wave_h);

        // Zero-crossing center line
        float center_y = wave_y + wave_h * 0.5f;
        d.draw_line(o, wave_x, center_y, wave_x + wave_w, center_y, 1.0f,
                    {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.15f});

        // Draw waveform min/max bars
        float bin_w = wave_w / static_cast<float>(kWaveformBins);
        float half_h = (wave_h - 4.0f) * 0.5f;

        for (int b = 0; b < kWaveformBins; ++b) {
            float bx = wave_x + static_cast<float>(b) * bin_w;
            float lo = snap.bins[b].min_val;
            float hi = snap.bins[b].max_val;

            // Clamp to [-1, 1] for display
            lo = std::fmax(-1.0f, std::fmin(1.0f, lo));
            hi = std::fmax(-1.0f, std::fmin(1.0f, hi));

            float y_top = center_y - hi * half_h;
            float y_bot = center_y - lo * half_h;
            float bar_h = std::fmax(1.0f, y_bot - y_top);

            d.draw_rect(o, bx, y_top, bin_w, bar_h,
                        {th.accent.r, th.accent.g, th.accent.b, 0.4f});
        }

        // --- Grain overlays ---
        for (int g = 0; g < kMaxGrains; ++g) {
            const auto& gs = snap.grains[g];
            if (!gs.active) continue;

            float g_start = gs.bin_start;
            float g_width = gs.bin_width;

            // Convert bin coordinates to pixel coordinates
            float gx = wave_x + g_start * bin_w;
            float gw = g_width * bin_w;

            // Clamp to visible area (handle wrapping)
            if (gx < wave_x) {
                // Grain wraps from left — draw the visible right portion
                float overflow = wave_x - gx;
                gx = wave_x;
                gw -= overflow;
            }
            if (gx + gw > wave_x + wave_w) {
                gw = wave_x + wave_w - gx;
            }
            if (gw <= 0.0f) continue;

            // Grain rectangle — semi-transparent
            float grain_alpha = 0.15f + 0.1f * (1.0f - gs.phase);
            d.draw_rect(o, gx, wave_y + 2.0f, gw, wave_h - 4.0f,
                        {th.accent.r, th.accent.g, th.accent.b, grain_alpha});

            // Playback cursor within grain
            float cursor_x = gx + gs.phase * gw;
            if (cursor_x >= wave_x && cursor_x <= wave_x + wave_w) {
                d.draw_line(o, cursor_x, wave_y + 2.0f, cursor_x, wave_y + wave_h - 2.0f,
                            1.5f, {th.bright_text.r, th.bright_text.g, th.bright_text.b, 0.6f});
            }
        }

        // --- Position indicator ---
        // position=0 means newest (right), position=1 means oldest (left)
        float pos_x = wave_x + (1.0f - snap.position_norm) * wave_w;
        d.draw_line(o, pos_x, wave_y, pos_x, wave_y + wave_h, 2.0f,
                    {th.accent.r, th.accent.g, th.accent.b, 0.9f});

        d.pop_clip_rect(o);

        py += wave_h + kInspPad;

        // --- Interaction: click-drag to set position ---
        if (ctx->mouse.left_clicked &&
            ctx->mouse.x >= wave_x && ctx->mouse.x <= wave_x + wave_w &&
            ctx->mouse.y >= wave_y && ctx->mouse.y <= wave_y + wave_h) {
            insp_dragging_ = true;
        }
        if (insp_dragging_ && ctx->mouse.left_down) {
            float rel = (ctx->mouse.x - wave_x) / wave_w;
            rel = std::fmax(0.0f, std::fmin(1.0f, rel));
            float new_pos = 1.0f - rel; // left=oldest(1), right=newest(0)
            ctx->commands.set_param(ctx->commands.opaque, "position", new_pos);
        }
        if (!ctx->mouse.left_down) {
            insp_dragging_ = false;
        }

        // --- Info bar ---
        // Active grain count
        char grain_text[32];
        snprintf(grain_text, sizeof(grain_text), "%d grain%s",
                 snap.active_count, snap.active_count == 1 ? "" : "s");
        d.draw_text(o, px + 4.0f, py + 2.0f, grain_text, th.dim_text, 1.0f);

        // Window shape mini preview
        float win_x = px + w - 44.0f;
        float win_y = py;
        float win_w = 40.0f;
        float win_h = kInfoBarH - 2.0f;

        d.draw_rounded_rect(o, win_x, win_y, win_w, win_h, 2.0f,
                            {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.6f});

        // Draw window shape as polyline
        int segs = 20;
        float prev_lx = win_x + 1.0f;
        float prev_ly = win_y + win_h - 1.0f;
        for (int s = 0; s <= segs; ++s) {
            float t = static_cast<float>(s) / static_cast<float>(segs);
            float env = grain_window(t, snap.window_type);
            float lx = win_x + 1.0f + t * (win_w - 2.0f);
            float ly = win_y + win_h - 1.0f - env * (win_h - 2.0f);
            if (s > 0) {
                d.draw_line(o, prev_lx, prev_ly, lx, ly, 1.0f,
                            {th.accent.r, th.accent.g, th.accent.b, 0.7f});
            }
            prev_lx = lx;
            prev_ly = ly;
        }

        // Window type label
        const char* win_labels[] = {"Hn", "Hm", "Bk", "Tr"};
        int wt = std::max(0, std::min(3, snap.window_type));
        float label_w = d.text_width(o, win_labels[wt], 0.85f);
        d.draw_text(o, win_x - label_w - 3.0f, py + 2.0f,
                    win_labels[wt], {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.5f}, 0.85f);

        py += kInfoBarH;

        ctx->consumed_height = py - ctx->content_y;
    }
};

VIVID_REGISTER(GranularSynth)
VIVID_INSPECTOR(GranularSynth)
