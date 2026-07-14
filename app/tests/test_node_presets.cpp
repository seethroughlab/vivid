// ADR-0021/P4 — node-preset STORAGE (save/list/load/remove + name sanitization). Uses a uniquely
// namespaced op type under the real user presets dir and cleans up after, so it never collides with
// a real operator's presets. capture()/apply() run against a live NodeGraph and are verified over
// MCP (they need a real graph), not here.
#include "app/node_presets.h"
#include "test_helpers.h"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using namespace vivid;

namespace {

const char* kOp = "__preset_selftest_op__";

void cleanup() {
    const std::string dir = node_presets::user_presets_dir(kOp);
    std::error_code ec;
    if (!dir.empty()) fs::remove_all(dir, ec);
}

void test_round_trip() {
    cleanup();
    std::string err;
    nlohmann::json data = { {"params", {{"gain", 0.75}}}, {"file_params", {{"file", "x.png"}}} };

    // Save, then it appears in the list as a user (non-factory) preset.
    const std::string path = node_presets::save(kOp, "My Look", data, err);
    CHECK(!path.empty());
    CHECK(err.empty());
    auto listed = node_presets::list(kOp);
    CHECK(listed.size() == 1);
    CHECK(listed[0].name == "My Look");
    CHECK(listed[0].factory == false);

    // Load it back; the payload round-trips and name/op_type are stamped in.
    nlohmann::json got = node_presets::load(kOp, "My Look");
    CHECK(got.is_object());
    CHECK(got["params"]["gain"].get<double>() == 0.75);
    CHECK(got["file_params"]["file"].get<std::string>() == "x.png");
    CHECK(got["op_type"].get<std::string>() == kOp);

    // A missing preset loads as null (not an exception).
    CHECK(node_presets::load(kOp, "does not exist").is_null());

    // Remove deletes the user preset; the list empties.
    CHECK(node_presets::remove(kOp, "My Look"));
    CHECK(node_presets::list(kOp).empty());
    CHECK(!node_presets::remove(kOp, "My Look"));   // second remove: nothing there
    cleanup();
}

void test_name_sanitization() {
    cleanup();
    std::string err;
    // Path separators / traversal are stripped, so a saved file can't escape the presets dir.
    const std::string p = node_presets::save(kOp, "../../evil/name", {{"params", nlohmann::json::object()}}, err);
    CHECK(!p.empty());
    CHECK(fs::path(p).parent_path().filename() == kOp);   // stayed inside the op's dir
    CHECK(fs::path(p).filename().string().find("..") == std::string::npos);

    // An all-illegal name is rejected outright.
    std::string err2;
    CHECK(node_presets::save(kOp, "/@#$/", {{"params", nlohmann::json::object()}}, err2).empty());
    CHECK(!err2.empty());
    cleanup();
}

}  // namespace

int main() {
    test_round_trip();
    test_name_sanitization();
    return vivid::test::summary("node_presets");
}
