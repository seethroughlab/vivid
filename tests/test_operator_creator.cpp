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
    // Test 3: create() control domain — verify files
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 3: create() control domain ===\n");
        std::string tmp = "/tmp/vivid_test_creator_control";
        fs::remove_all(tmp);
        fs::create_directories(tmp);

        // Create a fake CMakeLists.txt with the insertion markers
        {
            std::ofstream ofs(tmp + "/CMakeLists.txt");
            ofs << "# --- Control operators ---\n"
                << "add_vivid_operator(lfo operators/control/lfo/lfo.cpp)\n"
                << "\n"
                << "# --- GPU operator plugins ---\n"
                << "add_vivid_operator(noise operators/gpu/noise/noise.cpp)\n"
                << "\n"
                << "# --- Movie File In\n"
                << "\n"
                << "# --- Audio operator plugins ---\n"
                << "add_vivid_operator(gain operators/audio/gain/gain.cpp)\n"
                << "\n"
                << "# --- Movie File Audio In\n";
        }

        auto result = vivid::OperatorCreator::create("my_op", VIVID_DOMAIN_CONTROL, tmp);
        check(result.success, "create control op succeeds");
        check(result.error.empty(), "no error");
        check(result.target_name == "my_op", "target_name = my_op");

        // Verify .cpp was created
        std::string cpp_path = tmp + "/operators/control/my_op/my_op.cpp";
        check(fs::exists(cpp_path), "cpp file exists");

        // Verify template content
        std::string src = read_file(cpp_path);
        check(src.find("struct MyOp") != std::string::npos, "struct name is MyOp (PascalCase)");
        check(src.find("VIVID_DOMAIN_CONTROL") != std::string::npos, "domain is control");
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
    // Test 4: create() audio domain
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 4: create() audio domain ===\n");
        std::string tmp = "/tmp/vivid_test_creator_audio";
        fs::remove_all(tmp);
        fs::create_directories(tmp);

        {
            std::ofstream ofs(tmp + "/CMakeLists.txt");
            ofs << "# --- Audio operator plugins ---\n"
                << "\n"
                << "# --- Movie File Audio In\n";
        }

        auto result = vivid::OperatorCreator::create("my_synth", VIVID_DOMAIN_AUDIO, tmp);
        check(result.success, "create audio op succeeds");

        std::string cpp_path = tmp + "/operators/audio/my_synth/my_synth.cpp";
        check(fs::exists(cpp_path), "audio cpp file exists");

        std::string src = read_file(cpp_path);
        check(src.find("struct MySynth") != std::string::npos, "struct name is MySynth");
        check(src.find("VIVID_DOMAIN_AUDIO") != std::string::npos, "domain is audio");
        check(src.find("audio_operator.h") != std::string::npos, "includes audio header");
        check(src.find("semantic_tag(gain, \"amplitude_linear\")") != std::string::npos,
              "audio template includes semantic_tag example");
        check(src.find("semantic_shape(gain, \"scalar\")") != std::string::npos,
              "audio template includes semantic_shape example");

        fs::remove_all(tmp);
    }

    // =================================================================
    // Test 5: create() GPU domain — verify shader file too
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 5: create() GPU domain ===\n");
        std::string tmp = "/tmp/vivid_test_creator_gpu";
        fs::remove_all(tmp);
        fs::create_directories(tmp);

        {
            std::ofstream ofs(tmp + "/CMakeLists.txt");
            ofs << "# --- GPU operator plugins ---\n"
                << "\n"
                << "# --- Movie File In\n";
        }

        auto result = vivid::OperatorCreator::create("cool_fx", VIVID_DOMAIN_GPU, tmp);
        check(result.success, "create gpu op succeeds");

        std::string cpp_path = tmp + "/operators/gpu/cool_fx/cool_fx.cpp";
        check(fs::exists(cpp_path), "gpu cpp file exists");

        // Verify .wgsl shader was also created
        std::string wgsl_path = tmp + "/operators/gpu/cool_fx/cool_fx.wgsl";
        check(fs::exists(wgsl_path), "wgsl shader file exists");

        std::string src = read_file(cpp_path);
        check(src.find("struct CoolFx") != std::string::npos, "struct name is CoolFx");
        check(src.find("VIVID_DOMAIN_GPU") != std::string::npos, "domain is gpu");
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

        auto result = vivid::OperatorCreator::create("foo", VIVID_DOMAIN_CONTROL, tmp);
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

        auto result = vivid::OperatorCreator::create("bar", VIVID_DOMAIN_CONTROL, tmp);
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

        auto result = vivid::OperatorCreator::create("lo_fi_delay", VIVID_DOMAIN_CONTROL, tmp);
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

        auto result = vivid::OperatorCreator::create("mod_filter", VIVID_DOMAIN_CONTROL, tmp, "composite");
        check(result.success, "create composite op succeeds");
        check(result.error.empty(), "no error");

        std::string cpp_path = tmp + "/operators/control/mod_filter/mod_filter.cpp";
        check(fs::exists(cpp_path), "composite cpp file exists");

        std::string src = read_file(cpp_path);
        check(src.find("child_op.h") != std::string::npos, "includes child_op.h");
        check(src.find("ChildOp<LFO>") != std::string::npos, "has ChildOp<LFO>");
        check(src.find("ChildOp<Smooth>") != std::string::npos, "has ChildOp<Smooth>");
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
    // Test 10: composite variant rejected for GPU domain
    // =================================================================
    {
        std::fprintf(stderr, "\n=== Test 10: Reject composite for GPU domain ===\n");
        std::string tmp = "/tmp/vivid_test_creator_composite_gpu";
        fs::remove_all(tmp);
        fs::create_directories(tmp);

        {
            std::ofstream ofs(tmp + "/CMakeLists.txt");
            ofs << "# --- GPU operator plugins ---\n"
                << "\n"
                << "# --- Movie File In\n";
        }

        auto result = vivid::OperatorCreator::create("bad_comp", VIVID_DOMAIN_GPU, tmp, "composite");
        check(!result.success, "composite GPU rejected");
        check(result.error.find("control domain") != std::string::npos,
              "error mentions 'control domain'");

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

        auto result = vivid::OperatorCreator::create("team_gain", VIVID_DOMAIN_AUDIO, tmp, "", true);
        check(result.success, "create package-layout op succeeds");

        std::string cpp_path = tmp + "/src/team_gain.cpp";
        check(fs::exists(cpp_path), "package-layout cpp exists under src/");

        std::string cmake = read_file(tmp + "/CMakeLists.txt");
        check(cmake.find("team_gain") != std::string::npos,
              "package CMake ops list includes new target");

        fs::remove_all(tmp);
    }

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
