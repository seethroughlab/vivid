#include "operator_api/note_types.h"
#include "operator_api/types.h"
#include "runtime/operators/operator_loader.h"

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
constexpr int kMaxVoices = 16;

struct LaneOutBuf {
    std::vector<float> data;
    static float* resize_cb(void* h, uint32_t len) {
        auto* self = static_cast<LaneOutBuf*>(h);
        self->data.assign(len, 0.0f);
        return self->data.data();
    }
    static void commit_cb(void* /*h*/, uint32_t /*len*/) {}
};

struct NoteBreakoutHarness {
    LaneOutBuf voice_ids_buf;
    LaneOutBuf voice_gates_buf;
    LaneOutBuf voice_velocities_buf;
    LaneOutBuf voice_freqs_buf;
    VividLaneOutput lane_outputs[4] = {};

    VividNoteBuffer notes{};
    void* custom_inputs[1] = {&notes};
    VividAudioContext ctx{};

    NoteBreakoutHarness() {
        ctx.sample_rate = kSampleRate;
        ctx.buffer_size = kFrames;
        ctx.custom_inputs = custom_inputs;
        ctx.custom_input_count = 1;

        lane_outputs[0] = {&voice_ids_buf, LaneOutBuf::resize_cb, LaneOutBuf::commit_cb};
        lane_outputs[1] = {&voice_gates_buf, LaneOutBuf::resize_cb, LaneOutBuf::commit_cb};
        lane_outputs[2] = {&voice_velocities_buf, LaneOutBuf::resize_cb, LaneOutBuf::commit_cb};
        lane_outputs[3] = {&voice_freqs_buf, LaneOutBuf::resize_cb, LaneOutBuf::commit_cb};
        ctx.output_lanes = lane_outputs;
    }

    void clear_notes() { notes.count = 0; }

    void push_note_on(uint8_t note, float vel_0_1, uint64_t id) {
        auto& e = notes.events[notes.count++];
        e.type = VIVID_NOTE_ON;
        e.note_number = note;
        e.value = vel_0_1;
        e.note_id = id;
    }

    void push_note_off(uint64_t id) {
        auto& e = notes.events[notes.count++];
        e.type = VIVID_NOTE_OFF;
        e.note_id = id;
    }
};

struct EnvelopeHarness {
    float beat_phase[kFrames] = {};
    float output[kMaxVoices * kFrames] = {};
    float* input_buffers[3] = {nullptr, nullptr, beat_phase};
    float* output_buffers[1] = {output};
    VividLaneView input_lanes[2] = {};
    VividAudioContext ctx{};

    EnvelopeHarness() {
        ctx.sample_rate = kSampleRate;
        ctx.buffer_size = kFrames;
        ctx.input_buffers = input_buffers;
        ctx.output_buffers = output_buffers;
        ctx.input_lanes = input_lanes;
    }

    void bind_from(const NoteBreakoutHarness& src) {
        input_lanes[0].data = src.voice_gates_buf.data.empty() ? nullptr : src.voice_gates_buf.data.data();
        input_lanes[0].length = static_cast<uint32_t>(src.voice_gates_buf.data.size());
        input_lanes[1].data = src.voice_ids_buf.data.empty() ? nullptr : src.voice_ids_buf.data.data();
        input_lanes[1].length = static_cast<uint32_t>(src.voice_ids_buf.data.size());
    }

    void clear_output() {
        std::memset(output, 0, sizeof(output));
    }

    float channel_rms(int ch) const {
        const float* p = output + ch * kFrames;
        double sum = 0.0;
        for (int i = 0; i < kFrames; ++i) sum += p[i] * p[i];
        return static_cast<float>(std::sqrt(sum / kFrames));
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

}  // namespace

int main(int argc, char** argv) {
    const std::string build_dir = (argc > 1) ? argv[1] : ".";
    const std::string note_breakout_path = build_dir + "/note_breakout.dylib";
    const std::string envelope_path = build_dir + "/envelope.dylib";

    if (!std::filesystem::exists(note_breakout_path) || !std::filesystem::exists(envelope_path)) {
        std::fprintf(stderr, "FATAL: required dylib missing\n");
        return 1;
    }

    vivid::OperatorLoader note_loader;
    vivid::OperatorLoader env_loader;
    check(note_loader.load(note_breakout_path.c_str()), "note_breakout dylib loads");
    check(env_loader.load(envelope_path.c_str()), "envelope dylib loads");
    if (!note_loader.is_loaded() || !env_loader.is_loaded()) return 1;

    const auto* env_desc = env_loader.descriptor();
    check(env_desc != nullptr, "Envelope descriptor not null");
    if (!env_desc) return 1;

    auto env_params = make_params(env_desc, {
        {"attack", 0.001f}, {"decay", 0.005f}, {"sustain", 0.0f}, {"release", 0.03f},
        {"amplitude", 1.0f}, {"offset", 0.0f},
    });

    {
        std::fprintf(stderr, "\n--- NoteBreakout -> Envelope preserves release when lanes shrink ---\n");

        NoteBreakoutHarness note_h;
        EnvelopeHarness env_h;
        env_h.ctx.param_values = env_params.data();

        void* note_inst = note_loader.create_instance();
        void* env_inst = env_loader.create_instance();
        check(note_inst != nullptr, "note_breakout instance created");
        check(env_inst != nullptr, "envelope instance created");
        if (!note_inst || !env_inst) return 1;

        // Block 1: held note appears on the breakout lanes.
        note_h.push_note_on(60, 1.0f, /*id=*/100);
        note_loader.process_audio(note_inst, &note_h.ctx);
        env_h.bind_from(note_h);
        env_h.clear_output();
        env_loader.process_audio(env_inst, &env_h.ctx);
        check(note_h.voice_ids_buf.data.size() == 1, "breakout emits one held voice");
        check(env_h.channel_rms(0) > 0.01f, "envelope output is audible while held");

        // Block 2: NoteBreakout removes the released voice from its current lanes.
        note_h.clear_notes();
        note_h.push_note_off(/*id=*/100);
        note_loader.process_audio(note_inst, &note_h.ctx);
        env_h.bind_from(note_h);
        env_h.clear_output();
        env_loader.process_audio(env_inst, &env_h.ctx);
        check(note_h.voice_ids_buf.data.empty(), "NoteBreakout drops released voice from current lanes");
        check(env_h.channel_rms(0) > 1e-4f,
              "Envelope keeps rendering release after input lanes shrink away");

        // After enough release blocks, the remembered lane should disappear.
        float tail_rms = env_h.channel_rms(0);
        bool became_silent = false;
        for (int i = 0; i < 16; ++i) {
            note_h.clear_notes();
            note_loader.process_audio(note_inst, &note_h.ctx);
            env_h.bind_from(note_h);
            env_h.clear_output();
            env_loader.process_audio(env_inst, &env_h.ctx);
            if (env_h.channel_rms(0) < 1e-5f) {
                became_silent = true;
                break;
            }
        }
        check(tail_rms > 0.0f, "release block captured non-zero tail");
        check(became_silent, "remembered envelope lane eventually decays to silence");

        note_loader.destroy_instance(note_inst);
        env_loader.destroy_instance(env_inst);
    }

    std::fprintf(stderr, "\n%s (%d failures)\n",
                 failures == 0 ? "PASSED" : "FAILED", failures);
    return failures == 0 ? 0 : 1;
}
