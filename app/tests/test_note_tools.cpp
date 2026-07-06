// Headless test for the M5 musical transforms in midi/note_tools.h.
#include "midi/note_tools.h"
#include "test_helpers.h"
#include <vector>

using vivid::session::ClipNote;

int main() {
    using namespace vivid::session;

    // tool_targets: selection, or all notes when nothing selected.
    {
        std::vector<uint8_t> none = {0,0,0}, some = {1,0,1};
        CHECK(tool_targets(none, 3).size() == 3);   // empty selection -> whole clip
        CHECK(tool_targets(some, 3).size() == 2);
    }

    // invert_pitches: mirror around the range center (lo+hi)/2.
    {
        std::vector<ClipNote> n = { {60,0,1,0.8f,{}}, {64,1,1,0.8f,{}}, {67,2,1,0.8f,{}} };
        std::vector<uint8_t> sel = {1,1,1};
        invert_pitches(n, sel);   // lo=60, hi=67, axis2=127 -> p' = 127-p
        CHECK(n[0].pitch == 67);
        CHECK(n[1].pitch == 63);
        CHECK(n[2].pitch == 60);
    }

    // retrograde: reverse in time within [minStart, maxEnd].
    {
        std::vector<ClipNote> n = { {60,0,1,0.8f,{}}, {62,1,1,0.8f,{}}, {64,2,2,0.8f,{}} };
        std::vector<uint8_t> sel = {1,1,1};   // span [0,4]
        retrograde(n, sel);
        CHECK_NEAR(n[0].start, 3.0, 1e-9);    // 0+4-(0+1)=3
        CHECK_NEAR(n[1].start, 2.0, 1e-9);    // 4-(1+1)=2
        CHECK_NEAR(n[2].start, 0.0, 1e-9);    // 4-(2+2)=0
    }

    // quantize_to_scale: snap into C major (mask), off-scale notes move to nearest degree.
    {
        std::vector<ClipNote> n = { {61,0,1,0.8f,{}}, {66,1,1,0.8f,{}}, {60,2,1,0.8f,{}} };
        std::vector<uint8_t> sel = {1,1,1};
        const uint16_t Cmaj = (1<<0)|(1<<2)|(1<<4)|(1<<5)|(1<<7)|(1<<9)|(1<<11);
        quantize_to_scale(n, sel, 0, Cmaj);
        CHECK(n[0].pitch == 60 || n[0].pitch == 62);   // C#4 -> C or D (nearest)
        CHECK(n[1].pitch == 65 || n[1].pitch == 67);   // F#4 -> F or G
        CHECK(n[2].pitch == 60);                       // C already in scale
    }

    // humanize: deterministic given the seed; bounded jitter; stays in range.
    {
        std::vector<ClipNote> a = { {60,1.0,1,0.5f,{}} }, b = a;
        std::vector<uint8_t> sel = {1};
        humanize(a, sel, 0.1, 0.1f, 42);
        humanize(b, sel, 0.1, 0.1f, 42);
        CHECK_NEAR(a[0].start, b[0].start, 1e-9);      // same seed -> same result
        CHECK(a[0].start >= 0.9 - 1e-6 && a[0].start <= 1.1 + 1e-6);
        CHECK(a[0].vel >= 0.f && a[0].vel <= 1.f);
    }

    // strum: a 3-note chord at the same start staggers by `off`, low pitch first.
    {
        std::vector<ClipNote> n = { {67,0,1,0.8f,{}}, {60,0,1,0.8f,{}}, {64,0,1,0.8f,{}} };
        std::vector<uint8_t> sel = {1,1,1};
        strum(n, sel, 0.1);
        CHECK_NEAR(n[1].start, 0.0, 1e-9);   // pitch 60 first
        CHECK_NEAR(n[2].start, 0.1, 1e-9);   // pitch 64 second
        CHECK_NEAR(n[0].start, 0.2, 1e-9);   // pitch 67 third
    }

    // apply_glide: each later note bends in from the previous pitch to 0.
    {
        std::vector<ClipNote> n = { {60,0,1,0.8f,{}}, {67,1,1,0.8f,{}} };
        std::vector<uint8_t> sel = {1,1};
        apply_glide(n, sel, 0.3f, 12.f);
        CHECK(n[0].expr[AXIS_BEND].empty());              // first note: no glide
        const auto& c = n[1].expr[AXIS_BEND];
        CHECK(!c.empty());
        CHECK_NEAR(c.sample(0.f), -7.0, 1e-5);            // from 60 (7 below 67)
        CHECK_NEAR(c.sample(1.f), 0.0, 1e-5);             // resolves to its own pitch
    }

    return vivid::test::summary("test_note_tools");
}
