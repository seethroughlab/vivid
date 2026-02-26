#include "runtime/operator_loader.h"
#include "runtime/operator_registry.h"
#include <cstdio>
#include <cstring>
#include <cmath>
#include <filesystem>
#include <fstream>
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

// --- Builtin test helpers (no dylib needed) ---

static VividParamDescriptor s_builtin_param =
    {"val", VIVID_PARAM_FLOAT, 0.0f, 0.0f, 1.0f, nullptr, 0};
static VividPortDescriptor s_builtin_port =
    {"out", VIVID_PORT_CONTROL_FLOAT, VIVID_PORT_OUTPUT};
static VividOperatorDescriptor s_builtin_desc =
    {"BuiltinTestOp", VIVID_DOMAIN_CONTROL, 1, &s_builtin_param, 1, &s_builtin_port, 0};

static const VividOperatorDescriptor* builtin_descriptor() { return &s_builtin_desc; }
static void* builtin_create() { return new int(42); }
static void  builtin_destroy(void* p) { delete static_cast<int*>(p); }
static void  builtin_process(void*, const VividProcessContext*) {}

int main() {
    std::string build_dir = ".";

    // Setup staging directories
    std::string staging       = build_dir + "/.test_loader_staging";
    std::string staging_mixed = build_dir + "/.test_loader_mixed";
    std::string staging_empty = build_dir + "/.test_loader_empty";
    std::filesystem::create_directories(staging);
    std::filesystem::create_directories(staging_mixed);
    std::filesystem::create_directories(staging_empty);

    // Copy test plugins
    std::filesystem::copy_file(build_dir + "/test_op_v1.dylib",
        staging + "/test_op_v1.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/test_op_v1.dylib",
        staging_mixed + "/test_op_v1.dylib",
        std::filesystem::copy_options::overwrite_existing);
    std::filesystem::copy_file(build_dir + "/control_pass_op.dylib",
        staging_mixed + "/control_pass_op.dylib",
        std::filesystem::copy_options::overwrite_existing);

    std::fprintf(stderr, "\n=== Test: OperatorLoader + OperatorRegistry ===\n\n");

    // =========================================================================
    // OperatorLoader tests
    // =========================================================================
    std::fprintf(stderr, "--- OperatorLoader ---\n");

    // Test 1: load valid dylib
    {
        vivid::OperatorLoader loader;
        std::string path = staging + "/test_op_v1.dylib";
        check(loader.load(path.c_str()), "load valid dylib");
        check(loader.is_loaded(), "is_loaded after load");
        const auto* desc = loader.descriptor();
        check(desc != nullptr, "descriptor not null");
        if (desc) {
            check(std::strcmp(desc->name, "TestOp") == 0, "descriptor name = TestOp");
            check(desc->domain == VIVID_DOMAIN_CONTROL, "domain = CONTROL");
            check(desc->param_count == 1, "param_count = 1");
            check(desc->port_count == 1, "port_count = 1");
            check(std::strcmp(desc->params[0].name, "scale") == 0, "param[0] = scale");
            check(std::strcmp(desc->ports[0].name, "out") == 0, "port[0] = out");
        }
    }

    // Test 2: initial state
    {
        vivid::OperatorLoader loader;
        check(!loader.is_loaded(), "initial: not loaded");
        check(loader.descriptor() == nullptr, "initial: descriptor null");
    }

    // Test 3: load non-existent file
    {
        vivid::OperatorLoader loader;
        check(!loader.load("nonexistent.dylib"), "load non-existent fails");
        check(!loader.is_loaded(), "not loaded after failure");
    }

    // Test 4: load invalid file
    {
        std::string bad_path = staging + "/bad.dylib";
        {
            std::ofstream f(bad_path);
            f << "not a dylib";
        }
        vivid::OperatorLoader loader;
        check(!loader.load(bad_path.c_str()), "load invalid file fails");
        check(!loader.is_loaded(), "not loaded after invalid");
        std::filesystem::remove(bad_path);
    }

    // Test 5: instance lifecycle
    {
        vivid::OperatorLoader loader;
        std::string path = staging + "/test_op_v1.dylib";
        loader.load(path.c_str());

        void* instance = loader.create_instance();
        check(instance != nullptr, "create_instance not null");

        // Process: scale=5.0 → output = 5.0 * 2.0 = 10.0
        float params[] = {5.0f};
        float outputs[] = {0.0f};
        VividProcessContext ctx{};
        ctx.param_values = params;
        ctx.output_values = outputs;
        loader.process(instance, &ctx);
        check(std::fabs(outputs[0] - 10.0f) < 1e-4f, "process output = 10.0");

        loader.destroy_instance(instance);
    }

    // Test 6: unload
    {
        vivid::OperatorLoader loader;
        std::string path = staging + "/test_op_v1.dylib";
        loader.load(path.c_str());
        check(loader.is_loaded(), "loaded before unload");

        loader.unload();
        check(!loader.is_loaded(), "not loaded after unload");
        check(loader.descriptor() == nullptr, "descriptor null after unload");
        check(loader.create_instance() == nullptr, "create_instance null after unload");
    }

    // Test 7: init_builtin
    {
        vivid::OperatorLoader loader;
        loader.init_builtin(builtin_descriptor, builtin_create, builtin_destroy, builtin_process);
        check(loader.is_loaded(), "builtin: is_loaded");
        check(loader.descriptor() != nullptr, "builtin: descriptor not null");
        check(std::strcmp(loader.descriptor()->name, "BuiltinTestOp") == 0, "builtin: name correct");
        void* inst = loader.create_instance();
        check(inst != nullptr, "builtin: create_instance not null");
        loader.destroy_instance(inst);
    }

    // Test 8: builtin unload is no-op
    {
        vivid::OperatorLoader loader;
        loader.init_builtin(builtin_descriptor, builtin_create, builtin_destroy, builtin_process);
        loader.unload();
        check(loader.is_loaded(), "builtin: still loaded after unload");
        check(loader.descriptor() != nullptr, "builtin: descriptor still valid after unload");
    }

    // Test 9: has_draw_thumbnail
    {
        vivid::OperatorLoader loader;
        std::string path = staging + "/test_op_v1.dylib";
        loader.load(path.c_str());
        check(!loader.has_draw_thumbnail(), "test_op_v1 has no draw_thumbnail");
    }

    // Test 10: move semantics
    {
        vivid::OperatorLoader loader;
        std::string path = staging + "/test_op_v1.dylib";
        loader.load(path.c_str());
        check(loader.is_loaded(), "move: source loaded before move");

        // Move construct
        vivid::OperatorLoader moved(std::move(loader));
        check(moved.is_loaded(), "move-construct: target loaded");
        check(!loader.is_loaded(), "move-construct: source unloaded");

        // Move assign
        vivid::OperatorLoader assigned;
        assigned = std::move(moved);
        check(assigned.is_loaded(), "move-assign: target loaded");
        check(!moved.is_loaded(), "move-assign: source unloaded");
    }

    // =========================================================================
    // OperatorRegistry tests
    // =========================================================================
    std::fprintf(stderr, "\n--- OperatorRegistry ---\n");

    // Test 11: scan valid directory (test_op_v1 + test_op_v2 both → "TestOp" → 1 type)
    {
        std::string staging_v1v2 = build_dir + "/.test_loader_v1v2";
        std::filesystem::create_directories(staging_v1v2);
        std::filesystem::copy_file(build_dir + "/test_op_v1.dylib",
            staging_v1v2 + "/test_op_v1.dylib",
            std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(build_dir + "/test_op_v2.dylib",
            staging_v1v2 + "/test_op_v2.dylib",
            std::filesystem::copy_options::overwrite_existing);

        vivid::OperatorRegistry reg;
        check(reg.scan(staging_v1v2.c_str()), "scan valid directory");
        auto names = reg.type_names();
        check(names.size() == 1, "1 type (both map to TestOp)");
        if (!names.empty()) check(names[0] == "TestOp", "type name = TestOp");

        std::filesystem::remove_all(staging_v1v2);
    }

    // Test 12: scan non-existent directory
    {
        vivid::OperatorRegistry reg;
        check(!reg.scan("/nonexistent/path"), "scan non-existent dir fails");
    }

    // Test 13: scan empty directory
    {
        vivid::OperatorRegistry reg;
        check(reg.scan(staging_empty.c_str()), "scan empty dir succeeds");
        check(reg.type_names().empty(), "empty dir = 0 types");
    }

    // Test 14: find existing type
    {
        vivid::OperatorRegistry reg;
        reg.scan(staging.c_str());
        check(reg.find("TestOp") != nullptr, "find TestOp");
    }

    // Test 15: find non-existent type
    {
        vivid::OperatorRegistry reg;
        reg.scan(staging.c_str());
        check(reg.find("NoSuchType") == nullptr, "find NoSuchType = nullptr");
    }

    // Test 16: type_names sorted
    {
        vivid::OperatorRegistry reg;
        reg.scan(staging_mixed.c_str());
        auto names = reg.type_names();
        check(names.size() == 2, "mixed dir: 2 types");
        if (names.size() == 2) {
            check(names[0] == "ControlPassOp", "sorted[0] = ControlPassOp");
            check(names[1] == "TestOp", "sorted[1] = TestOp");
        }
    }

    // Test 17: register_builtin
    {
        vivid::OperatorRegistry reg;
        reg.register_builtin("BuiltinTestOp", builtin_descriptor, builtin_create,
                             builtin_destroy, builtin_process);
        check(reg.find("BuiltinTestOp") != nullptr, "find registered builtin");
        auto names = reg.type_names();
        check(names.size() == 1 && names[0] == "BuiltinTestOp", "type_names includes builtin");
    }

    // Test 18: type_name_for_target
    {
        std::string staging_v1v2 = build_dir + "/.test_loader_v1v2_2";
        std::filesystem::create_directories(staging_v1v2);
        std::filesystem::copy_file(build_dir + "/test_op_v1.dylib",
            staging_v1v2 + "/test_op_v1.dylib",
            std::filesystem::copy_options::overwrite_existing);
        std::filesystem::copy_file(build_dir + "/test_op_v2.dylib",
            staging_v1v2 + "/test_op_v2.dylib",
            std::filesystem::copy_options::overwrite_existing);

        vivid::OperatorRegistry reg;
        reg.scan(staging_v1v2.c_str());

        const std::string* t1 = reg.type_name_for_target("test_op_v1");
        check(t1 != nullptr && *t1 == "TestOp", "target test_op_v1 -> TestOp");

        const std::string* t2 = reg.type_name_for_target("test_op_v2");
        check(t2 != nullptr && *t2 == "TestOp", "target test_op_v2 -> TestOp");

        std::filesystem::remove_all(staging_v1v2);
    }

    // Test 19: type_name_for_target unknown
    {
        vivid::OperatorRegistry reg;
        reg.scan(staging.c_str());
        check(reg.type_name_for_target("unknown_target") == nullptr, "unknown target = nullptr");
    }

    // Test 20: reload_operator success
    {
        vivid::OperatorRegistry reg;
        reg.scan(staging.c_str());  // loads test_op_v1 as "TestOp" (1 param)

        auto* loader = reg.find("TestOp");
        check(loader != nullptr, "reload: find TestOp");
        if (loader) {
            check(loader->descriptor()->param_count == 1, "reload: v1 has 1 param");
        }

        std::string v2_path = build_dir + "/test_op_v2.dylib";
        check(reg.reload_operator("TestOp", v2_path), "reload_operator succeeds");

        loader = reg.find("TestOp");
        if (loader) {
            check(loader->descriptor()->param_count == 2, "reload: v2 has 2 params");
        }
    }

    // Test 21: reload_operator unknown type
    {
        vivid::OperatorRegistry reg;
        reg.scan(staging.c_str());
        check(!reg.reload_operator("NoSuchType", "whatever.dylib"), "reload unknown type fails");
    }

    // Test 22: reload_operator bad path
    {
        vivid::OperatorRegistry reg;
        reg.scan(staging.c_str());
        check(!reg.reload_operator("TestOp", "/nonexistent/bad.dylib"), "reload bad path fails");

        // After failed reload, loader is in unloaded state
        // (load() calls unload() first, then dlopen fails)
        auto* loader = reg.find("TestOp");
        check(loader != nullptr, "reload bad: loader still in registry");
        if (loader) {
            check(!loader->is_loaded(), "reload bad: loader is unloaded");
        }
    }

    // Cleanup
    std::filesystem::remove_all(staging);
    std::filesystem::remove_all(staging_mixed);
    std::filesystem::remove_all(staging_empty);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n\n",
        failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
