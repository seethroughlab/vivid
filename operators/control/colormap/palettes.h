#pragma once

#include <array>
#include <cstddef>

namespace vivid::colormap {

// 9-stop palette tables. Stops are assumed to be uniformly spaced (position
// i/(kStopCount-1) for stop i). The Colormap operator samples these with
// piecewise-linear interpolation.
//
// Rationale for 9 stops: the operator outputs three scalar values, not a
// texture — a downstream consumer sees one (r, g, b) triple per frame, so
// perceptual smoothness across the whole palette is irrelevant. What matters
// is that the sampled color is close to the "correct" matplotlib value at
// any given t, and that sweeping t produces smooth color motion. 9 stops
// with linear interp meets both goals, and is small enough to audit by hand.
// The thumbnail (64+ samples across a gradient bar) still renders smoothly
// because the interpolation is continuous.
//
// Source: values downsampled from matplotlib's canonical colormaps
// (matplotlib/lib/matplotlib/_cm_listed.py), MIT-licensed. Picked at indices
// 0, 32, 64, 96, 128, 160, 192, 224, 255 of matplotlib's 256-entry LUTs.
// Endpoint and midpoint values are referenced by tests/ops/test_colormap_op.

constexpr size_t kStopCount = 9;
using Stops = std::array<std::array<float, 3>, kStopCount>;

constexpr Stops kViridis = {{
    {0.267f, 0.004f, 0.329f},  // 0/8  deep purple
    {0.278f, 0.165f, 0.478f},  // 1/8
    {0.243f, 0.290f, 0.537f},  // 2/8
    {0.192f, 0.408f, 0.557f},  // 3/8
    {0.153f, 0.498f, 0.557f},  // 4/8  teal
    {0.122f, 0.588f, 0.545f},  // 5/8
    {0.231f, 0.682f, 0.443f},  // 6/8
    {0.518f, 0.776f, 0.247f},  // 7/8
    {0.992f, 0.906f, 0.145f},  // 8/8  yellow
}};

constexpr Stops kMagma = {{
    {0.000f, 0.000f, 0.016f},  // 0/8  near-black
    {0.067f, 0.051f, 0.224f},
    {0.220f, 0.055f, 0.420f},
    {0.392f, 0.090f, 0.502f},
    {0.557f, 0.145f, 0.506f},  // 4/8  magenta
    {0.733f, 0.216f, 0.471f},
    {0.898f, 0.325f, 0.365f},
    {0.984f, 0.561f, 0.286f},
    {0.988f, 0.992f, 0.749f},  // 8/8  pale yellow
}};

constexpr Stops kInferno = {{
    {0.000f, 0.000f, 0.016f},  // 0/8  black
    {0.078f, 0.043f, 0.208f},
    {0.259f, 0.039f, 0.408f},
    {0.416f, 0.090f, 0.431f},
    {0.576f, 0.149f, 0.404f},  // 4/8  plum
    {0.729f, 0.212f, 0.333f},
    {0.867f, 0.318f, 0.227f},
    {0.969f, 0.537f, 0.192f},
    {0.988f, 1.000f, 0.643f},  // 8/8  pale yellow
}};

constexpr Stops kPlasma = {{
    {0.051f, 0.031f, 0.529f},  // 0/8  blue-violet
    {0.251f, 0.012f, 0.612f},
    {0.416f, 0.000f, 0.655f},
    {0.561f, 0.051f, 0.643f},
    {0.690f, 0.165f, 0.561f},  // 4/8  magenta
    {0.800f, 0.278f, 0.471f},
    {0.882f, 0.392f, 0.384f},
    {0.965f, 0.580f, 0.216f},
    {0.941f, 0.976f, 0.129f},  // 8/8  yellow
}};

constexpr Stops kTurbo = {{
    {0.188f, 0.071f, 0.231f},  // 0/8  dark violet
    {0.282f, 0.322f, 0.745f},
    {0.251f, 0.608f, 0.925f},
    {0.141f, 0.855f, 0.788f},
    {0.420f, 0.984f, 0.455f},  // 4/8  green
    {0.788f, 0.910f, 0.161f},
    {0.992f, 0.714f, 0.141f},
    {0.945f, 0.373f, 0.106f},
    {0.478f, 0.016f, 0.008f},  // 8/8  dark red
}};

// Twilight is cyclic — start and end stops match so sweeping t = 0..1 returns
// to the origin color.
constexpr Stops kTwilight = {{
    {0.886f, 0.851f, 0.886f},  // 0/8  pale lavender
    {0.741f, 0.753f, 0.910f},
    {0.529f, 0.596f, 0.839f},
    {0.322f, 0.388f, 0.647f},
    {0.169f, 0.180f, 0.373f},  // 4/8  midnight
    {0.263f, 0.153f, 0.173f},
    {0.478f, 0.263f, 0.267f},
    {0.761f, 0.522f, 0.451f},
    {0.886f, 0.851f, 0.886f},  // 8/8  back to pale lavender
}};

// Indexed by palette enum order (viridis first). Indices that correspond to
// "rainbow" and "grayscale" and "custom" are nullptr — the operator handles
// those code paths separately.
enum PaletteIndex : int {
    kIxViridis   = 0,
    kIxMagma     = 1,
    kIxInferno   = 2,
    kIxPlasma    = 3,
    kIxTurbo     = 4,
    kIxTwilight  = 5,
    kIxRainbow   = 6,   // HSV sweep, generated in colormap.cpp
    kIxGrayscale = 7,   // t → (t, t, t), generated in colormap.cpp
    kIxCustom    = 8,   // user-supplied stops string
    kPaletteCount = 9,
};

constexpr const Stops* kBuiltinPalettes[] = {
    &kViridis,
    &kMagma,
    &kInferno,
    &kPlasma,
    &kTurbo,
    &kTwilight,
    nullptr,   // rainbow: computed
    nullptr,   // grayscale: computed
    nullptr,   // custom: from Param<TextValue>
};

} // namespace vivid::colormap
