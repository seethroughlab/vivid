#include "operator_api/operator.h"
#include "operator_api/audio_operator.h"

#include <cmath>
#include <vector>

// ---------------------------------------------------------------------------
// State Variable Filter (SVF) — Hal Chamberlin / Andy Simper formulation
//
// Per-sample update (run twice for numerical stability and tighter response):
//   low  += f * band
//   high  = input - low - q * band
//   band += f * high
//   notch = high + low
//
// f = 2 * sin(pi * cutoff / sample_rate)  — MUST be clamped to <= 0.95
// q = 1 / resonance
//
// Float CV input ordinals:
//   cutoff_cv    -> input_float_values[0]  semitone offset (±72 st), default 0.0
//   resonance_cv -> input_float_values[1]  additive offset (±2.0),   default 0.0
// ---------------------------------------------------------------------------

struct Filter : vivid::AudioOperatorBase {
    static constexpr const char* kName   = "Filter";
    static constexpr bool kTimeDependent = false;

    vivid::Param<float> cutoff    {"cutoff",    2000.0f, 20.0f, 20000.0f};
    vivid::Param<float> resonance {"resonance",  0.7f,   0.1f,   4.0f};
    vivid::Param<int>   mode      {"mode",       0, {"Low-pass", "High-pass", "Band-pass", "Notch"}};

    float low_  = 0.0f;
    float band_ = 0.0f;

    Filter() {
        vivid::semantic_tag(cutoff, "frequency_hz");
        vivid::semantic_shape(cutoff, "scalar");
        vivid::semantic_unit(cutoff, "Hz");
        vivid::display_hint(cutoff, VIVID_DISPLAY_KNOB);

        vivid::semantic_tag(resonance, "amplitude_linear");
        vivid::semantic_shape(resonance, "scalar");
        vivid::semantic_intent(resonance, "resonance");
        vivid::display_hint(resonance, VIVID_DISPLAY_KNOB);

        vivid::semantic_shape(mode, "scalar");
        vivid::semantic_intent(mode, "filter_mode");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&cutoff);
        out.push_back(&resonance);
        out.push_back(&mode);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"input",        VIVID_PORT_AUDIO, VIVID_PORT_INPUT,  0, 1, 0.0f});
        out.push_back({"output",       VIVID_PORT_AUDIO, VIVID_PORT_OUTPUT, 0, 1, 0.0f});
        out.push_back({"cutoff_cv",    VIVID_PORT_FLOAT, VIVID_PORT_INPUT,  0, 0, 0.0f});
        out.push_back({"resonance_cv", VIVID_PORT_FLOAT, VIVID_PORT_INPUT,  0, 0, 0.0f});
    }

    void process_audio(const VividAudioContext* ctx) override {
        const float* in  = ctx->input_buffers[0];
        float*       out = ctx->output_buffers[0];
        uint32_t frames  = ctx->buffer_size;

        float cutoff_cv_val    = ctx->input_float_values ? ctx->input_float_values[0] : 0.0f;
        float resonance_cv_val = ctx->input_float_values ? ctx->input_float_values[1] : 0.0f;

        // cutoff CV: semitone offset via 2^(cv/12), clamped to [20, 20000]
        float mod_cutoff = cutoff.value * std::pow(2.0f, cutoff_cv_val / 12.0f);
        if (mod_cutoff < 20.0f)    mod_cutoff = 20.0f;
        if (mod_cutoff > 20000.0f) mod_cutoff = 20000.0f;

        // resonance CV: additive, clamped to [0.1, 4.0]
        float mod_resonance = resonance.value + resonance_cv_val;
        if (mod_resonance < 0.1f) mod_resonance = 0.1f;
        if (mod_resonance > 4.0f) mod_resonance = 4.0f;

        float sr = static_cast<float>(ctx->sample_rate);
        float f  = 2.0f * std::sin(3.14159265f * mod_cutoff / sr);
        // Clamp f: values above ~0.95 cause unstable feedback (f can reach ~1.93 at 20kHz/48kHz)
        if (f > 0.95f) f = 0.95f;
        float q = 1.0f / mod_resonance;

        int filter_mode = mode.int_value();

        float low  = low_;
        float band = band_;

        for (uint32_t i = 0; i < frames; i++) {
            float input = in[i];

            // First SVF pass
            low  += f * band;
            float high  = input - low - q * band;
            band += f * high;
            // Second SVF pass — reduces aliasing, tightens slope (Andy Simper recommendation)
            low  += f * band;
            high  = input - low - q * band;
            band += f * high;

            float notch = high + low;

            switch (filter_mode) {
                case 0:  out[i] = low;   break;  // Low-pass
                case 1:  out[i] = high;  break;  // High-pass
                case 2:  out[i] = band;  break;  // Band-pass
                case 3:  out[i] = notch; break;  // Notch
                default: out[i] = low;   break;
            }
        }

        low_  = low;
        band_ = band;
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        float p_cutoff    = (ctx->param_count > 0) ? ctx->param_values[0] : 2000.0f;
        float p_resonance = (ctx->param_count > 1) ? ctx->param_values[1] : 0.7f;
        int   p_mode      = (ctx->param_count > 2) ? static_cast<int>(ctx->param_values[2]) : 0;
        if (p_cutoff < 20.0f)    p_cutoff = 20.0f;
        if (p_cutoff > 20000.0f) p_cutoff = 20000.0f;
        if (p_resonance < 0.1f)  p_resonance = 0.1f;
        if (p_resonance > 4.0f)  p_resonance = 4.0f;

        uint32_t W = ctx->width;
        uint32_t H = ctx->height;
        float w = static_cast<float>(W);
        float h = static_cast<float>(H);
        float pad = 4.0f;

        float log_lo = std::log10(20.0f);
        float log_hi = std::log10(20000.0f);

        // Ideal 2-pole magnitude formula (closely matches SVF at audio frequencies)
        // r = freq / cutoff, Q = resonance (not 1/Q — biquad convention here)
        auto magnitude = [&](float freq) -> float {
            float r     = freq / p_cutoff;
            float r2    = r * r;
            float Q     = p_resonance;
            float denom = (1.0f - r2) * (1.0f - r2) + (r / Q) * (r / Q);
            if (denom < 1e-10f) denom = 1e-10f;
            switch (p_mode) {
                case 0: return 1.0f / std::sqrt(denom);                               // LP
                case 1: return r2   / std::sqrt(denom);                               // HP
                case 2: return (r / Q) / std::sqrt(denom);                            // BP
                case 3: return std::sqrt((1.0f - r2) * (1.0f - r2) / denom);         // Notch
                default: return 1.0f / std::sqrt(denom);
            }
        };

        // Y axis: -48 dB (bottom) to +12 dB (top)
        float db_min = -48.0f;
        float db_max =  12.0f;

        auto mag_to_y = [&](float mag) -> float {
            float db = 20.0f * std::log10(mag > 1e-6f ? mag : 1e-6f);
            if (db < db_min) db = db_min;
            if (db > db_max) db = db_max;
            float t = (db - db_min) / (db_max - db_min);  // 0=bottom, 1=top
            return pad + (1.0f - t) * (h - 2.0f * pad);
        };

        std::vector<float> curve_y(W);
        for (uint32_t x = 0; x < W; ++x) {
            float t     = static_cast<float>(x) / (w - 1.0f);
            float log_f = log_lo + t * (log_hi - log_lo);
            float freq  = std::pow(10.0f, log_f);
            curve_y[x]  = mag_to_y(magnitude(freq));
        }

        float zero_y  = mag_to_y(1.0f);
        int   zero_iy = static_cast<int>(zero_y + 0.5f);

        // Color palette
        const uint8_t bg_r = 18,  bg_g = 20,  bg_b = 23,  bg_a = 230;
        const uint8_t ln_r = 100, ln_g = 190, ln_b = 200, ln_a = 230;
        const uint8_t fi_r = 60,  fi_g = 130, fi_b = 160, fi_a = 140;

        for (uint32_t y = 0; y < H; ++y) {
            uint8_t* row = ctx->pixels + y * ctx->stride;
            float fy = static_cast<float>(y);
            for (uint32_t x = 0; x < W; ++x) {
                uint8_t* px = row + x * 4;
                float dist  = fy - curve_y[x];
                if (std::fabs(dist) < 1.2f) {
                    px[0] = ln_r; px[1] = ln_g; px[2] = ln_b; px[3] = ln_a;
                } else if (dist > 0.0f) {
                    // Below the curve — attenuated region fill
                    px[0] = fi_r; px[1] = fi_g; px[2] = fi_b; px[3] = fi_a;
                } else {
                    px[0] = bg_r; px[1] = bg_g; px[2] = bg_b; px[3] = bg_a;
                }
            }
        }

        // 0 dB reference line (dim gray, within padded area)
        if (zero_iy >= 0 && zero_iy < static_cast<int>(H)) {
            uint8_t* row = ctx->pixels + zero_iy * ctx->stride;
            for (uint32_t x = static_cast<uint32_t>(pad);
                 x < W - static_cast<uint32_t>(pad); ++x) {
                uint8_t* px = row + x * 4;
                px[0] = (px[0] + 60) / 2;
                px[1] = (px[1] + 60) / 2;
                px[2] = (px[2] + 60) / 2;
                px[3] = 180;
            }
        }
    }
};

VIVID_REGISTER(Filter)
VIVID_THUMBNAIL(Filter)
