#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/spectrum_bus.h"   // host: vivid_master_spectrum (resolved at dlopen)
#include "operator_api/value_view.h"     // FLOAT-MANY lane output
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// =============================================================================
// AudioSpectrum — the live master spectrum as a FLOAT-MANY value lane
// =============================================================================
//
// A pure ANALYSIS node: it reads the master-spectrum bus (log-band magnitudes, low→high) and emits ONE
// value lane of `bands` smoothed 0..1 magnitudes. It knows nothing about geometry — wire it into a
// consumer (InstancesFromLanes' scale_y for a 3D equaliser, a Deformer, whatever) so the audio→visual
// chain is COMPOSABLE and every stage is a visible node, not one monolith.
//
// Per-band envelope follower (fast attack / slow release) + optional per-band AGC (`normalize`) so each
// band uses its own dynamic range and a quiet high band dances as much as a loud low one.

struct AudioSpectrum : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName         = "AudioSpectrum";
    static constexpr bool kTimeDependent       = true;   // reads live audio every frame
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_GENERATE;

    vivid::Param<int>   bands    {"bands",    48, 1, 64};
    vivid::Param<float> gain     {"gain",     1.2f, 0.1f, 20.0f};   // absolute-mode boost
    vivid::Param<float> tilt     {"tilt",     0.7f, 0.0f, 4.0f};    // rising high-freq boost (1/f comp)
    vivid::Param<float> normalize{"normalize",0.6f, 0.0f, 1.0f};    // per-band AGC blend
    vivid::Param<float> attack   {"attack",   0.02f, 0.0f, 1.0f};   // rise time constant (s)
    vivid::Param<float> release  {"release",  0.18f, 0.0f, 2.0f};   // fall time constant (s)

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(bands,     "Spectrum");
        vivid::param_group(gain,      "Response");
        vivid::param_group(tilt,      "Response");
        vivid::param_group(normalize, "Response");
        vivid::param_group(attack,    "Response");
        vivid::param_group(release,   "Response");
        out.push_back(&bands);  out.push_back(&gain);   out.push_back(&tilt);
        out.push_back(&normalize); out.push_back(&attack); out.push_back(&release);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        VividPortDescriptor p{};
        p.name = "spectrum"; p.type = VIVID_PORT_SCALAR;
        p.direction = VIVID_PORT_OUTPUT; p.multiplicity = VIVID_MULTIPLICITY_MANY;
        p.semantic_shape = "lane_array";
        out.push_back(p);
    }

    void draw_thumbnail(const VividThumbnailContext*) override {}

    void process_gpu(const VividGpuContext* ctx) override {
        const uint32_t n = static_cast<uint32_t>(std::clamp(bands.int_value(), 1, VIVID_SPECTRUM_MAX_BANDS));
        float raw[VIVID_SPECTRUM_MAX_BANDS] = {0.f};
        const uint32_t got = vivid_master_spectrum(raw, VIVID_SPECTRUM_MAX_BANDS);

        if (smoothed_.size() != n) smoothed_.assign(n, 0.f);
        if (peak_.size() != n)     peak_.assign(n, 0.05f);
        const float dt = std::max(0.f, static_cast<float>(ctx->delta_time));
        const float atk = attack.value, rel = release.value, g = gain.value, nrm = normalize.value;

        out_.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            const float frac = (n > 1) ? static_cast<float>(i) / static_cast<float>(n - 1) : 0.f;
            float target = 0.f;
            if (got > 0) {
                const float fb = frac * static_cast<float>(got - 1);
                const uint32_t b0 = static_cast<uint32_t>(fb);
                const uint32_t b1 = std::min(got - 1, b0 + 1);
                const float ft = fb - static_cast<float>(b0);
                const float mag = raw[b0] * (1.f - ft) + raw[b1] * ft;
                peak_[i] = std::max(peak_[i] * 0.9985f, mag);
                const float agc = mag / std::max(peak_[i], 0.02f);
                const float lin = mag * g;
                const float tiltf = 1.f + tilt.value * frac;
                target = std::clamp((lin * (1.f - nrm) + agc * nrm) * tiltf, 0.f, 1.f);
            }
            const float tau = (target > smoothed_[i]) ? atk : rel;
            if (tau <= 1e-5f || dt <= 0.f) smoothed_[i] = target;
            else smoothed_[i] += (target - smoothed_[i]) * (1.f - std::exp(-dt / tau));
            out_[i] = smoothed_[i];
        }

        // Publish the lane.
        if (ctx->value_outputs) {
            if (float* buf = vivid_value_output_floats(&ctx->value_outputs[0], n)) {
                std::copy(out_.begin(), out_.end(), buf);
                vivid_value_output_commit(&ctx->value_outputs[0], n);
            }
        }
    }

private:
    std::vector<float> out_, smoothed_, peak_;
};

VIVID_REGISTER(AudioSpectrum)
VIVID_THUMBNAIL(AudioSpectrum)
