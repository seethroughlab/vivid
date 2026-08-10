#include "operator_api/operator.h"
#include "operator_api/gpu_operator.h"
#include "operator_api/value_view.h"
#include "operator_api/lane_thumb.h"
#include <array>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

// =============================================================================
// LanePalette — a per-index colour gradient as THREE FLOAT-MANY lanes (r, g, b)
// =============================================================================
//
// Emits `count` colours sampled across a palette, as three separate value lanes (color_r / color_g /
// color_b). Wire them into InstancesFromLanes' colour inputs so a row of instances gets a per-index
// gradient — e.g. a spectrum rainbow across an equaliser's bars. This is the first MULTI-OUTPUT lane op:
// a consumer picks r/g/b via the edge's source-output-port (v.connect(..., src_port=0|1|2)).
//
// Palettes use Inigo Quilez's cosine model  color = a + b*cos(2π(c*t + d))  — cheap, smooth, seamless.

struct LanePalette : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName         = "LanePalette";
    static constexpr VividOperatorRole kRole = VIVID_OP_ROLE_SOURCE;   // ADR-0046
    static constexpr const char* kSummary = "Emits per-lane r/g/b colour, so a band's index picks its hue.";
    static constexpr std::array<const char*, 3> kKeywords = {"lanes", "colour", "palette"};
    static constexpr bool kTimeDependent       = false;
    static constexpr VividMultiplicityBehavior kMultiplicityBehavior = VIVID_MULTIPLICITY_GENERATE;

    vivid::Param<int>   count   {"count",   48, 1, 4096};
    vivid::Param<int>   palette {"palette", 0, {"Spectrum", "Rainbow", "Fire", "Ice", "Viridis"}};
    vivid::Param<float> offset  {"offset",  0.0f, 0.0f, 1.0f};   // shift the gradient along the row
    vivid::Param<float> spread  {"spread",  1.0f, 0.05f, 4.0f};  // how many palette cycles across the row

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        vivid::param_group(count,   "Palette");
        vivid::param_group(palette, "Palette");
        vivid::param_group(offset,  "Palette");
        vivid::param_group(spread,  "Palette");
        out.push_back(&count); out.push_back(&palette); out.push_back(&offset); out.push_back(&spread);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        for (const char* nm : { "color_r", "color_g", "color_b" }) {
            VividPortDescriptor p{};
            p.name = nm; p.type = VIVID_PORT_SCALAR;
            p.direction = VIVID_PORT_OUTPUT; p.multiplicity = VIVID_MULTIPLICITY_MANY;
            p.semantic_shape = "lane_array";
            out.push_back(p);
        }
    }

    void draw_thumbnail(const VividThumbnailContext*) override {}

    void process_gpu(const VividGpuContext* ctx) override {
        const uint32_t n = static_cast<uint32_t>(std::max(1, count.int_value()));
        r_.resize(n); g_.resize(n); b_.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            const float t = ((n > 1) ? static_cast<float>(i) / static_cast<float>(n - 1) : 0.f) * spread.value
                            + offset.value;
            float c[3];
            eval(palette.int_value(), t, c);
            r_[i] = c[0]; g_[i] = c[1]; b_[i] = c[2];
        }
        publish(ctx, 0, r_);
        publish(ctx, 1, g_);
        publish(ctx, 2, b_);
        // Node thumbnail: the palette itself, as a horizontal colour gradient.
        vivid::lanethumb::render_gradient(ctx, thumb_, r_.data(), g_.data(), b_.data(), n);
    }

    ~LanePalette() override { vivid::lanethumb::destroy(thumb_); }

private:
    static void cosine(float t, const float a[3], const float b[3], const float c[3], const float d[3], float out[3]) {
        for (int k = 0; k < 3; ++k)
            out[k] = std::clamp(a[k] + b[k] * std::cos(6.28318531f * (c[k] * t + d[k])), 0.f, 1.f);
    }
    static void eval(int pal, float t, float out[3]) {
        switch (pal) {
            case 1: { // Rainbow (hue sweep)
                const float a[3]={0.5f,0.5f,0.5f}, b[3]={0.5f,0.5f,0.5f}, c[3]={1,1,1}, d[3]={0.0f,0.33f,0.67f};
                cosine(t, a, b, c, d, out); break; }
            case 2: { // Fire: black → red → orange → yellow-white
                const float a[3]={0.5f,0.2f,0.1f}, b[3]={0.5f,0.3f,0.2f}, c[3]={1.0f,1.0f,1.0f}, d[3]={0.0f,0.15f,0.2f};
                cosine(t, a, b, c, d, out); break; }
            case 3: { // Ice: deep blue → cyan → white
                const float a[3]={0.4f,0.5f,0.7f}, b[3]={0.3f,0.4f,0.3f}, c[3]={1.0f,1.0f,0.5f}, d[3]={0.6f,0.5f,0.4f};
                cosine(t, a, b, c, d, out); break; }
            case 4: { // Viridis-ish: purple → teal → green → yellow
                const float a[3]={0.4f,0.5f,0.35f}, b[3]={0.35f,0.45f,0.3f}, c[3]={1.0f,1.0f,1.0f}, d[3]={0.7f,0.5f,0.1f};
                cosine(t, a, b, c, d, out); break; }
            default: { // Spectrum: red (low) → violet (high), like a real light spectrum
                const float a[3]={0.5f,0.5f,0.5f}, b[3]={0.5f,0.5f,0.5f}, c[3]={1.0f,1.0f,1.0f}, d[3]={0.9f,0.6f,0.3f};
                cosine(t, a, b, c, d, out); break; }
        }
    }
    void publish(const VividGpuContext* ctx, int port, const std::vector<float>& v) {
        if (!ctx->value_outputs) return;
        if (float* buf = vivid_value_output_floats(&ctx->value_outputs[port], static_cast<uint32_t>(v.size()))) {
            std::copy(v.begin(), v.end(), buf);
            vivid_value_output_commit(&ctx->value_outputs[port], static_cast<uint32_t>(v.size()));
        }
    }
    std::vector<float> r_, g_, b_;
    vivid::lanethumb::State thumb_{};
};

VIVID_REGISTER(LanePalette)
VIVID_THUMBNAIL(LanePalette)
