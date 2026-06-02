// Euclidean notes_out smoke test.
//
// Verifies that:
//   1. The Euclidean operator declares a notes_out custom-ref port.
//   2. With a 4-in-4 pattern (every step a hit), processing one full step
//      produces a NOTE_ON + NOTE_OFF pair on notes_out per beat with a
//      consistent note_id (same id on the matching off as the on).
//   3. The note number and velocity are taken from the operator's params.
//   4. With hits=0, no events are emitted.

#include "operator_api/note_types.h"
#include "operator_api/types.h"
#include "runtime/operators/operator_loader.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace {

constexpr int kFrames = 256;
constexpr uint32_t kSampleRate = 48000;

struct LaneStateEntry {
    uint32_t lane_id;
    uint32_t byte_size;
    std::vector<uint8_t> data;
};
static std::vector<LaneStateEntry> g_lane_states;

static void* test_lane_state(void* /*service*/, uint32_t lane_id, uint32_t byte_size) {
    for (auto& e : g_lane_states) {
        if (e.lane_id == lane_id && e.byte_size == byte_size)
            return e.data.data();
    }
    g_lane_states.push_back({lane_id, byte_size, std::vector<uint8_t>(byte_size, 0)});
    return g_lane_states.back().data.data();
}
static void reset_lane_states() { g_lane_states.clear(); }

struct LaneOutBuf {
    std::vector<float> data;
    static float* resize_cb(void* h, uint32_t len) {
        auto* self = static_cast<LaneOutBuf*>(h);
        self->data.assign(len, 0.0f);
        return self->data.data();
    }
    static void commit_cb(void* /*h*/, uint32_t /*len*/) {}
};

struct Harness {
    float beat_phase_in = 0.0f;
    float* in_bufs[1]   = {&beat_phase_in};
    float trigger_buf[kFrames] = {};
    float gate_buf[kFrames]    = {};
    float step_buf[kFrames]    = {};
    float* out_bufs[3] = {trigger_buf, gate_buf, step_buf};

    LaneOutBuf pattern_lane_buf;
    VividLaneOutput lane_outputs[4] = {};

    void* custom_outs[1] = {nullptr};

    VividAudioContext ctx{};

    Harness() {
        ctx.sample_rate        = kSampleRate;
        ctx.buffer_size        = kFrames;
        ctx.input_buffers      = in_bufs;
        ctx.output_buffers     = out_bufs;
        ctx.lane_state_fn      = test_lane_state;
        ctx.lane_state_service = nullptr;
        ctx.lane_id            = 1;

        lane_outputs[3].handle = &pattern_lane_buf;
        lane_outputs[3].resize = LaneOutBuf::resize_cb;
        lane_outputs[3].commit = LaneOutBuf::commit_cb;
        ctx.output_lanes = lane_outputs;

        ctx.custom_outputs      = custom_outs;
        ctx.custom_output_count = 1;

        ctx.metronome_bpm            = 120.0f;
        ctx.metronome_beats_per_bar  = 4;
        ctx.metronome_beats_elapsed  = 0.0;
        ctx.metronome_beat_phase     = 0.0f;
    }
};

static int find_param(const VividOperatorDescriptor* desc, const char* name) {
    for (uint32_t p = 0; p < desc->param_count; ++p)
        if (std::strcmp(desc->params[p].name, name) == 0) return static_cast<int>(p);
    return -1;
}

struct ParamOverride { const char* name; float value; };
static std::vector<float> make_params(const VividOperatorDescriptor* desc,
                                      std::initializer_list<ParamOverride> ov = {}) {
    std::vector<float> params(desc->param_count);
    for (uint32_t p = 0; p < desc->param_count; ++p)
        params[p] = desc->params[p].default_value;
    for (auto& o : ov) {
        int idx = find_param(desc, o.name);
        if (idx >= 0) params[idx] = o.value;
    }
    return params;
}

static bool has_port(const VividOperatorDescriptor* desc, const char* name) {
    for (uint32_t p = 0; p < desc->port_count; ++p)
        if (std::strcmp(desc->ports[p].name, name) == 0) return true;
    return false;
}

const VividNoteBuffer* drive(vivid::OperatorLoader& loader, void* inst,
                             Harness& h, std::initializer_list<float> phases) {
    const VividNoteBuffer* last = nullptr;
    for (float phase : phases) {
        h.beat_phase_in = phase;
        h.ctx.metronome_beat_phase = phase;
        h.ctx.metronome_beats_elapsed += phase;
        h.custom_outs[0] = nullptr;
        loader.process_audio(inst, &h.ctx);
        if (h.custom_outs[0])
            last = static_cast<const VividNoteBuffer*>(h.custom_outs[0]);
    }
    return last;
}

int count_type(const VividNoteBuffer* buf, VividNoteEventType t) {
    if (!buf) return 0;
    int n = 0;
    for (uint32_t m = 0; m < buf->count; ++m)
        if (buf->events[m].type == t) ++n;
    return n;
}

const VividNoteEvent* first_of_type(const VividNoteBuffer* buf, VividNoteEventType t) {
    if (!buf) return nullptr;
    for (uint32_t m = 0; m < buf->count; ++m)
        if (buf->events[m].type == t) return &buf->events[m];
    return nullptr;
}

} // namespace

int main(int argc, char** argv) {
    const std::string build_dir = (argc > 1) ? argv[1] : ".";
    const std::string dylib_path = build_dir + "/euclidean.dylib";

    if (!std::filesystem::exists(dylib_path)) {
        std::fprintf(stderr, "FATAL: %s not found\n", dylib_path.c_str());
        return 1;
    }

    vivid::OperatorLoader loader;
    if (!loader.load(dylib_path.c_str())) {
        std::fprintf(stderr, "FATAL: failed to load %s\n", dylib_path.c_str());
        return 1;
    }

    const auto* desc = loader.descriptor();
    check(desc != nullptr, "Euclidean descriptor not null");
    if (!desc) return 1;
    check(std::strcmp(desc->name, "Euclidean") == 0, "operator name is Euclidean");
    check(has_port(desc, "notes_out"), "Euclidean declares notes_out port");

    // ---------------------------------------------------------------------
    // Test 1: 4-in-4 pattern emits NOTE_ON then NOTE_OFF on rising/falling
    //         gate edges as we sweep through one step. Verifies note_id
    //         round-trip: the off carries the same id as the on.
    // ---------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- Euclidean: 4-in-4 emits NOTE_ON then NOTE_OFF with matching note_id ---\n");
        reset_lane_states();
        Harness h;
        auto params = make_params(desc, {
            {"hits", 4.0f}, {"steps", 4.0f}, {"rotation", 0.0f},
            {"gate_length", 0.5f}, {"rate", 2.0f},
            {"clock_mode", 0.0f},
            {"note", 36.0f}, {"velocity", 100.0f},
        });
        h.ctx.param_values = params.data();
        void* inst = loader.create_instance();

        // Block 1: phase 0.0 → step 0, gate rises → NOTE_ON(36)
        const auto* on_buf = drive(loader, inst, h, {0.0f});
        check(count_type(on_buf, VIVID_NOTE_ON) == 1, "one NOTE_ON emitted on rising gate");
        check(count_type(on_buf, VIVID_NOTE_OFF) == 0, "no NOTE_OFF in first block");
        const VividNoteEvent* on_ev = first_of_type(on_buf, VIVID_NOTE_ON);
        uint64_t on_id = 0;
        if (on_ev) {
            check(on_ev->note_number == 36, "note number is 36 (param)");
            check(on_ev->note_id != 0, "note_id is non-zero");
            check_float(on_ev->value, 100.0f / 127.0f, 1e-4f,
                        "velocity is 100/127 (param normalized to 0..1)");
            on_id = on_ev->note_id;
        }

        // Block 2: phase 0.6 → gate falls → NOTE_OFF carrying the same id
        const auto* off_buf = drive(loader, inst, h, {0.6f});
        check(count_type(off_buf, VIVID_NOTE_OFF) == 1, "NOTE_OFF emitted on falling gate");
        check(count_type(off_buf, VIVID_NOTE_ON) == 0, "no extra NOTE_ON after falling edge");
        const VividNoteEvent* off_ev = first_of_type(off_buf, VIVID_NOTE_OFF);
        if (off_ev) {
            check(off_ev->note_id == on_id,
                  "NOTE_OFF carries the same note_id as the matching NOTE_ON");
        }

        loader.destroy_instance(inst);
    }

    // ---------------------------------------------------------------------
    // Test 2: hits=0 → no events at all over many blocks.
    // ---------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- Euclidean: hits=0 emits no events ---\n");
        reset_lane_states();
        Harness h;
        auto params = make_params(desc, {
            {"hits", 0.0f}, {"steps", 8.0f}, {"rate", 2.0f},
            {"clock_mode", 0.0f},
        });
        h.ctx.param_values = params.data();
        void* inst = loader.create_instance();

        int total_events = 0;
        for (float p : {0.0f, 0.25f, 0.5f, 0.75f, 0.0f, 0.25f}) {
            h.beat_phase_in = p;
            h.ctx.metronome_beat_phase = p;
            h.custom_outs[0] = nullptr;
            loader.process_audio(inst, &h.ctx);
            if (h.custom_outs[0]) {
                auto* buf = static_cast<const VividNoteBuffer*>(h.custom_outs[0]);
                total_events += static_cast<int>(buf->count);
            }
        }
        check(total_events == 0, "no events with hits=0");

        loader.destroy_instance(inst);
    }

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
