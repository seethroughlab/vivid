// Verifies VividDrawAPI's additive extension for draw_tri / draw_arc /
// draw_text_wrapped: the fields exist, can be assigned with custom thunks,
// and callers (operators) dispatch correctly without needing a real
// Renderer2D/GPU device. Does NOT exercise Renderer2D itself — that is
// covered by the host's regular UI rendering paths.

#include "operator_api/types.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace {

struct RecordedCall {
    std::string name;
    std::vector<float> floats;
    int ints = 0;
    std::string text;
    VividColor color{};
};

struct Recorder {
    std::vector<RecordedCall> calls;
};

void rec_draw_tri(void* o, float x0, float y0, float x1, float y1,
                   float x2, float y2, VividColor c) {
    auto* r = static_cast<Recorder*>(o);
    RecordedCall call;
    call.name = "draw_tri";
    call.floats = {x0, y0, x1, y1, x2, y2};
    call.color = c;
    r->calls.push_back(std::move(call));
}

void rec_draw_arc(void* o, float cx, float cy, float radius,
                   float start_angle, float end_angle,
                   float thickness, int segments, VividColor c) {
    auto* r = static_cast<Recorder*>(o);
    RecordedCall call;
    call.name = "draw_arc";
    call.floats = {cx, cy, radius, start_angle, end_angle, thickness};
    call.ints = segments;
    call.color = c;
    r->calls.push_back(std::move(call));
}

float rec_draw_text_wrapped(void* o, float x, float y, const char* text,
                             float max_width, VividColor c, float scale) {
    auto* r = static_cast<Recorder*>(o);
    RecordedCall call;
    call.name = "draw_text_wrapped";
    call.floats = {x, y, max_width, scale};
    call.text = text ? text : "";
    call.color = c;
    r->calls.push_back(std::move(call));
    return 42.0f;  // arbitrary consumed-height return value
}

} // namespace

int main() {
    std::fprintf(stderr, "=== Test: VividDrawAPI new-field dispatch ===\n\n");

    // --- field addressable: the new pointers exist at the struct's end ---
    {
        VividDrawAPI api{};
        api.draw_tri = nullptr;
        api.draw_arc = nullptr;
        api.draw_text_wrapped = nullptr;
        check(true, "new fields are addressable on VividDrawAPI");
    }

    // --- thunks dispatch via the function-pointer fields ---
    {
        Recorder rec;
        VividDrawAPI api{};
        api.opaque = &rec;
        api.draw_tri = rec_draw_tri;
        api.draw_arc = rec_draw_arc;
        api.draw_text_wrapped = rec_draw_text_wrapped;

        VividColor red{1.0f, 0.0f, 0.0f, 1.0f};
        api.draw_tri(api.opaque, 1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, red);
        api.draw_arc(api.opaque, 10.0f, 20.0f, 30.0f, 0.0f, 3.14f, 2.0f, 16, red);
        const float consumed =
            api.draw_text_wrapped(api.opaque, 100.0f, 200.0f,
                                  "hello", 250.0f, red, 1.0f);

        check(rec.calls.size() == 3, "three calls recorded");
        check(rec.calls[0].name == "draw_tri", "draw_tri dispatched");
        check(rec.calls[1].name == "draw_arc", "draw_arc dispatched");
        check(rec.calls[2].name == "draw_text_wrapped", "draw_text_wrapped dispatched");
        check(rec.calls[2].text == "hello", "draw_text_wrapped forwards text");
        check(rec.calls[1].ints == 16, "draw_arc forwards segments integer");
        check(rec.calls[1].color.r == 1.0f, "draw_arc forwards color.r");
        check(consumed == 42.0f, "draw_text_wrapped return propagates");
    }

    // --- opting not to populate the new fields is safe ---
    {
        VividDrawAPI api{};
        check(api.draw_tri == nullptr, "default-initialised draw_tri is null");
        check(api.draw_arc == nullptr, "default-initialised draw_arc is null");
        check(api.draw_text_wrapped == nullptr,
              "default-initialised draw_text_wrapped is null");
    }

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
