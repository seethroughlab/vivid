// FmSynth notes_in smoke tests.
//
// Verifies that:
//   1. FmSynth declares a notes_in custom-ref port.
//   2. With notes_in connected and a NOTE_ON, the synth produces audio.
//   3. NOTE_OFF (matched by note_id) triggers release and audio decays.
//   4. Polyphony works — chord (3 simultaneous notes, distinct ids) produces
//      louder/richer output than a single note.
//   5. The lane-array path still works when notes_in is absent (regression
//      check for the legacy override).
//   6. Fresh instance with no input is silent.
//   7. Per-note pitch-bend semitones shift the carrier frequency:
//      a +12 semitone bend shifts the spectrum higher (RMS roughly the
//      same magnitude but different waveform — we just verify the synth
//      consumes the event without going silent or NaN).

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

struct Harness {
    float output[kFrames] = {};
    // FmSynth has kMaxVoices = 8 voices; voices_out is multichannel mono-per-voice.
    float voices_out[8 * kFrames] = {};
    float scalar_in_freq = 0.0f;
    float scalar_in_mod  = 0.0f;
    float scalar_in_gate = 0.0f;
    // FmSynth port order:
    //   inputs (audio buffers): [0]=freq_cv, [1]=mod_index_cv, [2]=gate_cv
    //   outputs (audio buffers): [0]=output (mono), [1]=voices_out (8 ch breakout)
    float* output_bufs[2] = {output, voices_out};
    float* input_bufs[3]  = {&scalar_in_freq, &scalar_in_mod, &scalar_in_gate};

    VividAudioContext ctx{};
    VividNoteBuffer   notes{};
    void* note_inputs[1] = {&notes};

    Harness() {
        ctx.sample_rate        = kSampleRate;
        ctx.buffer_size        = kFrames;
        ctx.input_buffers      = input_bufs;
        ctx.output_buffers     = output_bufs;
        ctx.lane_state_fn      = test_lane_state;
        ctx.lane_state_service = nullptr;
        ctx.lane_id            = 1;
    }

    void zero_output() { std::memset(output, 0, sizeof(output)); }

    void enable_note_input() {
        ctx.custom_inputs      = note_inputs;
        ctx.custom_input_count = 1;
    }
    void disable_note_input() {
        ctx.custom_inputs      = nullptr;
        ctx.custom_input_count = 0;
        notes.count = 0;
    }

    void push_note_on(uint8_t note, float vel_0_1, uint64_t id, uint32_t offset = 0) {
        if (notes.count >= VIVID_NOTE_BUFFER_CAPACITY) return;
        auto& e = notes.events[notes.count++];
        e.type        = VIVID_NOTE_ON;
        e.note_number = note;
        e.value       = vel_0_1;
        e.note_id     = id;
        e.frame_offset_samples = offset;
    }
    void push_note_off(uint64_t id, uint32_t offset = 0) {
        if (notes.count >= VIVID_NOTE_BUFFER_CAPACITY) return;
        auto& e = notes.events[notes.count++];
        e.type        = VIVID_NOTE_OFF;
        e.note_id     = id;
        e.frame_offset_samples = offset;
    }
    void push_pitch_bend(uint64_t id, float semis, uint32_t offset = 0) {
        if (notes.count >= VIVID_NOTE_BUFFER_CAPACITY) return;
        auto& e = notes.events[notes.count++];
        e.type        = VIVID_NOTE_PITCH_BEND;
        e.value       = semis;
        e.note_id     = id;
        e.frame_offset_samples = offset;
    }
    void clear_notes() { notes.count = 0; }

    float rms() const {
        double s = 0.0;
        for (int i = 0; i < kFrames; ++i) s += output[i] * output[i];
        return static_cast<float>(std::sqrt(s / kFrames));
    }

    bool any_finite_nonzero() const {
        for (int i = 0; i < kFrames; ++i) {
            if (!std::isfinite(output[i])) return false;
            if (std::fabs(output[i]) > 1e-6f) return true;
        }
        return false;
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

static bool has_port(const VividOperatorDescriptor* desc, const char* name) {
    for (uint32_t p = 0; p < desc->port_count; ++p)
        if (std::strcmp(desc->ports[p].name, name) == 0) return true;
    return false;
}

} // namespace

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
    check(desc != nullptr, "FmSynth descriptor not null");
    if (!desc) return 1;
    check(std::strcmp(desc->name, "FmSynth") == 0, "operator name is FmSynth");
    check(has_port(desc, "notes_in"), "FmSynth declares notes_in port");
    check(has_port(desc, "gates"), "FmSynth keeps lane gates port (override)");

    // ---------------------------------------------------------------------
    // Test 1: notes_in NOTE_ON produces audio.
    // ---------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- FmSynth: notes_in NOTE_ON produces audio ---\n");
        reset_lane_states();
        Harness h;
        h.enable_note_input();
        h.push_note_on(60, 100.0f / 127.0f, /*id=*/100);
        auto params = make_params(desc, {
            {"attack", 0.001f}, {"decay", 0.05f}, {"sustain", 0.8f}, {"release", 0.1f},
            {"amplitude", 0.5f}, {"mod_index", 1.0f},
        });
        h.ctx.param_values = params.data();
        void* inst = loader.create_instance();
        check(inst != nullptr, "instance created");
        loader.process_audio(inst, &h.ctx);
        check(h.all_finite(), "output is finite");
        check(h.any_finite_nonzero(), "output is non-silent after NOTE_ON");
        const float on_rms = h.rms();
        std::fprintf(stderr, "  note-on RMS: %.4f\n", on_rms);
        loader.destroy_instance(inst);
    }

    // ---------------------------------------------------------------------
    // Test 2: NOTE_OFF (matched by note_id) triggers release.
    // ---------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- FmSynth: NOTE_OFF releases envelope ---\n");
        reset_lane_states();
        Harness h;
        h.enable_note_input();
        auto params = make_params(desc, {
            {"attack", 0.001f}, {"decay", 0.01f}, {"sustain", 1.0f}, {"release", 0.02f},
            {"amplitude", 0.5f}, {"mod_index", 1.0f},
        });
        h.ctx.param_values = params.data();
        void* inst = loader.create_instance();

        h.push_note_on(60, 100.0f / 127.0f, /*id=*/200);
        loader.process_audio(inst, &h.ctx);
        h.clear_notes();
        loader.process_audio(inst, &h.ctx);
        const float sustain_rms = h.rms();
        h.push_note_off(/*id=*/200);
        loader.process_audio(inst, &h.ctx);
        h.clear_notes();
        loader.process_audio(inst, &h.ctx);
        const float release_rms = h.rms();
        check(release_rms < sustain_rms, "release-tail RMS lower than sustained RMS");
        std::fprintf(stderr, "  sustain RMS: %.4f, release-tail RMS: %.4f\n",
                     sustain_rms, release_rms);
        loader.destroy_instance(inst);
    }

    // ---------------------------------------------------------------------
    // Test 3: polyphonic chord — 3 simultaneous notes with distinct ids.
    // ---------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- FmSynth: polyphonic chord (3 voices, distinct ids) ---\n");
        reset_lane_states();
        Harness h;
        h.enable_note_input();
        auto params = make_params(desc, {
            {"attack", 0.001f}, {"decay", 0.05f}, {"sustain", 0.8f}, {"release", 0.1f},
            {"amplitude", 0.3f}, {"mod_index", 1.0f},
        });
        h.ctx.param_values = params.data();

        void* inst1 = loader.create_instance();
        h.push_note_on(60, 100.0f / 127.0f, /*id=*/300);
        loader.process_audio(inst1, &h.ctx);
        h.clear_notes();
        loader.process_audio(inst1, &h.ctx);
        const float mono_rms = h.rms();
        loader.destroy_instance(inst1);

        h.zero_output();
        void* inst2 = loader.create_instance();
        h.push_note_on(60, 100.0f / 127.0f, /*id=*/400);
        h.push_note_on(64, 100.0f / 127.0f, /*id=*/401);
        h.push_note_on(67, 100.0f / 127.0f, /*id=*/402);
        loader.process_audio(inst2, &h.ctx);
        h.clear_notes();
        loader.process_audio(inst2, &h.ctx);
        const float chord_rms = h.rms();
        check(chord_rms > 1.5f * mono_rms,
              "chord RMS > 1.5x mono RMS (polyphonic summing)");
        check(h.all_finite(), "chord output finite");
        std::fprintf(stderr, "  mono RMS: %.4f, chord RMS: %.4f\n", mono_rms, chord_rms);
        loader.destroy_instance(inst2);
    }

    // ---------------------------------------------------------------------
    // Test 4: legacy lane-array path still works when notes_in is absent.
    // ---------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- FmSynth: legacy lane-array path (notes_in absent) ---\n");
        reset_lane_states();
        Harness h;
        h.disable_note_input();
        // Port order: freq_cv(0), mod_index_cv(1), gate_cv(2),
        //             gates(3), notes(4), velocities(5)
        float gate_data[1]  = {1.0f};
        float note_data[1]  = {60.0f};
        float vel_data[1]   = {1.0f};
        VividLaneView lanes[6] = {};
        lanes[3].data = gate_data; lanes[3].length = 1;
        lanes[4].data = note_data; lanes[4].length = 1;
        lanes[5].data = vel_data;  lanes[5].length = 1;
        h.ctx.input_lanes = lanes;

        auto params = make_params(desc, {
            {"attack", 0.001f}, {"decay", 0.05f}, {"sustain", 0.9f}, {"release", 0.1f},
            {"amplitude", 0.5f}, {"mod_index", 1.0f},
        });
        h.ctx.param_values = params.data();

        void* inst = loader.create_instance();
        loader.process_audio(inst, &h.ctx);
        check(h.any_finite_nonzero(), "lane-array path produces audio");
        check(h.all_finite(), "lane-array output finite");
        std::fprintf(stderr, "  lane RMS: %.4f\n", h.rms());
        loader.destroy_instance(inst);
    }

    // ---------------------------------------------------------------------
    // Test 5: silent when no input.
    // ---------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- FmSynth: silent with no input ---\n");
        reset_lane_states();
        Harness h;
        h.enable_note_input();
        auto params = make_params(desc);
        h.ctx.param_values = params.data();
        void* inst = loader.create_instance();
        loader.process_audio(inst, &h.ctx);
        check(h.all_finite(), "output finite when no notes");
        check(h.rms() < 1e-4f, "output is silent with no notes");
        loader.destroy_instance(inst);
    }

    // ---------------------------------------------------------------------
    // Test 6: per-note pitch-bend is consumed (audio stays finite + audible).
    // ---------------------------------------------------------------------
    {
        std::fprintf(stderr, "\n--- FmSynth: per-note pitch-bend ---\n");
        reset_lane_states();
        Harness h;
        h.enable_note_input();
        auto params = make_params(desc, {
            {"attack", 0.001f}, {"decay", 0.05f}, {"sustain", 0.9f}, {"release", 0.1f},
            {"amplitude", 0.5f}, {"mod_index", 1.0f},
        });
        h.ctx.param_values = params.data();
        void* inst = loader.create_instance();

        h.push_note_on(60, 100.0f / 127.0f, /*id=*/500);
        loader.process_audio(inst, &h.ctx);
        h.clear_notes();
        // Bend the held note up an octave; audio should stay valid.
        h.push_pitch_bend(/*id=*/500, /*semis=*/12.0f);
        loader.process_audio(inst, &h.ctx);
        check(h.all_finite(), "output stays finite after pitch_bend");
        check(h.any_finite_nonzero(), "output stays audible after pitch_bend");
        std::fprintf(stderr, "  bent RMS: %.4f\n", h.rms());
        loader.destroy_instance(inst);
    }

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
