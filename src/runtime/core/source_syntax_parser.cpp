#include "runtime/core/source_syntax_parser.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <regex>
#include <string_view>
#include <unordered_set>

extern "C" {
#include <tree_sitter/api.h>
TSLanguage* tree_sitter_cpp();
}

namespace vivid {
namespace fs = std::filesystem;

namespace {

constexpr std::size_t kMaxParseFileBytes = 512 * 1024;

struct ParserState {
    const std::string& path;
    const std::string& source;
    SourceSyntaxRecord& record;
};

std::string trim_copy(std::string text) {
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    auto begin = std::find_if(text.begin(), text.end(), not_space);
    auto end = std::find_if(text.rbegin(), text.rend(), not_space).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

std::string canonicalize_path(const std::string& file_path) {
    if (file_path.empty()) {
        return {};
    }
    std::error_code ec;
    fs::path path(file_path);
    if (!fs::exists(path, ec)) {
        return {};
    }
    fs::path normalized = fs::weakly_canonical(path, ec);
    if (ec) {
        ec.clear();
        normalized = fs::absolute(path, ec).lexically_normal();
    }
    if (ec) {
        return {};
    }
    return normalized.generic_string();
}

std::string slice_bytes(const std::string& source, uint32_t start_byte, uint32_t end_byte) {
    if (start_byte >= end_byte || end_byte > source.size()) {
        return {};
    }
    return source.substr(start_byte, end_byte - start_byte);
}

SourceRange node_range(TSNode node) {
    const TSPoint start = ts_node_start_point(node);
    const TSPoint end = ts_node_end_point(node);
    return SourceRange{
        ts_node_start_byte(node),
        ts_node_end_byte(node),
        static_cast<int>(start.row) + 1,
        static_cast<int>(end.row) + 1,
    };
}

bool is_identifier_type(const char* type) {
    return std::strcmp(type, "identifier") == 0 ||
           std::strcmp(type, "type_identifier") == 0 ||
           std::strcmp(type, "field_identifier") == 0 ||
           std::strcmp(type, "namespace_identifier") == 0;
}

std::string last_identifier_in_subtree(TSNode node, const std::string& source) {
    if (ts_node_is_null(node)) {
        return {};
    }
    const char* type = ts_node_type(node);
    if (is_identifier_type(type)) {
        return slice_bytes(source, ts_node_start_byte(node), ts_node_end_byte(node));
    }

    std::string best;
    const uint32_t named_child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < named_child_count; ++i) {
        std::string candidate = last_identifier_in_subtree(ts_node_named_child(node, i), source);
        if (!candidate.empty()) {
            best = std::move(candidate);
        }
    }
    return best;
}

std::string node_name_by_field(TSNode node, const std::string& source) {
    TSNode name_node = ts_node_child_by_field_name(node, "name", 4);
    if (!ts_node_is_null(name_node)) {
        std::string text = last_identifier_in_subtree(name_node, source);
        if (!text.empty()) {
            return text;
        }
    }

    const uint32_t named_child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < named_child_count; ++i) {
        TSNode child = ts_node_named_child(node, i);
        const char* type = ts_node_type(child);
        if (is_identifier_type(type)) {
            return slice_bytes(source, ts_node_start_byte(child), ts_node_end_byte(child));
        }
    }
    return {};
}

std::vector<std::string> extract_base_names(TSNode node, const std::string& source) {
    std::vector<std::string> bases;
    const uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        TSNode child = ts_node_named_child(node, i);
        const char* type = ts_node_type(child);
        if (std::strcmp(type, "base_class_clause") != 0 &&
            std::strcmp(type, "base_clause") != 0) {
            continue;
        }

        const uint32_t base_count = ts_node_named_child_count(child);
        for (uint32_t j = 0; j < base_count; ++j) {
            TSNode base_node = ts_node_named_child(child, j);
            std::string base_name = last_identifier_in_subtree(base_node, source);
            if (base_name.empty()) {
                continue;
            }
            if (std::find(bases.begin(), bases.end(), base_name) == bases.end()) {
                bases.push_back(std::move(base_name));
            }
        }
        break;
    }
    return bases;
}

bool is_doc_comment_text(const std::string& text) {
    return text.rfind("/**", 0) == 0;
}

std::string strip_include_delimiters(std::string text) {
    if (text.size() >= 2) {
        if ((text.front() == '"' && text.back() == '"') ||
            (text.front() == '<' && text.back() == '>')) {
            return text.substr(1, text.size() - 2);
        }
    }
    return text;
}

std::optional<MemberConstant> parse_member_constant(TSNode node,
                                                    const std::string& path,
                                                    const std::string& source) {
    const std::string text = slice_bytes(source, ts_node_start_byte(node), ts_node_end_byte(node));
    static const std::regex re(
        R"(static\s+constexpr\b[\s\S]*?\b(kName|kTimeDependent)\b\s*=\s*([^;]+);)");
    std::smatch match;
    if (!std::regex_search(text, match, re)) {
        return std::nullopt;
    }

    MemberConstant constant;
    constant.name = match[1].str();
    constant.path = path;
    constant.range = node_range(node);
    constant.value_text = trim_copy(match[2].str());
    return constant;
}

void append_type_definition(TSNode node, ParserState& state) {
    TypeDefinition def;
    def.name = node_name_by_field(node, state.source);
    if (def.name.empty()) {
        return;
    }

    const char* type = ts_node_type(node);
    def.kind = std::strcmp(type, "class_specifier") == 0 ? "class" : "struct";
    def.path = state.path;
    def.range = node_range(node);
    def.start_line = def.range.start_line;
    def.end_line = def.range.end_line;
    def.base_class_names = extract_base_names(node, state.source);
    state.record.type_definitions.push_back(std::move(def));

    SymbolDefinition symbol;
    symbol.name = state.record.type_definitions.back().name;
    symbol.kind = state.record.type_definitions.back().kind == "class"
        ? "class_declaration"
        : "struct_declaration";
    symbol.path = state.path;
    symbol.range = state.record.type_definitions.back().range;
    symbol.start_line = symbol.range.start_line;
    symbol.end_line = symbol.range.end_line;
    state.record.symbol_definitions.push_back(std::move(symbol));
}

void append_function_like_definition(TSNode node, ParserState& state) {
    SymbolDefinition symbol;
    symbol.path = state.path;
    symbol.range = node_range(node);
    symbol.start_line = symbol.range.start_line;
    symbol.end_line = symbol.range.end_line;

    const char* type = ts_node_type(node);
    if (std::strcmp(type, "namespace_definition") == 0) {
        symbol.name = node_name_by_field(node, state.source);
        symbol.kind = "namespace";
    } else if (std::strcmp(type, "enum_specifier") == 0 ||
               std::strcmp(type, "enum_declaration") == 0) {
        symbol.name = node_name_by_field(node, state.source);
        symbol.kind = "enum_declaration";
    } else if (std::strcmp(type, "type_alias_declaration") == 0 ||
               std::strcmp(type, "alias_declaration") == 0 ||
               std::strcmp(type, "typedef_declaration") == 0) {
        symbol.name = node_name_by_field(node, state.source);
        symbol.kind = "alias";
    } else if (std::strcmp(type, "function_definition") == 0) {
        TSNode declarator = ts_node_child_by_field_name(node, "declarator", 10);
        if (ts_node_is_null(declarator)) {
            declarator = node;
        }
        symbol.name = last_identifier_in_subtree(declarator, state.source);
        symbol.kind = "function";

        MethodDefinition method;
        method.name = symbol.name;
        method.path = state.path;
        method.range = symbol.range;
        TSNode body = ts_node_child_by_field_name(node, "body", 4);
        if (!ts_node_is_null(body)) {
            method.body_start_byte = ts_node_start_byte(body);
            method.body_end_byte = ts_node_end_byte(body);
        }
        if (!method.name.empty()) {
            state.record.method_definitions.push_back(std::move(method));
        }
    }

    if (!symbol.name.empty()) {
        state.record.symbol_definitions.push_back(std::move(symbol));
    }
}

void walk_tree(TSNode node, ParserState& state) {
    const char* type = ts_node_type(node);

    if (std::strcmp(type, "struct_specifier") == 0 ||
        std::strcmp(type, "class_specifier") == 0) {
        append_type_definition(node, state);
    } else if (std::strcmp(type, "preproc_include") == 0) {
        IncludeTarget include;
        include.range = node_range(node);

        const uint32_t child_count = ts_node_child_count(node);
        for (uint32_t i = 0; i < child_count; ++i) {
            TSNode child = ts_node_child(node, i);
            const char* child_type = ts_node_type(child);
            if (std::strcmp(child_type, "system_lib_string") == 0) {
                include.is_system = true;
                include.quoted_path = strip_include_delimiters(
                    slice_bytes(state.source, ts_node_start_byte(child), ts_node_end_byte(child)));
                break;
            }
            if (std::strcmp(child_type, "string_literal") == 0) {
                include.is_system = false;
                include.quoted_path = strip_include_delimiters(
                    slice_bytes(state.source, ts_node_start_byte(child), ts_node_end_byte(child)));
                break;
            }
        }

        if (!include.quoted_path.empty()) {
            state.record.include_targets.push_back(std::move(include));
        }
    } else if (std::strcmp(type, "comment") == 0) {
        const std::string comment = slice_bytes(
            state.source, ts_node_start_byte(node), ts_node_end_byte(node));
        if (is_doc_comment_text(comment)) {
            DocCommentRange range;
            range.range = node_range(node);
            range.start_line = range.range.start_line;
            range.end_line = range.range.end_line;
            state.record.doc_comment_ranges.push_back(std::move(range));
        }
    } else if (std::strcmp(type, "field_declaration") == 0) {
        auto constant = parse_member_constant(node, state.path, state.source);
        if (constant.has_value()) {
            state.record.member_constants.push_back(std::move(*constant));
        }
    } else if (std::strcmp(type, "function_definition") == 0 ||
               std::strcmp(type, "namespace_definition") == 0 ||
               std::strcmp(type, "enum_specifier") == 0 ||
               std::strcmp(type, "enum_declaration") == 0 ||
               std::strcmp(type, "type_alias_declaration") == 0 ||
               std::strcmp(type, "alias_declaration") == 0 ||
               std::strcmp(type, "typedef_declaration") == 0) {
        append_function_like_definition(node, state);
    }

    const uint32_t child_count = ts_node_named_child_count(node);
    for (uint32_t i = 0; i < child_count; ++i) {
        walk_tree(ts_node_named_child(node, i), state);
    }
}

bool is_token_boundary(char ch) {
    return !(std::isalnum(static_cast<unsigned char>(ch)) || ch == '_');
}

bool starts_with_token(const std::string& source, std::size_t pos, const std::string& token) {
    if (pos + token.size() > source.size()) {
        return false;
    }
    if (source.compare(pos, token.size(), token) != 0) {
        return false;
    }
    if (pos > 0 && !is_token_boundary(source[pos - 1])) {
        return false;
    }
    const std::size_t end = pos + token.size();
    if (end < source.size() && !is_token_boundary(source[end])) {
        return false;
    }
    return true;
}

std::size_t skip_ws(const std::string& source, std::size_t pos) {
    while (pos < source.size() && std::isspace(static_cast<unsigned char>(source[pos]))) {
        ++pos;
    }
    return pos;
}

std::optional<std::size_t> find_matching_delimiter(const std::string& source,
                                                   std::size_t open_pos,
                                                   char open_ch,
                                                   char close_ch) {
    int depth = 0;
    bool in_line_comment = false;
    bool in_block_comment = false;
    bool in_string = false;
    bool in_char = false;
    for (std::size_t i = open_pos; i < source.size(); ++i) {
        const char ch = source[i];
        const char next = (i + 1 < source.size()) ? source[i + 1] : '\0';

        if (in_line_comment) {
            if (ch == '\n') {
                in_line_comment = false;
            }
            continue;
        }
        if (in_block_comment) {
            if (ch == '*' && next == '/') {
                in_block_comment = false;
                ++i;
            }
            continue;
        }
        if (in_string) {
            if (ch == '\\') {
                ++i;
                continue;
            }
            if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (in_char) {
            if (ch == '\\') {
                ++i;
                continue;
            }
            if (ch == '\'') {
                in_char = false;
            }
            continue;
        }

        if (ch == '/' && next == '/') {
            in_line_comment = true;
            ++i;
            continue;
        }
        if (ch == '/' && next == '*') {
            in_block_comment = true;
            ++i;
            continue;
        }
        if (ch == '"') {
            in_string = true;
            continue;
        }
        if (ch == '\'') {
            in_char = true;
            continue;
        }

        if (ch == open_ch) {
            ++depth;
        } else if (ch == close_ch) {
            --depth;
            if (depth == 0) {
                return i;
            }
        }
    }
    return std::nullopt;
}

int line_for_byte(const std::string& source, std::size_t byte) {
    int line = 1;
    const std::size_t capped = std::min(byte, source.size());
    for (std::size_t i = 0; i < capped; ++i) {
        if (source[i] == '\n') {
            ++line;
        }
    }
    return line;
}

std::string first_macro_argument(const std::string& args_text) {
    std::string current;
    int paren_depth = 0;
    int brace_depth = 0;
    int bracket_depth = 0;
    bool in_string = false;
    for (std::size_t i = 0; i < args_text.size(); ++i) {
        const char ch = args_text[i];
        if (in_string) {
            current.push_back(ch);
            if (ch == '\\' && i + 1 < args_text.size()) {
                current.push_back(args_text[++i]);
                continue;
            }
            if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (ch == '"') {
            in_string = true;
            current.push_back(ch);
            continue;
        }
        if (ch == '(') ++paren_depth;
        if (ch == ')') --paren_depth;
        if (ch == '{') ++brace_depth;
        if (ch == '}') --brace_depth;
        if (ch == '[') ++bracket_depth;
        if (ch == ']') --bracket_depth;
        if (ch == ',' && paren_depth == 0 && brace_depth == 0 && bracket_depth == 0) {
            break;
        }
        current.push_back(ch);
    }
    return trim_copy(current);
}

void extract_register_calls(SourceSyntaxRecord& record) {
    static const std::array<std::string, 2> kMacros = {
        "VIVID_REGISTER",
        "VIVID_DEFINE_OP",
    };

    const std::string& source = record.raw_source;
    for (std::size_t pos = 0; pos < source.size(); ++pos) {
        for (const auto& macro_name : kMacros) {
            if (!starts_with_token(source, pos, macro_name)) {
                continue;
            }

            std::size_t cursor = skip_ws(source, pos + macro_name.size());
            if (cursor >= source.size() || source[cursor] != '(') {
                continue;
            }
            const auto close_paren = find_matching_delimiter(source, cursor, '(', ')');
            if (!close_paren.has_value()) {
                continue;
            }

            RegisterCall call;
            call.macro_name = macro_name;
            call.type_name = first_macro_argument(
                source.substr(cursor + 1, *close_paren - cursor - 1));
            call.path = record.path;
            call.range.start_byte = static_cast<uint32_t>(pos);
            call.range.end_byte = static_cast<uint32_t>(*close_paren + 1);
            call.range.start_line = line_for_byte(source, pos);
            call.range.end_line = line_for_byte(source, *close_paren);
            call.line = call.range.start_line;

            std::size_t body_pos = skip_ws(source, *close_paren + 1);
            if (macro_name == "VIVID_DEFINE_OP" &&
                body_pos < source.size() &&
                source[body_pos] == '{') {
                const auto close_brace = find_matching_delimiter(source, body_pos, '{', '}');
                if (close_brace.has_value()) {
                    call.body_start_byte = static_cast<uint32_t>(body_pos);
                    call.body_end_byte = static_cast<uint32_t>(*close_brace + 1);
                    call.range.end_byte = static_cast<uint32_t>(*close_brace + 1);
                    call.range.end_line = line_for_byte(source, *close_brace);
                    pos = *close_brace;
                } else {
                    pos = *close_paren;
                }
            } else {
                pos = *close_paren;
            }

            record.register_calls.push_back(std::move(call));
            break;
        }
    }
}

} // namespace

std::unordered_map<std::string, SourceSyntaxRecord>& SourceSyntaxParser::cache() {
    static std::unordered_map<std::string, SourceSyntaxRecord> s_cache;
    return s_cache;
}

std::string SourceSyntaxParser::get_extension(const std::string& file_path) {
    std::string ext = fs::path(file_path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return ext;
}

bool SourceSyntaxParser::is_cpp_extension(const std::string& ext) {
    static const std::unordered_set<std::string> kCppExtensions = {
        ".c", ".cc", ".cpp", ".cxx", ".h", ".hh", ".hpp", ".inl", ".m", ".mm",
    };
    return kCppExtensions.count(ext) > 0;
}

void SourceSyntaxParser::invalidate_root(const std::string& root) {
    const std::string normalized = canonicalize_path(root);
    if (normalized.empty()) {
        return;
    }

    auto& entries = cache();
    for (auto it = entries.begin(); it != entries.end();) {
        if (it->first.rfind(normalized, 0) == 0) {
            it = entries.erase(it);
        } else {
            ++it;
        }
    }
}

void SourceSyntaxParser::invalidate_file(const std::string& file_path) {
    const std::string normalized = canonicalize_path(file_path);
    if (normalized.empty()) {
        return;
    }
    cache().erase(normalized);
}

bool SourceSyntaxParser::has_cached(const std::string& file_path) {
    const std::string normalized = canonicalize_path(file_path);
    return !normalized.empty() && cache().count(normalized) > 0;
}

SourceSyntaxRecord SourceSyntaxParser::get_cached(const std::string& file_path) {
    const std::string normalized = canonicalize_path(file_path);
    if (normalized.empty()) {
        return {};
    }
    auto it = cache().find(normalized);
    return it == cache().end() ? SourceSyntaxRecord{} : it->second;
}

void SourceSyntaxParser::clear_cache() {
    cache().clear();
}

SourceSyntaxRecord SourceSyntaxParser::parse(const std::string& file_path) {
    const std::string normalized_path = canonicalize_path(file_path);
    if (normalized_path.empty()) {
        return {};
    }

    if (auto it = cache().find(normalized_path); it != cache().end()) {
        return it->second;
    }

    if (!is_cpp_extension(get_extension(normalized_path))) {
        return {};
    }

    std::error_code ec;
    const auto file_size = fs::file_size(normalized_path, ec);
    if (!ec && file_size > kMaxParseFileBytes) {
        return {};
    }

    std::ifstream input(normalized_path, std::ios::binary);
    if (!input.is_open()) {
        return {};
    }

    SourceSyntaxRecord record;
    record.path = normalized_path;
    record.raw_source.assign(std::istreambuf_iterator<char>(input),
                             std::istreambuf_iterator<char>());
    if (record.raw_source.empty()) {
        cache()[normalized_path] = record;
        return record;
    }

    TSParser* parser = ts_parser_new();
    if (!parser) {
        return record;
    }
    ts_parser_set_language(parser, tree_sitter_cpp());

    TSTree* tree = ts_parser_parse_string(
        parser,
        nullptr,
        record.raw_source.c_str(),
        static_cast<uint32_t>(record.raw_source.size()));

    if (!tree) {
        ts_parser_delete(parser);
        cache()[normalized_path] = record;
        return record;
    }

    record.valid = true;
    extract_register_calls(record);

    ParserState state{record.path, record.raw_source, record};
    walk_tree(ts_tree_root_node(tree), state);

    ts_tree_delete(tree);
    ts_parser_delete(parser);

    cache()[normalized_path] = record;
    return record;
}

} // namespace vivid
