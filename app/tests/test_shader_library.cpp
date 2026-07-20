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
#include "gpu/shader_file_op.h"   // ShaderSlot (generation bumps on reload)
#include "gpu/shader_library.h"
#include "test_helpers.h"

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
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

    // ---- S4: reload ----------------------------------------------------------------------
    // The watcher keys on mtime, so make the rewrites land in a later tick than the scan did.
    auto touch_later = [&](const fs::path& p, const std::string& body) {
        write_file(p, body);
        fs::last_write_time(p, fs::last_write_time(p, ec) + std::chrono::seconds(2), ec);
    };

    ShaderSlot* slot = good ? good->slot.get() : nullptr;
    CHECK(slot != nullptr);
    const uint64_t gen0 = slot ? slot->generation : 0;

    // A BODY edit: same declared interface, so the live nodes just recompile. No rebuild.
    touch_later(dir / "tinter.wgsl", R"(/*{
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
    return textureSample(input, samp, inp.uv) * vec4f(u.tint, 1.0) * 0.5;   // a different body
})");
    {
        auto reloads = lib.poll(reg);
        CHECK(reloads.size() == 1);
        if (!reloads.empty()) {
            CHECK(reloads[0].name == "TestTint");
            CHECK(reloads[0].change == ShaderChange::Body);
        }
        CHECK(slot && slot->generation == gen0 + 1);   // live nodes see the new def next frame
    }

    // An INTERFACE edit (a param added): the caller must rebuild that op's nodes, and the
    // registry's cached descriptor must be refreshed — a stale one would misreport the params.
    touch_later(dir / "tinter.wgsl", R"(/*{
      "name": "TestTint",
      "inputs": ["input"],
      "params": [
        {"name": "amount", "type": "float", "default": 0.6, "min": 0, "max": 2, "display": "knob"},
        {"name": "tint",   "type": "color", "default": [1.0, 0.5, 0.25]},
        {"name": "gamma",  "type": "float", "default": 1.0, "min": 0.1, "max": 4.0}
      ]
    }*/
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f {
    return textureSample(input, samp, inp.uv);
})");
    {
        auto reloads = lib.poll(reg);
        CHECK(reloads.size() == 1);
        if (!reloads.empty()) CHECK(reloads[0].change == ShaderChange::Interface);

        const VividOperatorDescriptor* d2 = reg.descriptor_for("TestTint");
        CHECK(d2 != nullptr);
        if (d2) {
            CHECK(d2->param_count == 5);                                  // + gamma
            CHECK(std::string(d2->params[4].name) == "gamma");
        }
        auto i2 = reg.create("TestTint", issues);
        CHECK(i2 && i2->param_ptrs.size() == 5);
    }

    // A BROKEN edit: reported, and the last good version keeps running (the slot does NOT move).
    const uint64_t gen_before_break = slot ? slot->generation : 0;
    touch_later(dir / "tinter.wgsl", "/*{ \"name\": \"TestTint\", }*/\nbody\n");
    {
        auto reloads = lib.poll(reg);
        CHECK(reloads.size() == 1);
        if (!reloads.empty()) {
            CHECK(reloads[0].change == ShaderChange::Failed);
            CHECK(!reloads[0].error.empty());
        }
        CHECK(slot && slot->generation == gen_before_break);   // nothing swapped under the nodes
        CHECK(reg.has("TestTint"));                            // and the type is still there
    }

    // ---- S4: a NEW file appears (no restart, no command) ----------------------------------
    write_file(dir / "arrival.wgsl", "/*{ \"name\": \"Arrival\", \"inputs\": [] }*/\n"
                                     "@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f "
                                     "{ return vec4f(u.time); }\n");
    fs::last_write_time(dir, fs::last_write_time(dir, ec) + std::chrono::seconds(2), ec);
    {
        auto reloads = lib.poll(reg);
        bool added = false;
        for (const auto& r : reloads) if (r.change == ShaderChange::Added && r.name == "Arrival") added = true;
        CHECK(added);
        CHECK(reg.has("Arrival"));
    }

    // ---- S4: fork-to-edit -----------------------------------------------------------------
    {
        std::string error;
        const std::string forked = lib.fork("Arrival", "ArrivalFork", reg, error);
        CHECK(!forked.empty());
        CHECK(error.empty());
        CHECK(reg.has("ArrivalFork"));               // spawnable immediately
        CHECK(lib.is_shader("ArrivalFork"));
        // The fork is a real file, and only its NAME changed — the body is the author's.
        const std::string src = [&]{ std::ifstream in(forked); std::ostringstream ss; ss << in.rdbuf(); return ss.str(); }();
        CHECK(src.find("\"ArrivalFork\"") != std::string::npos);
        CHECK(src.find("\"Arrival\"") == std::string::npos);
        CHECK(src.find("u.time") != std::string::npos);

        std::string err2;
        CHECK(lib.fork("Arrival", "ArrivalFork", reg, err2).empty());   // the name is taken now
        CHECK(!err2.empty());
        CHECK(lib.fork("NotAShader", "X", reg, err2).empty());
    }

    // ---- project-scoped operators ------------------------------------------------------------
    // set_project registers the shaders under a project's shaders/ dir (the mechanism behind an
    // operator that ships with an EXAMPLE, not the core — e.g. a demoted field generator), and
    // retires them again on New/close. The user/bundled tiers are untouched throughout.
    {
        const fs::path proj = fs::temp_directory_path() / "vivid_shader_proj_test";
        fs::remove_all(proj, ec);
        fs::create_directories(proj / "shaders", ec);
        write_file(proj / "shaders" / "field.wgsl", R"(/*{ "name": "ProjField", "inputs": [], "params": [] }*/
@fragment fn fs_main(inp: FullscreenOutput) -> @location(0) vec4f { return vec4f(1.0); })");

        CHECK(!reg.has("ProjField"));                                // absent before the project opens
        CHECK(lib.set_project(reg, proj.string()) == 1);            // opening the project registers it
        CHECK(reg.has("ProjField"));
        CHECK(reg.has("TestTint"));                                  // a user-tier op is left alone

        lib.set_project(reg, "");                                    // New / close → project ops retired
        CHECK(!reg.has("ProjField"));
        CHECK(reg.has("TestTint"));                                  // user tier still intact

        CHECK(lib.set_project(reg, proj.string()) == 1);           // re-opening re-registers cleanly
        CHECK(reg.has("ProjField"));
        lib.set_project(reg, "");
        fs::remove_all(proj, ec);
    }

    fs::remove_all(dir, ec);
    return vivid::test::summary("shader_library");
}
