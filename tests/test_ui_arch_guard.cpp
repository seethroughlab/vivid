#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstdio>

static int failures = 0;

static void check(bool cond, const char* msg) {
    if (!cond) {
        std::fprintf(stderr, "FAIL: %s\n", msg);
        ++failures;
    } else {
        std::fprintf(stderr, "PASS: %s\n", msg);
    }
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: test_ui_arch_guard <source_dir>\n");
        return 1;
    }
    const std::filesystem::path ui_dir = std::filesystem::path(argv[1]) / "src" / "ui";
    const std::string forbidden_include = "runtime/package_catalog.h";

    std::vector<std::filesystem::path> offenders;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(ui_dir)) {
        if (!entry.is_regular_file()) continue;
        const auto ext = entry.path().extension().string();
        if (ext != ".h" && ext != ".cpp" && ext != ".mm") continue;

        std::ifstream in(entry.path());
        if (!in) continue;
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (content.find(forbidden_include) != std::string::npos) {
            offenders.push_back(entry.path());
        }
    }

    if (!offenders.empty()) {
        for (const auto& p : offenders) {
            std::fprintf(stderr, "forbidden include found in %s\n", p.string().c_str());
        }
    }
    check(offenders.empty(), "UI files do not include runtime/package_catalog.h");

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED",
                 failures);
    return failures == 0 ? 0 : 1;
}

