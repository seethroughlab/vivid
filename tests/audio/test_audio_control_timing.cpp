#include "runtime/operators/operator_loader.h"
#include "operator_api/note_types.h"
#include "operator_api/metronome_sync.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include "test_helpers.h"

struct AudioTestBuffers {
    std::vector<std::vector<float>> inputs;
    std::vector<std::vector<float>> outputs;
    std::vector<float*> input_ptrs;
    std::vector<float*> output_ptrs;
    std::vector<void*> custom_outputs;
    VividAudioContext ctx{};

    AudioTestBuffers(uint32_t input_port_count, uint32_t output_port_count,
                     uint32_t custom_output_count = 0, uint32_t buffer_size = 8,
                     uint32_t sample_rate = 48000)
        : inputs(input_port_count, std::vector<float>(buffer_size, 0.0f))
        , outputs(output_port_count, std::vector<float>(buffer_size, 0.0f))
        , input_ptrs(input_port_count, nullptr)
        , output_ptrs(output_port_count, nullptr)
        , custom_outputs(custom_output_count, nullptr) {
        for (uint32_t i = 0; i < input_port_count; ++i) input_ptrs[i] = inputs[i].data();
        for (uint32_t i = 0; i < output_port_count; ++i) output_ptrs[i] = outputs[i].data();

        ctx.input_buffers = input_ptrs.empty() ? nullptr : input_ptrs.data();
        ctx.output_buffers = output_ptrs.empty() ? nullptr : output_ptrs.data();
        ctx.custom_outputs = custom_outputs.empty() ? nullptr : custom_outputs.data();
        ctx.custom_output_count = custom_output_count;
        ctx.buffer_size = buffer_size;
        ctx.sample_rate = sample_rate;
        ctx.delta_time = static_cast<double>(buffer_size) / static_cast<double>(sample_rate);
    }
};

static void set_audio_metronome(AudioTestBuffers& tb, float bpm,
                                uint32_t beats_per_bar, double beats_elapsed,
                                float beat_phase, float bar_phase) {
    tb.ctx.metronome_bpm = bpm;
    tb.ctx.metronome_beats_per_bar = beats_per_bar;
    tb.ctx.metronome_beats_elapsed = beats_elapsed;
    tb.ctx.metronome_beat_phase = beat_phase;
    tb.ctx.metronome_bar_phase = bar_phase;
    tb.ctx.metronome_beat_ms = bpm > 0.0f ? 60000.0f / bpm : 0.0f;
}

static std::vector<float> default_params(const VividOperatorDescriptor* desc) {
    std::vector<float> params(desc ? desc->param_count : 0, 0.0f);
    if (!desc) return params;
    for (uint32_t i = 0; i < desc->param_count; ++i) {
        params[i] = desc->params[i].default_value;
    }
    return params;
}

static int find_param(const VividOperatorDescriptor* desc, const char* name) {
    if (!desc) return -1;
    for (uint32_t i = 0; i < desc->param_count; ++i) {
        if (std::strcmp(desc->params[i].name, name) == 0) return static_cast<int>(i);
    }
    return -1;
}

static void set_param_by_name(std::vector<float>& params, const VividOperatorDescriptor* desc,
                              const char* name, float value) {
    int idx = find_param(desc, name);
    std::string msg = std::string("find param ") + name;
    check(idx >= 0, msg.c_str());
    if (idx >= 0 && static_cast<size_t>(idx) < params.size()) {
        params[static_cast<size_t>(idx)] = value;
    }
}

static void test_gate_short_pulse(const std::string& build_dir) {
    std::fprintf(stderr, "\n--- gate preserves short gate pulses ---\n");
    vivid::OperatorLoader loader;
    check(loader.load((build_dir + "/gate.dylib").c_str()), "load gate");
    if (!loader.is_loaded()) return;

    void* inst = loader.create_instance();
    check(inst != nullptr, "create gate instance");
    if (!inst) return;

    auto params = default_params(loader.descriptor());
    AudioTestBuffers tb(2, 2);
    tb.ctx.param_values = params.data();
    tb.inputs[0].assign(tb.ctx.buffer_size, 1.0f);
    tb.inputs[1][0] = 1.0f;

    loader.process_audio(inst, &tb.ctx);

    check_float(tb.outputs[0][0], 1.0f, 1e-5f, "gate passes the signal during the early pulse");
    check_float(tb.outputs[0][1], 0.0f, 1e-5f, "gate closes again after the pulse");
    check_float(tb.outputs[1][0], 1.0f, 1e-5f, "open output is high during the pulse");
    check_float(tb.outputs[1][1], 0.0f, 1e-5f, "open output returns low after the pulse");

    loader.destroy_instance(inst);
}

static void test_sample_hold_short_trigger(const std::string& build_dir) {
    std::fprintf(stderr, "\n--- sample_hold captures a short trigger ---\n");
    vivid::OperatorLoader loader;
    check(loader.load((build_dir + "/sample_hold.dylib").c_str()), "load sample_hold");
    if (!loader.is_loaded()) return;

    void* inst = loader.create_instance();
    check(inst != nullptr, "create sample_hold instance");
    if (!inst) return;

    auto params = default_params(loader.descriptor());
    AudioTestBuffers tb(2, 1);
    tb.ctx.param_values = params.data();
    tb.inputs[0][1] = 0.75f;
    tb.inputs[1][1] = 1.0f;

    loader.process_audio(inst, &tb.ctx);

    check_float(tb.outputs[0][0], 0.0f, 1e-5f, "held value starts at zero before the trigger");
    check_float(tb.outputs[0][1], 0.75f, 1e-5f, "short trigger captures the signal immediately");
    check_float(tb.outputs[0][7], 0.75f, 1e-5f, "captured value persists after the trigger ends");

    loader.destroy_instance(inst);
}

static void test_step_counter_short_trigger(const std::string& build_dir) {
    std::fprintf(stderr, "\n--- step_counter advances on a short trigger ---\n");
    vivid::OperatorLoader loader;
    check(loader.load((build_dir + "/step_counter.dylib").c_str()), "load step_counter");
    if (!loader.is_loaded()) return;

    void* inst = loader.create_instance();
    check(inst != nullptr, "create step_counter instance");
    if (!inst) return;

    auto params = default_params(loader.descriptor());
    AudioTestBuffers tb(3, 2);
    tb.ctx.param_values = params.data();
    tb.inputs[0][2] = 1.0f;
    for (float& v : tb.inputs[1]) v = 4.0f;

    loader.process_audio(inst, &tb.ctx);

    check_float(tb.outputs[0][1], 0.0f, 1e-5f, "counter stays at the initial step before the pulse");
    check_float(tb.outputs[0][2], 1.0f, 1e-5f, "counter increments on the short trigger sample");
    check_float(tb.outputs[0][7], 1.0f, 1e-5f, "incremented step persists after the pulse");

    loader.destroy_instance(inst);
}

static void test_phase_to_midi_midblock_wrap(const std::string& build_dir) {
    std::fprintf(stderr, "\n--- phase_to_midi preserves mid-block timing ---\n");
    vivid::OperatorLoader loader;
    check(loader.load((build_dir + "/phase_to_midi.dylib").c_str()), "load phase_to_midi");
    if (!loader.is_loaded()) return;

    void* inst = loader.create_instance();
    check(inst != nullptr, "create phase_to_midi instance");
    if (!inst) return;

    auto params = default_params(loader.descriptor());
    // This test exercises external-clock block-start sampling, so opt into the
    // external beat_phase input (operators default to metronome since the
    // transport-lock change in 17aa940d).
    set_param_by_name(params, loader.descriptor(), "clock_mode",
                      static_cast<float>(vivid::kClockModeSyncedExternal));
    AudioTestBuffers tb(1, 0, 1);
    tb.ctx.param_values = params.data();
    tb.inputs[0] = {0.70f, 0.80f, 0.90f, 0.05f, 0.10f, 0.20f, 0.30f, 0.40f};

    loader.process_audio(inst, &tb.ctx);

    auto* notes = static_cast<VividNoteBuffer*>(tb.custom_outputs[0]);
    check(notes != nullptr, "phase_to_midi publishes a note buffer");
    if (notes) {
        check(notes->count == 1, "exactly one wrap event is emitted");
        if (notes->count == 1) {
            check(notes->events[0].type == VIVID_NOTE_ON,
                  "wrap event is a NOTE_ON");
            check(notes->events[0].frame_offset_samples == 3,
                  "note event keeps the in-buffer wrap offset");
            check(notes->events[0].note_id != 0,
                  "wrap event carries a non-zero note_id");
        }
    }

    loader.destroy_instance(inst);
}

static void test_step_seq_block_start_snapshot(const std::string& build_dir) {
    std::fprintf(stderr, "\n--- sequencer uses block-start beat phase ---\n");
    vivid::OperatorLoader loader;
    check(loader.load((build_dir + "/sequencer.dylib").c_str()), "load sequencer");
    if (!loader.is_loaded()) return;

    void* inst = loader.create_instance();
    check(inst != nullptr, "create sequencer instance");
    if (!inst) return;

    const auto* desc = loader.descriptor();
    auto params = default_params(desc);
    set_param_by_name(params, desc, "source", 0.0f);
    set_param_by_name(params, desc, "steps", 4.0f);
    set_param_by_name(params, desc, "step_value_0", 0.2f);
    set_param_by_name(params, desc, "step_value_2", 0.8f);
    set_param_by_name(params, desc, "clock_mode", 1.0f);
    set_param_by_name(params, desc, "frequency", 1.0f);
    set_param_by_name(params, desc, "sync_division", 2.0f);
    set_param_by_name(params, desc, "glide", 0.0f);
    set_param_by_name(params, desc, "amplitude", 1.0f);
    set_param_by_name(params, desc, "offset", 0.0f);
    set_param_by_name(params, desc, "polarity", 1.0f);

    AudioTestBuffers tb(3, 3);
    tb.ctx.param_values = params.data();
    tb.inputs[0] = {0.10f, 0.20f, 0.30f, 0.40f, 0.45f, 0.50f, 0.55f, 0.60f};

    loader.process_audio(inst, &tb.ctx);

    check_float(tb.outputs[0][0], 0.2f, 1e-5f, "sequencer chooses sample 0 instead of the final sample");
    check_float(tb.outputs[0][7], 0.2f, 1e-5f, "sequencer output stays block-constant");

    loader.destroy_instance(inst);
}

static void test_step_seq_metronome_snapshot(const std::string& build_dir) {
    std::fprintf(stderr, "\n--- sequencer locks to graph metronome without a beat-phase wire ---\n");
    vivid::OperatorLoader loader;
    check(loader.load((build_dir + "/sequencer.dylib").c_str()), "load sequencer");
    if (!loader.is_loaded()) return;

    void* inst = loader.create_instance();
    check(inst != nullptr, "create sequencer instance");
    if (!inst) return;

    const auto* desc = loader.descriptor();
    auto params = default_params(desc);
    set_param_by_name(params, desc, "source", 0.0f);
    set_param_by_name(params, desc, "steps", 4.0f);
    set_param_by_name(params, desc, "step_value_0", 0.2f);
    set_param_by_name(params, desc, "step_value_2", 0.8f);
    set_param_by_name(params, desc, "clock_mode", 2.0f);
    set_param_by_name(params, desc, "sync_division", 2.0f);
    set_param_by_name(params, desc, "amplitude", 1.0f);
    set_param_by_name(params, desc, "polarity", 1.0f);

    AudioTestBuffers tb(3, 3);
    tb.ctx.param_values = params.data();
    // With per-step division timing: 4 steps at 1/4 notes (1 beat each) = 4 beat cycle.
    // beats_elapsed=2.5 → phase=2.5/4=0.625 → step=int(0.625*4)=2 → step_value_2=0.8
    set_audio_metronome(tb, 120.0f, 4, 2.50, 0.50f, 0.125f);

    loader.process_audio(inst, &tb.ctx);

    check_float(tb.outputs[0][0], 0.8f, 1e-5f, "metronome mode selects the step from graph beat phase");
    check_float(tb.outputs[0][7], 0.8f, 1e-5f, "metronome-synced output stays block-constant");

    loader.destroy_instance(inst);
}

static void test_euclidean_block_start_snapshot(const std::string& build_dir) {
    std::fprintf(stderr, "\n--- euclidean avoids block-end lookahead ---\n");
    vivid::OperatorLoader loader;
    check(loader.load((build_dir + "/euclidean.dylib").c_str()), "load euclidean");
    if (!loader.is_loaded()) return;

    void* inst = loader.create_instance();
    check(inst != nullptr, "create euclidean instance");
    if (!inst) return;

    auto params = default_params(loader.descriptor());
    params[0] = 1.0f;  // hits
    params[1] = 8.0f;  // steps
    params[2] = 0.0f;  // rotation
    params[3] = 0.5f;  // gate_length
    params[4] = 5.0f;  // sync_division = 1/32 -> multiplier 8
    params[5] = static_cast<float>(vivid::kClockModeSyncedExternal);  // external clock (default is metronome)

    AudioTestBuffers tb(1, 3);
    tb.ctx.param_values = params.data();

    for (float& v : tb.inputs[0]) v = 0.9f;
    loader.process_audio(inst, &tb.ctx);

    tb.inputs[0] = {0.90f, 0.85f, 0.80f, 0.70f, 0.50f, 0.30f, 0.20f, 0.10f};
    loader.process_audio(inst, &tb.ctx);
    check_float(tb.outputs[2][0], 7.0f, 1e-5f, "stable beat phase keeps the current step");

    tb.inputs[0] = {0.90f, 0.80f, 0.70f, 0.50f, 0.30f, 0.20f, 0.15f, 0.10f};
    loader.process_audio(inst, &tb.ctx);
    check_float(tb.outputs[2][0], 7.0f, 1e-5f, "sample 0 wins even when the final sample crosses a beat wrap");

    loader.destroy_instance(inst);
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    std::fprintf(stderr, "\n=== Test: Audio Control Timing ===\n");

    test_gate_short_pulse(build_dir);
    test_sample_hold_short_trigger(build_dir);
    test_step_counter_short_trigger(build_dir);
    test_phase_to_midi_midblock_wrap(build_dir);
    test_step_seq_block_start_snapshot(build_dir);
    test_step_seq_metronome_snapshot(build_dir);
    test_euclidean_block_start_snapshot(build_dir);

    std::fprintf(stderr, "\n%s (%d failure%s)\n\n",
                 failures ? "FAILED" : "PASSED", failures, failures == 1 ? "" : "s");
    return failures ? 1 : 0;
}
