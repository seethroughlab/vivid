#include "test_helpers.h"
#include "runtime/operator_creator.h"
#include "runtime/operator_registry.h"
#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

static std::string read_file(const std::string& path) {
    std::ifstream ifs(path);
    return {std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>()};
}

// Helper to create tmp dir with cmake markers for all envs
static void write_full_cmake(const std::string& dir) {
    std::ofstream ofs(dir + "/CMakeLists.txt");
    ofs << "# --- Control operators ---\n"
        << "\n"
        << "# --- GPU operator plugins ---\n"
        << "\n"
        << "# --- SyphonOut operator ---\n"
        << "\n"
        << "# --- Audio operator plugins ---\n"
        << "\n"
        << "# --- Operators meta-target ---\n";
}

static vivid::CreateOperatorResult create_op(const std::string& name, VividExecutionEnv env,
                                              const std::string& src_dir,
                                              const std::string& variant = "",
                                              bool package_layout = false) {
    VividCreateOperatorRequest req;
    req.name = name;
    req.env = env;
    req.variant = variant;
    return vivid::OperatorCreator::create(req, src_dir, package_layout);
}

int main() {
    vivid::OperatorRegistry reg;  // empty registry — no collisions

    // =================================================================
    // Test 1: Valid names
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 1: Valid names ===\n");
        check(vivid::OperatorCreator::validate_name("foo", reg).empty(), "foo is valid");
        check(vivid::OperatorCreator::validate_name("my_op", reg).empty(), "my_op is valid");
        check(vivid::OperatorCreator::validate_name("delay2", reg).empty(), "delay2 is valid");
        check(vivid::OperatorCreator::validate_name("lo_fi", reg).empty(), "lo_fi is valid");
        check(vivid::OperatorCreator::validate_name("a", reg).empty(), "single char 'a' is valid");
        check(vivid::OperatorCreator::validate_name("x2y", reg).empty(), "x2y is valid");
    }

    // =================================================================
    // Test 2: Invalid names
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 2: Invalid names ===\n");
        check(!vivid::OperatorCreator::validate_name("", reg).empty(), "empty is invalid");
        check(!vivid::OperatorCreator::validate_name("Foo", reg).empty(), "uppercase start is invalid");
        check(!vivid::OperatorCreator::validate_name("MyOp", reg).empty(), "PascalCase is invalid");
        check(!vivid::OperatorCreator::validate_name("2fast", reg).empty(), "leading digit is invalid");
        check(!vivid::OperatorCreator::validate_name("my__op", reg).empty(), "double underscore is invalid");
        check(!vivid::OperatorCreator::validate_name("_foo", reg).empty(), "leading underscore is invalid");
        check(!vivid::OperatorCreator::validate_name("foo_", reg).empty(), "trailing underscore is invalid");
        check(!vivid::OperatorCreator::validate_name("my op", reg).empty(), "spaces are invalid");
        check(!vivid::OperatorCreator::validate_name("my-op", reg).empty(), "hyphens are invalid");
        check(!vivid::OperatorCreator::validate_name("FOO", reg).empty(), "all-caps is invalid");
    }

    // =================================================================
    // Test 3: create() control env — verify files
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 3: create() control env ===\n");
        std::string tmp = "/tmp/vivid_test_creator_control";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        write_full_cmake(tmp);

        auto result = create_op("my_op", VIVID_ENV_FRAME, tmp);
        check(result.success, "create control op succeeds");
        check(result.error.empty(), "no error");
        check(result.target_name == "my_op", "target_name = my_op");

        // Verify .cpp was created
        std::string cpp_path = tmp + "/operators/control/my_op/my_op.cpp";
        check(fs::exists(cpp_path), "cpp file exists");

        // Verify template content
        std::string src = read_file(cpp_path);
        check(src.find("struct MyOp") != std::string::npos, "struct name is MyOp (PascalCase)");
        check(src.find("ControlOperatorBase") != std::string::npos, "inherits ControlOperatorBase");
        check(src.find("VIVID_REGISTER(MyOp)") != std::string::npos, "VIVID_REGISTER present");
        check(src.find("semantic_tag(amount, \"probability_01\")") != std::string::npos,
              "control template includes semantic_tag example");
        check(src.find("semantic_shape(amount, \"scalar\")") != std::string::npos,
              "control template includes semantic_shape example");

        // Verify CMakeLists.txt was patched
        std::string cmake = read_file(tmp + "/CMakeLists.txt");
        check(cmake.find("add_vivid_operator(my_op") != std::string::npos,
              "cmake patched with my_op");
        // Inserted before the GPU marker
        auto my_op_pos = cmake.find("add_vivid_operator(my_op");
        auto gpu_marker_pos = cmake.find("# --- GPU operator plugins ---");
        check(my_op_pos < gpu_marker_pos, "inserted before GPU marker");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 4: create() audio env
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 4: create() audio env ===\n");
        std::string tmp = "/tmp/vivid_test_creator_audio";
        fs::remove_all(tmp);
        fs::create_directories(tmp);

        {
            std::ofstream ofs(tmp + "/CMakeLists.txt");
            ofs << "# --- Audio operator plugins ---\n"
                << "\n"
                << "# --- Operators meta-target ---\n";
        }

        auto result = create_op("my_synth", VIVID_ENV_AUDIO, tmp);
        check(result.success, "create audio op succeeds");

        std::string cpp_path = tmp + "/operators/audio/my_synth/my_synth.cpp";
        check(fs::exists(cpp_path), "audio cpp file exists");

        std::string src = read_file(cpp_path);
        check(src.find("struct MySynth") != std::string::npos, "struct name is MySynth");
        check(src.find("AudioOperatorBase") != std::string::npos, "inherits AudioOperatorBase");
        check(src.find("process_audio") != std::string::npos, "has process_audio method");
        check(src.find("semantic_tag(gain, \"amplitude_linear\")") != std::string::npos,
              "audio template includes semantic_tag example");
        check(src.find("semantic_shape(gain, \"scalar\")") != std::string::npos,
              "audio template includes semantic_shape example");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 5: create() GPU env — verify shader file too
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 5: create() GPU env ===\n");
        std::string tmp = "/tmp/vivid_test_creator_gpu";
        fs::remove_all(tmp);
        fs::create_directories(tmp);

        {
            std::ofstream ofs(tmp + "/CMakeLists.txt");
            ofs << "# --- GPU operator plugins ---\n"
                << "\n"
                << "# --- SyphonOut operator ---\n";
        }

        auto result = create_op("cool_fx", VIVID_ENV_GPU, tmp);
        check(result.success, "create gpu op succeeds");

        std::string cpp_path = tmp + "/operators/gpu/cool_fx/cool_fx.cpp";
        check(fs::exists(cpp_path), "gpu cpp file exists");

        // Verify .wgsl shader was also created
        std::string wgsl_path = tmp + "/operators/gpu/cool_fx/cool_fx.wgsl";
        check(fs::exists(wgsl_path), "wgsl shader file exists");

        std::string src = read_file(cpp_path);
        check(src.find("struct CoolFx") != std::string::npos, "struct name is CoolFx");
        check(src.find("WgslFilterBase") != std::string::npos, "inherits WgslFilterBase");
        check(src.find("EXTRA_LIBS webgpu") == std::string::npos,
              "template source doesn't contain cmake flags");
        check(src.find("semantic_tag(amount, \"probability_01\")") != std::string::npos,
              "gpu template includes semantic_tag example");
        check(src.find("semantic_shape(amount, \"scalar\")") != std::string::npos,
              "gpu template includes semantic_shape example");

        // Verify cmake patching added EXTRA_LIBS webgpu
        std::string cmake = read_file(tmp + "/CMakeLists.txt");
        check(cmake.find("EXTRA_LIBS webgpu") != std::string::npos,
              "cmake includes EXTRA_LIBS webgpu for gpu op");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 6: create() collision — directory already exists
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 6: Directory collision ===\n");
        std::string tmp = "/tmp/vivid_test_creator_collision";
        fs::remove_all(tmp);
        fs::create_directories(tmp + "/operators/control/foo");

        {
            std::ofstream ofs(tmp + "/CMakeLists.txt");
            ofs << "# --- GPU operator plugins ---\n";
        }

        auto result = create_op("foo", VIVID_ENV_FRAME, tmp);
        check(!result.success, "collision detected");
        check(result.error.find("already exists") != std::string::npos,
              "error mentions 'already exists'");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 7: create() missing CMakeLists.txt marker
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 7: Missing cmake marker ===\n");
        std::string tmp = "/tmp/vivid_test_creator_no_marker";
        fs::remove_all(tmp);
        fs::create_directories(tmp);

        {
            std::ofstream ofs(tmp + "/CMakeLists.txt");
            ofs << "# nothing relevant here\n";
        }

        auto result = create_op("bar", VIVID_ENV_FRAME, tmp);
        check(!result.success, "fails when marker missing");
        check(result.error.find("insertion marker") != std::string::npos,
              "error mentions insertion marker");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 8: to_pascal_case verification via generated source
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 8: PascalCase in generated code ===\n");
        std::string tmp = "/tmp/vivid_test_creator_pascal";
        fs::remove_all(tmp);
        fs::create_directories(tmp);

        {
            std::ofstream ofs(tmp + "/CMakeLists.txt");
            ofs << "# --- GPU operator plugins ---\n";
        }

        auto result = create_op("lo_fi_delay", VIVID_ENV_FRAME, tmp);
        check(result.success, "create lo_fi_delay succeeds");

        std::string src = read_file(tmp + "/operators/control/lo_fi_delay/lo_fi_delay.cpp");
        check(src.find("struct LoFiDelay") != std::string::npos,
              "lo_fi_delay -> LoFiDelay PascalCase");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 9: create() composite variant — verify ChildOp template
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 9: create() composite variant ===\n");
        std::string tmp = "/tmp/vivid_test_creator_composite";
        fs::remove_all(tmp);
        fs::create_directories(tmp);

        {
            std::ofstream ofs(tmp + "/CMakeLists.txt");
            ofs << "# --- Control operators ---\n"
                << "\n"
                << "# --- GPU operator plugins ---\n";
        }

        auto result = create_op("mod_filter", VIVID_ENV_FRAME, tmp, "composite");
        check(result.success, "create composite op succeeds");
        check(result.error.empty(), "no error");

        std::string cpp_path = tmp + "/operators/control/mod_filter/mod_filter.cpp";
        check(fs::exists(cpp_path), "composite cpp file exists");

        std::string src = read_file(cpp_path);
        check(src.find("child_op.h") != std::string::npos, "includes child_op.h");
        check(src.find("ChildOp<LFO>") != std::string::npos, "has ChildOp<LFO>");
        check(src.find("ChildOp<Smooth>") != std::string::npos, "has ChildOp<Smooth>");
        check(src.find("operators documented as embeddable") != std::string::npos,
              "composite template documents embeddable ChildOp contract");
        check(src.find("vivid_composable_ops through a *_composable.cpp support file") != std::string::npos,
              "composite template documents composable support path");
        check(src.find("lfo_.process(ctx)") != std::string::npos, "calls lfo_.process(ctx)");
        check(src.find("smoother_.process(ctx)") != std::string::npos, "calls smoother_.process(ctx)");
        check(src.find("struct ModFilter") != std::string::npos, "struct name is ModFilter");
        check(src.find("semantic_tag(lfo_rate, \"frequency_hz\")") != std::string::npos,
              "composite template includes semantic_tag for lfo_rate");
        check(src.find("semantic_unit(lfo_rate, \"Hz\")") != std::string::npos,
              "composite template includes semantic_unit for lfo_rate");
        check(src.find("semantic_tag(smooth_time, \"time_seconds\")") != std::string::npos,
              "composite template includes semantic_tag for smooth_time");

        // Verify CMakeLists.txt includes EXTRA_LIBS vivid_composable_ops
        std::string cmake = read_file(tmp + "/CMakeLists.txt");
        check(cmake.find("EXTRA_LIBS vivid_composable_ops") != std::string::npos,
              "cmake includes EXTRA_LIBS vivid_composable_ops");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 10: composite variant rejected for GPU env
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 10: Reject composite for GPU env ===\n");
        std::string tmp = "/tmp/vivid_test_creator_composite_gpu";
        fs::remove_all(tmp);
        fs::create_directories(tmp);

        {
            std::ofstream ofs(tmp + "/CMakeLists.txt");
            ofs << "# --- GPU operator plugins ---\n"
                << "\n"
                << "# --- SyphonOut operator ---\n";
        }

        auto result = create_op("bad_comp", VIVID_ENV_GPU, tmp, "composite");
        check(!result.success, "composite GPU rejected");
        check(result.error.find("frame execution environment") != std::string::npos,
              "error mentions 'frame execution environment'");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 11: create() package layout destination
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 11: create() package layout ===\n");
        std::string tmp = "/tmp/vivid_test_creator_package_layout";
        fs::remove_all(tmp);
        fs::create_directories(tmp + "/src");

        {
            std::ofstream mof(tmp + "/vivid-package.json");
            mof << "{ \"name\": \"test-pkg\", \"operators\": [] }\n";
        }
        {
            std::ofstream cof(tmp + "/CMakeLists.txt");
            cof << "set(TEST_PKG_OPS\n"
                << "  existing_op\n"
                << ")\n";
        }

        auto result = create_op("team_gain", VIVID_ENV_AUDIO, tmp, "", true);
        check(result.success, "create package-layout op succeeds");

        std::string cpp_path = tmp + "/src/team_gain.cpp";
        check(fs::exists(cpp_path), "package-layout cpp exists under src/");

        std::string cmake = read_file(tmp + "/CMakeLists.txt");
        check(cmake.find("team_gain") != std::string::npos,
              "package CMake ops list includes new target");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 12: "empty" variant — control env
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 12: empty variant control ===\n");
        std::string tmp = "/tmp/vivid_test_creator_empty_ctrl";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        write_full_cmake(tmp);

        auto result = create_op("bare_ctrl", VIVID_ENV_FRAME, tmp, "empty");
        check(result.success, "create empty control succeeds");

        std::string src = read_file(result.cpp_path);
        check(src.find("struct BareCtrl") != std::string::npos, "struct BareCtrl");
        check(src.find("ControlOperatorBase") != std::string::npos, "inherits ControlOperatorBase");
        check(src.find("collect_ports") != std::string::npos, "has collect_ports");
        check(src.find("process") != std::string::npos, "has process");
        // Empty variant should NOT have Param declarations, but must have empty collect_params
        check(src.find("Param<") == std::string::npos, "no Param declarations");
        check(src.find("collect_params") != std::string::npos, "has empty collect_params override");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 13: "empty" variant — audio env
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 13: empty variant audio ===\n");
        std::string tmp = "/tmp/vivid_test_creator_empty_audio";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        write_full_cmake(tmp);

        auto result = create_op("bare_audio", VIVID_ENV_AUDIO, tmp, "empty");
        check(result.success, "create empty audio succeeds");

        std::string src = read_file(result.cpp_path);
        check(src.find("struct BareAudio") != std::string::npos, "struct BareAudio");
        check(src.find("AudioOperatorBase") != std::string::npos, "inherits AudioOperatorBase");
        check(src.find("process_audio") != std::string::npos, "has process_audio");
        check(src.find("Param<") == std::string::npos, "no Param declarations");
        check(src.find("collect_params") != std::string::npos, "has empty collect_params override");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 14: "empty" variant — gpu env
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 14: empty variant gpu ===\n");
        std::string tmp = "/tmp/vivid_test_creator_empty_gpu";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        write_full_cmake(tmp);

        auto result = create_op("bare_gpu", VIVID_ENV_GPU, tmp, "empty");
        check(result.success, "create empty gpu succeeds");

        std::string src = read_file(result.cpp_path);
        check(src.find("struct BareGpu") != std::string::npos, "struct BareGpu");
        check(src.find("WgslFilterBase") != std::string::npos, "inherits WgslFilterBase");
        check(src.find("Param<") == std::string::npos, "no Param declarations");
        check(src.find("collect_params") != std::string::npos, "has empty collect_params override");
        // Should have .wgsl
        std::string wgsl = tmp + "/operators/gpu/bare_gpu/bare_gpu.wgsl";
        check(fs::exists(wgsl), "empty gpu has wgsl file");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 15: CreateOperatorRequest with custom input + output ports
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 15: Custom ports ===\n");
        std::string tmp = "/tmp/vivid_test_creator_custom_ports";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        write_full_cmake(tmp);

        VividCreateOperatorRequest req;
        req.name = "multi_port";
        req.env = VIVID_ENV_FRAME;
        req.ports = {
            {"signal",    VIVID_PORT_SIGNAL, VIVID_PORT_INPUT},
            {"modulator", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT},
            {"result",    VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT},
            {"error",     VIVID_PORT_SIGNAL,   VIVID_PORT_OUTPUT},
        };

        auto result = vivid::OperatorCreator::create(req, tmp);
        check(result.success, "create with custom ports succeeds");

        std::string src = read_file(result.cpp_path);
        check(src.find("\"signal\"") != std::string::npos, "input port 'signal'");
        check(src.find("\"modulator\"") != std::string::npos, "input port 'modulator'");
        check(src.find("\"result\"") != std::string::npos, "output port 'result'");
        check(src.find("\"error\"") != std::string::npos, "output port 'error'");
        check(src.find("VIVID_PORT_INPUT") != std::string::npos, "has VIVID_PORT_INPUT");
        check(src.find("VIVID_PORT_OUTPUT") != std::string::npos, "has VIVID_PORT_OUTPUT");
        check(src.find("VIVID_PORT_SIGNAL") != std::string::npos, "has float port type (int maps to float)");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 15b: CreateOperatorRequest with custom typed ports
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 15b: Custom typed ports ===\n");
        std::string tmp = "/tmp/vivid_test_creator_custom_typed_ports";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        write_full_cmake(tmp);

        VividCreateOperatorRequest req;
        req.name = "typed_port";
        req.env = VIVID_ENV_FRAME;
        req.ports = {
            {"stream_in", 0, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_CUSTOM_REF,
                32, "TestStreamToken", "tests.vivid.test_stream_token_v1", true},
            {"stream_out", 0, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_CUSTOM_REF,
                32, "TestStreamToken", "tests.vivid.test_stream_token_v1", true},
        };

        auto result = vivid::OperatorCreator::create(req, tmp);
        check(result.success, "create with custom typed ports succeeds");

        std::string src = read_file(result.cpp_path);
        check(src.find("VIVID_DECLARE_CUSTOM_REF_TYPE(TestStreamToken") != std::string::npos,
              "custom type declaration emitted");
        check(src.find("tests.vivid.test_stream_token_v1") != std::string::npos,
              "stable custom type id emitted");
        check(src.find("VIVID_CUSTOM_REF_PORT(\"stream_in\"") != std::string::npos,
              "custom ref input port emitted");
        check(src.find("vivid_describe_custom_types") != std::string::npos,
              "custom type registration boilerplate emitted");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 16: CreateOperatorRequest with custom params
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 16: Custom params ===\n");
        std::string tmp = "/tmp/vivid_test_creator_custom_params";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        write_full_cmake(tmp);

        VividCreateOperatorRequest req;
        req.name = "param_test";
        req.env = VIVID_ENV_FRAME;
        req.params = {
            {"speed",  VIVID_PARAM_FLOAT, 1.0f, 0.0f, 10.0f},
            {"count",  VIVID_PARAM_INT,   4.0f, 1.0f, 8.0f},
            {"active", VIVID_PARAM_BOOL,  1.0f, 0.0f, 1.0f},
            {"source", VIVID_PARAM_FILE,  0.0f, 0.0f, 0.0f},
            {"label",  VIVID_PARAM_TEXT,  0.0f, 0.0f, 0.0f, "hello"},
        };

        auto result = vivid::OperatorCreator::create(req, tmp);
        check(result.success, "create with custom params succeeds");

        std::string src = read_file(result.cpp_path);
        check(src.find("Param<float> speed") != std::string::npos, "Param<float> speed");
        check(src.find("Param<int> count") != std::string::npos, "Param<int> count");
        check(src.find("Param<bool> active") != std::string::npos, "Param<bool> active");
        check(src.find("Param<vivid::FilePath> source") != std::string::npos, "Param<FilePath> source");
        check(src.find("Param<vivid::TextValue> label") != std::string::npos, "Param<TextValue> label");
        // Check float param has min/max
        check(src.find("1f, 0f, 10f") != std::string::npos ||
              src.find("1.0f, 0.0f, 10.0f") != std::string::npos ||
              src.find("\"speed\", 1") != std::string::npos,
              "float param has value range");
        // Bool default
        check(src.find("\"active\", true") != std::string::npos, "bool param default true");
        // Text default
        check(src.find("\"label\", \"hello\"") != std::string::npos, "text param default");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 16b: Custom typed ports require payload_size
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 16b: Custom typed ports require payload_size ===\n");
        std::string tmp = "/tmp/vivid_test_creator_custom_typed_ports_invalid";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        write_full_cmake(tmp);

        VividCreateOperatorRequest req;
        req.name = "typed_port_invalid";
        req.env = VIVID_ENV_FRAME;
        req.ports = {
            {"stream_in", 0, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_CUSTOM_REF,
                0, "TestStreamToken", "tests.vivid.test_stream_token_v1", true},
        };

        auto result = vivid::OperatorCreator::create(req, tmp);
        check(!result.success, "create with zero-size custom port fails");
        check(result.error.find("payload_size") != std::string::npos,
              "error mentions payload_size");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 16c: Custom typed ports require valid stable_type_id
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 16c: Custom typed ports require valid stable_type_id ===\n");
        std::string tmp = "/tmp/vivid_test_creator_custom_typed_ports_bad_stable_id";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        write_full_cmake(tmp);

        VividCreateOperatorRequest req;
        req.name = "typed_port_bad_stable_id";
        req.env = VIVID_ENV_FRAME;
        req.ports = {
            {"stream_in", 0, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_CUSTOM_REF,
                16, "TestStreamToken", "Tests.Vivid.BadId", true},
        };

        auto result = vivid::OperatorCreator::create(req, tmp);
        check(!result.success, "create with invalid stable_type_id fails");
        check(result.error.find("stable_type_id") != std::string::npos,
              "error mentions stable_type_id validation");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 16d: Custom typed ports require valid C++ type_name
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 16d: Custom typed ports require valid type_name ===\n");
        std::string tmp = "/tmp/vivid_test_creator_custom_typed_ports_bad_type_name";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        write_full_cmake(tmp);

        VividCreateOperatorRequest req;
        req.name = "typed_port_bad_type_name";
        req.env = VIVID_ENV_FRAME;
        req.ports = {
            {"stream_in", 0, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_CUSTOM_REF,
                16, "bad-type-name", "tests.vivid.test_stream_token_v1", true},
        };

        auto result = vivid::OperatorCreator::create(req, tmp);
        check(!result.success, "create with invalid type_name fails");
        check(result.error.find("type_name") != std::string::npos,
              "error mentions type_name validation");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 16e: Custom typed ports must not reuse stable ids with conflicting metadata
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 16e: Custom typed ports require consistent metadata ===\n");
        std::string tmp = "/tmp/vivid_test_creator_custom_typed_ports_conflict";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        write_full_cmake(tmp);

        VividCreateOperatorRequest req;
        req.name = "typed_port_conflict";
        req.env = VIVID_ENV_FRAME;
        req.ports = {
            {"stream_in", 0, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_CUSTOM_REF,
                16, "TestStreamToken", "tests.vivid.test_stream_token_v1", true},
            {"stream_out", 0, VIVID_PORT_OUTPUT, VIVID_PORT_TRANSPORT_CUSTOM_REF,
                32, "TestStreamToken", "tests.vivid.test_stream_token_v1", true},
        };

        auto result = vivid::OperatorCreator::create(req, tmp);
        check(!result.success, "create with conflicting stable_type_id metadata fails");
        check(result.error.find("stable_type_id") != std::string::npos,
              "error mentions conflicting stable_type_id metadata");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 16f: Built-in ports reject stray custom metadata
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 16f: Built-in ports reject custom metadata ===\n");
        std::string tmp = "/tmp/vivid_test_creator_builtin_port_custom_metadata";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        write_full_cmake(tmp);

        VividCreateOperatorRequest req;
        req.name = "builtin_port_bad_metadata";
        req.env = VIVID_ENV_FRAME;
        req.ports = {
            {"value_in", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT, VIVID_PORT_TRANSPORT_CUSTOM_REF,
                16, "IgnoredType", "", true},
        };

        auto result = vivid::OperatorCreator::create(req, tmp);
        check(!result.success, "built-in port with custom metadata fails");
        check(result.error.find("custom port metadata") != std::string::npos,
              "error mentions custom port metadata on built-in port");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 17: Custom ports + params together
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 17: Mixed ports + params ===\n");
        std::string tmp = "/tmp/vivid_test_creator_mixed";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        write_full_cmake(tmp);

        VividCreateOperatorRequest req;
        req.name = "mixed_op";
        req.env = VIVID_ENV_FRAME;
        req.ports = {
            {"in_a", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT},
            {"in_b", VIVID_PORT_SPREAD, VIVID_PORT_INPUT},
            {"out",  VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT},
        };
        req.params = {
            {"gain", VIVID_PARAM_FLOAT, 1.0f, 0.0f, 2.0f},
        };

        auto result = vivid::OperatorCreator::create(req, tmp);
        check(result.success, "create mixed succeeds");

        std::string src = read_file(result.cpp_path);
        check(src.find("\"in_a\"") != std::string::npos, "port in_a");
        check(src.find("\"in_b\"") != std::string::npos, "port in_b");
        check(src.find("VIVID_PORT_SPREAD") != std::string::npos, "spread port type");
        check(src.find("Param<float> gain") != std::string::npos, "custom param gain");
        check(src.find("collect_params") != std::string::npos, "has collect_params");
        check(src.find("collect_ports") != std::string::npos, "has collect_ports");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 18: Port validation — duplicate names rejected
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 18: Duplicate port names ===\n");
        std::string tmp = "/tmp/vivid_test_creator_dup_ports";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        write_full_cmake(tmp);

        VividCreateOperatorRequest req;
        req.name = "dup_port_op";
        req.env = VIVID_ENV_FRAME;
        req.ports = {
            {"input", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT},
            {"input", VIVID_PORT_SIGNAL, VIVID_PORT_OUTPUT},  // duplicate name
        };

        auto result = vivid::OperatorCreator::create(req, tmp);
        check(!result.success, "duplicate port names rejected");
        check(result.error.find("duplicate") != std::string::npos, "error mentions duplicate");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 19: Port validation — empty name rejected
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 19: Empty port name ===\n");
        std::string tmp = "/tmp/vivid_test_creator_empty_port_name";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        write_full_cmake(tmp);

        VividCreateOperatorRequest req;
        req.name = "empty_name_op";
        req.env = VIVID_ENV_FRAME;
        req.ports = {
            {"", VIVID_PORT_SIGNAL, VIVID_PORT_INPUT},
        };

        auto result = vivid::OperatorCreator::create(req, tmp);
        check(!result.success, "empty port name rejected");
        check(result.error.find("empty") != std::string::npos, "error mentions empty");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 20: GPU op with mixed-type inputs
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 20: GPU mixed-type inputs ===\n");
        std::string tmp = "/tmp/vivid_test_creator_gpu_mixed";
        fs::remove_all(tmp);
        fs::create_directories(tmp);
        write_full_cmake(tmp);

        VividCreateOperatorRequest req;
        req.name = "gpu_mixer";
        req.env = VIVID_ENV_GPU;
        req.ports = {
            {"texture_in", VIVID_PORT_TEXTURE, VIVID_PORT_INPUT},
            {"texture_out", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT},
        };

        auto result = vivid::OperatorCreator::create(req, tmp);
        check(result.success, "gpu mixed-type create succeeds");

        std::string src = read_file(result.cpp_path);
        check(src.find("\"texture_in\"") != std::string::npos, "texture_in port");
        check(src.find("\"texture_out\"") != std::string::npos, "texture_out port");
        check(src.find("VIVID_PORT_TEXTURE") != std::string::npos, "texture type");

        fs::remove_all(tmp);
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
