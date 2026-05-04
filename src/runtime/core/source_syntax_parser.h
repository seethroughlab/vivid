#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace vivid {

struct SourceRange {
    uint32_t start_byte = 0;
    uint32_t end_byte = 0;
    int start_line = 0;
    int end_line = 0;
};

struct TypeDefinition {
    std::string name;
    std::string kind;
    std::string path;
    SourceRange range;
    int start_line = 0;
    int end_line = 0;
    std::vector<std::string> base_class_names;
};

struct RegisterCall {
    std::string macro_name;
    std::string type_name;
    std::string path;
    SourceRange range;
    int line = 0;
    uint32_t body_start_byte = 0;
    uint32_t body_end_byte = 0;
};

struct IncludeTarget {
    std::string quoted_path;
    bool is_system = false;
    SourceRange range;
};

struct DocCommentRange {
    SourceRange range;
    int start_line = 0;
    int end_line = 0;
};

struct SymbolDefinition {
    std::string name;
    std::string kind;
    std::string path;
    SourceRange range;
    int start_line = 0;
    int end_line = 0;
};

struct MethodDefinition {
    std::string name;
    std::string path;
    SourceRange range;
    uint32_t body_start_byte = 0;
    uint32_t body_end_byte = 0;
};

struct MemberConstant {
    std::string name;
    std::string path;
    SourceRange range;
    std::string value_text;
};

struct SourceSyntaxRecord {
    std::string path;
    std::vector<TypeDefinition> type_definitions;
    std::vector<RegisterCall> register_calls;
    std::vector<IncludeTarget> include_targets;
    std::vector<DocCommentRange> doc_comment_ranges;
    std::vector<SymbolDefinition> symbol_definitions;
    std::vector<MethodDefinition> method_definitions;
    std::vector<MemberConstant> member_constants;
    bool valid = false;
    std::string raw_source;
};

class SourceSyntaxParser {
public:
    static SourceSyntaxRecord parse(const std::string& file_path);

    static std::string get_extension(const std::string& file_path);
    static bool is_cpp_extension(const std::string& ext);

    static void invalidate_root(const std::string& root);
    static void invalidate_file(const std::string& file_path);
    static bool has_cached(const std::string& file_path);
    static SourceSyntaxRecord get_cached(const std::string& file_path);
    static void clear_cache();

private:
    static std::unordered_map<std::string, SourceSyntaxRecord>& cache();
};

} // namespace vivid
