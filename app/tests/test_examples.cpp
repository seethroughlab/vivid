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

}  // namespace

int main() {
    test_discovers_only_project_dirs_sorted();
    return vivid::test::summary("examples");
}
