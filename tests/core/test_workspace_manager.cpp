// Tests for workspace manager: tree copying, workspace seeding.
#include "runtime/core/workspace_manager.h"
#include "runtime/control/graph_file_io.h"
#include <cstdio>
#include <fstream>
#include <filesystem>
#include "test_helpers.h"

namespace fs = std::filesystem;

static void test_default_workspace_root() {
    std::fprintf(stderr, "\n--- default_workspace_root ---\n");

    auto root = vivid::default_workspace_root();
    check(!root.empty(), "returns non-empty path");
    // On macOS/Linux, should be under HOME
    const char* home = std::getenv("HOME");
    if (home) {
        std::string root_s = root.string();
        check(root_s.find(home) != std::string::npos ||
              root_s.find("Documents") != std::string::npos,
              "path is under HOME or Documents");
    }
}

static void test_copy_tree_missing() {
    std::fprintf(stderr, "\n--- copy_tree_missing ---\n");

    ScopedTempDir tmp("copy_tree");
    fs::path src = tmp / "src";
    fs::path dst = tmp / "dst";

    // Create source tree
    fs::create_directories(src / "sub");
    { std::ofstream(src / "a.txt") << "hello"; }
    { std::ofstream(src / "sub" / "b.txt") << "world"; }

    // Copy to empty destination
    bool ok = vivid::copy_tree_missing(src, dst);
    check(ok, "copy to empty dest succeeds");
    check(fs::exists(dst / "a.txt"), "a.txt copied");
    check(fs::exists(dst / "sub" / "b.txt"), "sub/b.txt copied");

    // Modify destination file, re-copy should NOT overwrite
    { std::ofstream(dst / "a.txt") << "modified"; }
    vivid::copy_tree_missing(src, dst);

    std::ifstream ifs(dst / "a.txt");
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    check(content == "modified", "existing file NOT overwritten");
}

static void test_copy_tree_overwrite_newer() {
    std::fprintf(stderr, "\n--- copy_tree_overwrite_newer ---\n");

    ScopedTempDir tmp("copy_newer");
    fs::path src = tmp / "src";
    fs::path dst = tmp / "dst";

    // Create source and dest
    fs::create_directories(src);
    fs::create_directories(dst);
    { std::ofstream(src / "file.txt") << "new content"; }
    { std::ofstream(dst / "file.txt") << "old content"; }

    // Make src file newer by touching it
    // (files created in sequence should already have src newer,
    //  but let's be explicit)
    auto now = fs::file_time_type::clock::now();
    fs::last_write_time(src / "file.txt", now);
    fs::last_write_time(dst / "file.txt", now - std::chrono::seconds(10));

    bool ok = vivid::copy_tree_overwrite_newer(src, dst);
    check(ok, "copy_tree_overwrite_newer succeeds");

    std::ifstream ifs(dst / "file.txt");
    std::string content((std::istreambuf_iterator<char>(ifs)),
                         std::istreambuf_iterator<char>());
    check(content == "new content", "newer file overwrites older");
}

static void test_copy_tree_missing_nonexistent_src() {
    std::fprintf(stderr, "\n--- copy_tree_missing: nonexistent source ---\n");

    ScopedTempDir tmp("copy_missing_none");
    bool ok = vivid::copy_tree_missing(tmp / "nonexistent_src_dir", tmp / "dst");
    check(!ok, "returns false for nonexistent source");
}

int main() {
    std::fprintf(stderr, "=== test_workspace_manager ===\n");

    test_default_workspace_root();
    test_copy_tree_missing();
    test_copy_tree_overwrite_newer();
    test_copy_tree_missing_nonexistent_src();

    std::fprintf(stderr, "\n=== %d failures ===\n", failures);
    return failures > 0 ? 1 : 0;
}
