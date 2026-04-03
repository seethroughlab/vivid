#include "common/path_util.h"
#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "  FAIL: %s\n", msg);
        failures++;
    } else {
        std::fprintf(stderr, "  PASS: %s\n", msg);
    }
}

int main() {
    std::fprintf(stderr, "--- test_path_util ---\n");

    fs::path tmp = fs::temp_directory_path() / "vivid_path_util_test";
    fs::remove_all(tmp);
    fs::create_directories(tmp / "sub");

    // Create a real file so canonical() can succeed
    fs::path real_file = tmp / "sub" / "foo.cpp";
    std::ofstream(real_file) << "";

    // ---- resolve_file_path ----

    // 1. Absolute path returned unchanged
    {
        std::string abs = "/usr/local/bin/clang";
        auto result = vivid::resolve_file_path(abs, tmp);
        check(result == abs, "resolve_file_path: absolute path returned unchanged");
    }

    // 2. Relative path resolved against base
    {
        auto result = vivid::resolve_file_path("sub/other.cpp", tmp);
        // File doesn't exist, so lexically_normal
        std::string expected = (tmp / "sub" / "other.cpp").lexically_normal().string();
        check(result == expected, "resolve_file_path: relative resolved against base");
    }

    // 3. Relative path to existing file → canonicalized
    {
        auto result = vivid::resolve_file_path("sub/foo.cpp", tmp);
        std::string canonical = fs::canonical(tmp / "sub" / "foo.cpp").string();
        check(result == canonical, "resolve_file_path: existing file canonicalized");
    }

    // 4. Empty path → returned unchanged
    {
        auto result = vivid::resolve_file_path("", tmp);
        check(result.empty(), "resolve_file_path: empty path returns empty");
    }

    // ---- make_relative_path ----

    fs::path base = tmp;

    // 5. Path inside base dir → relative form
    {
        std::string abs = (tmp / "foo" / "bar.cpp").string();
        auto result = vivid::make_relative_path(abs, base);
        // Should be "foo/bar.cpp" (no leading ..)
        check(result.find("..") == std::string::npos && !result.empty(),
              "make_relative_path: path inside base returns relative without ..");
    }

    // 6. Path outside base dir → relative with .. segments
    {
        fs::path sibling = tmp.parent_path() / "sibling" / "file.cpp";
        auto result = vivid::make_relative_path(sibling.string(), base);
        check(result.find("..") != std::string::npos,
              "make_relative_path: path outside base has .. segments");
    }

    // 7. Path requiring >3 .. segments → returned as absolute
    {
        // Build a path that is 4 levels up from base then into something else.
        // base = /tmp/vivid_path_util_test  (depth ~3 from /)
        // We need >3 up segments relative to base; construct something far away.
        fs::path deep_base = tmp / "a" / "b" / "c" / "d";
        std::string target = "/very/different/path/file.cpp";
        auto result = vivid::make_relative_path(target, deep_base);
        // More than 3 .. required; should stay absolute
        fs::path rel = fs::proximate(target, deep_base);
        int dots = 0;
        for (const auto& c : rel) if (c == "..") dots++;
        if (dots > 3) {
            check(result == target, "make_relative_path: >3 .. keeps absolute path");
        } else {
            // On some systems the path might not need >3 .., skip this check
            check(true, "make_relative_path: >3 .. check skipped (insufficient depth)");
        }
    }

    // 8. Path equal to base → "."
    {
        auto result = vivid::make_relative_path(base.string(), base);
        check(result == ".", "make_relative_path: path equal to base returns '.'");
    }

    fs::remove_all(tmp);
    std::fprintf(stderr, "%s (%d failures)\n", failures == 0 ? "PASSED" : "FAILED", failures);
    return failures > 0 ? 1 : 0;
}
