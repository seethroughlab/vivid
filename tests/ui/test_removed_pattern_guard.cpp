#include <filesystem>
#include <fstream>
#include <string>
#include <vector>
#include <cstdio>
#include "test_helpers.h"

namespace {

std::string rel_path(const std::filesystem::path& root, const std::filesystem::path& path) {
    return std::filesystem::relative(path, root).generic_string();
}

bool should_scan(const std::string& rel) {
    if (rel == "CMakeLists.txt") return true;
    if (rel.rfind("src/", 0) != 0 &&
        rel.rfind("mcp/", 0) != 0 &&
        rel.rfind("docs/", 0) != 0 &&
        rel.rfind("tests/", 0) != 0 &&
        rel.rfind("cmake/", 0) != 0) {
        return false;
    }

    const std::filesystem::path p(rel);
    const auto filename = p.filename().string();
    if (filename == ".DS_Store") return false;

    const auto ext = p.extension().string();
    return ext == ".h" || ext == ".cpp" || ext == ".mm" || ext == ".py" ||
           ext == ".md" || ext == ".cmake" || ext == ".txt" || ext == ".json";
}

bool allowed_reference_path(const std::string& rel) {
    return rel == "docs/ARCHITECTURE-GUARDRAILS.md" ||
           rel == "docs/archive/instrument-coherence/INSTRUMENT-COHERENCE-PLAN-STEP-2.md";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: test_removed_pattern_guard <source_dir>\n");
        return 1;
    }

    const std::filesystem::path root = argv[1];
    const std::string forbidden_symbol = std::string("Embedded") + "Op";
    const std::string forbidden_header = std::string("embedded") + "_op.h";

    std::vector<std::string> offenders;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(root)) {
        if (!entry.is_regular_file()) continue;

        const std::string rel = rel_path(root, entry.path());
        if (!should_scan(rel)) continue;

        if (rel == "src/operator_api/" + forbidden_header) {
            offenders.push_back(rel + ": header must not exist");
            continue;
        }

        std::ifstream in(entry.path());
        if (!in) continue;
        std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        if (content.find(forbidden_symbol) == std::string::npos &&
            content.find(forbidden_header) == std::string::npos) {
            continue;
        }

        if (!allowed_reference_path(rel)) {
            offenders.push_back(rel);
        }
    }

    if (!offenders.empty()) {
        std::fprintf(stderr,
                     "%s references are forbidden outside the guardrail docs.\n"
                     "Use ChildOp<T>, ordinary ports plus lanes, or explicit outputs instead.\n",
                     forbidden_symbol.c_str());
        for (const auto& offender : offenders) {
            std::fprintf(stderr, "offender: %s\n", offender.c_str());
        }
    }

    check(offenders.empty(), "Runtime-polymorphic embedded composition stays removed from code, tooling, tests, and active docs");

    std::fprintf(stderr, "%s (%d failures)\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED",
                 failures);
    return failures == 0 ? 0 : 1;
}
