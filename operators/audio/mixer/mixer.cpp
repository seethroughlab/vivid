#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"

struct Mixer : vivid::AudioOperatorBase {
    static constexpr const char* kName   = "Mixer";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> gain1{"gain_1", 1.0f, 0.0f, 2.0f};
    vivid::Param<float> gain2{"gain_2", 1.0f, 0.0f, 2.0f};
    vivid::Param<float> gain3{"gain_3", 1.0f, 0.0f, 2.0f};
    vivid::Param<float> gain4{"gain_4", 1.0f, 0.0f, 2.0f};

    Mixer() {
        // 2×2 knob grid
        vivid::layout_row(gain1, 2, 0);  vivid::display_hint(gain1, VIVID_DISPLAY_KNOB);
        vivid::layout_row(gain2, 2, 1);  vivid::display_hint(gain2, VIVID_DISPLAY_KNOB);
        vivid::layout_row(gain3, 2, 0);  vivid::display_hint(gain3, VIVID_DISPLAY_KNOB);
        vivid::layout_row(gain4, 2, 1);  vivid::display_hint(gain4, VIVID_DISPLAY_KNOB);
        for (auto* p : {&gain1, &gain2, &gain3, &gain4}) {
            vivid::semantic_tag(*p, "amplitude_linear");
            vivid::semantic_shape(*p, "scalar");
        }
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&gain1);
        out.push_back(&gain2);
        out.push_back(&gain3);
        out.push_back(&gain4);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input_1", VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"input_2", VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"input_3", VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"input_4", VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
        out.push_back({"output",  VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_AUDIO_BUFFER, 0, nullptr, 1, 0.0f});
    }

    void process_audio(const VividAudioContext* ctx) override {
        const float* in1 = ctx->input_buffers[0];
        const float* in2 = ctx->input_buffers[1];
        const float* in3 = ctx->input_buffers[2];
        const float* in4 = ctx->input_buffers[3];
        float*       out = ctx->output_buffers[0];
        float g1 = gain1.value, g2 = gain2.value;
        float g3 = gain3.value, g4 = gain4.value;

        for (uint32_t i = 0; i < ctx->buffer_size; i++)
            out[i] = in1[i] * g1 + in2[i] * g2 + in3[i] * g3 + in4[i] * g4;
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        // 4 input bars on left converging to 1 output bar on right
        uint32_t W = ctx->width;
        uint32_t H = ctx->height;

        const uint8_t bg_r = 18,  bg_g = 20,  bg_b = 23,  bg_a = 230;
        const uint8_t ln_r = 100, ln_g = 190, ln_b = 200, ln_a = 220;
        const uint8_t dm_r = 60,  dm_g = 130, dm_b = 160, dm_a = 160;

        // Fill background
        for (uint32_t y = 0; y < H; ++y) {
            uint8_t* row = ctx->pixels + y * ctx->stride;
            for (uint32_t x = 0; x < W; ++x) {
                uint8_t* px = row + x * 4;
                px[0] = bg_r; px[1] = bg_g; px[2] = bg_b; px[3] = bg_a;
            }
        }

        // Draw 4 horizontal input lane lines converging to center
        float cx = static_cast<float>(W) * 0.6f;  // convergence x
        float cy = static_cast<float>(H) * 0.5f;  // convergence y
        float lane_ys[4] = {
            static_cast<float>(H) * 0.2f,
            static_cast<float>(H) * 0.4f,
            static_cast<float>(H) * 0.6f,
            static_cast<float>(H) * 0.8f,
        };

        for (int lane = 0; lane < 4; lane++) {
            float y0 = lane_ys[lane];
            // Horizontal segment from left to convergence x
            for (uint32_t x = 4; x < static_cast<uint32_t>(cx); ++x) {
                // lerp y toward cy
                float t  = static_cast<float>(x) / cx;
                float fy = y0 + (cy - y0) * t * t;
                uint32_t py = static_cast<uint32_t>(fy);
                if (py < H) {
                    uint8_t* px = ctx->pixels + py * ctx->stride + x * 4;
                    px[0] = dm_r; px[1] = dm_g; px[2] = dm_b; px[3] = dm_a;
                }
            }
        }

        // Output line from convergence to right edge
        uint32_t out_y = static_cast<uint32_t>(cy);
        for (uint32_t x = static_cast<uint32_t>(cx); x < W - 4; ++x) {
            if (out_y < H) {
                uint8_t* px = ctx->pixels + out_y * ctx->stride + x * 4;
                px[0] = ln_r; px[1] = ln_g; px[2] = ln_b; px[3] = ln_a;
            }
        }
    }
};

VIVID_REGISTER(Mixer)
VIVID_THUMBNAIL(Mixer)
