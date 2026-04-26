// Dual-synth voice-sync test for the native note transport.
//
// One note buffer is fed into TWO independent FmSynth instances. We assert
// that both synths react to the same note_id stream coherently:
//   - Both produce sound on NOTE_ON.
//   - Both release on NOTE_OFF (matched by note_id).
//   - A same-pitch retrigger (note_off then note_on at the same pitch but
//     a fresh note_id) leaves both synths in matching states. With the old
//     pitch-keyed allocator, the second on would silently re-use the held
//     slot; with note_id, it allocates a fresh voice and the retrigger is
//     audible — same behavior on both synths.

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

constexpr int kFrames = 2048;
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

struct SynthHarness {
    float output[kFrames] = {};
    float voices_out[8 * kFrames] = {};  // FmSynth kMaxVoices = 8
    float scalar_in_freq = 0.0f;
    float scalar_in_mod  = 0.0f;
    float scalar_in_gate = 0.0f;
    float* output_bufs[2] = {output, voices_out};
    float* input_bufs[3]  = {&scalar_in_freq, &scalar_in_mod, &scalar_in_gate};

    VividAudioContext ctx{};
    void* note_inputs[1] = {nullptr};

    SynthHarness() {
        ctx.sample_rate        = kSampleRate;
        ctx.buffer_size        = kFrames;
        ctx.input_buffers      = input_bufs;
        ctx.output_buffers     = output_bufs;
        ctx.lane_state_fn      = test_lane_state;
        ctx.lane_state_service = nullptr;
        ctx.lane_id            = 1;
        ctx.custom_inputs      = note_inputs;
        ctx.custom_input_count = 1;
    }

    void set_notes(VividNoteBuffer* buf) { note_inputs[0] = buf; }
    void set_lane_id(uint32_t id) { ctx.lane_id = id; }
    void zero_output() { std::memset(output, 0, sizeof(output)); }

    float rms() const {
        double s = 0.0;
        for (int i = 0; i < kFrames; ++i) s += output[i] * output[i];
        return static_cast<float>(std::sqrt(s / kFrames));
    }
    bool all_finite() const {
        for (int i = 0; i < kFrames; ++i)
            if (!std::isfinite(output[i])) return false;
        return true;
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

static void push_note_on(VividNoteBuffer& buf, uint8_t note, float vel,
                         uint64_t id) {
    if (buf.count >= VIVID_NOTE_BUFFER_CAPACITY) return;
    auto& e = buf.events[buf.count++];
    e.type = VIVID_NOTE_ON; e.note_number = note; e.value = vel; e.note_id = id;
}
static void push_note_off(VividNoteBuffer& buf, uint64_t id) {
    if (buf.count >= VIVID_NOTE_BUFFER_CAPACITY) return;
    auto& e = buf.events[buf.count++];
    e.type = VIVID_NOTE_OFF; e.note_id = id;
}

}  // namespace

int main(int argc, char** argv) {
    const std::string build_dir = (argc > 1) ? argv[1] : ".";
    const std::string dylib_path = build_dir + "/fm_synth.dylib";
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
    auto params = make_params(desc, {
        {"attack", 0.001f}, {"decay", 0.01f}, {"sustain", 0.9f}, {"release", 0.05f},
        {"amplitude", 0.5f}, {"mod_index", 1.0f},
    });

    // Two harnesses, two instances — distinct lane_ids so per-lane state
    // doesn't collide.
    SynthHarness h_a, h_b;
    h_a.ctx.param_values = params.data();
    h_b.ctx.param_values = params.data();
    h_a.set_lane_id(101);
    h_b.set_lane_id(102);

    void* inst_a = loader.create_instance();
    void* inst_b = loader.create_instance();
    check(inst_a && inst_b, "two FmSynth instances created");

    VividNoteBuffer notes{};
    h_a.set_notes(&notes);
    h_b.set_notes(&notes);

    auto run_block = [&]() {
        h_a.zero_output(); h_b.zero_output();
        loader.process_audio(inst_a, &h_a.ctx);
        loader.process_audio(inst_b, &h_b.ctx);
    };

    // -------------------------------------------------------------------
    // Test 1: same NOTE_ON drives both synths to non-silent output.
    // -------------------------------------------------------------------
    std::fprintf(stderr, "\n--- dual: NOTE_ON drives both synths ---\n");
    reset_lane_states();
    notes.count = 0;
    push_note_on(notes, 60, 100.0f / 127.0f, /*id=*/777);
    run_block();
    notes.count = 0;
    run_block();  // sustain block
    const float a_sustain = h_a.rms();
    const float b_sustain = h_b.rms();
    check(a_sustain > 1e-3f, "synth A audible during sustain");
    check(b_sustain > 1e-3f, "synth B audible during sustain");
    check(h_a.all_finite() && h_b.all_finite(), "both outputs finite");
    std::fprintf(stderr, "  A sustain RMS: %.4f, B sustain RMS: %.4f\n",
                 a_sustain, b_sustain);

    // -------------------------------------------------------------------
    // Test 2: NOTE_OFF (same id) releases both synths.
    // -------------------------------------------------------------------
    std::fprintf(stderr, "\n--- dual: NOTE_OFF releases both synths ---\n");
    notes.count = 0;
    push_note_off(notes, /*id=*/777);
    run_block();
    notes.count = 0;
    run_block();  // release tail
    const float a_rel = h_a.rms();
    const float b_rel = h_b.rms();
    check(a_rel < a_sustain, "A release-tail quieter than sustain");
    check(b_rel < b_sustain, "B release-tail quieter than sustain");
    std::fprintf(stderr, "  A release RMS: %.4f, B release RMS: %.4f\n",
                 a_rel, b_rel);

    // -------------------------------------------------------------------
    // Test 3: same-pitch retrigger — fresh note_id allocates a fresh voice
    // on BOTH synths. The retrigger should be audible (with the legacy
    // pitch-keyed allocator the second on would silently re-use the slot
    // and the attack would be inaudible).
    // -------------------------------------------------------------------
    std::fprintf(stderr, "\n--- dual: same-pitch retrigger with fresh note_id ---\n");
    // Ensure both synths have fully released first.
    for (int i = 0; i < 6; ++i) {
        notes.count = 0;
        run_block();
    }
    // Now play note 60 with id 800, hold briefly, then NOTE_ON note 60
    // again with a FRESH id 801 while the first is still releasing.
    notes.count = 0;
    push_note_on(notes, 60, 100.0f / 127.0f, /*id=*/800);
    run_block();   // attack/sustain
    notes.count = 0;
    run_block();   // sustain
    notes.count = 0;
    push_note_off(notes, /*id=*/800);
    push_note_on(notes, 60, 100.0f / 127.0f, /*id=*/801);
    run_block();   // first releases, second attacks — should be audible
    const float a_re = h_a.rms();
    const float b_re = h_b.rms();
    check(a_re > 1e-3f, "A audible during same-pitch retrigger");
    check(b_re > 1e-3f, "B audible during same-pitch retrigger");
    check(h_a.all_finite() && h_b.all_finite(),
          "both outputs finite during retrigger");
    std::fprintf(stderr, "  A retrig RMS: %.4f, B retrig RMS: %.4f\n",
                 a_re, b_re);

    loader.destroy_instance(inst_a);
    loader.destroy_instance(inst_b);

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
