// Unit tests for the Colormap control operator (Phase 3 of operator-gaps plan).
//
// Covers:
//   - descriptor shape (9 palette choices, visible_when wiring)
//   - endpoint (t=0, t=1) RGB for each built-in palette
//   - value clamp: t < 0 matches t = 0, t > 1 matches t = 1
//   - reverse flips endpoints
//   - custom palette: valid input interpolates; malformed input falls back
//   - rainbow is cyclic (t=0 == t=1 at pure red)
//   - grayscale is (t, t, t)
//
// Partition 10: pure CPU, no GPU/audio/window.

#include "runtime/operators/operator_loader.h"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>
#include "test_helpers.h"

namespace {

// Indices from palettes.h. Duplicated here rather than including the header
// because this test must link only against the public operator API.
constexpr int kIxViridis   = 0;
constexpr int kIxMagma     = 1;
constexpr int kIxInferno   = 2;
constexpr int kIxPlasma    = 3;
constexpr int kIxTurbo     = 4;
constexpr int kIxTwilight  = 5;
constexpr int kIxRainbow   = 6;
constexpr int kIxGrayscale = 7;
constexpr int kIxCustom    = 8;

struct ColormapHarness {
    vivid::OperatorLoader loader;
    void* instance = nullptr;
    float params[2]  = {0.0f, 0.0f};              // palette, reverse (custom_stops is TEXT)
    float input[1]   = {0.0f};
    float outputs[3] = {0.0f, 0.0f, 0.0f};
    std::string custom_stops = "1,0,0; 0,1,0; 0,0,1";

    bool load(const std::string& path) {
        if (!loader.load(path.c_str())) return false;
        instance = loader.create_instance();
        return instance != nullptr;
    }

    ~ColormapHarness() {
        if (instance) loader.destroy_instance(instance);
    }

    void run(int palette, float t, bool reverse = false) {
        params[0] = static_cast<float>(palette);
        params[1] = reverse ? 1.0f : 0.0f;
        input[0] = t;
        outputs[0] = outputs[1] = outputs[2] = 0.0f;

        const char* text_bufs[1] = {custom_stops.c_str()};

        VividFrameContext ctx{};
        ctx.time = 0.0; ctx.delta_time = 1.0 / 60.0; ctx.frame = 0;
        ctx.param_values      = params;
        ctx.input_values      = input;
        ctx.output_values     = outputs;
        ctx.file_param_values = text_bufs;
        ctx.file_param_count  = 1;

        loader.process_frame(instance, &ctx);
    }
};

void test_descriptor(const vivid::OperatorLoader& loader) {
    std::fprintf(stderr, "\n--- Descriptor ---\n");
    const auto* desc = loader.descriptor();
    check(desc != nullptr, "descriptor not null");
    if (!desc) return;

    check(std::strcmp(desc->name, "Colormap") == 0, "name is Colormap");
    check(desc->param_count == 3, "three params");
    check(desc->port_count  == 4, "four ports (value in, r/g/b out)");

    // Ports in order: value (in), r, g, b (out)
    if (desc->port_count >= 4) {
        check(std::strcmp(desc->ports[0].name, "value") == 0, "port 0 = value (in)");
        check(desc->ports[0].direction == VIVID_PORT_INPUT, "value is INPUT");
        check(std::strcmp(desc->ports[1].name, "r") == 0, "port 1 = r");
        check(std::strcmp(desc->ports[2].name, "g") == 0, "port 2 = g");
        check(std::strcmp(desc->ports[3].name, "b") == 0, "port 3 = b");
    }

    // Params: palette (enum 9), reverse (enum 2), custom_stops (TEXT)
    check(desc->params[0].choice_count == 9, "palette has 9 choices");
    if (desc->params[0].choice_count >= 9) {
        const char* const* c = desc->params[0].choice_labels;
        check(std::strcmp(c[0], "viridis")   == 0, "choice 0 = viridis");
        check(std::strcmp(c[4], "turbo")     == 0, "choice 4 = turbo");
        check(std::strcmp(c[6], "rainbow")   == 0, "choice 6 = rainbow");
        check(std::strcmp(c[7], "grayscale") == 0, "choice 7 = grayscale");
        check(std::strcmp(c[8], "custom")    == 0, "choice 8 = custom");
    }
    check(desc->params[1].choice_count == 2, "reverse has 2 choices");
    check(desc->params[2].type == VIVID_PARAM_TEXT, "custom_stops is TEXT");

    // visible_when on custom_stops: param=palette, op=EQ, value=kIxCustom (8)
    check(desc->params[2].visible_when_param != nullptr
          && std::strcmp(desc->params[2].visible_when_param, "palette") == 0,
          "custom_stops.visible_when_param = palette");
    check(desc->params[2].visible_when_op == VIVID_PARAM_VIS_EQ,
          "custom_stops.visible_when_op = EQ");
    check(desc->params[2].visible_when_value_count == 1,
          "custom_stops has one visibility value");
    if (desc->params[2].visible_when_value_count >= 1) {
        check(desc->params[2].visible_when_values[0] == kIxCustom,
              "custom_stops visible when palette == kIxCustom (8)");
    }
}

// Each entry: (palette index, start RGB, end RGB). Tolerance is loose (0.02)
// because the in-source stops are themselves rounded to three decimals.
struct PaletteReference {
    int palette;
    const char* name;
    float start[3];
    float end[3];
};

constexpr PaletteReference kRefs[] = {
    {kIxViridis,   "viridis",   {0.267f, 0.004f, 0.329f}, {0.992f, 0.906f, 0.145f}},
    {kIxMagma,     "magma",     {0.000f, 0.000f, 0.016f}, {0.988f, 0.992f, 0.749f}},
    {kIxInferno,   "inferno",   {0.000f, 0.000f, 0.016f}, {0.988f, 1.000f, 0.643f}},
    {kIxPlasma,    "plasma",    {0.051f, 0.031f, 0.529f}, {0.941f, 0.976f, 0.129f}},
    {kIxTurbo,     "turbo",     {0.188f, 0.071f, 0.231f}, {0.478f, 0.016f, 0.008f}},
    {kIxTwilight,  "twilight",  {0.886f, 0.851f, 0.886f}, {0.886f, 0.851f, 0.886f}},
    {kIxRainbow,   "rainbow",   {1.000f, 0.000f, 0.000f}, {1.000f, 0.000f, 0.000f}},
    {kIxGrayscale, "grayscale", {0.000f, 0.000f, 0.000f}, {1.000f, 1.000f, 1.000f}},
};

void test_builtin_endpoints(ColormapHarness& h) {
    std::fprintf(stderr, "\n--- Built-in palette endpoints ---\n");
    for (const auto& ref : kRefs) {
        char msg[128];
        h.run(ref.palette, 0.0f);
        std::snprintf(msg, sizeof(msg), "%s t=0 → start R", ref.name);
        check_float(h.outputs[0], ref.start[0], 0.02f, msg);
        std::snprintf(msg, sizeof(msg), "%s t=0 → start G", ref.name);
        check_float(h.outputs[1], ref.start[1], 0.02f, msg);
        std::snprintf(msg, sizeof(msg), "%s t=0 → start B", ref.name);
        check_float(h.outputs[2], ref.start[2], 0.02f, msg);

        h.run(ref.palette, 1.0f);
        std::snprintf(msg, sizeof(msg), "%s t=1 → end R", ref.name);
        check_float(h.outputs[0], ref.end[0], 0.02f, msg);
        std::snprintf(msg, sizeof(msg), "%s t=1 → end G", ref.name);
        check_float(h.outputs[1], ref.end[1], 0.02f, msg);
        std::snprintf(msg, sizeof(msg), "%s t=1 → end B", ref.name);
        check_float(h.outputs[2], ref.end[2], 0.02f, msg);
    }
}

void test_midpoints_smoke(ColormapHarness& h) {
    std::fprintf(stderr, "\n--- Midpoint smoke (not grey except grayscale) ---\n");
    // For perceptual palettes the midpoint is colorful, not neutral grey.
    // We check that NOT all three channels are nearly equal — i.e. hue is real.
    const int perceptual[] = {kIxViridis, kIxMagma, kIxInferno, kIxPlasma,
                              kIxTurbo, kIxRainbow};
    for (int p : perceptual) {
        h.run(p, 0.5f);
        float r = h.outputs[0], g = h.outputs[1], b = h.outputs[2];
        float min_v = std::min({r, g, b});
        float max_v = std::max({r, g, b});
        char msg[96];
        std::snprintf(msg, sizeof(msg), "palette %d midpoint has real hue (range > 0.1)", p);
        check(max_v - min_v > 0.1f, msg);
    }
    // Grayscale midpoint should be (0.5, 0.5, 0.5)
    h.run(kIxGrayscale, 0.5f);
    check_float(h.outputs[0], 0.5f, 0.01f, "grayscale midpoint R = 0.5");
    check_float(h.outputs[1], 0.5f, 0.01f, "grayscale midpoint G = 0.5");
    check_float(h.outputs[2], 0.5f, 0.01f, "grayscale midpoint B = 0.5");
}

void test_clamp(ColormapHarness& h) {
    std::fprintf(stderr, "\n--- Input clamp ---\n");
    // Out-of-range values must match the closest in-range value.
    h.run(kIxViridis, 0.0f);
    float r0 = h.outputs[0], g0 = h.outputs[1], b0 = h.outputs[2];
    h.run(kIxViridis, -0.5f);
    check_float(h.outputs[0], r0, 0.002f, "t=-0.5 R matches t=0");
    check_float(h.outputs[1], g0, 0.002f, "t=-0.5 G matches t=0");
    check_float(h.outputs[2], b0, 0.002f, "t=-0.5 B matches t=0");

    h.run(kIxViridis, 1.0f);
    float r1 = h.outputs[0], g1 = h.outputs[1], b1 = h.outputs[2];
    h.run(kIxViridis, 1.5f);
    check_float(h.outputs[0], r1, 0.002f, "t=1.5 R matches t=1");
    check_float(h.outputs[1], g1, 0.002f, "t=1.5 G matches t=1");
    check_float(h.outputs[2], b1, 0.002f, "t=1.5 B matches t=1");
}

void test_reverse(ColormapHarness& h) {
    std::fprintf(stderr, "\n--- Reverse ---\n");
    // With reverse=1, t=0 should equal forward t=1 and vice versa.
    h.run(kIxViridis, 1.0f, /*reverse=*/false);
    float fr = h.outputs[0], fg = h.outputs[1], fb = h.outputs[2];
    h.run(kIxViridis, 0.0f, /*reverse=*/true);
    check_float(h.outputs[0], fr, 0.002f, "reverse: t=0 reversed matches t=1 forward (R)");
    check_float(h.outputs[1], fg, 0.002f, "reverse: t=0 reversed matches t=1 forward (G)");
    check_float(h.outputs[2], fb, 0.002f, "reverse: t=0 reversed matches t=1 forward (B)");
}

void test_custom(ColormapHarness& h) {
    std::fprintf(stderr, "\n--- Custom palette ---\n");
    // Red → Blue
    h.custom_stops = "1,0,0; 0,0,1";
    h.run(kIxCustom, 0.0f);
    check_float(h.outputs[0], 1.0f, 0.002f, "custom start R");
    check_float(h.outputs[1], 0.0f, 0.002f, "custom start G");
    check_float(h.outputs[2], 0.0f, 0.002f, "custom start B");
    h.run(kIxCustom, 1.0f);
    check_float(h.outputs[0], 0.0f, 0.002f, "custom end R");
    check_float(h.outputs[1], 0.0f, 0.002f, "custom end G");
    check_float(h.outputs[2], 1.0f, 0.002f, "custom end B");
    h.run(kIxCustom, 0.5f);
    check_float(h.outputs[0], 0.5f, 0.002f, "custom mid R = 0.5");
    check_float(h.outputs[1], 0.0f, 0.002f, "custom mid G = 0");
    check_float(h.outputs[2], 0.5f, 0.002f, "custom mid B = 0.5");

    // Single stop → constant
    h.custom_stops = "0.3,0.6,0.9";
    h.run(kIxCustom, 0.0f);
    check_float(h.outputs[0], 0.3f, 0.002f, "single-stop R anywhere");
    h.run(kIxCustom, 0.7f);
    check_float(h.outputs[1], 0.6f, 0.002f, "single-stop G anywhere");
    h.run(kIxCustom, 1.0f);
    check_float(h.outputs[2], 0.9f, 0.002f, "single-stop B anywhere");

    // Malformed input: falls back to the default tri-stop rainbow
    // (1,0,0; 0,1,0; 0,0,1), so t=0 → red, t=0.5 → green, t=1 → blue.
    h.custom_stops = "garbage not a palette";
    h.run(kIxCustom, 0.0f);
    check_float(h.outputs[0], 1.0f, 0.002f, "malformed → fallback t=0 R=1");
    check_float(h.outputs[1], 0.0f, 0.002f, "malformed → fallback t=0 G=0");
    h.run(kIxCustom, 0.5f);
    check_float(h.outputs[1], 1.0f, 0.002f, "malformed → fallback t=0.5 G=1");
    h.run(kIxCustom, 1.0f);
    check_float(h.outputs[2], 1.0f, 0.002f, "malformed → fallback t=1 B=1");
}

void test_reparse_after_change(ColormapHarness& h) {
    std::fprintf(stderr, "\n--- Reparse after string change ---\n");
    // First string: red at t=0.
    h.custom_stops = "1,0,0; 0,1,0";
    h.run(kIxCustom, 0.0f);
    check_float(h.outputs[0], 1.0f, 0.002f, "first string t=0 R=1");

    // Change string: now t=0 should be blue, not red, on the very next process.
    h.custom_stops = "0,0,1; 1,1,0";
    h.run(kIxCustom, 0.0f);
    check_float(h.outputs[2], 1.0f, 0.002f, "after string change t=0 B=1");
    check_float(h.outputs[0], 0.0f, 0.002f, "after string change t=0 R=0");
}

} // namespace

int main(int argc, char** argv) {
    const std::string build_dir = (argc > 1) ? argv[1] : ".";
    const std::string path = build_dir + "/colormap.dylib";

    if (!std::filesystem::exists(path)) {
        std::fprintf(stderr, "FATAL: %s not found (build colormap.dylib first)\n", path.c_str());
        return 1;
    }

    std::fprintf(stderr, "=== Test: Colormap operator ===\n");

    ColormapHarness h;
    if (!h.load(path)) {
        std::fprintf(stderr, "FATAL: could not load %s\n", path.c_str());
        return 1;
    }

    test_descriptor(h.loader);
    test_builtin_endpoints(h);
    test_midpoints_smoke(h);
    test_clamp(h);
    test_reverse(h);
    test_custom(h);
    test_reparse_after_change(h);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
