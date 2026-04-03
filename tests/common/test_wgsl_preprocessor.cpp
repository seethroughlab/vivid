#include "operator_api/wgsl_preprocessor.h"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include "test_helpers.h"

static void write_text(const std::filesystem::path& p, const std::string& text) {
    std::ofstream ofs(p);
    ofs << text;
}

int main() {
    namespace fs = std::filesystem;
    std::fprintf(stderr, "\n=== test_wgsl_preprocessor ===\n");

    fs::path tmp = fs::temp_directory_path() / "vivid_test_wgsl_preprocessor";
    std::error_code ec;
    fs::remove_all(tmp, ec);
    fs::create_directories(tmp / "lib", ec);

    // --- Test 1: nested include + lib fallback ---
    write_text(tmp / "lib" / "common.wgsl", "fn common_fn(x: f32) -> f32 { return x * 2.0; }\n");
    write_text(tmp / "noise.wgsl", "// @include \"common.wgsl\"\nfn noise_fn(y: f32) -> f32 { return common_fn(y); }\n");
    write_text(tmp / "main_ok.wgsl", "const A: f32 = 1.0;\n// @include \"noise.wgsl\"\n@fragment fn fs_main() -> @location(0) vec4f { return vec4f(A); }\n");
    {
        auto r = vivid::preprocess_wgsl_file(tmp / "main_ok.wgsl");
        check(r.ok, "preprocess succeeds for nested include");
        if (r.ok) {
            check(r.output.find("common_fn") != std::string::npos, "output contains lib include content");
            check(r.output.find("noise_fn") != std::string::npos, "output contains nested include content");
        }
    }

    // --- Test 2: missing include has include-chain diagnostics ---
    write_text(tmp / "main_missing.wgsl", "// @include \"missing_file.wgsl\"\n");
    {
        auto r = vivid::preprocess_wgsl_file(tmp / "main_missing.wgsl");
        check(!r.ok, "missing include fails");
        if (!r.ok) {
            check(r.error.find("include not found") != std::string::npos, "missing include error text");
            check(r.error.find("main_missing.wgsl") != std::string::npos, "error includes parent file context");
        }
    }

    // --- Test 3: cycle diagnostics ---
    write_text(tmp / "a.wgsl", "// @include \"b.wgsl\"\nfn a() {}\n");
    write_text(tmp / "b.wgsl", "// @include \"a.wgsl\"\nfn b() {}\n");
    {
        auto r = vivid::preprocess_wgsl_file(tmp / "a.wgsl");
        check(!r.ok, "cycle include fails");
        if (!r.ok) {
            check(r.error.find("cycle") != std::string::npos, "cycle error text");
            check(r.error.find("a.wgsl") != std::string::npos && r.error.find("b.wgsl") != std::string::npos,
                  "cycle error includes both files");
        }
    }

    fs::remove_all(tmp, ec);
    std::fprintf(stderr, "\n=== %s (%d failures) ===\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
