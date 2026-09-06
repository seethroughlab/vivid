// P4 Phase E: the pure transforms the recording commit relies on. Sustain handling is the one that
// decides whether a recorded piano part sounds like what was played or like staccato stabs — a
// pianist's fingers leave the keys long before the notes stop, and the pedal is why.
#include "midi/note_record.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace vivid::session;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("  FAIL %s (%s:%d)\n", (msg), __FILE__, __LINE__); ++g_fail; } } while (0)

namespace {

bool near(double a, double b, double eps = 1e-9) { return std::fabs(a - b) < eps; }

// pedal down at `d`, up at `u`
std::vector<CcBp> pedal(std::initializer_list<std::pair<double, float>> pts) {
    std::vector<CcBp> v;
    for (auto& p : pts) v.push_back({ p.first, p.second });
    return v;
}

void test_sustain_extends_release_under_pedal() {
    // Note released at beat 1 while the pedal is held from 0 to 4 -> should ring to 4.
    double on[]  = { 0.0 };
    double off[] = { 1.0 };
    apply_sustain(on, off, 1, pedal({ {0.0, 1.0f}, {4.0, 0.0f} }));
    CHECK(near(off[0], 4.0), "a note released under a held pedal extends to the pedal release");
}

void test_sustain_ignores_notes_released_with_pedal_up() {
    double on[]  = { 0.0, 5.0 };
    double off[] = { 1.0, 6.0 };   // second note is entirely AFTER the pedal was let go
    apply_sustain(on, off, 2, pedal({ {0.0, 1.0f}, {4.0, 0.0f} }));
    CHECK(near(off[0], 4.0), "the note under the pedal extends");
    CHECK(near(off[1], 6.0), "a note released with the pedal up is untouched");
}

void test_sustain_before_pedal_is_untouched() {
    double on[]  = { 0.0 };
    double off[] = { 0.5 };        // released BEFORE the pedal ever went down
    apply_sustain(on, off, 1, pedal({ {1.0, 1.0f}, {3.0, 0.0f} }));
    CHECK(near(off[0], 0.5), "a note released before the pedal went down is untouched");
}

void test_sustain_never_released_leaves_note_alone() {
    // The caller closes still-open notes at the end of the take; inventing an infinite duration
    // here would produce a note longer than the clip.
    double on[]  = { 0.0 };
    double off[] = { 1.0 };
    apply_sustain(on, off, 1, pedal({ {0.0, 1.0f} }));
    CHECK(near(off[0], 1.0), "an unreleased pedal does not stretch the note to infinity");
}

void test_sustain_multiple_presses() {
    double on[]  = { 0.0, 5.0 };
    double off[] = { 1.0, 6.0 };
    apply_sustain(on, off, 2, pedal({ {0.0, 1.0f}, {2.0, 0.0f}, {4.0, 1.0f}, {8.0, 0.0f} }));
    CHECK(near(off[0], 2.0), "first press extends to its own release");
    CHECK(near(off[1], 8.0), "second press extends to the second release");
}

void test_sustain_no_pedal_is_a_noop() {
    double on[]  = { 0.0 };
    double off[] = { 1.0 };
    apply_sustain(on, off, 1, {});
    CHECK(near(off[0], 1.0), "no pedal data changes nothing");
}

void test_decimate_thins_a_ramp_to_its_endpoints() {
    // A controller sends ~100 messages/sec; a linear sweep must collapse to (near) two points.
    std::vector<CcBp> raw;
    for (int i = 0; i <= 400; ++i) raw.push_back({ i * 0.01, i / 400.0f });
    const auto out = decimate_cc(raw, kCcDecimateEps, kCcDecimateMinDt);
    CHECK(out.size() < 12, "a straight ramp thins drastically");
    CHECK(out.size() >= 2, "endpoints are kept");
    CHECK(near(out.front().t, 0.0) && near(out.back().t, 4.0), "the endpoints are the original ones");
    CHECK(std::fabs(out.back().v - 1.0f) < 1e-6, "the final value survives");
}

void test_decimate_keeps_a_real_corner() {
    // Up then down: the peak must survive or the shape is a lie.
    std::vector<CcBp> raw;
    for (int i = 0; i <= 50; ++i) raw.push_back({ i * 0.02, i / 50.0f });
    for (int i = 1; i <= 50; ++i) raw.push_back({ 1.0 + i * 0.02, 1.0f - i / 50.0f });
    const auto out = decimate_cc(raw, kCcDecimateEps, kCcDecimateMinDt);
    bool peak = false;
    for (const CcBp& p : out) if (std::fabs(p.t - 1.0) < 0.05 && p.v > 0.95f) peak = true;
    CHECK(peak, "the corner of an up-down sweep is preserved");
}

void test_decimate_min_dt_floor() {
    // A jittery pot: many points within a tiny time span, alternating enough to survive RDP.
    std::vector<CcBp> raw;
    for (int i = 0; i < 200; ++i) raw.push_back({ i * 0.0001, (i % 2) ? 0.9f : 0.1f });
    const auto out = decimate_cc(raw, kCcDecimateEps, kCcDecimateMinDt);
    for (size_t i = 1; i + 1 < out.size(); ++i)
        CHECK(out[i].t - out[i - 1].t >= kCcDecimateMinDt - 1e-12, "interior points respect the min spacing");
    CHECK(out.size() < raw.size(), "jitter is thinned");
}

void test_decimate_small_inputs() {
    CHECK(decimate_cc({}, kCcDecimateEps, kCcDecimateMinDt).empty(), "empty stays empty");
    std::vector<CcBp> one{ {1.0, 0.5f} };
    CHECK(decimate_cc(one, kCcDecimateEps, kCcDecimateMinDt).size() == 1, "a single point survives");
    std::vector<CcBp> two{ {2.0, 0.5f}, {0.0, 0.1f} };
    const auto s2 = decimate_cc(two, kCcDecimateEps, kCcDecimateMinDt);
    CHECK(s2.size() == 2 && s2[0].t < s2[1].t, "two points are kept and sorted");
}

}  // namespace

int main() {
    std::printf("test_cc_record\n");
    test_sustain_extends_release_under_pedal();
    test_sustain_ignores_notes_released_with_pedal_up();
    test_sustain_before_pedal_is_untouched();
    test_sustain_never_released_leaves_note_alone();
    test_sustain_multiple_presses();
    test_sustain_no_pedal_is_a_noop();
    test_decimate_thins_a_ramp_to_its_endpoints();
    test_decimate_keeps_a_real_corner();
    test_decimate_min_dt_floor();
    test_decimate_small_inputs();
    if (g_fail == 0) std::printf("ok   test_cc_record — sustain extension + controller decimation\n");
    return g_fail;
}
