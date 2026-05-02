#include "runtime/gpu/wgsl_uniform_layout.h"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace vivid {

namespace {

struct Token {
    std::string text;
    char symbol = '\0';
    bool is_identifier = false;
};

uint32_t align_up(uint32_t value, uint32_t alignment) {
    if (alignment == 0) return value;
    const uint32_t rem = value % alignment;
    return rem == 0 ? value : value + (alignment - rem);
}

bool is_identifier_start(char ch) {
    return std::isalpha(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool is_identifier_continue(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) != 0 || ch == '_';
}

bool is_symbol_char(char ch) {
    switch (ch) {
        case '{':
        case '}':
        case '(':
        case ')':
        case '[':
        case ']':
        case '<':
        case '>':
        case ':':
        case ',':
        case ';':
        case '@':
            return true;
        default:
            return false;
    }
}

std::vector<Token> tokenize_wgsl(const std::string& source) {
    std::vector<Token> tokens;
    std::size_t i = 0;
    while (i < source.size()) {
        const char ch = source[i];
        if (std::isspace(static_cast<unsigned char>(ch))) {
            ++i;
            continue;
        }
        if (ch == '/' && i + 1 < source.size()) {
            if (source[i + 1] == '/') {
                i += 2;
                while (i < source.size() && source[i] != '\n') ++i;
                continue;
            }
            if (source[i + 1] == '*') {
                i += 2;
                while (i + 1 < source.size() &&
                       !(source[i] == '*' && source[i + 1] == '/')) {
                    ++i;
                }
                if (i + 1 < source.size()) i += 2;
                continue;
            }
        }
        if (is_identifier_start(ch)) {
            const std::size_t start = i++;
            while (i < source.size() && is_identifier_continue(source[i])) ++i;
            tokens.push_back({source.substr(start, i - start), '\0', true});
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(ch))) {
            const std::size_t start = i++;
            while (i < source.size() && std::isdigit(static_cast<unsigned char>(source[i]))) ++i;
            tokens.push_back({source.substr(start, i - start), '\0', true});
            continue;
        }
        if (is_symbol_char(ch)) {
            tokens.push_back({std::string(1, ch), ch, false});
        }
        ++i;
    }
    return tokens;
}

bool token_is(const Token& token, const char* text) {
    return token.is_identifier && token.text == text;
}

std::string canonicalize_type(const std::vector<Token>& tokens,
                              std::size_t start,
                              std::size_t end) {
    std::string type;
    for (std::size_t i = start; i < end; ++i) {
        type += tokens[i].text;
    }
    return type;
}

struct TypeLayout {
    std::string cpp_declaration_suffix;
    uint32_t size = 0;
    uint32_t alignment = 0;
};

bool parse_positive_integer(const std::string& text, uint32_t& value) {
    if (text.empty()) return false;
    uint64_t acc = 0;
    for (char ch : text) {
        if (!std::isdigit(static_cast<unsigned char>(ch))) return false;
        acc = acc * 10u + static_cast<uint32_t>(ch - '0');
        if (acc > 0xffffffffu) return false;
    }
    value = static_cast<uint32_t>(acc);
    return true;
}

std::optional<TypeLayout> resolve_wgsl_type_layout(const std::string& canonical_type) {
    if (canonical_type == "f32" || canonical_type == "float") {
        return TypeLayout{"float", 4, 4};
    }
    if (canonical_type == "i32" || canonical_type == "int") {
        return TypeLayout{"int32_t", 4, 4};
    }
    if (canonical_type == "u32" || canonical_type == "uint") {
        return TypeLayout{"uint32_t", 4, 4};
    }
    if (canonical_type == "bool") {
        return TypeLayout{"uint32_t", 4, 4};
    }

    const auto try_vec = [&](const char* prefix,
                             const char* scalar_suffix,
                             const char* cpp_scalar) -> std::optional<TypeLayout> {
        if (canonical_type.rfind(prefix, 0) != 0) return std::nullopt;
        const std::size_t len = std::char_traits<char>::length(prefix);
        if (canonical_type.size() <= len) return std::nullopt;
        const char dim_ch = canonical_type[len];
        if (dim_ch < '2' || dim_ch > '4') return std::nullopt;
        const uint32_t count = static_cast<uint32_t>(dim_ch - '0');
        const std::string tail = canonical_type.substr(len + 1);
        if (tail != scalar_suffix) return std::nullopt;
        return TypeLayout{
            std::string(cpp_scalar) + "[" + std::to_string(count) + "]",
            count * 4u,
            count == 2 ? 8u : 16u
        };
    };

    if (auto layout = try_vec("vec", "f", "float")) return layout;
    if (auto layout = try_vec("vec", "i", "int32_t")) return layout;
    if (auto layout = try_vec("vec", "u", "uint32_t")) return layout;
    if (auto layout = try_vec("vec", "<f32>", "float")) return layout;
    if (auto layout = try_vec("vec", "<i32>", "int32_t")) return layout;
    if (auto layout = try_vec("vec", "<u32>", "uint32_t")) return layout;

    if (canonical_type.rfind("mat", 0) == 0) {
        if (canonical_type.size() < 6) return std::nullopt;
        const char cols_ch = canonical_type[3];
        const char rows_ch = canonical_type[4];
        if (cols_ch < '2' || cols_ch > '4' || rows_ch < '2' || rows_ch > '4') {
            return std::nullopt;
        }
        const std::string tail = canonical_type.substr(5);
        if (tail != "f" && tail != "<f32>") return std::nullopt;
        const uint32_t cols = static_cast<uint32_t>(cols_ch - '0');
        const uint32_t rows = static_cast<uint32_t>(rows_ch - '0');
        const uint32_t column_size = rows * 4u;
        const uint32_t column_align = rows == 2 ? 8u : 16u;
        const uint32_t column_stride = align_up(column_size, column_align);
        return TypeLayout{
            "float[" + std::to_string(cols * rows) + "]",
            cols * column_stride,
            column_align
        };
    }

    if (canonical_type.rfind("array<", 0) == 0 && canonical_type.back() == '>') {
        const std::string inner = canonical_type.substr(6, canonical_type.size() - 7);
        std::size_t comma = std::string::npos;
        int depth = 0;
        for (std::size_t i = 0; i < inner.size(); ++i) {
            if (inner[i] == '<') ++depth;
            else if (inner[i] == '>') --depth;
            else if (inner[i] == ',' && depth == 0) {
                comma = i;
                break;
            }
        }
        if (comma == std::string::npos) return std::nullopt;
        const std::string elem_type = inner.substr(0, comma);
        const std::string count_text = inner.substr(comma + 1);
        uint32_t count = 0;
        if (!parse_positive_integer(count_text, count)) return std::nullopt;
        auto elem = resolve_wgsl_type_layout(elem_type);
        if (!elem) return std::nullopt;
        const uint32_t stride = align_up(elem->size, elem->alignment);
        return TypeLayout{
            "std::byte[" + std::to_string(stride * count) + "]",
            stride * count,
            elem->alignment
        };
    }

    return std::nullopt;
}

bool find_struct_tokens(const std::vector<Token>& tokens,
                        const std::string& struct_name,
                        std::size_t& open_brace_index,
                        std::size_t& close_brace_index) {
    for (std::size_t i = 0; i + 2 < tokens.size(); ++i) {
        if (!token_is(tokens[i], "struct")) continue;
        if (!tokens[i + 1].is_identifier || tokens[i + 1].text != struct_name) continue;
        if (tokens[i + 2].symbol != '{') continue;
        open_brace_index = i + 2;
        int depth = 1;
        for (std::size_t j = open_brace_index + 1; j < tokens.size(); ++j) {
            if (tokens[j].symbol == '{') ++depth;
            else if (tokens[j].symbol == '}') {
                --depth;
                if (depth == 0) {
                    close_brace_index = j;
                    return true;
                }
            }
        }
        return false;
    }
    return false;
}

std::string find_next_raw_string(const std::string& source, std::size_t& cursor) {
    while (cursor + 2 < source.size()) {
        const std::size_t start = source.find("R\"", cursor);
        if (start == std::string::npos) {
            cursor = source.size();
            return {};
        }
        std::size_t delim_end = source.find('(', start + 2);
        if (delim_end == std::string::npos) {
            cursor = source.size();
            return {};
        }
        const std::string delimiter = source.substr(start + 2, delim_end - (start + 2));
        const std::string terminator = ")" + delimiter + "\"";
        const std::size_t content_start = delim_end + 1;
        const std::size_t end = source.find(terminator, content_start);
        if (end == std::string::npos) {
            cursor = source.size();
            return {};
        }
        cursor = end + terminator.size();
        return source.substr(content_start, end - content_start);
    }
    return {};
}

} // namespace

std::optional<WgslUniformLayout> parse_wgsl_uniform_layout(
    const std::string& wgsl_source,
    std::string& error,
    const std::string& struct_name) {
    error.clear();
    const std::vector<Token> tokens = tokenize_wgsl(wgsl_source);
    std::size_t open_brace_index = 0;
    std::size_t close_brace_index = 0;
    if (!find_struct_tokens(tokens, struct_name, open_brace_index, close_brace_index)) {
        error = "Could not find WGSL struct '" + struct_name + "'";
        return std::nullopt;
    }

    WgslUniformLayout layout;
    layout.struct_name = struct_name;

    uint32_t cursor = 0;
    uint32_t max_alignment = 1;
    std::size_t i = open_brace_index + 1;
    while (i < close_brace_index) {
        while (i < close_brace_index && tokens[i].symbol == '@') {
            ++i;
            if (i >= close_brace_index || !tokens[i].is_identifier) {
                error = "Malformed WGSL attribute in struct '" + struct_name + "'";
                return std::nullopt;
            }
            ++i;
            if (i < close_brace_index && tokens[i].symbol == '(') {
                int depth = 1;
                ++i;
                while (i < close_brace_index && depth > 0) {
                    if (tokens[i].symbol == '(') ++depth;
                    else if (tokens[i].symbol == ')') --depth;
                    ++i;
                }
                if (depth != 0) {
                    error = "Unterminated WGSL attribute in struct '" + struct_name + "'";
                    return std::nullopt;
                }
            }
        }
        if (i >= close_brace_index) break;
        if (!tokens[i].is_identifier) {
            error = "Expected WGSL member name in struct '" + struct_name + "'";
            return std::nullopt;
        }
        const std::string member_name = tokens[i++].text;
        if (i >= close_brace_index || tokens[i].symbol != ':') {
            error = "Expected ':' after WGSL member '" + member_name + "'";
            return std::nullopt;
        }
        ++i;
        const std::size_t type_start = i;
        int nested = 0;
        while (i < close_brace_index) {
            if ((tokens[i].symbol == ',' || tokens[i].symbol == ';') && nested == 0) break;
            if (tokens[i].symbol == '<' || tokens[i].symbol == '[' || tokens[i].symbol == '(') ++nested;
            else if (tokens[i].symbol == '>' || tokens[i].symbol == ']' || tokens[i].symbol == ')') --nested;
            ++i;
        }
        if (type_start == i) {
            error = "Missing type for WGSL member '" + member_name + "'";
            return std::nullopt;
        }
        const std::string canonical_type = canonicalize_type(tokens, type_start, i);
        auto type_layout = resolve_wgsl_type_layout(canonical_type);
        if (!type_layout) {
            error = "Unsupported WGSL uniform member type '" + canonical_type + "'";
            return std::nullopt;
        }
        if (i < close_brace_index && (tokens[i].symbol == ',' || tokens[i].symbol == ';')) ++i;

        WgslUniformMemberLayout member;
        member.name = member_name;
        member.wgsl_type = canonical_type;
        member.cpp_declaration = type_layout->cpp_declaration_suffix;
        member.alignment = type_layout->alignment;
        member.size = type_layout->size;
        member.offset = align_up(cursor, member.alignment);
        cursor = member.offset + member.size;
        max_alignment = std::max(max_alignment, member.alignment);
        layout.members.push_back(std::move(member));
    }

    layout.alignment = max_alignment;
    layout.size = align_up(cursor, max_alignment);
    return layout;
}

std::optional<WgslUniformLayout> extract_wgsl_uniform_layout_from_cpp_source(
    const std::string& cpp_source,
    std::string& error,
    const std::string& struct_name) {
    error.clear();
    std::size_t cursor = 0;
    while (cursor < cpp_source.size()) {
        const std::string literal = find_next_raw_string(cpp_source, cursor);
        if (literal.empty()) continue;
        if (literal.find("struct " + struct_name) == std::string::npos &&
            literal.find("struct " + struct_name + " {") == std::string::npos) {
            continue;
        }
        if (literal.find("var<uniform>") == std::string::npos) {
            continue;
        }
        auto layout = parse_wgsl_uniform_layout(literal, error, struct_name);
        if (layout) return layout;
        return std::nullopt;
    }
    error.clear();
    return std::nullopt;
}

} // namespace vivid
