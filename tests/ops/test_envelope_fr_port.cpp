// Phase 4 of the operator-gaps plan: EnvelopeFr now publishes its `value`
// output as a frame-rate SCALAR instead of an AUDIO_BUFFER so it can drive
// GPU params and control inputs directly (with auto-inferred bridges).
//
// This test verifies:
//   1. EnvelopeFr.value is declared as SCALAR with no channels (frame).
//   2. The audio variant (Envelope, envelope_au.dylib) still declares value
//      as AUDIO_BUFFER with kMaxVoices channels — we did NOT accidentally
//      change the audio wrapper.
//   3. process_frame traces a recognizable ADSR curve when driven by a gate.
//
// Partition 10: pure CPU, no GPU/audio/window.

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

const VividPortDescriptor* find_output_port(const VividOperatorDescriptor& desc,
                                            const char* name) {
    for (uint32_t i = 0; i < desc.port_count; ++i) {
        const auto& p = desc.ports[i];
        if (p.direction == VIVID_PORT_OUTPUT && std::strcmp(p.name, name) == 0) {
            return &p;
        }
    }
    return nullptr;
}

int output_port_index(const VividOperatorDescriptor& desc, const char* name) {
    int scalar_out_index = 0;
    for (uint32_t i = 0; i < desc.port_count; ++i) {
        const auto& p = desc.ports[i];
        if (p.direction != VIVID_PORT_OUTPUT) continue;
        if (std::strcmp(p.name, name) == 0) return scalar_out_index;
        ++scalar_out_index;
    }
    return -1;
}

void test_fr_scalar_port(vivid::OperatorLoader& loader) {
    std::fprintf(stderr, "\n--- EnvelopeFr descriptor: value is SCALAR ---\n");
    const auto* desc = loader.descriptor();
    check(desc != nullptr, "EnvelopeFr descriptor not null");
    if (!desc) return;

    check(std::strcmp(desc->name, "EnvelopeFr") == 0, "name is EnvelopeFr");

    const auto* value = find_output_port(*desc, "value");
    check(value != nullptr, "EnvelopeFr has output port 'value'");
    if (!value) return;

    check(value->type == VIVID_PORT_SCALAR,
          "EnvelopeFr value port type = SCALAR (frame-rate control)");
    check(value->transport == VIVID_PORT_TRANSPORT_SIGNAL,
          "EnvelopeFr value transport = SIGNAL");
    check(value->channels == 0,
          "EnvelopeFr value channels = 0 (lane-lifting handles polyphony)");
    check(value->semantic_shape != nullptr
          && std::strcmp(value->semantic_shape, "scalar") == 0,
          "EnvelopeFr value semantic_shape = scalar");
}

void test_au_audio_buffer_preserved(const std::string& path) {
    std::fprintf(stderr, "\n--- Envelope (audio) descriptor: value stays AUDIO_BUFFER ---\n");
    vivid::OperatorLoader au;
    if (!au.load(path.c_str())) {
        std::fprintf(stderr, "  SKIP: could not load %s\n", path.c_str());
        return;
    }
    const auto* desc = au.descriptor();
    check(desc != nullptr, "Envelope (audio) descriptor not null");
    if (!desc) return;

    check(std::strcmp(desc->name, "Envelope") == 0, "audio variant name is Envelope");

    const auto* value = find_output_port(*desc, "value");
    check(value != nullptr, "Envelope has output port 'value'");
    if (!value) return;

    check(value->type == VIVID_PORT_AUDIO_BUFFER,
          "Envelope (audio) value type stays AUDIO_BUFFER");
    check(value->channels >= 2,
          "Envelope (audio) value keeps multi-voice channels");
}

// Simulate a short ADSR cycle: gate high for ~50 ms, then low, at 60 Hz frame
// rate. Confirm the scalar output rises during attack, reaches near-peak,
// sustains, then releases. We use generous tolerances — this test verifies
// that the value-path plumbing works, not the exact ADSR math (which is
// already covered by existing envelope tests).
void test_adsr_trace(vivid::OperatorLoader& loader) {
    std::fprintf(stderr, "\n--- EnvelopeFr ADSR trace through scalar port ---\n");
    void* inst = loader.create_instance();
    check(inst != nullptr, "create EnvelopeFr instance");
    if (!inst) return;

    const auto* desc = loader.descriptor();

    const int value_idx = output_port_index(*desc, "value");
    check(value_idx >= 0, "found scalar-output index for value");
    if (value_idx < 0) { loader.destroy_instance(inst); return; }

    // Count scalar outputs for the outputs array size.
    int output_count = 0;
    for (uint32_t i = 0; i < desc->port_count; ++i) {
        if (desc->ports[i].direction == VIVID_PORT_OUTPUT) ++output_count;
    }
    int input_count = static_cast<int>(desc->port_count) - output_count;

    // Pull param defaults. We tweak attack/decay/sustain/release to fit the
    // frame timing (dt = 1/60 ≈ 16.7 ms per tick).
    std::vector<float> params(desc->param_count);
    for (uint32_t i = 0; i < desc->param_count; ++i) {
        params[i] = desc->params[i].default_value;
    }
    auto find_param = [&](const char* name) {
        for (uint32_t i = 0; i < desc->param_count; ++i) {
            if (std::strcmp(desc->params[i].name, name) == 0) return static_cast<int>(i);
        }
        return -1;
    };
    params[find_param("attack")]    = 0.05f;  // 50 ms — crosses 3 frames
    params[find_param("decay")]     = 0.05f;
    params[find_param("sustain")]   = 0.6f;
    params[find_param("release")]   = 0.1f;
    params[find_param("amplitude")] = 1.0f;
    params[find_param("offset")]    = 0.0f;

    std::vector<float> inputs(input_count, 0.0f);
    std::vector<float> outputs(output_count, 0.0f);

    VividFrameContext ctx{};
    ctx.time          = 0.0;
    ctx.delta_time    = 1.0 / 60.0;
    ctx.frame         = 0;
    ctx.param_values  = params.data();
    ctx.input_values  = inputs.data();
    ctx.output_values = outputs.data();

    auto step = [&](float gate) {
        inputs[0] = gate;
        loader.process_frame(inst, &ctx);
        ctx.time  += ctx.delta_time;
        ctx.frame += 1;
        return outputs[value_idx];
    };

    // Attack: pulse gate high for 8 frames (~133 ms — well past attack+decay).
    float peak_seen = 0.0f;
    for (int i = 0; i < 8; ++i) {
        float v = step(1.0f);
        peak_seen = std::max(peak_seen, v);
    }
    check(peak_seen > 0.8f, "ADSR reached near-peak during attack (> 0.8)");

    // Sustain: output should settle toward sustain=0.6 after decay.
    float sustain_v = outputs[value_idx];
    check(sustain_v > 0.4f && sustain_v < 0.9f,
          "ADSR is around sustain level (0.4 < v < 0.9)");

    // Release: drop gate, run 15 frames (~250 ms — past release time of 100 ms).
    float final_v = 0.0f;
    for (int i = 0; i < 15; ++i) final_v = step(0.0f);
    check(final_v < 0.05f, "ADSR released to near zero (< 0.05)");

    loader.destroy_instance(inst);
}

} // namespace

int main(int argc, char** argv) {
    const std::string build_dir = (argc > 1) ? argv[1] : ".";
    const std::string fr_path = build_dir + "/envelope_fr.dylib";
    const std::string au_path = build_dir + "/envelope_au.dylib";

    if (!std::filesystem::exists(fr_path)) {
        std::fprintf(stderr, "FATAL: %s not found\n", fr_path.c_str());
        return 1;
    }

    std::fprintf(stderr, "=== Test: EnvelopeFr frame-rate scalar port ===\n");

    vivid::OperatorLoader fr;
    if (!fr.load(fr_path.c_str())) {
        std::fprintf(stderr, "FATAL: could not load %s\n", fr_path.c_str());
        return 1;
    }

    test_fr_scalar_port(fr);
    test_au_audio_buffer_preserved(au_path);
    test_adsr_trace(fr);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
