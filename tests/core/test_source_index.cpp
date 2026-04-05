#include "runtime/core/source_index.h"

#include <filesystem>
#include <fstream>

#include "test_helpers.h"

namespace fs = std::filesystem;

static void write_file(const fs::path& path, const std::string& content) {
    fs::create_directories(path.parent_path());
    std::ofstream ofs(path);
    ofs << content;
}

int main() {
    std::fprintf(stderr, "\n=== Test: SourceIndex ===\n");

    ScopedTempDir checkout_root("source_index_checkout");
    ScopedTempDir bundled_root("source_index_bundle");

    write_file(checkout_root.path / "src" / "runtime" / "widget.cpp",
               "struct WidgetAlpha {\n"
               "};\n"
               "int use_widget() {\n"
               "    WidgetAlpha widget;\n"
               "    return 0;\n"
               "}\n");
    write_file(checkout_root.path / "operators" / "control" / "demo" / "demo.cpp",
               "void demo_use() {\n"
               "    WidgetAlpha other;\n"
               "}\n");
    write_file(bundled_root.path / "docs" / "guide.md",
               "# Widget Guide\nWidgetAlpha appears in bundled docs.\n");
    write_file(bundled_root.path / "tests" / "test_widget.cpp",
               "void test_widget() { WidgetAlpha sample; }\n");

    vivid::SourceIndex index;
    index.set_checkout_root(checkout_root.path.string());
    index.set_bundled_root(bundled_root.path.string());

    auto roots = index.list_roots();
    check(roots.is_array(), "list_roots returns an array");
    if (roots.is_array()) {
        bool saw_src = false;
        bool saw_docs = false;
        for (const auto& item : roots) {
            if (!item.contains("name") || !item["name"].is_string()) continue;
            const std::string name = item["name"].get<std::string>();
            if (name == "src") {
                saw_src = true;
                check(item.value("origin", "") == "checkout", "src root prefers checkout");
            } else if (name == "docs") {
                saw_docs = true;
                check(item.value("origin", "") == "bundle", "docs root falls back to bundle");
            }
        }
        check(saw_src, "src root listed");
        check(saw_docs, "docs root listed");
    }

    auto search = index.search("WidgetAlpha");
    check(search.value("ok", false), "search succeeds");
    check(search.value("count", 0) >= 3, "search finds matches across roots");

    auto read_file = index.read_file("src/runtime/widget.cpp", 1000);
    check(read_file.value("ok", false), "read_file succeeds");
    if (read_file.value("ok", false)) {
        check(read_file.value("origin", "") == "checkout", "read_file reports checkout origin");
        check(read_file.value("content", "").find("WidgetAlpha") != std::string::npos,
              "read_file returns file contents");
    }

    auto read_span = index.read_span("src/runtime/widget.cpp", 1, 2);
    check(read_span.value("ok", false), "read_span succeeds");
    if (read_span.value("ok", false)) {
        check(read_span.value("content", "").find("struct WidgetAlpha") != std::string::npos,
              "read_span returns requested lines");
    }

    auto symbol = index.find_symbol("WidgetAlpha");
    check(symbol.value("ok", false), "find_symbol succeeds");
    if (symbol.value("ok", false)) {
        check(symbol.value("count", 0) >= 1, "find_symbol returns at least one hit");
        const auto& matches = symbol["matches"];
        check(matches.is_array() && !matches.empty(), "find_symbol returns match array");
        if (matches.is_array() && !matches.empty()) {
            check(matches[0].value("is_definition", false), "find_symbol prefers definitions");
        }
    }

    auto refs = index.find_references("WidgetAlpha");
    check(refs.value("ok", false), "find_references succeeds");
    check(refs.value("count", 0) >= 4, "find_references returns all token hits");

    auto bundled_read = index.read_file("docs/guide.md", 1000);
    check(bundled_read.value("ok", false), "read_file can read bundled fallback roots");
    if (bundled_read.value("ok", false)) {
        check(bundled_read.value("origin", "") == "bundle", "bundled file reports bundle origin");
    }

    auto blocked = index.read_file("../outside.txt", 1000);
    check(!blocked.value("ok", true), "read_file blocks path traversal");

    std::fprintf(stderr, "\n=== %s (%d failures) ===\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
