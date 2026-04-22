#pragma once
// Shared helpers for the Euclidean operator: the Bjorklund pattern
// generator (previously duplicated between euclidean_core.h's private
// method and euclidean.cpp's thumbnail copy) and a list of density
// presets used by the editor's "D" quick-cycle shortcut.
//
// Header-only would work but the algorithm is ~60 lines and gets
// compiled into three call sites — .cpp keeps the binary compact.

namespace vivid::euclidean_editor {

inline constexpr int kMaxSteps = 32;

// Compute a Bjorklund-distributed Euclidean pattern. out_pattern must
// be at least kMaxSteps ints long; cells past `steps` are zeroed.
// Pattern of all zeros when `hits <= 0`; all ones when `hits >= steps`.
// `rotation` rotates the pattern left by N positions (modulo steps).
void compute_pattern(int hits, int steps, int rotation, int* out_pattern);

// Density presets cycled by the editor's D key and exposed in the
// side panel. Paired (hits, steps) with a short display label.
struct DensityPreset {
    int hits;
    int steps;
    const char* label;
};

inline constexpr DensityPreset kDensityPresets[] = {
    {3,  8,  "3/8  tresillo"},
    {5,  8,  "5/8  cinquillo"},
    {2,  5,  "2/5"},
    {3,  7,  "3/7"},
    {4,  9,  "4/9"},
    {3, 16,  "3/16"},
    {5, 16,  "5/16"},
    {7, 16,  "7/16"},
    {5, 12,  "5/12"},
    {7, 12,  "7/12"},
};
inline constexpr int kDensityPresetCount =
    sizeof(kDensityPresets) / sizeof(kDensityPresets[0]);

} // namespace vivid::euclidean_editor
