#include "runtime/packages/package_scaffolder.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include "test_helpers.h"

static void write_file(const std::filesystem::path& p, const std::string& content) {
    std::filesystem::create_directories(p.parent_path());
    std::ofstream ofs(p);
    ofs << content;
}

int main(int argc, char* argv[]) {
    namespace fs = std::filesystem;

    std::string build_dir = ".";
    if (argc > 1) build_dir = argv[1];
    fs::path build_root = fs::absolute(fs::path(build_dir));

    std::fprintf(stderr, "\n=== Test: PackageScaffolder ===\n\n");

    fs::path sandbox = build_root / ".test_package_scaffolder";
    fs::remove_all(sandbox);
    fs::create_directories(sandbox);

    fs::path template_root = sandbox / "templates";
    fs::path single = template_root / "single-operator";
    fs::path multi = template_root / "multi-operator";

    write_file(single / "vivid-package.json", "{\n  \"name\": \"your-package-name\"\n}\n");
    write_file(single / "CMakeLists.txt", "project(your_package_name LANGUAGES CXX)\n");
    write_file(single / "README.md", "# your-package-name\n");

    write_file(multi / "vivid-package.json", "{\n  \"name\": \"your-package-name\"\n}\n");
    write_file(multi / "CMakeLists.txt", "project(your_package_name LANGUAGES CXX)\n");
    write_file(multi / "README.md", "# your-package-name\n");
    write_file(multi / "graphs" / "demo.json", "{}\n");

    // Single scaffold success.
    vivid::PackageScaffoldOptions opts_single;
    opts_single.name = "demo-pack";
    opts_single.variant = "single";
    opts_single.output_dir = sandbox.string();
    opts_single.template_root = template_root.string();

    auto r1 = vivid::PackageScaffolder::scaffold(opts_single);
    check(r1.success, "single template scaffold succeeds");
    check(fs::exists(fs::path(r1.package_dir) / "vivid-package.json"), "single manifest copied");

    std::string cmake_text;
    {
        std::ifstream ifs(fs::path(r1.package_dir) / "CMakeLists.txt");
        cmake_text.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }
    check(cmake_text.find("project(demo_pack") != std::string::npos,
          "project placeholder replaced with package name variant");

    std::string manifest_text;
    {
        std::ifstream ifs(fs::path(r1.package_dir) / "vivid-package.json");
        manifest_text.assign((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    }
    check(manifest_text.find("\"demo-pack\"") != std::string::npos,
          "manifest placeholder replaced with package name");

    // Existing destination without force fails.
    auto r2 = vivid::PackageScaffolder::scaffold(opts_single);
    check(!r2.success, "scaffold fails if destination exists without --force");

    // Existing destination with force succeeds.
    opts_single.force = true;
    auto r3 = vivid::PackageScaffolder::scaffold(opts_single);
    check(r3.success, "scaffold succeeds with --force when destination exists");

    // Multi scaffold success.
    vivid::PackageScaffoldOptions opts_multi;
    opts_multi.name = "multi-pack";
    opts_multi.variant = "multi";
    opts_multi.output_dir = sandbox.string();
    opts_multi.template_root = template_root.string();

    auto r4 = vivid::PackageScaffolder::scaffold(opts_multi);
    check(r4.success, "multi template scaffold succeeds");
    check(fs::exists(fs::path(r4.package_dir) / "graphs" / "demo.json"),
          "multi template graph files copied");

    // Invalid name fails.
    vivid::PackageScaffoldOptions bad;
    bad.name = "BadName";
    bad.variant = "single";
    bad.output_dir = sandbox.string();
    bad.template_root = template_root.string();
    auto r5 = vivid::PackageScaffolder::scaffold(bad);
    check(!r5.success, "invalid package name is rejected");

    fs::remove_all(sandbox);

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED",
                 failures);
    return failures == 0 ? 0 : 1;
}
