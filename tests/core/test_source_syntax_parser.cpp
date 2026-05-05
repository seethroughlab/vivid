#include "runtime/core/source_syntax_parser.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "test_helpers.h"

namespace fs = std::filesystem;

// Resolve the test fixtures directory relative to the test binary.
static fs::path find_fixtures_dir() {
    // Try several locations relative to CWD (which is the build dir).
    for (const auto& candidate : {
             fs::current_path() / "tests" / "fixtures" / "source_syntax_parser",
             fs::current_path().parent_path() / "tests" / "fixtures" / "source_syntax_parser",
             fs::current_path().parent_path().parent_path() / "tests" / "fixtures" / "source_syntax_parser",
         }) {
        if (fs::exists(candidate)) {
            return candidate;
        }
    }
    return {};
}

// Read a file's contents.
static std::string read_file(const fs::path& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f.is_open()) return {};
    return std::string((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
}

// Find a type definition by name in a record.
static const vivid::TypeDefinition* find_type(const vivid::SourceSyntaxRecord& record,
                                               const std::string& name) {
    for (const auto& td : record.type_definitions) {
        if (td.name == name) return &td;
    }
    return nullptr;
}

// Find a register call by type name in a record.
static const vivid::RegisterCall* find_register(const vivid::SourceSyntaxRecord& record,
                                                 const std::string& name) {
    for (const auto& rc : record.register_calls) {
        if (rc.type_name == name) return &rc;
    }
    return nullptr;
}

// Find an include target by path in a record.
static const vivid::IncludeTarget* find_include(const vivid::SourceSyntaxRecord& record,
                                                 const std::string& path) {
    for (const auto& inc : record.include_targets) {
        if (inc.quoted_path == path) return &inc;
    }
    return nullptr;
}

int main() {
    std::fprintf(stderr, "\n=== Test: SourceSyntaxParser ===\n");

    int test_num = 0;
    auto check_test = [&](const char* name) {
        ++test_num;
        std::fprintf(stderr, "\n  [Test %d] %s\n", test_num, name);
    };

    fs::path fixtures_dir = find_fixtures_dir();
    bool fixtures_found = !fixtures_dir.empty();

    // ---- Test 1: Parse valid C++ (multiline_class.cpp) ----
    check_test("parse valid C++ — multiline class");
    if (fixtures_found) {
        fs::path fixture = fixtures_dir / "multiline_class.cpp";
        auto record = vivid::SourceSyntaxParser::parse(fixture.string());
        check(record.valid, "parse succeeds for valid C++");
        if (record.valid) {
            check(record.type_definitions.size() >= 1, "finds at least one type definition");
            auto* td = find_type(record, "MultilineBaseOp");
            check(td != nullptr, "finds MultilineBaseOp struct");
            if (td) {
                check(td->kind == "struct", "kind is 'struct'");
                check(td->start_line > 0, "start_line > 0");
                check(td->end_line >= td->start_line, "end_line >= start_line");
                check(td->base_class_names.size() >= 2,
                      "extracts at least 2 base classes from multiline declaration");
                // Check that base classes contain expected names
                bool has_operator_base = false, has_gpu_processable = false;
                for (const auto& base : td->base_class_names) {
                    if (base == "OperatorBase") has_operator_base = true;
                    if (base == "GpuProcessable") has_gpu_processable = true;
                }
                check(has_operator_base, "base class 'OperatorBase' found");
                check(has_gpu_processable, "base class 'GpuProcessable' found");
            }

        }
    } else {
        std::fprintf(stderr, "  SKIP: fixtures not found\n");
    }

    // ---- Test 2: Parse templated bases ----
    check_test("parse templated base classes");
    if (fixtures_found) {
        fs::path fixture = fixtures_dir / "templated_bases.cpp";
        auto record = vivid::SourceSyntaxParser::parse(fixture.string());
        check(record.valid, "parse succeeds for templated bases");
        if (record.valid) {
            auto* td = find_type(record, "TemplatedOp");
            check(td != nullptr, "finds TemplatedOp struct");
            if (td) {
                check(td->base_class_names.size() >= 2,
                      "extracts 2 base classes from templated declaration");
                bool has_operator_base = false, has_gpu_processable = false;
                for (const auto& base : td->base_class_names) {
                    if (base == "OperatorBase") has_operator_base = true;
                    if (base == "GpuProcessable") has_gpu_processable = true;
                }
                check(has_operator_base, "base class 'OperatorBase' found");
                check(has_gpu_processable, "base class 'GpuProcessable' found");
            }
        }
    } else {
        std::fprintf(stderr, "  SKIP: fixtures not found\n");
    }

    // ---- Test 3: Parse namespace-wrapped types ----
    check_test("parse namespace-wrapped types");
    if (fixtures_found) {
        fs::path fixture = fixtures_dir / "namespace_wrapped.cpp";
        auto record = vivid::SourceSyntaxParser::parse(fixture.string());
        check(record.valid, "parse succeeds for namespace-wrapped code");
        if (record.valid) {
            auto* td = find_type(record, "NamespaceOp");
            check(td != nullptr, "finds NamespaceOp inside namespaces");
            if (td) {
                check(td->kind == "struct", "kind is 'struct'");
                check(td->base_class_names.size() >= 2,
                      "extracts base classes from namespace-wrapped struct");
            }

        }
    } else {
        std::fprintf(stderr, "  SKIP: fixtures not found\n");
    }

    // ---- Test 4: Parse no-doc block ----
    check_test("parse operator without doc block");
    if (fixtures_found) {
        fs::path fixture = fixtures_dir / "no_doc_block.cpp";
        auto record = vivid::SourceSyntaxParser::parse(fixture.string());
        check(record.valid, "parse succeeds for no-doc operator");
        if (record.valid) {
            auto* td = find_type(record, "NoDocOp");
            check(td != nullptr, "finds NoDocOp struct");
            // Doc comment ranges should be empty (no doc blocks)
            check(record.doc_comment_ranges.empty(),
                  "no doc comment ranges for operator without doc block");
        }
    } else {
        std::fprintf(stderr, "  SKIP: fixtures not found\n");
    }

    // ---- Test 5: Parse malformed C++ (graceful fallback) ----
    check_test("graceful fallback on malformed C++");
    if (fixtures_found) {
        fs::path fixture = fixtures_dir / "malformed_cpp.cpp";
        auto record = vivid::SourceSyntaxParser::parse(fixture.string());
        check(!record.valid || record.type_definitions.empty(),
              "malformed C++ returns empty or invalid record (no crash)");
        std::fprintf(stderr, "  PASS: malformed C++ handled gracefully (valid=%d, types=%zu)\n",
                      record.valid, record.type_definitions.size());
    } else {
        std::fprintf(stderr, "  SKIP: fixtures not found\n");
    }

    // ---- Test 6: Parse multiple types in one file ----
    check_test("parse multiple types in one file");
    if (fixtures_found) {
        fs::path fixture = fixtures_dir / "multiple_types.cpp";
        auto record = vivid::SourceSyntaxParser::parse(fixture.string());
        check(record.valid, "parse succeeds for multi-type file");
        if (record.valid) {
            check(record.type_definitions.size() >= 2,
                  "finds at least 2 type definitions");
            auto* first = find_type(record, "FirstOp");
            auto* second = find_type(record, "SecondOp");
            check(first != nullptr, "finds FirstOp");
            check(second != nullptr, "finds SecondOp");
            if (second) {
                check(second->base_class_names.size() >= 2,
                      "SecondOp has 2 base classes");
            }

            // SecondOp should have doc comments
            check(!record.doc_comment_ranges.empty(),
                  "doc comment ranges found for operator with doc block");
        }
    } else {
        std::fprintf(stderr, "  SKIP: fixtures not found\n");
    }

    // ---- Test 7: Doc comment variants ----
    check_test("doc comment variants (block vs single-line vs non-doc)");
    if (fixtures_found) {
        fs::path fixture = fixtures_dir / "doc_comment_variants.cpp";
        auto record = vivid::SourceSyntaxParser::parse(fixture.string());
        check(record.valid, "parse succeeds for doc comment variants");
        if (record.valid) {
            check(!record.doc_comment_ranges.empty(),
                  "doc comment ranges found");

            // BlockDocOp should have a doc comment before it
            auto* block_doc = find_type(record, "BlockDocOp");
            if (block_doc) {
                bool found_doc_before = false;
                for (const auto& dcr : record.doc_comment_ranges) {
                    // Doc comment should be on a line before the struct
                    if (dcr.end_line < block_doc->start_line) {
                        found_doc_before = true;
                        break;
                    }
                }
                check(found_doc_before,
                      "BlockDocOp has a doc comment before it");
            }

            // NonDocBlockOp should NOT have a doc comment immediately before it
            auto* non_doc = find_type(record, "NonDocBlockOp");
            if (non_doc) {
                bool found_nearby_doc = false;
                for (const auto& dcr : record.doc_comment_ranges) {
                    // Check if any doc comment is within 3 lines before the struct
                    if (dcr.end_line < non_doc->start_line &&
                        (non_doc->start_line - dcr.end_line) <= 3) {
                        found_nearby_doc = true;
                        break;
                    }
                }
                check(!found_nearby_doc,
                      "NonDocBlockOp does NOT have a doc comment nearby");
            }
        }
    } else {
        std::fprintf(stderr, "  SKIP: fixtures not found\n");
    }

    // ---- Test 8: Include directives ----
    check_test("extract include directives");
    if (fixtures_found) {
        fs::path fixture = fixtures_dir / "include_directives.cpp";
        auto record = vivid::SourceSyntaxParser::parse(fixture.string());
        check(record.valid, "parse succeeds for include directives");
        if (record.valid) {
            check(!record.include_targets.empty(),
                  "include targets found");

            auto* local1 = find_include(record, "some_local_header.h");
            auto* local2 = find_include(record, "operators/shared/another_header.h");
            check(local1 != nullptr, "finds local include 'some_local_header.h'");
            check(local2 != nullptr, "finds local include 'operators/shared/another_header.h'");
            if (local1) check(!local1->is_system, "local include is not system");
            if (local2) check(!local2->is_system, "local include is not system");

            // System includes
            auto* sys1 = find_include(record, "system_header");
            auto* sys2 = find_include(record, "another/system.h");
            check(sys1 != nullptr, "finds system include '<system_header>'");
            check(sys2 != nullptr, "finds system include '<another/system.h>'");
            if (sys1) check(sys1->is_system, "system include is marked as system");
            if (sys2) check(sys2->is_system, "system include is marked as system");
        }
    } else {
        std::fprintf(stderr, "  SKIP: fixtures not found\n");
    }

    // ---- Test 9: Cache management ----
    check_test("cache management");
    {
        vivid::SourceSyntaxParser::clear_cache();

        // Parse a file — should populate cache
        fs::path test_file = fs::temp_directory_path() / "test_cache_parse.cpp";
        {
            std::ofstream ofs(test_file);
            ofs << "#include \"operator_api/operator.h\"\n"
                   "struct CacheTestOp : vivid::OperatorBase, vivid::FrameProcessable {\n"
                   "    static constexpr const char* kName = \"CacheTestOp\";\n"
                   "    void collect_params(std::vector<vivid::ParamBase*>&) override {}\n"
                   "    void collect_ports(std::vector<VividPortDescriptor>&) override {}\n"
                   "    void process_frame(const VividFrameContext*) override {}\n"
                   "};\n";
        }

        auto record1 = vivid::SourceSyntaxParser::parse(test_file.string());
        check(record1.valid, "first parse succeeds");

        // Cache hit — should be fast
        check(vivid::SourceSyntaxParser::has_cached(test_file.string()),
              "file is in cache after parse");

        auto record2 = vivid::SourceSyntaxParser::get_cached(test_file.string());
        check(record2.valid, "cached record is valid");
        check(record2.type_definitions.size() == record1.type_definitions.size(),
              "cached record has same type count");

        // Invalidate file
        vivid::SourceSyntaxParser::invalidate_file(test_file.string());
        check(!vivid::SourceSyntaxParser::has_cached(test_file.string()),
              "file removed from cache after invalidate_file");

        // Clear all
        vivid::SourceSyntaxParser::clear_cache();
        check(!vivid::SourceSyntaxParser::has_cached(test_file.string()),
              "all cache cleared");

        // Cleanup
        fs::remove(test_file);
    }

    // ---- Test 10: Extension filtering ----
    check_test("extension filtering");
    {
        check(vivid::SourceSyntaxParser::is_cpp_extension(".cpp"), ".cpp is C++");
        check(vivid::SourceSyntaxParser::is_cpp_extension(".cc"), ".cc is C++");
        check(vivid::SourceSyntaxParser::is_cpp_extension(".cxx"), ".cxx is C++");
        check(vivid::SourceSyntaxParser::is_cpp_extension(".mm"), ".mm is C++");
        check(vivid::SourceSyntaxParser::is_cpp_extension(".h"), ".h is C++");
        check(vivid::SourceSyntaxParser::is_cpp_extension(".hh"), ".hh is C++");
        check(vivid::SourceSyntaxParser::is_cpp_extension(".hpp"), ".hpp is C++");
        check(vivid::SourceSyntaxParser::is_cpp_extension(".c"), ".c is C++");

        check(!vivid::SourceSyntaxParser::is_cpp_extension(".py"), ".py is not C++");
        check(!vivid::SourceSyntaxParser::is_cpp_extension(".js"), ".js is not C++");
        check(!vivid::SourceSyntaxParser::is_cpp_extension(".wgsl"), ".wgsl is not C++");
        check(!vivid::SourceSyntaxParser::is_cpp_extension(".json"), ".json is not C++");
    }

    // ---- Test 11: File not found ----
    check_test("file not found returns empty record");
    {
        auto record = vivid::SourceSyntaxParser::parse("/nonexistent/path/file.cpp");
        check(!record.valid, "nonexistent file returns invalid record");
        check(record.type_definitions.empty(), "nonexistent file has no type definitions");
    }

    // ---- Test 12: get_extension ----
    check_test("get_extension utility");
    {
        check(vivid::SourceSyntaxParser::get_extension("file.cpp") == ".cpp",
              "get_extension('.cpp')");
        check(vivid::SourceSyntaxParser::get_extension("file.CPP") == ".cpp",
              "get_extension lowercases");
        check(vivid::SourceSyntaxParser::get_extension("path/to/file.h") == ".h",
              "get_extension extracts from path");
    }

    // ---- Test 13: Symbol definitions ----
    check_test("symbol definitions extraction");
    if (fixtures_found) {
        fs::path fixture = fixtures_dir / "multiple_types.cpp";
        auto record = vivid::SourceSyntaxParser::parse(fixture.string());
        check(record.valid, "parse succeeds for symbol test");
        if (record.valid) {
            check(!record.symbol_definitions.empty(),
                  "symbol definitions found");
            // Should find struct FirstOp, struct SecondOp
            bool found_first = false, found_second = false;
            for (const auto& sym : record.symbol_definitions) {
                if (sym.name == "FirstOp" && sym.kind == "struct_declaration") found_first = true;
                if (sym.name == "SecondOp" && sym.kind == "struct_declaration") found_second = true;
            }
            check(found_first, "symbol 'FirstOp' found as struct_declaration");
            check(found_second, "symbol 'SecondOp' found as struct_declaration");
        }
    } else {
        std::fprintf(stderr, "  SKIP: fixtures not found\n");
    }

    // ---- Summary ----
    std::fprintf(stderr, "\n=== %s (%d failures) ===\n",
                 failures == 0 ? "ALL PASSED" : "SOME FAILED", failures);
    return failures == 0 ? 0 : 1;
}
