// ADR-0016 / S3 — the shader library: a .wgsl file in a folder becomes an operator TYPE in
// the registry, with the params its header declares.
//
// The assertion that matters is the last one: instantiate the registered type and read its
// params back through the operator ABI (collect_params), because that is the door the
// inspector, the graph, mappings and persistence all come through. If a shader's params
// arrive there intact, a shader node is not a special case anywhere downstream.
//
// Needs webgpu headers (ShaderFileOp holds WGPU handles) but touches no device — it never
// renders, so it stays a headless test.
#include "gpu/op_runtime.h"
#include "gpu/shader_library.h"
#include "test_helpers.h"

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>

using namespace vivid;
namespace fs = std::filesystem;

static void write_file(const fs::path& p, const std::string& s) {
    std::ofstream out(p, std::ios::binary);
    out << s;
}

static const ShaderLibraryEntry* entry_for(const ShaderLibrary& lib, const std::string& file) {
    for (const auto& e : lib.entries())
        if (fs::path(e.path).filename() == file) return &e;
    return nullptr;
}

int main() {
    const fs::path dir = fs::temp_directory_path() / "vivid_shader_lib_test";
    std::error_code ec;
    fs::remove_all(dir, ec);
    fs::create_directories(dir, ec);

    write_file(dir / "tinter.wgsl", R"(/*{
      "name": "TestTint",
      "summary": "Tints its input.",
      "keywords": ["effect", "color"],
      "inputs": ["input"],
      "params": [
        {"name": "amount", "type": "float", "default": 0.6, "min": 0, "max": 2, "display": "knob"},
        {"name": "tint",   "type": "color", "default": [1.0, 0.5, 0.25]}
      ]
    }*/
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    return textureSample(input, samp, inp.uv) * vec4f(u.tint * u.amount, 1.0);
})");

    // Malformed: must still produce a catalog ROW carrying its error — the catalog may not
    // lie about what is in the folder — but must not register a type.
    write_file(dir / "broken.wgsl", "/*{ \"name\": \"Broken\", ]}*/\nbody\n");

    // A second file claiming a name that is already registered: shadowed, and said so.
    // (Named so it sorts AFTER tinter.wgsl — a directory is scanned in sorted order, so within
    // one folder the first file to claim a name wins and the later one is the shadowed row.)
    write_file(dir / "zz_dupe.wgsl", "/*{ \"name\": \"TestTint\" }*/\n@fragment fn fs_main() {}\n");

    // Point the USER tier at our temp dir (the dev override), so the scan is deterministic.
    ::setenv("VIVID_SHADERS_DIR", dir.string().c_str(), 1);

    OpRegistry reg;
    ShaderLibrary lib;
    const int registered = lib.scan(reg);

    CHECK(registered == 1);                       // TestTint only
    CHECK(lib.entries().size() == 3);             // every file gets a row, valid or not

    const ShaderLibraryEntry* good = entry_for(lib, "tinter.wgsl");
    CHECK(good != nullptr);
    if (good) {
        CHECK(good->name == "TestTint");
        CHECK(good->registered);
        CHECK(good->error.empty());
        CHECK(good->tier == "user");
        CHECK(good->summary == "Tints its input.");
    }

    const ShaderLibraryEntry* bad = entry_for(lib, "broken.wgsl");
    CHECK(bad != nullptr);
    if (bad) {
        CHECK(!bad->registered);
        CHECK(!bad->error.empty());               // a row WITH an error, not a vanished row
    }

    const ShaderLibraryEntry* dupe = entry_for(lib, "zz_dupe.wgsl");
    CHECK(dupe != nullptr);
    if (dupe) {
        CHECK(!dupe->registered);
        CHECK(dupe->error.find("shadowed") != std::string::npos);
    }

    // The file is a registry TYPE, indistinguishable from a compiled operator's.
    CHECK(reg.has("TestTint"));

    std::vector<DescriptorValidationIssue> issues;
    auto inst = reg.create("TestTint", issues);
    CHECK(inst.has_value());
    if (inst) {
        CHECK(issues.empty());                    // a shader op's descriptor is a VALID descriptor
        CHECK(inst->input_port_count == 1);       // "inputs": ["input"]
        CHECK(inst->output_port_count == 1);

        // color expanded into three consecutive host params, the first carrying the COLOR hint.
        CHECK(inst->param_ptrs.size() == 4);      // amount, tint_r, tint_g, tint_b
        if (inst->param_ptrs.size() == 4) {
            CHECK(std::string(inst->param_ptrs[0]->name) == "amount");
            CHECK(inst->param_ptrs[0]->display_hint == VIVID_DISPLAY_KNOB);
            CHECK_NEAR(inst->param_ptrs[0]->default_value, 0.6f, 1e-6);
            CHECK_NEAR(inst->param_ptrs[0]->max_value, 2.0f, 1e-6);
            CHECK(std::string(inst->param_ptrs[1]->name) == "tint_r");
            CHECK(inst->param_ptrs[1]->display_hint == VIVID_DISPLAY_COLOR);
            CHECK(std::string(inst->param_ptrs[3]->name) == "tint_b");
            CHECK_NEAR(inst->param_ptrs[3]->default_value, 0.25f, 1e-6);
        }
    }

    // The cached descriptor outlives the instance it was built from: its const char* fields
    // point into the ShaderDef the library owns, not into that temporary. (This is the trap
    // the ShaderDef exists to avoid — a dangling param name here would corrupt the inspector.)
    const VividOperatorDescriptor* d = reg.descriptor_for("TestTint");
    CHECK(d != nullptr);
    if (d) {
        CHECK(d->param_count == 4);
        CHECK(std::string(d->name) == "TestTint");
        if (d->param_count == 4) CHECK(std::string(d->params[1].name) == "tint_r");
    }

    fs::remove_all(dir, ec);
    return vivid::test::summary("shader_library");
}
