#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/gpu_3d.h"
#include "operator_api/spectrum_bus.h"   // host: vivid_master_spectrum (resolved at dlopen)
#include "operator_api/thumbnail_3d.h"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// =============================================================================
// AudioSpectrum3D — the live master spectrum as a 3D instance field (equaliser)
// =============================================================================
//
// Reads the master-spectrum bus (per-frame log-band magnitudes, low→high) and emits an InstanceArray3D
// where each instance is one frequency band: height (scale.y) = that band's energy, colour = a cosine
// palette across the spectrum. Wire it into Instancer3D's `instances` input with a Shape3D (a cube) as
// the `scene` and you get a 3D equaliser whose bars dance to the actual frequency content — reactivity
// that reads as PER-BAND, not a single global pump. Each bar is temporally smoothed (fast attack, slow
// release) so bars snap up on transients then fall smoothly instead of flickering.

struct AudioSpectrum3D : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName         = "AudioSpectrum3D";
    static constexpr bool kTimeDependent       = true;   // reads live audio every frame
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_GENERATE;

    vivid::Param<int>   bars     {"bars",     32, 1, 64};
    vivid::Param<int>   layout   {"layout",   0, {"Line", "Arc", "Circle"}};
    vivid::Param<float> width    {"width",    18.0f, 1.0f, 80.0f};   // Line span / Arc+Circle radius
    vivid::Param<float> height   {"height",   9.0f, 0.1f, 40.0f};    // magnitude → bar height
    vivid::Param<float> gain     {"gain",     3.0f, 0.1f, 20.0f};    // spectrum magnitude boost
    vivid::Param<float> tilt     {"tilt",     1.6f, 0.0f, 4.0f};     // extra high-freq boost (1/f comp)
    vivid::Param<float> thickness{"thickness",0.6f, 0.05f, 5.0f};    // bar cross-section (scale x/z)
    vivid::Param<float> floor_h  {"floor",    0.3f, 0.0f, 5.0f};     // min bar height (always visible)
    vivid::Param<float> attack   {"attack",   0.02f, 0.0f, 1.0f};    // rise time constant (s)
    vivid::Param<float> release  {"release",  0.18f, 0.0f, 2.0f};    // fall time constant (s)
    vivid::Param<int>   palette  {"palette",  0, {"Spectrum", "Warm", "Cool", "Mono"}};
    vivid::Param<float> arc_span {"arc_span", 3.14159f, 0.2f, 6.2832f};  // Arc sweep (radians)

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(bars,      "Layout");
        vivid::param_group(layout,    "Layout");
        vivid::param_group(width,     "Layout");
        vivid::param_group(arc_span,  "Layout");
        vivid::param_group(thickness, "Layout");
        vivid::param_group(height,    "Response");
        vivid::param_group(gain,      "Response");
        vivid::param_group(tilt,      "Response");
        vivid::param_group(floor_h,   "Response");
        vivid::param_group(attack,    "Response");
        vivid::param_group(release,   "Response");
        vivid::param_group(palette,   "Color");
        out.push_back(&bars);      out.push_back(&layout);  out.push_back(&width);
        out.push_back(&arc_span);  out.push_back(&thickness);
        out.push_back(&height);    out.push_back(&gain);    out.push_back(&tilt);
        out.push_back(&floor_h);
        out.push_back(&attack);    out.push_back(&release); out.push_back(&palette);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back(VIVID_CUSTOM_REF_PORT("instances", VIVID_PORT_OUTPUT,
                                            vivid::gpu::InstanceArray3D));
    }

    void draw_thumbnail(const VividThumbnailContext*) override {}

    void process_gpu(const VividGpuContext* ctx) override {
        uint32_t n = static_cast<uint32_t>(std::clamp(bars.int_value(), 1, VIVID_SPECTRUM_MAX_BANDS));

        // Pull the live spectrum; fewer published bands than bars → resample by index.
        float raw[VIVID_SPECTRUM_MAX_BANDS] = {0.f};
        const uint32_t got = vivid_master_spectrum(raw, VIVID_SPECTRUM_MAX_BANDS);

        // Per-bar temporal smoothing (fast attack / slow release envelope follower).
        if (smoothed_.size() != n) { smoothed_.assign(n, 0.f); }
        const float dt = std::max(0.f, static_cast<float>(ctx->delta_time));
        const float atk = attack.value, rel = release.value, g = gain.value;
        instances_.resize(n);

        for (uint32_t i = 0; i < n; ++i) {
            // Map bar i → a source band (linear-interpolated so bar count is independent of band
            // count), with a rising high-frequency tilt so the naturally-quieter highs still register.
            const float frac0 = (n > 1) ? static_cast<float>(i) / static_cast<float>(n - 1) : 0.f;
            float target = 0.f;
            if (got > 0) {
                const float fb = frac0 * static_cast<float>(got - 1);
                const uint32_t b0 = static_cast<uint32_t>(fb);
                const uint32_t b1 = std::min(got - 1, b0 + 1);
                const float ft = fb - static_cast<float>(b0);
                const float mag = raw[b0] * (1.f - ft) + raw[b1] * ft;
                const float tiltf = 1.f + tilt.value * frac0;    // 1 at lows → 1+tilt at highs
                target = std::clamp(mag * g * tiltf, 0.f, 1.5f);
            }
            const float tau = (target > smoothed_[i]) ? atk : rel;
            if (tau <= 1e-5f || dt <= 0.f) smoothed_[i] = target;
            else smoothed_[i] += (target - smoothed_[i]) * (1.f - std::exp(-dt / tau));

            const float m   = smoothed_[i];
            const float bh  = floor_h.value + m * height.value;     // bar height
            const float th  = thickness.value;

            auto& d = instances_[i];
            place(d, i, n, frac0, bh);
            d.scale[0] = th; d.scale[1] = bh; d.scale[2] = th;
            palette_color(d.color, frac0, m);
        }

        bundle_.data  = instances_.data();
        bundle_.count = n;
        ctx->custom_outputs[0] = &bundle_;

        vivid::thumb3d::render_instances_cpu(ctx, thumb_, instances_.data(), n);
    }

    ~AudioSpectrum3D() override { vivid::thumb3d::destroy(thumb_); }

private:
    // Position bar i (grows UP from the y=0 floor; cube instances scale about their centre, so the
    // centre sits at bh/2 to keep the base planted).
    void place(vivid::gpu::InstanceData3D& d, uint32_t i, uint32_t n, float frac, float bh) {
        d.rotation_x = 0.f; d.rotation_y = 0.f;
        const int lay = layout.int_value();
        if (lay == 1) {                          // Arc: sweep across arc_span, radius = width
            const float a = (frac - 0.5f) * arc_span.value;
            const float r = width.value;
            d.position[0] = std::sin(a) * r;
            d.position[1] = bh * 0.5f;
            d.position[2] = -std::cos(a) * r + r;   // bow toward the camera
            d.rotation_y = a;
        } else if (lay == 2) {                   // Circle: full ring, bars point up
            const float a = 2.f * 3.14159265f * static_cast<float>(i) / static_cast<float>(n);
            const float r = width.value;
            d.position[0] = std::cos(a) * r;
            d.position[1] = bh * 0.5f;
            d.position[2] = std::sin(a) * r;
            d.rotation_y = -a;
        } else {                                 // Line: centred along x
            d.position[0] = (frac - 0.5f) * width.value;
            d.position[1] = bh * 0.5f;
            d.position[2] = 0.f;
        }
    }

    // Per-bar colour. Spectrum = iq cosine palette across the band index, brightened by magnitude.
    void palette_color(float c[4], float frac, float m) {
        const float glow = 0.35f + 0.9f * std::clamp(m, 0.f, 1.f);   // hot bars glow
        switch (palette.int_value()) {
            case 1: { // Warm: deep red → orange → yellow-white
                c[0] = 1.0f; c[1] = 0.25f + 0.6f * frac; c[2] = 0.1f + 0.3f * frac; break;
            }
            case 2: { // Cool: indigo → cyan → white
                c[0] = 0.2f + 0.5f * frac; c[1] = 0.4f + 0.55f * frac; c[2] = 1.0f; break;
            }
            case 3: { // Mono: white
                c[0] = c[1] = c[2] = 1.0f; break;
            }
            default: { // Spectrum: iq cosine palette a + b*cos(2π(c*t+d))
                const float t = frac;
                c[0] = 0.5f + 0.5f * std::cos(6.28318f * (1.0f * t + 0.00f));
                c[1] = 0.5f + 0.5f * std::cos(6.28318f * (1.0f * t + 0.33f));
                c[2] = 0.5f + 0.5f * std::cos(6.28318f * (1.0f * t + 0.67f));
                break;
            }
        }
        c[0] *= glow; c[1] *= glow; c[2] *= glow; c[3] = 1.0f;
    }

    std::vector<vivid::gpu::InstanceData3D> instances_;
    std::vector<float> smoothed_;
    vivid::gpu::InstanceArray3D bundle_{};
    vivid::thumb3d::State thumb_{};
};

VIVID_REGISTER(AudioSpectrum3D)
VIVID_THUMBNAIL(AudioSpectrum3D)

VIVID_DESCRIBE_REF_TYPE(vivid::gpu::InstanceArray3D)
