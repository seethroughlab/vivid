# Phase 1: SourceSyntaxParser Library — Detailed Plan

**Phase:** 1 of 5 (tree-sitter-foundation plan)  
**Duration:** 1.5 weeks  
**Deliverable:** `SourceSyntaxParser` library with tests  
**Depends on:** Phase 0 (tree-sitter dependency pinned)

---

## 1. Scope

### In Scope
- Pin `tree-sitter` C runtime and `tree-sitter-cpp` grammar in `cmake/dependencies.cmake`
- Build `tree_sitter_cpp_lib` as a static library
- Implement `SourceSyntaxParser` library (`source_syntax_parser.h/.cpp`)
- Implement AST-based extraction of: type definitions, register calls, include targets, doc comments, symbol definitions
- Cache parsed records per-root/file, invalidate through existing flows
- Unit tests with fixtures covering multiline, templated, namespace-wrapped, malformed C++
- Wire into `cmake/tests.cmake`

### Out of Scope (Phase 2A/2B)
- Migration of `OperatorSourceDocs` to use SourceSyntaxParser
- Migration of `SourceIndex::find_symbol()` to use SourceSyntaxParser
- `operator_codegen` tool
- `VIVID_REGISTER_V2` / `VIVID_DEFINE_OP`
- WGSL struct codegen

---

## 2. Dependency: tree-sitter + tree-sitter-cpp

### 2a. tree-sitter C Runtime

FetchContent from `tree-sitter/tree-sitter` (v0.22.6, the latest stable release).

The C runtime consists of `lib/tree_sitter/api.c` and `include/tree_sitter/api.h` (plus a few other headers in `include/tree_sitter/`). We only need the C API — no parser generator, no CLI.

```cmake
FetchContent_Declare(
    tree_sitter
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
    GIT_TAG        v0.22.6
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(tree_sitter)
# Build only the C runtime as a static library
add_library(tree_sitter_runtime STATIC
    ${tree_sitter_SOURCE_DIR}/lib/src/lib.c
)
target_include_directories(tree_sitter_runtime PUBLIC
    ${tree_sitter_SOURCE_DIR}/lib/include
)
```

**Note:** tree-sitter v0.22 has the C API in `lib/src/lib.c` and headers in `lib/include/tree_sitter/`. We need to verify the exact paths at configure time.

### 2b. tree-sitter-cpp Grammar

FetchContent from `tree-sitter/tree-sitter-cpp` (latest stable).

```cmake
FetchContent_Declare(
    tree_sitter_cpp
    GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter-cpp.git
    GIT_TAG        v0.23.4
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(tree_sitter_cpp)
# Build the generated parser as a static library
add_library(tree_sitter_cpp_lib STATIC
    ${tree_sitter_cpp_SOURCE_DIR}/src/parser.c
)
target_include_directories(tree_sitter_cpp_lib PUBLIC
    ${tree_sitter_cpp_SOURCE_DIR}/src
)
target_link_libraries(tree_sitter_cpp_lib PUBLIC tree_sitter_runtime)
```

### 2c. Dependency Manifest Update

Update `docs/ARCHITECTURE.md` §Dependency Manifest with the new entries.

---

## 3. SourceSyntaxParser Library Design

### 3a. Header: `source_syntax_parser.h`

```cpp
#pragma once

#include <string>
#include <vector>
#include <unordered_map>

namespace vivid {

// ---- Data structures returned by parsing ----

struct TypeDefinition {
    std::string name;           // e.g. "Noise"
    std::string kind;           // "struct" or "class"
    std::string path;           // file path where defined
    int start_line;             // 1-indexed
    int end_line;               // 1-indexed (closing brace)
    std::vector<std::string> base_class_names; // e.g. {"OperatorBase", "GpuProcessable"}
};

struct RegisterCall {
    std::string macro_name;     // "VIVID_REGISTER" or "VIVID_DEFINE_OP"
    std::string type_name;      // e.g. "Noise"
    std::string path;           // file path
    int line;                   // 1-indexed
};

struct IncludeTarget {
    std::string quoted_path;    // e.g. "operator_api/operator.h"
    bool is_system;             // true for <...>, false for "..."
};

struct DocCommentRange {
    int start_line;             // 1-indexed
    int end_line;               // 1-indexed
};

struct SymbolDefinition {
    std::string name;
    std::string kind;           // "struct", "class", "function", "variable", "enum", "namespace", etc.
    std::string path;
    int start_line;
    int end_line;
};

// ---- Parsed output ----

struct SourceSyntaxRecord {
    std::vector<TypeDefinition> type_definitions;
    std::vector<RegisterCall> register_calls;
    std::vector<IncludeTarget> include_targets;
    std::vector<DocCommentRange> doc_comment_ranges;
    std::vector<SymbolDefinition> symbol_definitions;
    
    // Whether parsing succeeded (empty record = graceful fallback)
    bool valid = false;
    
    // Raw source text (for debugging / fallback)
    std::string raw_source;
};

// ---- Parser interface ----

class SourceSyntaxParser {
public:
    // Parse a single file. Returns empty record on failure.
    static SourceSyntaxRecord parse(const std::string& file_path);
    
    // Get the file extension (lowercase, with dot).
    static std::string get_extension(const std::string& file_path);
    
    // Check if a file extension is C/C++/ObjC.
    static bool is_cpp_extension(const std::string& ext);
    
    // Invalidate cache for a given root (called by existing invalidate flows).
    static void invalidate_root(const std::string& root);
    
    // Invalidate cache for a given file path.
    static void invalidate_file(const std::string& file_path);
    
    // Check if a given file is in the cache.
    static bool has_cached(const std::string& file_path);
    
    // Get cached record (returns empty if not cached).
    static SourceSyntaxRecord get_cached(const std::string& file_path);
    
    // Clear all cache.
    static void clear_cache();

private:
    // Internal cache: file_path → SourceSyntaxRecord
    static std::unordered_map<std::string, SourceSyntaxRecord>& cache();
};

} // namespace vivid
```

### 3b. Implementation: `source_syntax_parser.cpp`

Key implementation details:

1. **Tree-sitter integration:**
   - Create `ts_parser_new()` once (static)
   - Use `ts_parser_parse_file()` to parse the file
   - Get the root tree via `ts_parser_parse()`
   - Walk the tree using `ts_tree_root_node()` and `ts_node_child()` / `ts_node_next_sibling()`

2. **Node type detection:**
   - Use `ts_node_type()` to identify node types
   - Use `ts_node_string()` for debugging (not in production)
   - Use `ts_node_named_child_count()` to find named children

3. **Type definition extraction:**
   - Walk for `struct_declaration` and `class_declaration` nodes
   - Extract `name` field via `ts_node_named_child()` (the identifier child)
   - Extract `bases` from `base_class_clause` → `field_identifier` children
   - Use `ts_node_start_byte()` / `ts_node_end_byte()` to get line numbers

4. **Register call extraction:**
   - Walk for `call_expression` nodes
   - Check if function name is `VIVID_REGISTER` or `VIVID_DEFINE_OP`
   - Extract first argument (the type name) from `arguments` → `identifier` or `type_identifier`

5. **Include target extraction:**
   - Walk for `preproc_include` nodes
   - Extract the string literal child

6. **Doc comment extraction:**
   - Walk for `comment` nodes
   - Check if comment starts with `/**` or `/*` (not `//`)
   - Use `ts_node_start_byte()` / `ts_node_end_byte()` to get line range

7. **Symbol definition extraction:**
   - Walk for `struct_declaration`, `class_declaration`, `function_definition`,
     `enum_declaration`, `namespace_definition`, `type_alias_declaration`
   - Extract name from `name` named child

8. **Caching:**
   - Static `unordered_map<string, SourceSyntaxRecord>` keyed by absolute path
   - `invalidate_root()` erases keys starting with the root prefix
   - `invalidate_file()` erases the specific key
   - `has_cached()` / `get_cached()` for cache access

9. **Graceful fallback:**
   - On any error (file not found, parse failure, etc.), return an empty `SourceSyntaxRecord` with `valid = false`
   - The `raw_source` field is populated even on failure for debugging

### 3c. Line Number Calculation

Tree-sitter uses byte offsets. To convert to line numbers:
- Walk from `ts_node_start_byte()` to find the number of `\n` characters in the source
- Or use `ts_point_row()` if available in the API version

---

## 4. Test Fixtures

Create `tests/fixtures/source_syntax_parser/` with the following C++ fixtures:

### 4a. `multiline_class.cpp`
```cpp
// File: multiline_class.cpp
#include "operator_api/operator.h"

/**
 * @brief Operator with multiline base class declaration.
 *
 * Tests tree-sitter's ability to handle declarations spanning
 * multiple lines with different base classes on separate lines.
 */
struct MultilineBaseOp
    : vivid::OperatorBase,
      vivid::GpuProcessable
{
    static constexpr const char* kName = "MultilineBaseOp";
    vivid::Param<float> intensity {"intensity", 1.0f, 0.0f, 1.0f};
    
    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&intensity);
    }
    void collect_ports(std::vector<VividPortDescriptor>& out) override {
        out.push_back({"output", VIVID_PORT_TEXTURE, VIVID_PORT_OUTPUT});
    }
};

VIVID_REGISTER(MultilineBaseOp)
```

### 4b. `templated_bases.cpp`
```cpp
// File: templated_bases.cpp
#include "operator_api/operator.h"

// Template base classes should extract the base name, not the full template.
struct TemplatedOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "TemplatedOp";
    void collect_params(std::vector<vivid::ParamBase*>&) override {}
    void collect_ports(std::vector<VividPortDescriptor>&) override {}
};

VIVID_REGISTER(TemplatedOp)
```

### 4c. `namespace_wrapped.cpp`
```cpp
// File: namespace_wrapped.cpp
#include "operator_api/operator.h"

namespace vivid {
namespace operators {

/**
 * @brief Operator wrapped in nested namespaces.
 */
struct NamespaceOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "NamespaceOp";
    void collect_params(std::vector<vivid::ParamBase*>&) override {}
    void collect_ports(std::vector<VividPortDescriptor>&) override {}
};

VIVID_REGISTER(NamespaceOp)

} // namespace operators
} // namespace vivid
```

### 4d. `no_doc_block.cpp`
```cpp
// File: no_doc_block.cpp
#include "operator_api/operator.h"

struct NoDocOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "NoDocOp";
    void collect_params(std::vector<vivid::ParamBase*>&) override {}
    void collect_ports(std::vector<VividPortDescriptor>&) override {}
};

VIVID_REGISTER(NoDocOp)
```

### 4e. `malformed_cpp.cpp`
```cpp
// File: malformed_cpp.cpp
#include "operator_api/operator.h"

struct MalformedOp : vivid::OperatorBase {
    static constexpr const char* kName = "MalformedOp";
    void collect_params(std::vector<vivid::ParamBase*>&) override {
        // Missing closing brace intentionally
    void collect_ports(std::vector<VividPortDescriptor>&) override {
```

### 4f. `multiple_types.cpp`
```cpp
// File: multiple_types.cpp
#include "operator_api/operator.h"

struct FirstOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "FirstOp";
    void collect_params(std::vector<vivid::ParamBase*>&) override {}
    void collect_ports(std::vector<VividPortDescriptor>&) override {}
};

/**
 * @brief Second operator with docs.
 */
struct SecondOp : vivid::OperatorBase, vivid::GpuProcessable {
    static constexpr const char* kName = "SecondOp";
    void collect_params(std::vector<vivid::ParamBase*>&) override {}
    void collect_ports(std::vector<VividPortDescriptor>&) override {}
};

VIVID_REGISTER(FirstOp)
VIVID_REGISTER(SecondOp)
```

### 4g. `doc_comment_variants.cpp`
```cpp
// File: doc_comment_variants.cpp
#include "operator_api/operator.h"

/**
 * @brief Block comment with docs.
 * @param x X parameter
 */
struct BlockDocOp : vivid::OperatorBase, vivid::FrameProcessable {
    static constexpr const char* kName = "BlockDocOp";
    vivid::Param<float> x {"x", 1.0f, 0.0f, 1.0f};
    void collect_params(std::vector<vivid::ParamBase*>& out) override {
        out.push_back(&x);
    }
    void collect_ports(std::vector<VividPortDescriptor>&) override {}
};

// Single-line comment (should NOT be a doc comment)
struct SingleLineCommentOp : vivid::OperatorBase {
    static constexpr const char* kName = "SingleLineCommentOp";
};

/* Block comment that is NOT a doc comment (no @brief) */
struct NonDocBlockOp : vivid::OperatorBase {
    static constexpr const char* kName = "NonDocBlockOp";
};

VIVID_REGISTER(BlockDocOp)
VIVID_REGISTER(SingleLineCommentOp)
VIVID_REGISTER(NonDocBlockOp)
```

### 4h. `include_directives.cpp`
```cpp
// File: include_directives.cpp
#include "operator_api/operator.h"
#include "some_local_header.h"
#include "operators/shared/another_header.h"
#include <system_header>
#include <another/system.h>

struct IncludeOp : vivid::OperatorBase {
    static constexpr const char* kName = "IncludeOp";
};

VIVID_REGISTER(IncludeOp)
```

---

## 5. Test File: `test_source_syntax_parser.cpp`

Location: `tests/core/test_source_syntax_parser.cpp`

Tests:
1. **Parse valid C++** — verify all records are populated correctly
2. **Parse multiline class** — verify base classes extracted from continuation lines
3. **Parse templated bases** — verify base names are extracted correctly
4. **Parse namespace-wrapped** — verify type definitions found in namespaces
5. **Parse no-doc block** — verify empty doc_comment_ranges
6. **Parse malformed C++** — verify graceful fallback (empty record, no crash)
7. **Parse multiple types** — verify all types found in one file
8. **Parse doc comment variants** — verify block doc comments vs. single-line comments
9. **Parse include directives** — verify include targets extracted correctly
10. **Cache invalidation** — verify invalidate_root/invalidate_file work
11. **Extension filtering** — verify non-C++ files are skipped
12. **File not found** — verify graceful fallback
13. **Byte-for-byte descriptor comparison** — verify parsed data matches expected

---

## 6. CMake Integration

### 6a. `cmake/dependencies.cmake`

Add tree-sitter and tree-sitter-cpp FetchContent declarations as described in §2.

### 6b. `cmake/tests.cmake`

Add the test executable:

```cmake
add_executable(test_source_syntax_parser
    tests/core/test_source_syntax_parser.cpp
    src/runtime/core/source_syntax_parser.cpp
)
target_include_directories(test_source_syntax_parser PRIVATE src tests)
target_link_libraries(test_source_syntax_parser PRIVATE
    vivid_operator_api tree_sitter_runtime tree_sitter_cpp_lib
    nlohmann_json::nlohmann_json
)
add_test(NAME test_source_syntax_parser COMMAND test_source_syntax_parser)
set_tests_properties(test_source_syntax_parser PROPERTIES TIMEOUT 15)
```

### 6c. `docs/ARCHITECTURE.md`

Update dependency manifest section.

---

## 7. Acceptance Criteria

1. `cmake --build build` compiles `tree_sitter_cpp_lib` without errors
2. `SourceSyntaxParser::parse()` returns correct records for all test fixtures
3. Malformed C++ returns empty records without crashing
4. Cache invalidation works through existing flows
5. All existing tests still pass
6. `test_source_syntax_parser` passes with all 13 test cases

---

## 8. Implementation Order

1. **Day 1-2:** Pin tree-sitter + tree-sitter-cpp in CMake, verify compilation
2. **Day 3-5:** Implement `SourceSyntaxParser` library (parsing logic)
3. **Day 5-6:** Create test fixtures
4. **Day 6-7:** Write `test_source_syntax_parser.cpp`
5. **Day 7:** Wire into CMake, run tests, fix any issues
