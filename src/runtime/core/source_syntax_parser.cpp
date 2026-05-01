#include "runtime/core/source_syntax_parser.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

// tree-sitter C API
extern "C" {
#include <tree_sitter/api.h>
}

// tree-sitter-cpp grammar
extern "C" TSLanguage* tree_sitter_cpp();

namespace vivid {
namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Cache management
// ---------------------------------------------------------------------------

std::unordered_map<std::string, SourceSyntaxRecord>& SourceSyntaxParser::cache() {
    static std::unordered_map<std::string, SourceSyntaxRecord> s_cache;
    return s_cache;
}

void SourceSyntaxParser::invalidate_root(const std::string& root) {
    if (root.empty()) return;
    auto& c = cache();
    for (auto it = c.begin(); it != c.end();) {
        if (it->first.rfind(root, 0) == 0) {
            it = c.erase(it);
        } else {
            ++it;
        }
    }
}

void SourceSyntaxParser::invalidate_file(const std::string& file_path) {
    auto& c = cache();
    c.erase(file_path);
}

bool SourceSyntaxParser::has_cached(const std::string& file_path) {
    return cache().count(file_path) > 0;
}

SourceSyntaxRecord SourceSyntaxParser::get_cached(const std::string& file_path) {
    auto it = cache().find(file_path);
    if (it != cache().end()) {
        return it->second;
    }
    return SourceSyntaxRecord{};
}

void SourceSyntaxParser::clear_cache() {
    cache().clear();
}

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

std::string SourceSyntaxParser::get_extension(const std::string& file_path) {
    fs::path p(file_path);
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext;
}

bool SourceSyntaxParser::is_cpp_extension(const std::string& ext) {
    static const std::unordered_set<std::string> kCppExts = {
        ".cpp", ".cc", ".cxx", ".mm", ".h", ".hh", ".hpp", ".c", ".m",
    };
    return kCppExts.count(ext) > 0;
}

SourceSyntaxRecord SourceSyntaxParser::parse(const std::string& file_path) {
    // Check cache first — try canonical path
    fs::path canonical_path;
    std::error_code ec;
    if (fs::exists(file_path, ec)) {
        canonical_path = fs::weakly_canonical(file_path, ec);
    }
    if (ec || !fs::exists(canonical_path, ec)) {
        SourceSyntaxRecord record;
        record.valid = false;
        return record;
    }
    std::string path_str = canonical_path.generic_string();

    // Check cache
    auto cached = get_cached(path_str);
    if (cached.valid) {
        return cached;
    }

    // Check extension
    std::string ext = get_extension(path_str);
    if (!is_cpp_extension(ext)) {
        return SourceSyntaxRecord{};
    }

    // Read source file
    std::ifstream f(path_str, std::ios::binary);
    if (!f.is_open()) {
        return SourceSyntaxRecord{};
    }
    std::string source((std::istreambuf_iterator<char>(f)),
                        std::istreambuf_iterator<char>());
    f.close();

    if (source.empty()) {
        return SourceSyntaxRecord{};
    }

    // Parse with tree-sitter
    SourceSyntaxRecord record;
    record.raw_source = std::move(source);
    record.path = path_str;

    // Create parser
    TSParser* parser = ts_parser_new();
    if (!parser) {
        return SourceSyntaxRecord{};
    }

    // Set language
    ts_parser_set_language(parser, tree_sitter_cpp());

    // Parse
    TSTree* tree = ts_parser_parse_string(
        parser,
        nullptr,  // old tree (none for first parse)
        record.raw_source.data(),
        static_cast<uint32_t>(record.raw_source.size())
    );

    if (!tree) {
        ts_parser_delete(parser);
        record.valid = false;
        return record;
    }

    // Get root node
    TSNode root_node = ts_tree_root_node(tree);

    // Walk the tree
    record = SourceSyntaxRecord{};  // reset
    record.path = path_str;
    record.raw_source = record.raw_source;  // already set
    record.valid = true;

    walk_node(root_node, path_str, record.raw_source, record);

    // Cleanup
    ts_tree_delete(tree);
    ts_parser_delete(parser);

    // Cache the result
    cache()[path_str] = record;

    return record;
}

// ---------------------------------------------------------------------------
// Text extraction helpers
// ---------------------------------------------------------------------------

// Get the text of a node. ts_node_string() returns a pointer to the start
// of the SOURCE buffer. We must add the node's start byte offset.
static std::string node_text_str(TSNode node, const std::string& source) {
    const char* base = ts_node_string(node);
    uint32_t start = ts_node_start_byte(node);
    uint32_t len = ts_node_end_byte(node) - start;
    return std::string(base + start, len);
}

// Get the line number from a TSPoint.
static int point_to_line(TSPoint point) {
    return static_cast<int>(point.row) + 1;  // 1-indexed
}

// ---------------------------------------------------------------------------
// Tree walking helpers
// ---------------------------------------------------------------------------

std::string SourceSyntaxParser::node_type(TSNode node) {
    return ts_node_type(node);
}

std::string SourceSyntaxParser::node_name(TSNode node, const std::string& source) {
    // Get the name child (named child at index 0 for declarations)
    uint32_t name_child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < name_child_count; ++i) {
        TSNode child = ts_node_named_child(node, i);
        const char* type = ts_node_type(child);
        if (std::string(type) == "identifier" ||
            std::string(type) == "type_identifier") {
            return node_text_str(child, source);
        }
    }
    return "";
}

std::vector<std::string> SourceSyntaxParser::extract_bases(TSNode node, const std::string& source) {
    std::vector<std::string> bases;

    // Find base_class_clause or base_clause child
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        TSNode child = ts_node_child(node, i);
        const char* type = ts_node_type(child);
        if (std::string(type) == "base_class_clause" ||
            std::string(type) == "base_clause") {
            // Iterate over base class entries
            uint32_t base_count = ts_node_child_count(child);
            for (uint32_t j = 0; j < base_count; ++j) {
                TSNode base_node = ts_node_child(child, j);
                const char* base_type = ts_node_type(base_node);
                if (std::string(base_type) == "type_identifier" ||
                    std::string(base_type) == "qualified_identifier" ||
                    std::string(base_type) == "field_identifier") {
                    // Get the type name
                    uint32_t type_child_count = ts_node_named_child_count(base_node);
                    for (uint32_t k = 0; k < type_child_count; ++k) {
                        TSNode type_child = ts_node_named_child(base_node, k);
                        const char* ct = ts_node_type(type_child);
                        if (std::string(ct) == "type_identifier" ||
                            std::string(ct) == "identifier") {
                            bases.push_back(node_text_str(type_child, source));
                        }
                    }
                }
            }
            break;
        }
    }

    return bases;
}

bool SourceSyntaxParser::is_doc_comment(const std::string& comment_text) {
    // Doc comments start with /**
    for (size_t i = 0; i < comment_text.size(); ++i) {
        if (comment_text[i] == '/') {
            if (i + 1 < comment_text.size() && comment_text[i + 1] == '*') {
                if (i + 2 < comment_text.size() && comment_text[i + 2] == '*') {
                    return true;
                }
                // */ immediately after /* = empty block comment, not a doc comment
                if (i + 2 < comment_text.size() && comment_text[i + 2] == '/') {
                    return false;
                }
                return false;
            }
            return false;
        }
        if (comment_text[i] != ' ' && comment_text[i] != '\t') {
            return false;
        }
    }
    return false;
}

std::string SourceSyntaxParser::extract_string_literal(TSNode node, const std::string& source) {
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        TSNode child = ts_node_child(node, i);
        const char* type = ts_node_type(child);
        if (std::string(type) == "string_content") {
            return node_text_str(child, source);
        }
    }
    return "";
}

bool SourceSyntaxParser::extract_call_name_arg(TSNode node, const std::string& source,
                                                std::string& name, std::string& arg) {
    const char* type = ts_node_type(node);
    if (std::string(type) != "call_expression") {
        return false;
    }

    // First named child is the function name
    uint32_t name_child_count = ts_node_named_child_count(node);
    if (name_child_count == 0) return false;

    TSNode name_node = ts_node_named_child(node, 0);
    const char* name_type = ts_node_type(name_node);
    
    // Handle both direct identifier and field_expression (method calls)
    if (std::string(name_type) == "identifier" ||
        std::string(name_type) == "type_identifier") {
        name = node_text_str(name_node, source);
    } else if (std::string(name_type) == "field_expression") {
        // For method calls like "out.push_back", get the field name
        uint32_t field_count = ts_node_named_child_count(name_node);
        for (uint32_t i = 0; i < field_count; ++i) {
            TSNode field = ts_node_named_child(name_node, i);
            if (std::string(ts_node_type(field)) == "field_identifier") {
                name = node_text_str(field, source);
                break;
            }
        }
    }
    
    if (name.empty()) return false;

    // Second named child is the arguments
    if (name_child_count < 2) return false;
    TSNode args_node = ts_node_named_child(node, 1);
    const char* args_type = ts_node_type(args_node);
    if (std::string(args_type) != "arguments") return false;

    // Get first argument
    uint32_t arg_count = ts_node_child_count(args_node);
    for (uint32_t i = 0; i < arg_count; ++i) {
        TSNode arg_node = ts_node_child(args_node, i);
        const char* arg_type = ts_node_type(arg_node);
        if (std::string(arg_type) == "identifier" ||
            std::string(arg_type) == "type_identifier" ||
            std::string(arg_type) == "scoped_identifier") {
            arg = node_text_str(arg_node, source);
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// Recursive tree walker
// ---------------------------------------------------------------------------

void SourceSyntaxParser::walk_node(TSNode node, const std::string& path,
                                    const std::string& source,
                                    SourceSyntaxRecord& record) {
    std::string type = node_type(node);
    TSPoint start = ts_node_start_point(node);
    TSPoint end = ts_node_end_point(node);
    int start_line = point_to_line(start);
    int end_line = point_to_line(end);

    // 1. Type definitions (struct/class declarations)
    // tree-sitter-cpp uses "struct_specifier" for both struct and class
    if (type == "struct_specifier") {
        // Determine kind: struct or class (check for 'class' keyword child)
        std::string kind = "struct";
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; ++i) {
            TSNode child = ts_node_child(node, i);
            const char* ct = ts_node_type(child);
            // The 'class' keyword is a named child
            if (std::string(ct) == "class") {
                kind = "class";
                break;
            }
        }

        std::string name = node_name(node, source);
        if (!name.empty()) {
            TypeDefinition def;
            def.name = name;
            def.kind = kind;
            def.path = path;
            def.start_line = start_line;
            def.end_line = end_line;
            def.base_class_names = extract_bases(node, source);
            record.type_definitions.push_back(std::move(def));
        }
    }

    // 2. Register calls (VIVID_REGISTER, VIVID_DEFINE_OP)
    if (type == "call_expression") {
        std::string call_name, call_arg;
        if (extract_call_name_arg(node, source, call_name, call_arg)) {
            if (call_name == "VIVID_REGISTER" || call_name == "VIVID_DEFINE_OP") {
                RegisterCall rc;
                rc.macro_name = call_name;
                rc.type_name = call_arg;
                rc.path = path;
                rc.line = start_line;
                record.register_calls.push_back(std::move(rc));
            }
        }
    }

    // 3. Include directives
    if (type == "preproc_include") {
        IncludeTarget inc;
        inc.is_system = false;

        // Find the path child (string_literal or system_lib_string)
        uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; ++i) {
            TSNode child = ts_node_child(node, i);
            const char* ct = ts_node_type(child);
            if (std::string(ct) == "system_lib_string") {
                inc.is_system = true;
                // Extract the path between <>
                inc.quoted_path = node_text_str(child, source);
                // Remove surrounding <>
                if (!inc.quoted_path.empty() && inc.quoted_path.front() == '<') {
                    inc.quoted_path = inc.quoted_path.substr(1, inc.quoted_path.size() - 2);
                }
            } else if (std::string(ct) == "string_literal") {
                // Regular include ("...")
                inc.is_system = false;
                inc.quoted_path = extract_string_literal(node, source);
            }
        }

        if (!inc.quoted_path.empty()) {
            record.include_targets.push_back(std::move(inc));
        }
    }

    // 4. Doc comments
    if (type == "comment") {
        std::string comment_text = node_text_str(node, source);
        if (is_doc_comment(comment_text)) {
            DocCommentRange dcr;
            dcr.start_line = start_line;
            dcr.end_line = end_line;
            record.doc_comment_ranges.push_back(dcr);
        }
    }

    // 5. Symbol definitions
    if (type == "struct_specifier" || type == "function_definition" ||
        type == "enum_declaration" || type == "namespace_definition" ||
        type == "type_alias_declaration" || type == "typedef_declaration") {
        std::string name = node_name(node, source);
        if (!name.empty()) {
            SymbolDefinition sym;
            sym.name = name;
            sym.kind = type;
            sym.path = path;
            sym.start_line = start_line;
            sym.end_line = end_line;
            record.symbol_definitions.push_back(std::move(sym));
        }
    }

    // Recurse into children
    uint32_t child_count = ts_node_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        TSNode child = ts_node_child(node, i);
        walk_node(child, path, source, record);
    }
}

} // namespace vivid
