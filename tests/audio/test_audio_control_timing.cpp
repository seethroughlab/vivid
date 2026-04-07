#include "runtime/operators/operator_loader.h"
#include "operator_api/midi_types.h"

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

static void test_gate_short_pulse(const std::string& build_dir) {
    std::fprintf(stderr, "\n--- gate_au preserves short gate pulses ---\n");
    vivid::OperatorLoader loader;
    check(loader.load((build_dir + "/gate_au.dylib").c_str()), "load gate_au");
    if (!loader.is_loaded()) return;

    void* inst = loader.create_instance();
    check(inst != nullptr, "create gate_au instance");
    if (!inst) return;

    auto params = default_params(loader.descriptor());
    AudioTestBuffers tb(2, 2);
    tb.ctx.param_values = params.data();
    tb.inputs[0].assign(tb.ctx.buffer_size, 1.0f);
    tb.inputs[1][0] = 1.0f;

    loader.process_audio(inst, &tb.ctx);

    check_float(tb.outputs[0][0], 1.0f, 1e-5f, "gate_au passes the signal during the early pulse");
    check_float(tb.outputs[0][1], 0.0f, 1e-5f, "gate_au closes again after the pulse");
    check_float(tb.outputs[1][0], 1.0f, 1e-5f, "open output is high during the pulse");
    check_float(tb.outputs[1][1], 0.0f, 1e-5f, "open output returns low after the pulse");

    loader.destroy_instance(inst);
}

static void test_sample_hold_short_trigger(const std::string& build_dir) {
    std::fprintf(stderr, "\n--- sample_hold_au captures a short trigger ---\n");
    vivid::OperatorLoader loader;
    check(loader.load((build_dir + "/sample_hold_au.dylib").c_str()), "load sample_hold_au");
    if (!loader.is_loaded()) return;

    void* inst = loader.create_instance();
    check(inst != nullptr, "create sample_hold_au instance");
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
    std::fprintf(stderr, "\n--- step_counter_au advances on a short trigger ---\n");
    vivid::OperatorLoader loader;
    check(loader.load((build_dir + "/step_counter_au.dylib").c_str()), "load step_counter_au");
    if (!loader.is_loaded()) return;

    void* inst = loader.create_instance();
    check(inst != nullptr, "create step_counter_au instance");
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
    std::fprintf(stderr, "\n--- phase_to_midi_au preserves mid-block timing ---\n");
    vivid::OperatorLoader loader;
    check(loader.load((build_dir + "/phase_to_midi_au.dylib").c_str()), "load phase_to_midi_au");
    if (!loader.is_loaded()) return;

    void* inst = loader.create_instance();
    check(inst != nullptr, "create phase_to_midi_au instance");
    if (!inst) return;

    auto params = default_params(loader.descriptor());
    AudioTestBuffers tb(1, 0, 1);
    tb.ctx.param_values = params.data();
    tb.inputs[0] = {0.70f, 0.80f, 0.90f, 0.05f, 0.10f, 0.20f, 0.30f, 0.40f};

    loader.process_audio(inst, &tb.ctx);

    auto* midi = static_cast<VividMidiBuffer*>(tb.custom_outputs[0]);
    check(midi != nullptr, "phase_to_midi_au publishes a MIDI buffer");
    if (midi) {
        check(midi->count == 1, "exactly one wrap event is emitted");
        if (midi->count == 1) {
            check(midi->messages[0].frame_offset_samples == 3,
                  "MIDI event keeps the in-buffer wrap offset");
        }
    }

    loader.destroy_instance(inst);
}

static void test_step_seq_block_start_snapshot(const std::string& build_dir) {
    std::fprintf(stderr, "\n--- step_seq_au uses block-start beat phase ---\n");
    vivid::OperatorLoader loader;
    check(loader.load((build_dir + "/step_seq_au.dylib").c_str()), "load step_seq_au");
    if (!loader.is_loaded()) return;

    void* inst = loader.create_instance();
    check(inst != nullptr, "create step_seq_au instance");
    if (!inst) return;

    auto params = default_params(loader.descriptor());
    params[0] = 4.0f;   // num_steps
    params[1] = 1.0f;   // frequency multiplier in external mode
    params[2] = 1.0f;   // rate_mode = external
    params[3] = 2.0f;   // sync_division (unused in external mode)
    params[4] = 0.0f;   // glide
    params[5] = 1.0f;   // amplitude
    params[6] = 0.0f;   // offset
    params[7] = 1.0f;   // polarity = unipolar
    params[8] = 0.2f;   // step 0 value
    params[10] = 0.8f;  // step 2 value

    AudioTestBuffers tb(2, 2);
    tb.ctx.param_values = params.data();
    tb.inputs[1] = {0.10f, 0.20f, 0.30f, 0.40f, 0.45f, 0.50f, 0.55f, 0.60f};

    loader.process_audio(inst, &tb.ctx);

    check_float(tb.outputs[0][0], 0.2f, 1e-5f, "step_seq_au chooses sample 0 instead of the final sample");
    check_float(tb.outputs[0][7], 0.2f, 1e-5f, "step_seq_au output stays block-constant");

    loader.destroy_instance(inst);
}

static void test_step_seq_metronome_snapshot(const std::string& build_dir) {
    std::fprintf(stderr, "\n--- step_seq_au locks to graph metronome without a beat-phase wire ---\n");
    vivid::OperatorLoader loader;
    check(loader.load((build_dir + "/step_seq_au.dylib").c_str()), "load step_seq_au");
    if (!loader.is_loaded()) return;

    void* inst = loader.create_instance();
    check(inst != nullptr, "create step_seq_au instance");
    if (!inst) return;

    auto params = default_params(loader.descriptor());
    params[0] = 4.0f;   // num_steps
    params[2] = 2.0f;   // rate_mode = metronome
    params[3] = 2.0f;   // sync_division = quarter notes
    params[5] = 1.0f;   // amplitude
    params[7] = 1.0f;   // polarity = unipolar
    params[8] = 0.2f;   // step 0 value
    params[10] = 0.8f;  // step 2 value

    AudioTestBuffers tb(2, 2);
    tb.ctx.param_values = params.data();
    set_audio_metronome(tb, 120.0f, 4, 0.60, 0.60f, 0.15f);

    loader.process_audio(inst, &tb.ctx);

    check_float(tb.outputs[0][0], 0.8f, 1e-5f, "metronome mode selects the step from graph beat phase");
    check_float(tb.outputs[0][7], 0.8f, 1e-5f, "metronome-synced output stays block-constant");

    loader.destroy_instance(inst);
}

static void test_euclidean_block_start_snapshot(const std::string& build_dir) {
    std::fprintf(stderr, "\n--- euclidean_au avoids block-end lookahead ---\n");
    vivid::OperatorLoader loader;
    check(loader.load((build_dir + "/euclidean_au.dylib").c_str()), "load euclidean_au");
    if (!loader.is_loaded()) return;

    void* inst = loader.create_instance();
    check(inst != nullptr, "create euclidean_au instance");
    if (!inst) return;

    auto params = default_params(loader.descriptor());
    params[0] = 1.0f;  // hits
    params[1] = 8.0f;  // steps
    params[2] = 0.0f;  // rotation
    params[3] = 0.5f;  // gate_length
    params[4] = 5.0f;  // rate = 1/32 -> multiplier 8

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
