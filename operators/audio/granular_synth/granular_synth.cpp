#include "operator_api/operator.h"
#include "shared/granular_dsp/granular_dsp.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>

// Inspector display constants
static constexpr float kWaveformH = 80.0f;
static constexpr float kInfoBarH = 18.0f;
static constexpr float kInspPad = 4.0f;

struct DoubleBufferedSnapshot {
    vivid::granular_dsp::InspectorSnapshot slots[2];
    std::atomic<int> read_idx{0};

    void write(const vivid::granular_dsp::Engine& engine, float position_param, int win_type) {
        const int wi = 1 - read_idx.load(std::memory_order_relaxed);
        auto& snap = slots[wi];
        snap = {};
        engine.fill_inspector_snapshot(snap, position_param, win_type);
        read_idx.store(wi, std::memory_order_release);
    }

    const vivid::granular_dsp::InspectorSnapshot& read() const {
        return slots[read_idx.load(std::memory_order_acquire)];
    }
};

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
    static constexpr const char* kName = "GranularSynth";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> grain_size{"grain_size", 80.0f, 5.0f, 500.0f};
    vivid::Param<float> density{"density", 10.0f, 0.5f, 60.0f};
    vivid::Param<float> position{"position", 0.8f, 0.0f, 1.0f};
    vivid::Param<float> pitch{"pitch", 0.0f, -24.0f, 24.0f};
    vivid::Param<float> randomize{"randomize", 0.1f, 0.0f, 1.0f};
    vivid::Param<int> window{"window", 0, {"Hann", "Hamming", "Blackman", "Triangle"}};
    vivid::Param<float> mix{"mix", 1.0f, 0.0f, 1.0f};

    vivid::granular_dsp::Engine engine_;
    DoubleBufferedSnapshot insp_snapshot_;
    bool insp_dragging_ = false;

    // Inspector snapshot rate-limiting. fill_inspector_snapshot scans the
    // entire 4-second capture buffer (~192k samples at 48kHz) into 280
    // waveform bins; doing that every audio block was the dominant cost in
    // this operator (~170us at 256 frames, ~95% of it pure UI work). UI
    // only refreshes at ~60Hz, so publishing every 8 audio blocks — ~43ms
    // period at 48kHz/256-frame blocks — is still faster than the display.
    static constexpr int kSnapshotEveryNBlocks = 8;
    int snapshot_counter_ = 0;

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
        out.push_back({"input", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT,
                       VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT,
                       VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"position_cv", VIVID_PORT_SCALAR, VIVID_PORT_INPUT,
                       VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"pitch_cv", VIVID_PORT_SCALAR, VIVID_PORT_INPUT,
                       VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        out.push_back({"density_cv", VIVID_PORT_SCALAR, VIVID_PORT_INPUT,
                       VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f});
        vivid::append_analysis_ports(out);
    }

    void process_audio(const VividAudioContext* ctx) override {
        const float* in = ctx->input_buffers[0];
        float* out = ctx->output_buffers[0];

        const float pos_cv = ctx->input_buffers[1] ? ctx->input_buffers[1][0] : 0.0f;
        const float pitch_cv = ctx->input_buffers[2] ? ctx->input_buffers[2][0] : 0.0f;
        const float density_cv = ctx->input_buffers[3] ? ctx->input_buffers[3][0] : 0.0f;

        vivid::granular_dsp::ProcessParams params{};
        params.position = std::clamp(position.value + pos_cv, 0.0f, 1.0f);
        params.pitch = std::clamp(pitch.value + pitch_cv, -24.0f, 24.0f);
        params.density = std::clamp(density.value + density_cv, 0.5f, 60.0f);
        params.grain_size_ms = grain_size.value;
        params.randomize = randomize.value;
        params.window_type = window.int_value();
        params.mix = mix.value;

        engine_.process(in, out, ctx->buffer_size, ctx->sample_rate, params);
        if (++snapshot_counter_ >= kSnapshotEveryNBlocks) {
            snapshot_counter_ = 0;
            insp_snapshot_.write(engine_, params.position, params.window_type);
        }
    }

    void draw_inspector(VividInspectorContext* ctx) override {
        auto& d = ctx->draw;
        void* o = d.opaque;
        const auto& th = ctx->theme;

        const float px = ctx->content_x;
        float py = ctx->content_y + kInspPad;
        const float w = ctx->content_width;

        const auto& snap = insp_snapshot_.read();

        const float wave_x = px;
        const float wave_y = py;
        const float wave_w = w;
        const float wave_h = kWaveformH;

        d.draw_rounded_rect(o, wave_x, wave_y, wave_w, wave_h, th.corner_radius,
                            {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.95f});

        d.push_clip_rect(o, wave_x, wave_y, wave_w, wave_h);

        const float center_y = wave_y + wave_h * 0.5f;
        d.draw_line(o, wave_x, center_y, wave_x + wave_w, center_y, 1.0f,
                    {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.15f});

        const float bin_w = wave_w / static_cast<float>(vivid::granular_dsp::kWaveformBins);
        const float half_h = (wave_h - 4.0f) * 0.5f;

        for (int b = 0; b < vivid::granular_dsp::kWaveformBins; ++b) {
            const float bx = wave_x + static_cast<float>(b) * bin_w;
            float lo = std::clamp(snap.bins[b].min_val, -1.0f, 1.0f);
            float hi = std::clamp(snap.bins[b].max_val, -1.0f, 1.0f);
            const float y_top = center_y - hi * half_h;
            const float y_bot = center_y - lo * half_h;
            const float bar_h = std::max(1.0f, y_bot - y_top);
            d.draw_rect(o, bx, y_top, bin_w, bar_h,
                        {th.accent.r, th.accent.g, th.accent.b, 0.4f});
        }

        for (int g = 0; g < vivid::granular_dsp::kMaxGrains; ++g) {
            const auto& gs = snap.grains[g];
            if (!gs.active) continue;

            float gx = wave_x + gs.bin_start * bin_w;
            float gw = gs.bin_width * bin_w;

            if (gx < wave_x) {
                const float overflow = wave_x - gx;
                gx = wave_x;
                gw -= overflow;
            }
            if (gx + gw > wave_x + wave_w)
                gw = wave_x + wave_w - gx;
            if (gw <= 0.0f) continue;

            const float grain_alpha = 0.15f + 0.1f * (1.0f - gs.phase);
            d.draw_rect(o, gx, wave_y + 2.0f, gw, wave_h - 4.0f,
                        {th.accent.r, th.accent.g, th.accent.b, grain_alpha});

            const float cursor_x = gx + gs.phase * gw;
            if (cursor_x >= wave_x && cursor_x <= wave_x + wave_w) {
                d.draw_line(o, cursor_x, wave_y + 2.0f, cursor_x, wave_y + wave_h - 2.0f,
                            1.5f, {th.bright_text.r, th.bright_text.g, th.bright_text.b, 0.6f});
            }
        }

        const float pos_x = wave_x + (1.0f - snap.position_norm) * wave_w;
        d.draw_line(o, pos_x, wave_y, pos_x, wave_y + wave_h, 2.0f,
                    {th.accent.r, th.accent.g, th.accent.b, 0.9f});

        d.pop_clip_rect(o);
        py += wave_h + kInspPad;

        if (ctx->mouse.left_clicked &&
            ctx->mouse.x >= wave_x && ctx->mouse.x <= wave_x + wave_w &&
            ctx->mouse.y >= wave_y && ctx->mouse.y <= wave_y + wave_h) {
            insp_dragging_ = true;
        }
        if (insp_dragging_ && ctx->mouse.left_down) {
            const float rel = std::clamp((ctx->mouse.x - wave_x) / wave_w, 0.0f, 1.0f);
            ctx->commands.set_param(ctx->commands.opaque, "position", 1.0f - rel);
        }
        if (!ctx->mouse.left_down)
            insp_dragging_ = false;

        char grain_text[32];
        std::snprintf(grain_text, sizeof(grain_text), "%d grain%s",
                      snap.active_count, snap.active_count == 1 ? "" : "s");
        d.draw_text(o, px + 4.0f, py + 2.0f, grain_text, th.dim_text, 1.0f);

        const float win_x = px + w - 44.0f;
        const float win_y = py;
        const float win_w = 40.0f;
        const float win_h = kInfoBarH - 2.0f;

        d.draw_rounded_rect(o, win_x, win_y, win_w, win_h, 2.0f,
                            {th.dark_bg.r, th.dark_bg.g, th.dark_bg.b, 0.6f});

        int wt = std::max(0, std::min(3, snap.window_type));
        auto preview_window = [](float phase, int type) {
            switch (type) {
                default:
                case 0: return 0.5f * (1.0f - std::cos(2.0f * 3.14159265f * phase));
                case 1: return 0.54f - 0.46f * std::cos(2.0f * 3.14159265f * phase);
                case 2: return 0.42f - 0.5f * std::cos(2.0f * 3.14159265f * phase)
                              + 0.08f * std::cos(4.0f * 3.14159265f * phase);
                case 3: return 1.0f - std::fabs(2.0f * phase - 1.0f);
            }
        };

        const int segs = 20;
        float prev_lx = win_x + 1.0f;
        float prev_ly = win_y + win_h - 1.0f;
        for (int s = 0; s <= segs; ++s) {
            const float t = static_cast<float>(s) / static_cast<float>(segs);
            const float env = preview_window(t, wt);
            const float lx = win_x + 1.0f + t * (win_w - 2.0f);
            const float ly = win_y + win_h - 1.0f - env * (win_h - 2.0f);
            if (s > 0) {
                d.draw_line(o, prev_lx, prev_ly, lx, ly, 1.0f,
                            {th.accent.r, th.accent.g, th.accent.b, 0.7f});
            }
            prev_lx = lx;
            prev_ly = ly;
        }

        const char* win_labels[] = {"Hn", "Hm", "Bk", "Tr"};
        const float label_w = d.text_width(o, win_labels[wt], 0.85f);
        d.draw_text(o, win_x - label_w - 3.0f, py + 2.0f,
                    win_labels[wt], {th.dim_text.r, th.dim_text.g, th.dim_text.b, 0.5f}, 0.85f);

        py += kInfoBarH;
        ctx->consumed_height = py - ctx->content_y;
    }
};

VIVID_DEFINE_OP(GranularSynth) {
}

VIVID_REGISTER(GranularSynth)
VIVID_INSPECTOR(GranularSynth)
