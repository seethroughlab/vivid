#pragma once

#include <string>
#include <vector>
#include <unordered_map>

// tree-sitter C API (needed for TSNode in public API)
extern "C" {
#include <tree_sitter/api.h>
}

namespace vivid {

// ---------------------------------------------------------------------------
// Parsed data structures returned by SourceSyntaxParser
// ---------------------------------------------------------------------------

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
    std::string path;  // file path where parsed
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

// ---------------------------------------------------------------------------
// SourceSyntaxParser — wraps tree-sitter C API + tree_sitter_cpp() grammar
// ---------------------------------------------------------------------------

class SourceSyntaxParser {
public:
    // Parse a single file. Returns empty record on failure (graceful fallback).
    static SourceSyntaxRecord parse(const std::string& file_path);

    // Get the file extension (lowercase, with dot).
    static std::string get_extension(const std::string& file_path);

    // Check if a file extension is C/C++/ObjC.
    static bool is_cpp_extension(const std::string& ext);

    // Invalidate cache for a given root prefix (called by existing invalidate flows).
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

    // Walk tree nodes recursively.
    static void walk_node(TSNode node, const std::string& path,
                          const std::string& source, SourceSyntaxRecord& record);

    // Get node type string.
    static std::string node_type(TSNode node);

    // Get node name (identifier).
    static std::string node_name(TSNode node, const std::string& source);

    // Extract base class names from a node.
    static std::vector<std::string> extract_bases(TSNode node, const std::string& source);

    // Check if a comment is a doc comment (starts with /**).
    static bool is_doc_comment(const std::string& comment_text);

    // Extract string content from a string literal node.
    static std::string extract_string_literal(TSNode node, const std::string& source);

    // Extract a function call name and its first argument.
    static bool extract_call_name_arg(TSNode node, const std::string& source,
                                       std::string& name, std::string& arg);
};

} // namespace vivid
