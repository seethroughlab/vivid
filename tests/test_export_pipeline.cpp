#define private public
#include "export/export_pipeline.h"
#undef private

#include "runtime/graph/graph.h"
#include "runtime/operators/operator_registry.h"
#include "operator_api/port_type_registry.h"

namespace test {
struct TestCustomRef {
    uint64_t handle_id = 0;
    uint64_t generation = 0;
};
} // namespace test
VIVID_DECLARE_CUSTOM_REF_TYPE(test::TestCustomRef,
                              "seethroughlab.vivid.test_custom_ref",
                              "TestCustomRef",
                              false);

#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

static std::string read_file(const std::filesystem::path& path) {
    std::ifstream ifs(path);
    if (!ifs) return {};
    std::ostringstream ss;
    ss << ifs.rdbuf();
    return ss.str();
}

int main(int argc, char* argv[]) {
    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];

    const std::filesystem::path staging =
        std::filesystem::path(build_dir) / ".test_export_pipeline_staging";
    const std::filesystem::path export_dir =
        std::filesystem::path(build_dir) / ".test_export_pipeline_artifacts";
    const std::filesystem::path manifest_build_dir =
        std::filesystem::path(build_dir) / ".test_export_pipeline_build";
    const std::filesystem::path webgpu_prefetch =
        manifest_build_dir / "_deps" / "webgpu-src";
    const std::filesystem::path ixwebsocket_prefetch =
        manifest_build_dir / "_deps" / "ixwebsocket-src";
    std::filesystem::remove_all(staging);
    std::filesystem::remove_all(export_dir);
    std::filesystem::remove_all(manifest_build_dir);
    std::filesystem::create_directories(staging);
    std::filesystem::create_directories(manifest_build_dir);
    std::filesystem::create_directories(webgpu_prefetch);
    std::filesystem::create_directories(ixwebsocket_prefetch);
    {
        std::ofstream ofs(webgpu_prefetch / "CMakeLists.txt");
        ofs << "cmake_minimum_required(VERSION 3.20)\nproject(fake_webgpu)\n";
    }
    {
        std::ofstream ofs(ixwebsocket_prefetch / "CMakeLists.txt");
        ofs << "cmake_minimum_required(VERSION 3.20)\nproject(fake_ixwebsocket)\n";
    }

    std::filesystem::copy_file(std::filesystem::path(build_dir) / "export_custom_port_op.dylib",
                               staging / "export_custom_port_op.dylib",
                               std::filesystem::copy_options::overwrite_existing);
    {
        std::ofstream manifest(manifest_build_dir / "operator_manifest.json");
        manifest << "{\n"
                 << "  \"export_custom_port_op\": {\n"
                 << "    \"sources\": [\"tests/operators/export_custom_port_op.cpp\"],\n"
                 << "    \"extra_libs\": [],\n"
                 << "    \"frameworks\": [],\n"
                 << "    \"objc_arc\": [],\n"
                 << "    \"include_dirs\": []\n"
                 << "  }\n"
                 << "}\n";
    }

    std::fprintf(stderr, "\n=== Test: ExportPipeline Contracts ===\n\n");

    vivid::OperatorRegistry registry;
    check(registry.scan(staging.c_str()), "registry.scan()");
    check(registry.find("ExportCustomPortOp") != nullptr, "ExportCustomPortOp found");

    vivid::Graph graph;
    check(graph.add_node("custom", "ExportCustomPortOp"), "graph.add_node(custom)");

    vivid::ExportPipeline pipeline("/Users/jeff/Developer/vivid", manifest_build_dir.string());
    check(pipeline.load_manifest(), "load_manifest()");

    vivid::ExportOptions opts;
    opts.output_name = "workstream_b_app";
    check(pipeline.resolve_operators(graph, opts, registry), "resolve_operators()");
    check(pipeline.required_custom_types_.size() == 1,
          "resolve_operators records required custom type");
    if (!pipeline.required_custom_types_.empty()) {
        const auto& info = pipeline.required_custom_types_[0];
        check(info.type_id == vivid_port_type<test::TestCustomRef>(),
              "required custom type matches TestCustomRef id");
        check(std::string(info.stable_type_id ? info.stable_type_id : "") ==
                  "seethroughlab.vivid.test_custom_ref",
              "required custom type keeps stable type id");
    }

    pipeline.export_dir_ = export_dir.string();
    pipeline.headless_ = true;
    pipeline.control_server_ = true;
    std::filesystem::create_directories(export_dir);
    check(pipeline.generate_static_registry(), "generate_static_registry()");
    check(pipeline.generate_cmakelists(), "generate_cmakelists()");

    const std::string static_registry = read_file(export_dir / "static_registry.cpp");
    const std::string cmakelists = read_file(export_dir / "CMakeLists.txt");
    check(!static_registry.empty(), "static_registry.cpp generated");
    check(!cmakelists.empty(), "CMakeLists.txt generated");
    check(static_registry.find("vivid_register_port_type") != std::string::npos,
          "static registry registers custom port types");
    check(static_registry.find("seethroughlab.vivid.test_custom_ref") != std::string::npos,
          "static registry embeds stable custom type id");
    check(static_registry.find("ExportCustomPortOp") != std::string::npos,
          "static registry registers the operator");
    check(cmakelists.find("Using pre-fetched WebGPU-distribution") != std::string::npos,
          "standalone CMake prefers pre-fetched WebGPU source");
    check(cmakelists.find(webgpu_prefetch.string()) != std::string::npos,
          "standalone CMake embeds local WebGPU source path");
    check(cmakelists.find("Using pre-fetched IXWebSocket") != std::string::npos,
          "standalone CMake prefers pre-fetched IXWebSocket source");
    check(cmakelists.find(ixwebsocket_prefetch.string()) != std::string::npos,
          "standalone CMake embeds local IXWebSocket source path");

    const std::filesystem::path fake_build = export_dir / "build";
    std::filesystem::create_directories(fake_build);
    {
        std::ofstream ofs(fake_build / "standalone");
        ofs << "fake binary";
    }
    {
        std::ofstream ofs(fake_build / "libwgpu_native.dylib");
        ofs << "fake sidecar";
    }

    const std::filesystem::path final_output = export_dir / "dest" / "nested" / "my_export_app";
    check(pipeline.copy_output(final_output.string()), "copy_output()");
    check(std::filesystem::exists(final_output), "binary copied to selected destination path");
    check(std::filesystem::exists(final_output.parent_path() / "libwgpu_native.dylib"),
          "wgpu sidecar copied next to selected destination");

    std::filesystem::remove_all(staging);
    std::filesystem::remove_all(export_dir);
    std::filesystem::remove_all(manifest_build_dir);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
