// ADR-0021/P3 — the file-drop registry. file_ext_lower is pure; the rest is exercised against a
// REAL loaded dylib (the fixture op declares a VIVID_FILE_DROP handler for .foo/.bar), so the
// dlsym → harvest → index path is tested end to end, headless.
#include "gpu/file_drop_registry.h"
#include "gpu/operator_loader.h"
#include "test_helpers.h"

#include <memory>
#include <string>
#include <vector>

using namespace vivid;

namespace {

void test_ext_lower() {
    CHECK(file_ext_lower("/a/b/IMAGE.PNG") == "png");
    CHECK(file_ext_lower("clip.MP4") == "mp4");
    CHECK(file_ext_lower("noext") == "");
    CHECK(file_ext_lower("/dir.with.dots/file.Glsl") == "glsl");
    CHECK(file_ext_lower("") == "");
}

void test_registry_from_real_dylib() {
    std::vector<std::unique_ptr<OperatorLoader>> loaders;
    auto L = std::make_unique<OperatorLoader>();
    CHECK(L->load(FIXTURE_OP_PATH));
    loaders.push_back(std::move(L));

    FileDropRegistry reg;
    reg.rebuild(loaders);
    CHECK(!reg.empty());

    // The fixture handles .foo/.bar (case-insensitive) and fills "file" on FixtureOp.
    auto m = reg.matches_for_path("/some/thing.FOO");
    CHECK(m.size() == 1);
    CHECK(m[0].op_type == "FixtureOp");
    CHECK(m[0].file_param == "file");
    CHECK(m[0].priority == 7);

    // An unhandled extension yields nothing (never throws).
    CHECK(reg.matches_for_path("/x/y.png").empty());
    CHECK(reg.matches_for_path("/x/y").empty());

    // Reverse lookup: the op's declared extensions, for the dialog filter.
    auto exts = reg.extensions_for_op("FixtureOp");
    CHECK(exts.size() == 2);
    bool has_foo = false, has_bar = false;
    for (const auto& e : exts) { has_foo |= (e == "foo"); has_bar |= (e == "bar"); }
    CHECK(has_foo && has_bar);
    CHECK(reg.extensions_for_op("Nonexistent").empty());
}

}  // namespace

int main() {
    test_ext_lower();
    test_registry_from_real_dylib();
    return vivid::test::summary("file_drop_registry");
}
