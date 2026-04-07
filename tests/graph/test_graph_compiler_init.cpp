// Unit tests for GraphCompiler::init_frame_state() and init_audio_state().
// These are pure initialization functions that populate CompiledNode state
// from VividOperatorDescriptor data — no dylib loading or runtime needed.

#include "runtime/graph/graph_compiler.h"
#include "operator_api/type_id.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>
#include "test_helpers.h"

// ---------------------------------------------------------------------------
// Helpers to build inline descriptors
// ---------------------------------------------------------------------------

static VividPortDescriptor make_port(const char* name, VividPortType type,
                                      VividPortDirection dir, uint8_t channels = 1) {
    VividPortDescriptor p{};
    p.name = name;
    p.type = type;
    p.direction = dir;
    p.transport = VIVID_PORT_TRANSPORT_SIGNAL;
    p.channels = channels;
    p.default_value = 0.0f;
    return p;
}

static VividParamDescriptor make_param(const char* name, VividParamType type,
                                        float def = 0.0f, const char* def_str = nullptr) {
    VividParamDescriptor p{};
    p.name = name;
    p.type = type;
    p.default_value = def;
    p.min_value = 0.0f;
    p.max_value = 1.0f;
    p.default_string = def_str;
    return p;
}

// ---------------------------------------------------------------------------
// init_frame_state tests
// ---------------------------------------------------------------------------

static void test_frame_state_basic_ports() {
    std::fprintf(stderr, "\n--- init_frame_state: basic port indexing ---\n");

    VividPortDescriptor ports[] = {
        make_port("in_a",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT),
        make_port("in_b",  VIVID_PORT_SCALAR, VIVID_PORT_INPUT),
        make_port("out_x", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT),
    };
    VividParamDescriptor params[] = {
        make_param("freq", VIVID_PARAM_FLOAT, 440.0f),
        make_param("gain", VIVID_PARAM_FLOAT, 0.5f),
    };
    VividOperatorDescriptor desc{};
    desc.name = "TestOp";
    desc.port_count = 3;
    desc.ports = ports;
    desc.param_count = 2;
    desc.params = params;

    vivid::CompiledNode cn;
    vivid::GraphCompiler::init_frame_state(cn, &desc, nullptr, nullptr, "");

    check(cn.input_port_count == 2, "input_port_count == 2");
    check(cn.output_port_count == 1, "output_port_count == 1");
    check(cn.input_port_indices.count("in_a") == 1, "in_a indexed");
    check(cn.input_port_indices.count("in_b") == 1, "in_b indexed");
    check(cn.output_port_indices.count("out_x") == 1, "out_x indexed");
    check(cn.input_port_indices["in_a"] == 0, "in_a index == 0");
    check(cn.input_port_indices["in_b"] == 1, "in_b index == 1");

    check(cn.input_values.size() == 2, "input_values sized");
    check(cn.output_values.size() == 1, "output_values sized");
    check(cn.input_string_values.size() == 2, "input_string_values sized");

    // Params
    check(cn.param_values.size() == 2, "param_values sized");
    check(cn.param_values[0] == 440.0f, "param default freq");
    check(cn.param_values[1] == 0.5f, "param default gain");
    check(cn.param_indices["freq"] == 0, "freq index");
    check(cn.param_indices["gain"] == 1, "gain index");
}

static void test_frame_state_param_overrides() {
    std::fprintf(stderr, "\n--- init_frame_state: param overrides ---\n");

    VividParamDescriptor params[] = {
        make_param("rate", VIVID_PARAM_FLOAT, 1.0f),
        make_param("depth", VIVID_PARAM_FLOAT, 0.5f),
    };
    VividOperatorDescriptor desc{};
    desc.name = "OverrideOp";
    desc.param_count = 2;
    desc.params = params;
    desc.port_count = 0;
    desc.ports = nullptr;

    std::unordered_map<std::string, float> overrides = {{"rate", 2.0f}};

    vivid::CompiledNode cn;
    vivid::GraphCompiler::init_frame_state(cn, &desc, &overrides, nullptr, "");

    check(cn.param_values[0] == 2.0f, "rate overridden to 2.0");
    check(cn.param_values[1] == 0.5f, "depth keeps default 0.5");
}

static void test_frame_state_file_params() {
    std::fprintf(stderr, "\n--- init_frame_state: file and text params ---\n");

    VividParamDescriptor params[] = {
        make_param("volume", VIVID_PARAM_FLOAT, 0.8f),
        make_param("sample_path", VIVID_PARAM_FILE, 0.0f, "samples/kick.wav"),
        make_param("label", VIVID_PARAM_TEXT, 0.0f, "hello"),
    };
    VividOperatorDescriptor desc{};
    desc.name = "FileOp";
    desc.param_count = 3;
    desc.params = params;
    desc.port_count = 0;
    desc.ports = nullptr;

    vivid::CompiledNode cn;
    vivid::GraphCompiler::init_frame_state(cn, &desc, nullptr, nullptr, "");

    check(cn.file_param_storage.size() == 2, "2 file params stored");
    check(cn.file_param_indices.count("sample_path") == 1, "sample_path indexed");
    check(cn.file_param_indices.count("label") == 1, "label indexed");
    check(cn.file_param_storage[cn.file_param_indices["sample_path"]] == "samples/kick.wav",
          "sample_path default value");
    check(cn.file_param_storage[cn.file_param_indices["label"]] == "hello",
          "label default value");
    check(cn.file_param_is_path[cn.file_param_indices["sample_path"]] == 1,
          "sample_path is_path flag");
    check(cn.file_param_is_path[cn.file_param_indices["label"]] == 0,
          "label is_path flag (TEXT = 0)");
    check(cn.file_param_ptrs.size() == 2, "file_param_ptrs sized");
}

static void test_frame_state_string_overrides() {
    std::fprintf(stderr, "\n--- init_frame_state: string param overrides ---\n");

    VividParamDescriptor params[] = {
        make_param("path", VIVID_PARAM_FILE, 0.0f, "default.wav"),
    };
    VividOperatorDescriptor desc{};
    desc.name = "StrOverrideOp";
    desc.param_count = 1;
    desc.params = params;
    desc.port_count = 0;
    desc.ports = nullptr;

    std::unordered_map<std::string, std::string> str_overrides = {{"path", "override.wav"}};

    vivid::CompiledNode cn;
    vivid::GraphCompiler::init_frame_state(cn, &desc, nullptr, &str_overrides, "");

    check(cn.file_param_storage[0] == "override.wav", "string override applied");
}

static void test_frame_state_mixed_port_types() {
    std::fprintf(stderr, "\n--- init_frame_state: mixed port types ---\n");

    VividPortDescriptor ports[] = {
        make_port("sig_in",  VIVID_PORT_SCALAR,  VIVID_PORT_INPUT),
        make_port("str_in",  VIVID_PORT_STRING,  VIVID_PORT_INPUT),
        make_port("tex_in",  VIVID_PORT_TEXTURE, VIVID_PORT_INPUT),
        make_port("sig_out", VIVID_PORT_SCALAR,  VIVID_PORT_OUTPUT),
        make_port("str_out", VIVID_PORT_STRING,  VIVID_PORT_OUTPUT),
    };
    VividOperatorDescriptor desc{};
    desc.name = "MixedOp";
    desc.port_count = 5;
    desc.ports = ports;
    desc.param_count = 0;
    desc.params = nullptr;
    desc.has_process_gpu = 1; // enable GPU path to test texture indexing

    vivid::CompiledNode cn;
    vivid::GraphCompiler::init_frame_state(cn, &desc, nullptr, nullptr, "");

    check(cn.input_port_count == 3, "3 inputs");
    check(cn.output_port_count == 2, "2 outputs");
    check(cn.string_input_port_indices.size() == 1, "1 string input index");
    check(cn.has_string_output, "has string output");
    check(cn.gpu != nullptr, "GPU state allocated");
    check(cn.gpu->texture_input_port_indices.size() == 1, "1 texture input");
    check(cn.gpu->has_texture_output == false, "no texture output");
}

static void test_frame_state_gpu_sink() {
    std::fprintf(stderr, "\n--- init_frame_state: GPU sink detection ---\n");

    VividPortDescriptor ports[] = {
        make_port("tex_in", VIVID_PORT_TEXTURE, VIVID_PORT_INPUT),
        make_port("sig_out", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT),
    };
    VividOperatorDescriptor desc{};
    desc.name = "SinkOp";
    desc.port_count = 2;
    desc.ports = ports;
    desc.param_count = 0;
    desc.params = nullptr;
    desc.has_process_gpu = 1;

    vivid::CompiledNode cn;
    vivid::GraphCompiler::init_frame_state(cn, &desc, nullptr, nullptr, "");

    check(cn.gpu != nullptr, "GPU state exists");
    check(cn.gpu->is_sink, "node with texture input + no texture output = sink");
}

static void test_frame_state_gpu_passthrough() {
    std::fprintf(stderr, "\n--- init_frame_state: GPU passthrough (not sink) ---\n");

    VividPortDescriptor ports[] = {
        make_port("tex_in",  VIVID_PORT_TEXTURE, VIVID_PORT_INPUT),
        make_port("tex_out", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT),
    };
    VividOperatorDescriptor desc{};
    desc.name = "PassOp";
    desc.port_count = 2;
    desc.ports = ports;
    desc.param_count = 0;
    desc.params = nullptr;
    desc.has_process_gpu = 1;

    vivid::CompiledNode cn;
    vivid::GraphCompiler::init_frame_state(cn, &desc, nullptr, nullptr, "");

    check(cn.gpu != nullptr, "GPU state exists");
    check(!cn.gpu->is_sink, "has texture output — not a sink");
    check(cn.gpu->has_texture_output, "has_texture_output set");
}

static void test_frame_state_no_ports() {
    std::fprintf(stderr, "\n--- init_frame_state: operator with no ports ---\n");

    VividParamDescriptor params[] = {
        make_param("value", VIVID_PARAM_FLOAT, 0.0f),
    };
    VividOperatorDescriptor desc{};
    desc.name = "NoPortsOp";
    desc.port_count = 0;
    desc.ports = nullptr;
    desc.param_count = 1;
    desc.params = params;

    vivid::CompiledNode cn;
    vivid::GraphCompiler::init_frame_state(cn, &desc, nullptr, nullptr, "");

    check(cn.input_port_count == 0, "no inputs");
    check(cn.output_port_count == 0, "no outputs");
    check(cn.param_values.size() == 1, "1 param");
    check(cn.gpu == nullptr, "no GPU state");
}

static void test_frame_state_lane_ports() {
    std::fprintf(stderr, "\n--- init_frame_state: lane port buffers ---\n");

    VividPortDescriptor ports[] = {
        make_port("sp_in",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT),
        make_port("sp_out", VIVID_PORT_LANE_ARRAY, VIVID_PORT_OUTPUT),
    };
    VividOperatorDescriptor desc{};
    desc.name = "SpreadOp";
    desc.port_count = 2;
    desc.ports = ports;
    desc.param_count = 0;
    desc.params = nullptr;

    vivid::CompiledNode cn;
    vivid::GraphCompiler::init_frame_state(cn, &desc, nullptr, nullptr, "");

    check(cn.c_in_lane_views.size() == 1, "1 input lane staging");
    check(cn.c_out_lane_outputs.size() == 1, "1 output lane staging");
    check(cn.out_lane_bufs.size() == 1, "output lane buf allocated");
    check(cn.out_lane_bufs[0].data.size() == 1024, "lane buf capacity 1024");
}

// ---------------------------------------------------------------------------
// init_audio_state tests
// ---------------------------------------------------------------------------

static void test_audio_state_basic() {
    std::fprintf(stderr, "\n--- init_audio_state: basic buffer allocation ---\n");

    VividPortDescriptor ports[] = {
        make_port("audio_in",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, 2),
        make_port("audio_out", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, 2),
    };
    VividOperatorDescriptor desc{};
    desc.name = "AudioOp";
    desc.port_count = 2;
    desc.ports = ports;
    desc.param_count = 0;
    desc.params = nullptr;
    desc.has_process_audio = 1;

    vivid::CompiledNode cn;
    cn.input_port_count = 1;
    cn.output_port_count = 1;
    cn.audio = std::make_unique<vivid::AudioNodeState>();

    vivid::GraphCompiler::init_audio_state(cn, &desc, 256);

    auto& a = *cn.audio;
    check(a.buffers_in.size() == 1, "1 input buffer");
    check(a.buffers_out.size() == 1, "1 output buffer");
    check(a.buffers_in[0].size() == 256, "input buffer size 256");
    check(a.buffers_out[0].size() == 256, "output buffer size 256");
    check(a.in_ptrs.size() == 1, "1 in ptr");
    check(a.out_ptrs.size() == 1, "1 out ptr");
    check(a.descriptor_input_channels.size() == 1, "1 input channel desc");
    check(a.descriptor_input_channels[0] == 2, "input channel count 2");
    check(a.descriptor_output_channels[0] == 2, "output channel count 2");
}

static void test_audio_state_scalar_ports() {
    std::fprintf(stderr, "\n--- init_audio_state: scalar ports still receive audio-side buffer slots ---\n");

    VividPortDescriptor ports[] = {
        make_port("freq",     VIVID_PORT_SCALAR, VIVID_PORT_INPUT),
        make_port("audio_in", VIVID_PORT_AUDIO_BUFFER,  VIVID_PORT_INPUT, 1),
        make_port("gain",     VIVID_PORT_SCALAR, VIVID_PORT_INPUT),
        make_port("out",      VIVID_PORT_AUDIO_BUFFER,  VIVID_PORT_OUTPUT, 1),
    };
    ports[0].default_value = 440.0f;
    ports[2].default_value = 1.0f;

    VividOperatorDescriptor desc{};
    desc.name = "CVOp";
    desc.port_count = 4;
    desc.ports = ports;
    desc.param_count = 0;
    desc.params = nullptr;

    vivid::CompiledNode cn;
    cn.input_port_count = 3;
    cn.output_port_count = 1;
    cn.audio = std::make_unique<vivid::AudioNodeState>();

    vivid::GraphCompiler::init_audio_state(cn, &desc, 128);

    auto& a = *cn.audio;
    check(a.buffers_in.size() == 3, "audio state allocates one input buffer slot per input port");
    check(a.buffers_out.size() == 1, "only AUDIO_BUFFER output allocates an audio buffer");
    check(a.in_ptrs.size() == 3, "3 audio input ptrs");
    check(a.out_ptrs.size() == 1, "1 audio output ptr");
}

// Scalar side-channel init removed in Phase 4B — scalar bridge delivery is explicit.

static void test_audio_state_lane_flags() {
    std::fprintf(stderr, "\n--- init_audio_state: lane/string/custom flags ---\n");

    VividPortDescriptor ports[] = {
        make_port("sp_in",  VIVID_PORT_LANE_ARRAY, VIVID_PORT_INPUT),
        make_port("str_in", VIVID_PORT_STRING, VIVID_PORT_INPUT),
        make_port("out",    VIVID_PORT_AUDIO_BUFFER,  VIVID_PORT_OUTPUT, 1),
    };
    VividOperatorDescriptor desc{};
    desc.name = "FlagOp";
    desc.port_count = 3;
    desc.ports = ports;
    desc.param_count = 0;
    desc.params = nullptr;

    vivid::CompiledNode cn;
    cn.input_port_count = 2;
    cn.output_port_count = 1;
    cn.audio = std::make_unique<vivid::AudioNodeState>();

    vivid::GraphCompiler::init_audio_state(cn, &desc, 256);

    auto& a = *cn.audio;
    check(a.has_lane_ports, "lane flag set");
    check(a.has_string_input_ports, "string input flag set");
    check(!a.has_custom_input_ports, "no custom input ports");
    check(!a.has_custom_output_ports, "no custom output ports");
}

static void test_audio_state_different_buffer_sizes() {
    std::fprintf(stderr, "\n--- init_audio_state: different buffer sizes ---\n");

    VividPortDescriptor ports[] = {
        make_port("in",  VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT, 1),
        make_port("out", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_OUTPUT, 1),
    };
    VividOperatorDescriptor desc{};
    desc.name = "BufOp";
    desc.port_count = 2;
    desc.ports = ports;
    desc.param_count = 0;
    desc.params = nullptr;

    for (uint32_t bs : {64u, 128u, 512u, 1024u}) {
        vivid::CompiledNode cn;
        cn.input_port_count = 1;
        cn.output_port_count = 1;
        cn.audio = std::make_unique<vivid::AudioNodeState>();

        vivid::GraphCompiler::init_audio_state(cn, &desc, bs);

        char label[64];
        std::snprintf(label, sizeof(label), "buffer size %u input", bs);
        check(cn.audio->buffers_in[0].size() == bs, label);
        std::snprintf(label, sizeof(label), "buffer size %u output", bs);
        check(cn.audio->buffers_out[0].size() == bs, label);
    }
}

// ---------------------------------------------------------------------------

int main() {
    std::fprintf(stderr, "=== test_graph_compiler_init ===\n");

    // init_frame_state tests
    test_frame_state_basic_ports();
    test_frame_state_param_overrides();
    test_frame_state_file_params();
    test_frame_state_string_overrides();
    test_frame_state_mixed_port_types();
    test_frame_state_gpu_sink();
    test_frame_state_gpu_passthrough();
    test_frame_state_no_ports();
    test_frame_state_lane_ports();

    // init_audio_state tests
    test_audio_state_basic();
    test_audio_state_scalar_ports();
    test_audio_state_lane_flags();
    test_audio_state_different_buffer_sizes();

    std::fprintf(stderr, "\n%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
