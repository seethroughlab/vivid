#include "operator_api/operator.h"
#include "operator_api/thumbnail.h"
#include "palettes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace vivid::colormap;

namespace {

// Piecewise-linear sample across a vector of stops treated as uniformly spaced
// between t=0 and t=1. Works for both the built-in kStopCount arrays and the
// variable-length custom-stops vector.
inline std::array<float, 3> sample_linear(const std::array<float, 3>* stops,
                                          size_t count, float t) {
    if (count == 0) return {0.0f, 0.0f, 0.0f};
    if (count == 1) return stops[0];
    if (t <= 0.0f) return stops[0];
    if (t >= 1.0f) return stops[count - 1];

    const float scaled  = t * static_cast<float>(count - 1);
    const size_t lo     = static_cast<size_t>(std::floor(scaled));
    const size_t hi     = std::min(lo + 1, count - 1);
    const float frac    = scaled - static_cast<float>(lo);

    const auto& a = stops[lo];
    const auto& b = stops[hi];
    return {
        a[0] + (b[0] - a[0]) * frac,
        a[1] + (b[1] - a[1]) * frac,
        a[2] + (b[2] - a[2]) * frac,
    };
}

inline std::array<float, 3> sample_builtin(const Stops& s, float t) {
    return sample_linear(s.data(), s.size(), t);
}

// Rainbow: full hue sweep 0..360° at max saturation and value. Generated
// instead of stored so t=0 and t=1 both render pure red (closed cycle).
inline std::array<float, 3> sample_rainbow(float t) {
    const float h = std::clamp(t, 0.0f, 1.0f) * 6.0f;   // 0..6
    const float c = 1.0f;                                // chroma
    const float x = c * (1.0f - std::fabs(std::fmod(h, 2.0f) - 1.0f));
    float r = 0.0f, g = 0.0f, b = 0.0f;
    if      (h < 1.0f) { r = c; g = x; }
    else if (h < 2.0f) { r = x; g = c; }
    else if (h < 3.0f) {        g = c; b = x; }
    else if (h < 4.0f) {        g = x; b = c; }
    else if (h < 5.0f) { r = x;        b = c; }
    else                { r = c;        b = x; }
    return {r, g, b};
}

inline std::array<float, 3> sample_grayscale(float t) {
    const float v = std::clamp(t, 0.0f, 1.0f);
    return {v, v, v};
}

// Parse `"r,g,b; r,g,b; ..."` into a vector of RGB triples. Whitespace is
// tolerated. Returns an empty vector on malformed input (caller falls back).
std::vector<std::array<float, 3>> parse_custom_stops(const std::string& src) {
    std::vector<std::array<float, 3>> out;
    const char* p = src.c_str();
    while (*p) {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == ';') ++p;
        if (!*p) break;
        float r = 0.0f, g = 0.0f, b = 0.0f;
        int consumed = 0;
        int matched = std::sscanf(p, " %f , %f , %f%n", &r, &g, &b, &consumed);
        if (matched != 3) { out.clear(); return out; }   // bail on first bad triple
        out.push_back({std::clamp(r, 0.0f, 1.0f),
                       std::clamp(g, 0.0f, 1.0f),
                       std::clamp(b, 0.0f, 1.0f)});
        p += consumed;
        while (*p == ' ' || *p == '\t' || *p == '\n') ++p;
        if (*p == ';') ++p;
        else if (*p != '\0') { out.clear(); return out; } // trailing garbage
    }
    return out;
}

constexpr const char* kDefaultCustomStops = "1,0,0; 0,1,0; 0,0,1";

} // namespace

/**
 * @brief Scalar-to-RGB lookup through perceptual and utility color palettes.
 *
 * Maps `value` (clamped to [0, 1]) through a built-in palette — viridis,
 * magma, inferno, plasma, turbo, twilight, rainbow, grayscale — or a custom
 * string of "r,g,b; r,g,b; ..." stops. Emits three scalar outputs `r`, `g`,
 * `b` that can drive any numeric color parameter directly.
 *
 * Use this to replace the common anti-pattern of remapping a scalar to r/g/b
 * via three separate linear wires (which produces grey at the midpoint).
 *
 * @tip Drive arp1/note through a remap to [0, 1], then Colormap → Shape.r/g/b
 *      for per-pitch colored hits.
 * @tip Set palette=custom and enter "1,0,0; 0,0,1" for a simple red-blue scale.
 * @see Macro, Math, HSV
 * @output r Red channel in [0, 1].
 * @output g Green channel in [0, 1].
 * @output b Blue channel in [0, 1].
 */
struct Colormap : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName   = "Colormap";
    static constexpr bool kTimeDependent = false;

    vivid::Param<int> palette{"palette", 0,
        {"viridis", "magma", "inferno", "plasma", "turbo",
         "twilight", "rainbow", "grayscale", "custom"}};
    vivid::Param<int> reverse{"reverse", 0, {"off", "on"}};
    vivid::Param<vivid::TextValue> custom_stops{"custom_stops", kDefaultCustomStops};

    Colormap() {
        vivid::description(palette,
            "Built-in perceptual and utility palettes, plus 'custom' for user-supplied stops.");
        vivid::description(reverse,
            "Flip the palette so t=0 samples the end color and t=1 samples the start.");
        vivid::description(custom_stops,
            "When palette=custom, a list of 'r,g,b; r,g,b; ...' triples in [0,1]. "
            "Stops are spaced uniformly across [0,1].");

        // Hide the custom-stops text box unless the user actually selects custom.
        vivid::visible_when_eq(custom_stops, palette, {kIxCustom});
    }

    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&palette);
        out.push_back(&reverse);
        out.push_back(&custom_stops);
    }

    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"value", VIVID_PORT_SCALAR, VIVID_PORT_INPUT});
        out.push_back({"r",     VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"g",     VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
        out.push_back({"b",     VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT});
    }

    std::array<float, 3> sample(int p, float t) {
        if (p == kIxRainbow)   return sample_rainbow(t);
        if (p == kIxGrayscale) return sample_grayscale(t);
        if (p == kIxCustom) {
            maybe_reparse_custom();
            if (custom_cache_.empty()) {
                // Fall back to the default tri-stop so malformed input reads
                // visibly differently from viridis.
                static const std::vector<std::array<float, 3>> kFallback =
                    parse_custom_stops(kDefaultCustomStops);
                return sample_linear(kFallback.data(), kFallback.size(), t);
            }
            return sample_linear(custom_cache_.data(), custom_cache_.size(), t);
        }
        if (p >= 0 && p < kIxRainbow) {
            return sample_builtin(*kBuiltinPalettes[p], t);
        }
        return {0.0f, 0.0f, 0.0f};
    }

    void process_frame(const VividFrameContext* ctx) override {
        float t = std::clamp(ctx->input_values[0], 0.0f, 1.0f);
        if (reverse.int_value()) t = 1.0f - t;

        const auto rgb = sample(palette.int_value(), t);
        ctx->output_values[0] = rgb[0];
        ctx->output_values[1] = rgb[1];
        ctx->output_values[2] = rgb[2];
    }

    void draw_thumbnail(const VividThumbnailContext* ctx) override {
        if (!ctx || !ctx->draw.opaque) return;
        const auto& d = ctx->draw;
        void* o = d.opaque;

        const float w = static_cast<float>(ctx->thumbnail_logical_width
            ? ctx->thumbnail_logical_width  : ctx->thumbnail_width);
        const float h = static_cast<float>(ctx->thumbnail_logical_height
            ? ctx->thumbnail_logical_height : ctx->thumbnail_height);

        d.draw_rect(o, 0, 0, w, h, {0.07f, 0.08f, 0.09f, 0.9f});

        // Gradient bar: sample the current palette across the thumbnail width.
        const int kSamples = 64;
        const float bar_x0 = w * 0.08f;
        const float bar_x1 = w * 0.92f;
        const float bar_y  = h * 0.30f;
        const float bar_h  = h * 0.30f;
        const float strip_w = (bar_x1 - bar_x0) / static_cast<float>(kSamples);
        const int p_int = (ctx->param_count > 0)
            ? std::clamp(static_cast<int>(ctx->param_values[0]), 0, kPaletteCount - 1) : 0;
        const bool reversed = (ctx->param_count > 1)
            && (static_cast<int>(ctx->param_values[1]) != 0);
        for (int i = 0; i < kSamples; ++i) {
            float t = static_cast<float>(i) / static_cast<float>(kSamples - 1);
            if (reversed) t = 1.0f - t;
            const auto rgb = sample(p_int, t);
            d.draw_rect(o, bar_x0 + i * strip_w, bar_y,
                           strip_w + 1.0f, bar_h,
                           {rgb[0], rgb[1], rgb[2], 1.0f});
        }

        // Live indicator: a tick at the current input-driven value.
        if (ctx->output_count >= 3) {
            // Sample R from the live output to back-solve t is hard; instead
            // show where t=output-red would land given the current palette by
            // rendering the output triple as a swatch below the bar.
            VividColor swatch{ctx->output_values[0],
                              ctx->output_values[1],
                              ctx->output_values[2], 1.0f};
            const float sw = h * 0.22f;
            d.draw_rounded_rect(o, w * 0.5f - sw * 0.5f, bar_y + bar_h + h * 0.06f,
                                sw, sw, sw * 0.20f, swatch);
        }

        // Palette name underneath.
        static const char* kNames[] = {"Viridis", "Magma", "Inferno", "Plasma",
                                       "Turbo", "Twilight", "Rainbow", "Grayscale",
                                       "Custom"};
        const char* name = kNames[p_int];
        const float tw = d.text_width(o, name, 0.75f);
        d.draw_text(o, (w - tw) * 0.5f, h - 13.0f, name,
                    {0.55f, 0.60f, 0.65f, 0.85f}, 0.75f);
    }

private:
    // Cached parse of the custom_stops text. Reparsed on string change.
    std::string custom_src_;
    std::vector<std::array<float, 3>> custom_cache_;

    void maybe_reparse_custom() {
        const std::string& live = custom_stops.str_value;
        if (live == custom_src_) return;
        custom_src_ = live;
        custom_cache_ = parse_custom_stops(live);
    }
};

VIVID_DEFINE_OP(Colormap) {
}

VIVID_REGISTER(Colormap)
VIVID_THUMBNAIL(Colormap)
