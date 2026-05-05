// Synth voice breakout tests.
//
// Verifies that every MIDI-driven core synth (FmSynth, Sampler, SP404,
// Slicer) emits the standardized advanced breakout surface — voices_out
// (multichannel audio, kMaxVoices channels, mono per voice) plus the four
// voice_* control lanes — with active voices in note_id-sorted order, and
// that voices_out channel `i` matches the voice at voice_ids[i].
//
// FmSynth is the simplest case: a sine carrier per voice produces audible
// audio without a sample bank. Sampler/SP404/Slicer require a sample
// bank; we exercise FmSynth here for the per-channel audio assertions and
// add lighter coverage for the others (port presence + port advanced flag).

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
constexpr int kFmMaxVoices = 8;

struct LaneOutBuf {
    std::vector<float> data;
    static float* resize_cb(void* h, uint32_t len) {
        auto* self = static_cast<LaneOutBuf*>(h);
        self->data.assign(len, 0.0f);
        return self->data.data();
    }
    static void commit_cb(void* /*h*/, uint32_t /*len*/) {}
};

static void* stub_lane_state(void*, uint32_t, uint32_t) { return nullptr; }

struct FmHarness {
    float output[kFrames] = {};
    float voices_out[kFmMaxVoices * kFrames] = {};
    float scalar_in_freq = 0.0f;
    float scalar_in_mod  = 0.0f;
    float scalar_in_gate = 0.0f;
    float* output_bufs[2] = {output, voices_out};
    float* input_bufs[3]  = {&scalar_in_freq, &scalar_in_mod, &scalar_in_gate};

    LaneOutBuf voice_ids_buf, voice_gates_buf, voice_velocities_buf, voice_freqs_buf;
    // output_lanes is indexed by overall output port ordinal. FmSynth's
    // output ports: output(0), voices_out(1), voice_ids(2), voice_gates(3),
    // voice_velocities(4), voice_freqs(5), then analysis ports.
    // Slots 0-1 (audio buffer ports) are left zero/unused.
    VividLaneOutput lane_outputs[6] = {};

    VividNoteBuffer notes{};
    void* note_inputs[1] = {&notes};

    VividAudioContext ctx{};

    FmHarness() {
        ctx.sample_rate        = kSampleRate;
        ctx.buffer_size        = kFrames;
        ctx.input_buffers      = input_bufs;
        ctx.output_buffers     = output_bufs;
        ctx.lane_state_fn      = stub_lane_state;
        ctx.lane_state_service = nullptr;
        ctx.lane_id            = 1;
        ctx.custom_inputs      = note_inputs;
        ctx.custom_input_count = 1;

        // lane_outputs[0] = output audio port, lane_outputs[1] = voices_out — both unused/zero.
        lane_outputs[2] = {&voice_ids_buf,        LaneOutBuf::resize_cb, LaneOutBuf::commit_cb};
        lane_outputs[3] = {&voice_gates_buf,      LaneOutBuf::resize_cb, LaneOutBuf::commit_cb};
        lane_outputs[4] = {&voice_velocities_buf, LaneOutBuf::resize_cb, LaneOutBuf::commit_cb};
        lane_outputs[5] = {&voice_freqs_buf,      LaneOutBuf::resize_cb, LaneOutBuf::commit_cb};
        ctx.output_lanes = lane_outputs;
    }

    void clear_notes() { notes.count = 0; }
    void zero_output() { std::memset(output, 0, sizeof(output));
                         std::memset(voices_out, 0, sizeof(voices_out)); }

    void push_note_on(uint8_t note, float vel, uint64_t id, uint32_t offset = 0) {
        if (notes.count >= VIVID_NOTE_BUFFER_CAPACITY) return;
        auto& e = notes.events[notes.count++];
        e.type = VIVID_NOTE_ON; e.note_number = note; e.value = vel;
        e.note_id = id; e.frame_offset_samples = offset;
    }
    void push_note_off(uint64_t id) {
        if (notes.count >= VIVID_NOTE_BUFFER_CAPACITY) return;
        auto& e = notes.events[notes.count++];
        e.type = VIVID_NOTE_OFF; e.note_id = id;
    }

    float channel_rms(int ch) const {
        const float* p = voices_out + ch * kFrames;
        double s = 0.0;
        for (int i = 0; i < kFrames; ++i) s += p[i] * p[i];
        return static_cast<float>(std::sqrt(s / kFrames));
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

static const VividPortDescriptor* find_port(const VividOperatorDescriptor* desc, const char* name) {
    for (uint32_t p = 0; p < desc->port_count; ++p)
        if (std::strcmp(desc->ports[p].name, name) == 0) return &desc->ports[p];
    return nullptr;
}

static float midi_to_hz(float note) {
    return 440.0f * std::pow(2.0f, (note - 69.0f) / 12.0f);
}

// --- Static surface check applied to every synth ----------------------------
// All four MIDI-driven core synths must declare the same standardized
// breakout surface and tag every breakout port advanced.
static void check_breakout_surface(vivid::OperatorLoader& loader, const char* op_name) {
    std::fprintf(stderr, "\n--- %s: declares standardized breakout ports ---\n", op_name);
    const auto* desc = loader.descriptor();
    if (!desc) { ++failures; return; }
    for (const char* name : {"voices_out", "voice_ids", "voice_gates",
                              "voice_velocities", "voice_freqs"}) {
        const auto* p = find_port(desc, name);
        check(p != nullptr, (std::string(op_name) + " declares " + name).c_str());
        if (p) {
            check(p->display_hint == VIVID_PORT_DISPLAY_ADVANCED,
                  (std::string(op_name) + "/" + name + " tagged ADVANCED").c_str());
            check(p->direction == VIVID_PORT_OUTPUT,
                  (std::string(op_name) + "/" + name + " is OUTPUT").c_str());
        }
    }
    const auto* voices_out_p = find_port(desc, "voices_out");
    if (voices_out_p) {
        check(voices_out_p->type == VIVID_PORT_AUDIO_BUFFER,
              (std::string(op_name) + "/voices_out is AUDIO_BUFFER").c_str());
        check(voices_out_p->channels >= 2,
              (std::string(op_name) + "/voices_out has multichannel layout").c_str());
    }
}

}  // namespace

int main(int argc, char** argv) {
    const std::string build_dir = (argc > 1) ? argv[1] : ".";

    // Static surface check on all four synths.
    for (const char* op_name : {"fm_synth", "sampler", "sp404", "slicer"}) {
        const std::string dylib_path = build_dir + "/" + op_name + ".dylib";
        if (!std::filesystem::exists(dylib_path)) {
            std::fprintf(stderr, "FATAL: %s not found\n", dylib_path.c_str());
            ++failures; continue;
        }
        vivid::OperatorLoader loader;
        if (!loader.load(dylib_path.c_str())) {
            std::fprintf(stderr, "FATAL: failed to load %s\n", dylib_path.c_str());
            ++failures; continue;
        }
        check_breakout_surface(loader, op_name);
    }

    // Behavioral check on FmSynth: a 3-note chord produces non-silent audio
    // on voices_out channels 0..2, voice_ids ascending, voice_freqs match
    // each voice's held note.
    {
        std::fprintf(stderr, "\n--- FmSynth: chord produces aligned voices_out + voice_* lanes ---\n");
        const std::string dylib_path = build_dir + "/fm_synth.dylib";
        vivid::OperatorLoader loader;
        if (!loader.load(dylib_path.c_str())) {
            std::fprintf(stderr, "FATAL: failed to load fm_synth\n");
            return 1;
        }
        const auto* desc = loader.descriptor();
        auto params = make_params(desc, {
            {"attack", 0.001f}, {"decay", 0.01f}, {"sustain", 0.9f}, {"release", 0.05f},
            {"amplitude", 0.5f}, {"mod_index", 1.0f},
        });

        FmHarness h;
        h.ctx.param_values = params.data();

        void* inst = loader.create_instance();
        // Three notes with monotonically increasing ids → voice_ids must
        // be in [10, 20, 30] order regardless of internal slot order.
        h.push_note_on(60, 100.0f / 127.0f, /*id=*/10);
        h.push_note_on(64, 100.0f / 127.0f, /*id=*/20);
        h.push_note_on(67, 100.0f / 127.0f, /*id=*/30);
        loader.process_audio(inst, &h.ctx);
        h.clear_notes();
        h.zero_output();
        loader.process_audio(inst, &h.ctx);  // sustain

        check(h.voice_ids_buf.data.size() == 3, "voice_ids length = 3");
        if (h.voice_ids_buf.data.size() == 3) {
            check_float(h.voice_ids_buf.data[0], 10.0f, 1e-3f, "voice_ids[0] = 10");
            check_float(h.voice_ids_buf.data[1], 20.0f, 1e-3f, "voice_ids[1] = 20");
            check_float(h.voice_ids_buf.data[2], 30.0f, 1e-3f, "voice_ids[2] = 30");
            check_float(h.voice_freqs_buf.data[0], midi_to_hz(60.0f), 1e-1f, "voice_freqs[0] ≈ C4");
            check_float(h.voice_freqs_buf.data[1], midi_to_hz(64.0f), 1e-1f, "voice_freqs[1] ≈ E4");
            check_float(h.voice_freqs_buf.data[2], midi_to_hz(67.0f), 1e-1f, "voice_freqs[2] ≈ G4");
            check_float(h.voice_gates_buf.data[0], 1.0f, "voice_gates[0] held");
            check_float(h.voice_gates_buf.data[1], 1.0f, "voice_gates[1] held");
            check_float(h.voice_gates_buf.data[2], 1.0f, "voice_gates[2] held");
        }

        // voices_out channels 0..2 should be non-silent (the three voices).
        // Channels 3..7 are inactive and stay near silent.
        check(h.channel_rms(0) > 0.01f, "voices_out channel 0 audible");
        check(h.channel_rms(1) > 0.01f, "voices_out channel 1 audible");
        check(h.channel_rms(2) > 0.01f, "voices_out channel 2 audible");
        check(h.channel_rms(3) < 1e-4f, "voices_out channel 3 silent (no voice)");
        check(h.channel_rms(7) < 1e-4f, "voices_out channel 7 silent (no voice)");

        loader.destroy_instance(inst);
    }

    // Stealing test on FmSynth: fill the 8-voice pool with monotonically
    // increasing ids, then send a 9th. The oldest (id=1) gets stolen, so
    // ids 2..8 + 9 remain — voice_ids stays sorted ascending.
    {
        std::fprintf(stderr, "\n--- FmSynth: stealing keeps voice_ids ascending ---\n");
        const std::string dylib_path = build_dir + "/fm_synth.dylib";
        vivid::OperatorLoader loader;
        if (!loader.load(dylib_path.c_str())) return 1;
        const auto* desc = loader.descriptor();
        auto params = make_params(desc, {
            {"attack", 0.001f}, {"decay", 0.01f}, {"sustain", 0.9f}, {"release", 0.05f},
            {"amplitude", 0.5f}, {"mod_index", 1.0f},
        });

        FmHarness h;
        h.ctx.param_values = params.data();
        void* inst = loader.create_instance();

        // Allocate all 8 voices (notes 60..67, ids 1..8).
        for (int i = 0; i < 8; ++i)
            h.push_note_on(static_cast<uint8_t>(60 + i),
                           100.0f / 127.0f,
                           static_cast<uint64_t>(i + 1));
        loader.process_audio(inst, &h.ctx);
        h.clear_notes();
        // Force stealing with a 9th note (id=9).
        h.push_note_on(72, 100.0f / 127.0f, /*id=*/9);
        loader.process_audio(inst, &h.ctx);

        check(h.voice_ids_buf.data.size() == 8u, "still 8 active voices after stealing");
        if (h.voice_ids_buf.data.size() == 8u) {
            // Whatever the surviving id set is, voice_ids must be sorted ascending.
            for (size_t i = 1; i < h.voice_ids_buf.data.size(); ++i) {
                check(h.voice_ids_buf.data[i] > h.voice_ids_buf.data[i - 1],
                      "voice_ids strictly ascending");
            }
            // The newest note (id=9) must be present.
            bool found_9 = false;
            for (float v : h.voice_ids_buf.data)
                if (std::fabs(v - 9.0f) < 1e-3f) { found_9 = true; break; }
            check(found_9, "newest note id=9 present after stealing");
        }
        loader.destroy_instance(inst);
    }

    // Mid-block release regression: if a voice reaches IDLE during this
    // render block, voices_out and voice_* lanes must still describe the
    // same per-block breakout set until emission finishes.
    {
        std::fprintf(stderr, "\n--- FmSynth: released voice stays aligned for the rest of the block ---\n");
        const std::string dylib_path = build_dir + "/fm_synth.dylib";
        vivid::OperatorLoader loader;
        if (!loader.load(dylib_path.c_str())) return 1;
        const auto* desc = loader.descriptor();
        auto params = make_params(desc, {
            {"attack", 0.001f}, {"decay", 0.01f}, {"sustain", 0.9f}, {"release", 0.001f},
            {"amplitude", 0.5f}, {"mod_index", 1.0f},
        });

        FmHarness h;
        h.ctx.param_values = params.data();
        void* inst = loader.create_instance();

        h.push_note_on(60, 100.0f / 127.0f, /*id=*/42);
        loader.process_audio(inst, &h.ctx);

        h.clear_notes();
        h.zero_output();
        h.push_note_off(/*id=*/42);
        loader.process_audio(inst, &h.ctx);

        check(h.channel_rms(0) > 1e-4f, "voices_out release channel still carries audio this block");
        check(h.voice_ids_buf.data.size() == 1u, "released voice still present in breakout lanes for this block");
        if (h.voice_ids_buf.data.size() == 1u) {
            check_float(h.voice_ids_buf.data[0], 42.0f, 1e-3f, "voice_ids[0] keeps released voice id");
        }
        check(h.voice_gates_buf.data.size() == 1u, "voice_gates stays aligned with released voice");
        if (h.voice_gates_buf.data.size() == 1u) {
            check_float(h.voice_gates_buf.data[0], 0.0f, 1e-3f, "released voice gate falls to 0");
        }

        h.clear_notes();
        h.zero_output();
        loader.process_audio(inst, &h.ctx);
        check(h.voice_ids_buf.data.empty(), "released voice removed on later block after emission completes");

        loader.destroy_instance(inst);
    }

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
