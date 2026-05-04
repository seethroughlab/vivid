#include "operator_api/wgsl_filter.h"

/**
 * @brief Solid color stripes from a 6-slot palette, horizontal or vertical.
 *
 * Source operator (no inputs) that fills the canvas with N evenly-spaced
 * bands using the first N entries of an embedded 6-color palette. Choose
 * horizontal (stripes vary by Y) or vertical (vary by X). Optional
 * scroll_speed scrolls the bands along their axis. Softness adds a
 * crossfade between adjacent bands at their boundary.
 *
 * Default palette is a cool magenta / cyan / violet / white set that
 * pairs well with Mirror + Bloom for projected wall-stripe aesthetics.
 *
 * Reference implementation for `WgslFilterBase` — see
 * `color_bands.wgsl` for the fragment shader; the C++ side just declares
 * params and the base class auto-generates uniforms / pipeline / bind
 * groups / hot-reload. The collect_ports override removes the inherited
 * input texture (this is a source operator, not a filter).
 *
 * @see Mirror, Scanlines, Bloom
 */
struct ColorBands : vivid::WgslFilterBase {
    static constexpr const char* kName = "ColorBands";
    static constexpr bool kTimeDependent = true;

    vivid::Param<int>   band_count   {"band_count",   5, 2, 6};
    vivid::Param<int>   orientation  {"orientation",  0, {"horizontal","vertical"}};
    vivid::Param<float> scroll_speed {"scroll_speed", 0.0f, -2.0f, 2.0f};
    vivid::Param<float> softness     {"softness",     0.0f, 0.0f, 1.0f};

    // 6-slot color palette. Defaults: cool magenta / cyan / violet / white.
    vivid::Param<float> c0_r{"c0_r", 0.05f, 0.0f, 1.0f};
    vivid::Param<float> c0_g{"c0_g", 0.05f, 0.0f, 1.0f};
    vivid::Param<float> c0_b{"c0_b", 0.18f, 0.0f, 1.0f};
    vivid::Param<float> c1_r{"c1_r", 0.45f, 0.0f, 1.0f};
    vivid::Param<float> c1_g{"c1_g", 0.10f, 0.0f, 1.0f};
    vivid::Param<float> c1_b{"c1_b", 0.55f, 0.0f, 1.0f};
    vivid::Param<float> c2_r{"c2_r", 0.95f, 0.0f, 1.0f};
    vivid::Param<float> c2_g{"c2_g", 0.20f, 0.0f, 1.0f};
    vivid::Param<float> c2_b{"c2_b", 0.75f, 0.0f, 1.0f};
    vivid::Param<float> c3_r{"c3_r", 0.75f, 0.0f, 1.0f};
    vivid::Param<float> c3_g{"c3_g", 0.55f, 0.0f, 1.0f};
    vivid::Param<float> c3_b{"c3_b", 0.95f, 0.0f, 1.0f};
    vivid::Param<float> c4_r{"c4_r", 0.30f, 0.0f, 1.0f};
    vivid::Param<float> c4_g{"c4_g", 0.85f, 0.0f, 1.0f};
    vivid::Param<float> c4_b{"c4_b", 0.95f, 0.0f, 1.0f};
    vivid::Param<float> c5_r{"c5_r", 0.95f, 0.0f, 1.0f};
    vivid::Param<float> c5_g{"c5_g", 0.95f, 0.0f, 1.0f};
    vivid::Param<float> c5_b{"c5_b", 1.00f, 0.0f, 1.0f};

    ColorBands() : WgslFilterBase("color_bands.wgsl") {
        vivid::description(band_count,
            "Number of stripes across the canvas (uses palette colors 0..N-1)");
        vivid::description(orientation,
            "Stripe direction: horizontal stripes vary by Y, vertical by X");
        vivid::description(scroll_speed,
            "Scroll speed along the band axis (negative reverses)");
        vivid::semantic_tag(scroll_speed, "frequency_hz");
        vivid::semantic_unit(scroll_speed, "Hz");
        vivid::description(softness,
            "Edge crossfade between adjacent bands (0 = hard, 1 = smooth)");

        for (auto* p : {&c0_r,&c0_g,&c0_b,&c1_r,&c1_g,&c1_b,
                        &c2_r,&c2_g,&c2_b,&c3_r,&c3_g,&c3_b,
                        &c4_r,&c4_g,&c4_b,&c5_r,&c5_g,&c5_b})
            vivid::semantic_shape(*p, "scalar");
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&band_count);
        out.push_back(&orientation);
        out.push_back(&scroll_speed);
        out.push_back(&softness);
        out.push_back(&c0_r); out.push_back(&c0_g); out.push_back(&c0_b);
        out.push_back(&c1_r); out.push_back(&c1_g); out.push_back(&c1_b);
        out.push_back(&c2_r); out.push_back(&c2_g); out.push_back(&c2_b);
        out.push_back(&c3_r); out.push_back(&c3_g); out.push_back(&c3_b);
        out.push_back(&c4_r); out.push_back(&c4_g); out.push_back(&c4_b);
        out.push_back(&c5_r); out.push_back(&c5_g); out.push_back(&c5_b);
    }

    // Override the inherited "input texture" port — this is a source
    // operator, not a filter; only the output texture port is needed.
    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"texture", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }
};

VIVID_DEFINE_OP(ColorBands) {
}

VIVID_REGISTER(ColorBands)
