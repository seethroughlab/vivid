// Clip-level controller lanes (P4 Phase A): sampling, the scheduler's per-block emission, and the
// JSON round-trip. The subtle parts are the CURSOR (which makes sampling O(points crossed) instead
// of O(n) from the start, and therefore has to survive seeking backwards) and the LOOP WRAP — a lane
// that ends high and starts low must restate its low value on the next pass, or the plugin stays
// stuck wherever the previous pass left it.
#include "midi/midi_clip.h"
#include "midi/note_json.h"

#include <cmath>
#include <cstdio>
#include <vector>

using namespace vivid::session;

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { std::printf("  FAIL %s (%s:%d)\n", (msg), __FILE__, __LINE__); ++g_fail; } } while (0)

namespace {

bool near(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) < eps; }

CcLane ramp(uint16_t cc, double t0, double t1, float v0, float v1) {
    CcLane l; l.cc = cc;
    l.bp.push_back({ t0, v0 });
    l.bp.push_back({ t1, v1 });
    return l;
}

void test_sample_basics() {
    CcLane l = ramp(1, 0.0, 4.0, 0.0f, 1.0f);
    uint32_t cur = 0;
    CHECK(near(l.sample(0.0, cur), 0.0f), "start of ramp");
    CHECK(near(l.sample(2.0, cur), 0.5f), "midpoint interpolates");
    CHECK(near(l.sample(4.0, cur), 1.0f), "end of ramp");
    // Held before the first and after the last breakpoint.
    CHECK(near(l.sample(-5.0, cur), 0.0f), "held before the first point");
    CHECK(near(l.sample(99.0, cur), 1.0f), "held after the last point");
    CcLane e; uint32_t c2 = 0;
    CHECK(near(e.sample(1.0, c2), 0.0f), "an empty lane samples 0");
}

void test_cursor_is_order_independent() {
    // The cursor is an optimization; it must never change the RESULT. Sample forwards, then
    // backwards, then randomly, and compare against a fresh cursor each time.
    CcLane l; l.cc = 74;
    for (int i = 0; i <= 10; ++i) l.bp.push_back({ i * 0.5, i / 10.0f });
    uint32_t shared = 0;
    const double probes[] = { 0.0, 1.3, 2.7, 4.9, 2.7, 0.4, 5.0, 3.3, 1.1, 4.4 };
    for (double t : probes) {
        uint32_t fresh = 0;
        const float a = l.sample(t, shared);     // reused cursor (seeks forwards AND backwards)
        const float b = l.sample(t, fresh);      // cold cursor
        if (!near(a, b)) { std::printf("  FAIL cursor changed the result at t=%f (%f vs %f)\n", t, a, b); ++g_fail; }
    }
}

// Drive a scheduler over `blocks` blocks of `delta` beats and collect every CcEvent.
std::vector<CcEvent> run(ClipScheduler& s, int blocks, double delta, double start = 0.0) {
    std::vector<CcEvent> out;
    for (int i = 0; i < blocks; ++i) s.emit_cc(start + i * delta, delta, 512, out);
    return out;
}

void test_emit_dedupes_and_tracks() {
    MidiClip c; c.length = 4.0;
    c.cc.push_back(ramp(1, 0.0, 4.0, 0.0f, 1.0f));
    ClipScheduler s; s.reset(&c, 0.0);
    auto evs = run(s, 4, 1.0);                    // beats 0,1,2,3
    CHECK(evs.size() == 4, "one event per block while the value is changing");
    if (evs.size() == 4) {
        CHECK(evs[0].cc == 1 && near(evs[0].value, 0.0f), "first block emits the start value");
        CHECK(near(evs[1].value, 0.25f) && near(evs[2].value, 0.5f), "values track the ramp");
        CHECK(evs[0].sample_offset == 0, "block-granular: emitted at the top of the block");
    }

    // A FLAT lane emits once and then goes quiet — re-sending an unchanged controller every block
    // would burn a param slot for nothing.
    MidiClip f; f.length = 4.0;
    CcLane flat; flat.cc = 7; flat.bp.push_back({ 0.0, 0.5f });
    f.cc.push_back(flat);
    ClipScheduler s2; s2.reset(&f, 0.0);
    auto fe = run(s2, 4, 1.0);          // beats 0..3 — deliberately short of the wrap at beat 4
    CHECK(fe.size() == 1, "a flat lane emits once, not once per block");
}

void test_loop_wrap_restates() {
    // The lane ends at 1.0 and starts at 0.0. On the second pass the plugin must be told 0.0 again;
    // without a wrap restate it would stay pinned at 1.0 forever.
    MidiClip c; c.length = 4.0;
    c.cc.push_back(ramp(11, 0.0, 4.0, 0.0f, 1.0f));
    ClipScheduler s; s.reset(&c, 0.0);
    auto evs = run(s, 9, 1.0);                    // two full passes plus one block
    CHECK(evs.size() >= 5, "events continue across the loop point");
    // Find the first event at or after the wrap (block index 4 => beat 4 => wraps to 0).
    bool restated = false;
    for (size_t i = 1; i < evs.size(); ++i)
        if (evs[i].value < evs[i - 1].value - 0.5f) { restated = true; break; }
    CHECK(restated, "the lane restates its low value after the loop wraps");
}

void test_no_lanes_costs_nothing() {
    MidiClip c; c.length = 4.0;                   // notes only, no cc
    ClipScheduler s; s.reset(&c, 0.0);
    auto evs = run(s, 8, 0.5);
    CHECK(evs.empty(), "a clip with no lanes emits nothing");
}

void test_lane_cap() {
    MidiClip c; c.length = 4.0;
    for (int i = 0; i < kMaxCcLanes + 6; ++i) {
        CcLane l; l.cc = static_cast<uint16_t>(i); l.bp.push_back({ 0.0, 0.25f });
        c.cc.push_back(l);
    }
    ClipScheduler s; s.reset(&c, 0.0);
    std::vector<CcEvent> out;
    s.emit_cc(0.0, 1.0, 512, out);
    CHECK(out.size() <= static_cast<size_t>(kMaxCcLanes),
          "emission is capped at kMaxCcLanes (the RT cursor state is a fixed array)");
}

void test_invalidate_forces_restate() {
    // After an edit-apply the lane vector has been re-assigned under the audio thread; the cursors
    // are meaningless and the last-sent values may no longer be true.
    MidiClip c; c.length = 4.0;
    CcLane flat; flat.cc = 1; flat.bp.push_back({ 0.0, 0.5f });
    c.cc.push_back(flat);
    ClipScheduler s; s.reset(&c, 0.0);
    std::vector<CcEvent> out;
    s.emit_cc(0.0, 1.0, 512, out);
    const size_t after_first = out.size();
    s.emit_cc(1.0, 1.0, 512, out);
    CHECK(out.size() == after_first, "flat lane stays quiet");
    s.invalidate_active_src();
    s.emit_cc(2.0, 1.0, 512, out);
    CHECK(out.size() == after_first + 1, "invalidate forces the lane to restate");
}

void test_json_roundtrip() {
    MidiClip c; c.length = 8.0;
    c.cc.push_back(ramp(1, 0.0, 4.0, 0.0f, 1.0f));
    CcLane bend; bend.cc = kCcPitchBend;
    bend.bp.push_back({ 0.0, 0.5f }); bend.bp.push_back({ 2.0, 0.75f }); bend.bp.push_back({ 4.0, 0.5f });
    c.cc.push_back(bend);

    nlohmann::json jc;
    cc_to_json(c, jc);
    CHECK(jc.contains("cc") && jc["cc"].size() == 2, "two lanes serialized");

    MidiClip back;
    cc_from_json(jc, back);
    CHECK(back.cc.size() == 2, "two lanes parsed");
    if (back.cc.size() == 2) {
        CHECK(back.cc[0].cc == 1 && back.cc[0].bp.size() == 2, "lane 0 round-trips");
        CHECK(back.cc[1].cc == kCcPitchBend && back.cc[1].bp.size() == 3, "pitch-bend lane round-trips");
        CHECK(near(static_cast<float>(back.cc[1].bp[1].t), 2.0f), "breakpoint beats round-trip");
        CHECK(near(back.cc[1].bp[1].v, 0.75f), "breakpoint values round-trip");
    }

    // An empty clip writes no key at all (so old files stay byte-identical).
    MidiClip empty; nlohmann::json je;
    cc_to_json(empty, je);
    CHECK(!je.contains("cc"), "no lanes => no 'cc' key");

    // A clip with no "cc" key parses to no lanes — the back-compat path.
    MidiClip old_clip; nlohmann::json jo = { {"length", 4.0}, {"notes", nlohmann::json::array()} };
    cc_from_json(jo, old_clip);
    CHECK(old_clip.cc.empty(), "a pre-CC clip loads with no lanes");
}

void test_json_is_defensive() {
    MidiClip c;
    nlohmann::json jc;
    jc["cc"] = nlohmann::json::array({
        { {"n", 999}, {"pts", nlohmann::json::array({ nlohmann::json::array({0.0, 0.5}) })} },  // out of range
        { {"n", 1},   {"pts", "not an array"} },                                                // malformed
        { {"n", 2},   {"pts", nlohmann::json::array({ nlohmann::json::array({1.0}) })} },        // short point
        { {"n", 3},   {"pts", nlohmann::json::array({ nlohmann::json::array({2.0, 5.0}),        // out-of-range v
                                                      nlohmann::json::array({0.0, 0.25}) })} }, // and unsorted
    });
    cc_from_json(jc, c);
    CHECK(c.cc.size() == 1, "only the one salvageable lane survives");
    if (c.cc.size() == 1) {
        CHECK(c.cc[0].cc == 3, "the surviving lane is the right one");
        CHECK(c.cc[0].bp.size() == 2 && c.cc[0].bp[0].t < c.cc[0].bp[1].t, "points are sorted on load");
        CHECK(c.cc[0].bp[1].v <= 1.0f, "out-of-range values are clamped");
    }
}

}  // namespace

int main() {
    std::printf("test_cc_lane\n");
    test_sample_basics();
    test_cursor_is_order_independent();
    test_emit_dedupes_and_tracks();
    test_loop_wrap_restates();
    test_no_lanes_costs_nothing();
    test_lane_cap();
    test_invalidate_forces_restate();
    test_json_roundtrip();
    test_json_is_defensive();
    if (g_fail == 0) std::printf("ok   test_cc_lane — sampling/cursor, per-block emission, loop restate, JSON round-trip\n");
    return g_fail;
}
