// The visual output's format: the aspect/size preset tables and the Fit/Fill/Stretch UV window
// (gpu/output_format.h). Pure math, so it runs headless — and it is exactly the kind of code whose
// two branches silently swap (letterbox vs crop) without anyone noticing on a 16:9 monitor.
#include "gpu/output_format.h"
#include "test_helpers.h"

#include <cmath>

using namespace vivid;

namespace {

// The rect of source actually shown, in UV space, must keep the SOURCE's aspect under Fit/Fill —
// that is the whole point: no distortion. Only Stretch may distort.
float shown_aspect(const BlitFit& f, float dst_a) {
    // The destination quad is dst_a wide per unit high; it samples an f.su x f.sv window of the
    // source. The visible source region therefore has aspect dst_a * (sv / su).
    return dst_a * (f.sv / f.su);
}

void test_size_presets() {
    uint32_t w = 0, h = 0;
    output_size_for(0, 2, w, h);   // 16:9 @ 720
    CHECK(w == 1280 && h == 720);
    output_size_for(0, 3, w, h);   // 16:9 @ 1080
    CHECK(w == 1920 && h == 1080);
    output_size_for(2, 3, w, h);   // 1:1 @ 1080
    CHECK(w == 1080 && h == 1080);
    output_size_for(3, 3, w, h);   // 9:16 @ 1080 -> vertical
    CHECK(h == 1080);
    CHECK(w < h);
    output_size_for(1, 1, w, h);   // 4:3 @ 540
    CHECK(w == 720 && h == 540);
    // Defaults land on 1280x720.
    output_size_for(kDefaultAspect, kDefaultHeight, w, h);
    CHECK(w == 1280 && h == 720);
    // Out-of-range enum indices clamp rather than read off the end of the table.
    output_size_for(-5, 99, w, h);
    CHECK(w > 0 && h > 0);
    // Both dimensions stay even.
    for (int a = 0; a < kNumAspects; ++a)
        for (int s = 0; s < kNumHeights; ++s) {
            output_size_for(a, s, w, h);
            CHECK(w % 2 == 0 && h % 2 == 0);
        }
}

void test_stretch_is_identity() {
    const BlitFit f = blit_fit(16.f / 9.f, 1.f, FitMode::Stretch);
    CHECK(std::fabs(f.su - 1.f) < 1e-5f);
    CHECK(std::fabs(f.sv - 1.f) < 1e-5f);
    CHECK(std::fabs(f.ou) < 1e-5f);
    CHECK(std::fabs(f.ov) < 1e-5f);
}

void test_same_aspect_is_identity() {
    // Source and destination agree -> no bars, no crop, whatever the mode.
    for (int m = 0; m < kNumFits; ++m) {
        const BlitFit f = blit_fit(16.f / 9.f, 16.f / 9.f, static_cast<FitMode>(m));
        CHECK(std::fabs(f.su - 1.f) < 1e-4f);
        CHECK(std::fabs(f.sv - 1.f) < 1e-4f);
    }
}

void test_fit_letterboxes_and_preserves_aspect() {
    // A 16:9 output shown in a SQUARE surface: Fit must show all of it (window grows past the
    // source on the short axis -> bars), and must not distort.
    const float src = 16.f / 9.f, dst = 1.f;
    const BlitFit f = blit_fit(src, dst, FitMode::Fit);
    CHECK(f.sv > 1.f);                       // v window overshoots [0,1] -> letterbox bars
    CHECK(std::fabs(f.su - 1.f) < 1e-4f);    // full width of the source is visible
    CHECK(f.ov < 0.f);                       // centered: the window starts above the texture
    CHECK(std::fabs(shown_aspect(f, dst) - src) < 1e-3f);

    // A 9:16 (vertical) output in a WIDE surface: pillarbox on the u axis instead.
    const BlitFit g = blit_fit(9.f / 16.f, 16.f / 9.f, FitMode::Fit);
    CHECK(g.su > 1.f);
    CHECK(std::fabs(g.sv - 1.f) < 1e-4f);
    CHECK(std::fabs(shown_aspect(g, 16.f / 9.f) - 9.f / 16.f) < 1e-3f);
}

void test_fill_crops_and_preserves_aspect() {
    // Same 16:9-in-a-square, but Fill: cover the surface, crop the overhang. The window must stay
    // INSIDE [0,1] (that is what "crop" means) and still not distort.
    const float src = 16.f / 9.f, dst = 1.f;
    const BlitFit f = blit_fit(src, dst, FitMode::Fill);
    CHECK(f.su < 1.f);                       // crops horizontally
    CHECK(std::fabs(f.sv - 1.f) < 1e-4f);
    CHECK(f.ou > 0.f);                       // centered crop
    CHECK(f.ou + f.su <= 1.f + 1e-4f);       // stays within the texture -> no bars
    CHECK(std::fabs(shown_aspect(f, dst) - src) < 1e-3f);
}

// Fit and Fill must be opposites on every src/dst pair — the bug that would ship silently is these
// two branches being swapped.
void test_fit_and_fill_are_opposites() {
    const float srcs[] = { 2.4f, 16.f / 9.f, 1.f, 9.f / 16.f };
    const float dsts[] = { 2.0f, 1.6f, 1.f, 0.5f };
    for (float s : srcs)
        for (float d : dsts) {
            const BlitFit fit = blit_fit(s, d, FitMode::Fit);
            const BlitFit fil = blit_fit(s, d, FitMode::Fill);
            CHECK(fit.su >= 1.f - 1e-4f && fit.sv >= 1.f - 1e-4f);   // Fit never crops
            CHECK(fil.su <= 1.f + 1e-4f && fil.sv <= 1.f + 1e-4f);   // Fill never bars
            CHECK(std::fabs(shown_aspect(fit, d) - s) < 1e-3f);      // neither distorts
            CHECK(std::fabs(shown_aspect(fil, d) - s) < 1e-3f);
        }
}

}  // namespace

int main() {
    test_size_presets();
    test_stretch_is_identity();
    test_same_aspect_is_identity();
    test_fit_letterboxes_and_preserves_aspect();
    test_fill_crops_and_preserves_aspect();
    test_fit_and_fill_are_opposites();
    return vivid::test::summary("test_output_format");
}
