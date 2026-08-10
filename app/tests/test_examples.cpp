// ADR-0021/P2 — the example-project enumerator (app/examples.h). discover_examples_in is pure over
// a directory, so this builds a temp tree and checks the "a subdir with a project.json is an
// example, sorted by name" contract without touching the real bundle.
#include "app/examples.h"
#include "test_helpers.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;
using namespace vivid::examples;

namespace {

void write_file(const fs::path& p, const char* body) {
    fs::create_directories(p.parent_path());
    std::ofstream(p) << body;
}

void test_discovers_only_project_dirs_sorted() {
    const fs::path root = fs::temp_directory_path() / "vivid_examples_test";
    std::error_code ec; fs::remove_all(root, ec);

    write_file(root / "neon" / "project.json", "{}");
    write_file(root / "drift" / "project.json", "{}");
    write_file(root / "notaproject" / "readme.txt", "hi");   // no project.json -> skipped
    write_file(root / "loose.json", "{}");                    // a file, not a dir -> skipped

    const auto found = discover_examples_in(root.string());
    CHECK(found.size() == 2);
    // Sorted by name, so the order is deterministic across machines.
    CHECK(found[0].name == "drift");
    CHECK(found[1].name == "neon");
    // The path is the project directory (folder project), not the project.json.
    CHECK(found[0].path == (root / "drift").string());

    // A missing directory yields empty, never throws.
    CHECK(discover_examples_in((root / "does_not_exist").string()).empty());
    CHECK(discover_examples_in("").empty());

    fs::remove_all(root, ec);
}

void test_groups_subdirs_without_own_project() {
    const fs::path root = fs::temp_directory_path() / "vivid_examples_group_test";
    std::error_code ec; fs::remove_all(root, ec);

    // Two layouts under one root: a top-level demo (ungrouped) + an "operators" GROUP dir whose
    // children are the grouped examples (operators/<op>/project.json).
    write_file(root / "blob" / "project.json", "{}");                     // ungrouped
    write_file(root / "operators" / "Render3D" / "project.json", "{}");   // group "operators"
    write_file(root / "operators" / "Bloom" / "project.json", "{}");      // group "operators"
    write_file(root / "operators" / "notes.txt", "hi");                   // non-project child -> skipped

    const auto found = discover_examples_in(root.string());
    CHECK(found.size() == 3);
    // Sorted by (group, name): ungrouped ("") first, then the "operators" group by name.
    CHECK(found[0].name == "blob"     && found[0].group == "");
    CHECK(found[1].name == "Bloom"    && found[1].group == "operators");
    CHECK(found[2].name == "Render3D" && found[2].group == "operators");
    CHECK(found[2].path == (root / "operators" / "Render3D").string());

    fs::remove_all(root, ec);
}

void test_stage_passes_through_packageless() {
    // No vivid-package.json -> stage_openable_example returns the path unchanged (open in place).
    const fs::path root = fs::temp_directory_path() / "vivid_examples_stage_test";
    std::error_code ec; fs::remove_all(root, ec);
    write_file(root / "demo" / "project.json", "{}");
    CHECK(stage_openable_example((root / "demo").string()) == (root / "demo").string());
    fs::remove_all(root, ec);
}

}  // namespace

int main() {
    test_discovers_only_project_dirs_sorted();
    test_groups_subdirs_without_own_project();
    test_stage_passes_through_packageless();
    return vivid::test::summary("examples");
}
