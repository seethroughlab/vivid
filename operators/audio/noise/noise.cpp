#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"
#include "operator_api/audio_dsp.h"

struct Noise : vivid::AudioOperatorBase {
    static constexpr const char* kName   = "Noise";
    static constexpr bool kTimeDependent = false;  // uses PRNG state, not ctx->time

    vivid::Param<int>   color    {"color",     0, {"White", "Pink", "Brown", "Blue", "Violet"}};
    vivid::Param<float> amplitude{"amplitude", 0.5f, 0.0f, 1.0f};

    audio_dsp::WhiteNoise  white_;
    audio_dsp::PinkNoise   pink_;
    audio_dsp::BrownNoise  brown_;
    audio_dsp::BlueNoise   blue_;
    audio_dsp::VioletNoise violet_;

    Noise() {
        vivid::semantic_tag(amplitude, "amplitude_linear");
        vivid::semantic_shape(amplitude, "scalar");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&color);
        out.push_back(&amplitude);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"output", VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, 0, 0, 0.0f});
    }

    void process_audio(const VividAudioContext* ctx) override {
        float*   out = ctx->output_buffers[0];
        float    amp = amplitude.value;
        int      col = color.int_value();

        switch (col) {
            case 1:  // Pink
                for (uint32_t i = 0; i < ctx->buffer_size; i++)
                    out[i] = pink_.next() * amp;
                break;
            case 2:  // Brown
                for (uint32_t i = 0; i < ctx->buffer_size; i++)
                    out[i] = brown_.next() * amp;
                break;
            case 3:  // Blue
                for (uint32_t i = 0; i < ctx->buffer_size; i++)
                    out[i] = blue_.next() * amp;
                break;
            case 4:  // Violet
                for (uint32_t i = 0; i < ctx->buffer_size; i++)
                    out[i] = violet_.next() * amp;
                break;
            default: // White
                for (uint32_t i = 0; i < ctx->buffer_size; i++)
                    out[i] = white_.next() * amp;
                break;
        }
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        // Static waveform: a few cycles of random vertical bars.
        // Uses a fixed seed so the thumbnail is deterministic.
        uint32_t W = ctx->width;
        uint32_t H = ctx->height;
        float    h = static_cast<float>(H);
        float    mid = h * 0.5f;

        const uint8_t bg_r = 18,  bg_g = 20,  bg_b = 23,  bg_a = 230;
        const uint8_t ln_r = 100, ln_g = 190, ln_b = 200, ln_a = 220;

        // Fill background
        for (uint32_t y = 0; y < H; ++y) {
            uint8_t* row = ctx->pixels + y * ctx->stride;
            for (uint32_t x = 0; x < W; ++x) {
                uint8_t* px = row + x * 4;
                px[0] = bg_r; px[1] = bg_g; px[2] = bg_b; px[3] = bg_a;
            }
        }

        // Draw noise bars using a deterministic LCG (same seed each call)
        uint32_t rng = 0xdeadbeef;
        for (uint32_t x = 0; x < W; ++x) {
            rng = rng * 1664525u + 1013904223u;
            float val = static_cast<float>(static_cast<int32_t>(rng)) / 2147483648.0f; // [-1, 1]
            float bar_top    = mid + val * mid * 0.85f;
            float bar_bottom = mid - val * mid * 0.85f;
            if (bar_top > bar_bottom) { float t = bar_top; bar_top = bar_bottom; bar_bottom = t; }
            uint32_t y0 = static_cast<uint32_t>(bar_top    < 0.0f ? 0.0f : bar_top);
            uint32_t y1 = static_cast<uint32_t>(bar_bottom > h - 1 ? h - 1 : bar_bottom);
            for (uint32_t y = y0; y <= y1 && y < H; ++y) {
                uint8_t* px = ctx->pixels + y * ctx->stride + x * 4;
                px[0] = ln_r; px[1] = ln_g; px[2] = ln_b; px[3] = ln_a;
            }
        }
    }
};

VIVID_REGISTER(Noise)
VIVID_THUMBNAIL(Noise)
