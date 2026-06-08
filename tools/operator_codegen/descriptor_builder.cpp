#include "descriptor_builder.h"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <optional>
#include <sstream>
#include <unordered_set>

namespace vivid {
namespace codegen {

namespace {

std::string trim_copy(std::string text) {
    const auto not_space = [](unsigned char ch) { return !std::isspace(ch); };
    auto begin = std::find_if(text.begin(), text.end(), not_space);
    auto end = std::find_if(text.rbegin(), text.rend(), not_space).base();
    if (begin >= end) {
        return {};
    }
    return std::string(begin, end);
}

bool is_ident_char(char ch) {
    return std::isalnum(static_cast<unsigned char>(ch)) || ch == '_';
}

std::size_t skip_ws(const std::string& text, std::size_t pos) {
    while (pos < text.size() && std::isspace(static_cast<unsigned char>(text[pos]))) {
        ++pos;
    }
    return pos;
}

std::optional<std::size_t> find_matching_delimiter(const std::string& text,
                                                   std::size_t open_pos,
                                                   char open_ch,
                                                   char close_ch) {
    int depth = 0;
    bool in_line_comment = false;
    bool in_block_comment = false;
    bool in_string = false;
    bool in_char = false;
    for (std::size_t i = open_pos; i < text.size(); ++i) {
        const char ch = text[i];
        const char next = (i + 1 < text.size()) ? text[i + 1] : '\0';

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
            // Skip C++14 digit separators (single quote preceded by a hex/decimal digit).
            if (i > open_pos && std::isxdigit(static_cast<unsigned char>(text[i - 1]))) {
                continue;
            }
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

std::vector<std::string> split_top_level(const std::string& text, char delimiter) {
    std::vector<std::string> parts;
    std::string current;
    int paren_depth = 0;
    int brace_depth = 0;
    int bracket_depth = 0;
    int angle_depth = 0;
    bool in_string = false;
    bool in_char = false;

    for (std::size_t i = 0; i < text.size(); ++i) {
        const char ch = text[i];
        if (in_string) {
            current.push_back(ch);
            if (ch == '\\' && i + 1 < text.size()) {
                current.push_back(text[++i]);
                continue;
            }
            if (ch == '"') {
                in_string = false;
            }
            continue;
        }
        if (in_char) {
            current.push_back(ch);
            if (ch == '\\' && i + 1 < text.size()) {
                current.push_back(text[++i]);
                continue;
            }
            if (ch == '\'') {
                in_char = false;
            }
            continue;
        }
        if (ch == '"') {
            in_string = true;
            current.push_back(ch);
            continue;
        }
        if (ch == '\'') {
            in_char = true;
            current.push_back(ch);
            continue;
        }

        // Skip C++ line comments — do not accumulate them into the current token.
        if (ch == '/' && i + 1 < text.size() && text[i + 1] == '/') {
            while (i < text.size() && text[i] != '\n') ++i;
            continue;
        }
        // Skip C-style block comments.
        if (ch == '/' && i + 1 < text.size() && text[i + 1] == '*') {
            i += 2;
            while (i + 1 < text.size() && !(text[i] == '*' && text[i + 1] == '/')) ++i;
            if (i + 1 < text.size()) i += 2;
            continue;
        }

        if (ch == '(') ++paren_depth;
        else if (ch == ')') --paren_depth;
        else if (ch == '{') ++brace_depth;
        else if (ch == '}') --brace_depth;
        else if (ch == '[') ++bracket_depth;
        else if (ch == ']') --bracket_depth;
        else if (ch == '<') ++angle_depth;
        else if (ch == '>' && angle_depth > 0) --angle_depth;

        if (ch == delimiter &&
            paren_depth == 0 &&
            brace_depth == 0 &&
            bracket_depth == 0 &&
            angle_depth == 0) {
            parts.push_back(trim_copy(current));
            current.clear();
            continue;
        }
        current.push_back(ch);
    }

    if (!current.empty()) {
        parts.push_back(trim_copy(current));
    }
    return parts;
}

std::string strip_outer_braces(const std::string& text) {
    std::string trimmed = trim_copy(text);
    if (trimmed.size() >= 2 && trimmed.front() == '{' && trimmed.back() == '}') {
        return trimmed.substr(1, trimmed.size() - 2);
    }
    return trimmed;
}

std::string unquote_string_literal(const std::string& text) {
    std::string trimmed = trim_copy(text);
    if (trimmed.size() >= 2 && trimmed.front() == '"' && trimmed.back() == '"') {
        return trimmed.substr(1, trimmed.size() - 2);
    }
    return trimmed;
}

std::string escape_for_c_string(const std::string& text) {
    std::string out;
    out.reserve(text.size() + 8);
    for (char ch : text) {
        switch (ch) {
            case '\\': out += "\\\\"; break;
            case '"': out += "\\\""; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default: out.push_back(ch); break;
        }
    }
    return out;
}

std::string normalize_cpp_type(std::string text) {
    text = trim_copy(std::move(text));
    if (text.rfind("vivid::", 0) == 0) {
        text = text.substr(7);
    }
    return text;
}

std::string map_param_type(const std::string& cpp_type) {
    if (cpp_type == "float") return "VIVID_PARAM_FLOAT";
    if (cpp_type == "int") return "VIVID_PARAM_INT";
    if (cpp_type == "bool") return "VIVID_PARAM_BOOL";
    if (cpp_type == "FilePath") return "VIVID_PARAM_FILE";
    if (cpp_type == "TextValue") return "VIVID_PARAM_TEXT";
    return {};
}

std::string extract_identifier(const std::string& text) {
    std::string trimmed = trim_copy(text);
    std::size_t start = 0;
    while (start < trimmed.size() && !is_ident_char(trimmed[start])) {
        ++start;
    }
    std::size_t end = start;
    while (end < trimmed.size() && is_ident_char(trimmed[end])) {
        ++end;
    }
    return start < end ? trimmed.substr(start, end - start) : std::string();
}

std::vector<std::string> parse_value_list_exprs(const std::string& text) {
    std::string trimmed = trim_copy(text);
    if (trimmed.empty()) {
        return {};
    }
    if (trimmed.front() == '{' && trimmed.back() == '}') {
        return split_top_level(trimmed.substr(1, trimmed.size() - 2), ',');
    }
    return {trimmed};
}

std::vector<std::string> extract_push_back_arguments(const std::string& method_body) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while ((pos = method_body.find("push_back", pos)) != std::string::npos) {
        std::size_t open_paren = method_body.find('(', pos);
        if (open_paren == std::string::npos) {
            break;
        }
        const auto close_paren = find_matching_delimiter(method_body, open_paren, '(', ')');
        if (!close_paren.has_value()) {
            break;
        }
        const std::string args = method_body.substr(open_paren + 1, *close_paren - open_paren - 1);
        auto parts = split_top_level(args, ',');
        if (!parts.empty()) {
            out.push_back(trim_copy(parts.front()));
        }
        pos = *close_paren + 1;
    }
    return out;
}

std::string strip_ampersand(std::string text) {
    text = trim_copy(std::move(text));
    if (!text.empty() && text.front() == '&') {
        text.erase(text.begin());
    }
    return trim_copy(std::move(text));
}

// ---------------------------------------------------------------------------
// For-loop array param support (e.g. std::array<Param<float>, N> arr_)
// ---------------------------------------------------------------------------

// Evaluate C string literal concatenation: '"kick_b_" "0"' → "kick_b_0"
// Returns the concatenated string (unquoted), or empty if not string literals.
std::string eval_c_string_concat(const std::string& text) {
    const std::string trimmed = trim_copy(text);
    std::string result;
    std::size_t pos = 0;
    bool found = false;
    while (pos < trimmed.size()) {
        while (pos < trimmed.size() &&
               std::isspace(static_cast<unsigned char>(trimmed[pos]))) ++pos;
        if (pos >= trimmed.size() || trimmed[pos] != '"') break;
        found = true;
        ++pos;  // skip opening "
        while (pos < trimmed.size()) {
            const char c = trimmed[pos];
            if (c == '\\' && pos + 1 < trimmed.size()) {
                result.push_back(trimmed[++pos]);
                ++pos;
            } else if (c == '"') {
                ++pos;  // skip closing "
                break;
            } else {
                result.push_back(c);
                ++pos;
            }
        }
    }
    return found ? result : std::string{};
}

struct MacroDef {
    std::string name;
    std::vector<std::string> params;
    std::string body;
};

// Find a function-like #define NAME(params) body in text, handling backslash continuation.
std::optional<MacroDef> find_macro_def(const std::string& macro_name, const std::string& text) {
    const std::string needle = "#define " + macro_name + "(";
    std::size_t pos = 0;
    while ((pos = text.find(needle, pos)) != std::string::npos) {
        const std::size_t paren_pos = pos + needle.size() - 1;
        const auto close = find_matching_delimiter(text, paren_pos, '(', ')');
        if (!close.has_value()) { ++pos; continue; }
        MacroDef def;
        def.name = macro_name;
        const std::string params_text = text.substr(paren_pos + 1, *close - paren_pos - 1);
        for (auto& p : split_top_level(params_text, ',')) def.params.push_back(trim_copy(p));
        // Collect body, joining backslash-continuation lines
        std::string body;
        std::size_t lp = *close + 1;
        while (lp < text.size() && text[lp] != '\n' &&
               std::isspace(static_cast<unsigned char>(text[lp]))) ++lp;
        while (lp < text.size()) {
            std::size_t le = text.find('\n', lp);
            if (le == std::string::npos) le = text.size();
            std::string line = text.substr(lp, le - lp);
            while (!line.empty() && std::isspace(static_cast<unsigned char>(line.back()))) line.pop_back();
            const bool cont = !line.empty() && line.back() == '\\';
            if (cont) line.pop_back();
            body += line;
            lp = le + 1;
            if (!cont) break;
            body += ' ';
        }
        def.body = trim_copy(body);
        return def;
    }
    return std::nullopt;
}

// Expand one function-like macro call.
std::string expand_macro_call(const MacroDef& def, const std::vector<std::string>& actual_args) {
    std::string result = def.body;
    // Build substitutions sorted longest-first to prevent partial matches
    std::vector<std::pair<std::string, std::string>> subs;
    for (std::size_t i = 0; i < def.params.size() && i < actual_args.size(); ++i) {
        if (!def.params[i].empty()) subs.push_back({def.params[i], actual_args[i]});
    }
    std::stable_sort(subs.begin(), subs.end(),
                     [](const auto& a, const auto& b) { return a.first.size() > b.first.size(); });
    for (const auto& [param, arg] : subs) {
        std::string out;
        out.reserve(result.size());
        std::size_t p = 0;
        while (p < result.size()) {
            const std::size_t f = result.find(param, p);
            if (f == std::string::npos) { out += result.substr(p); break; }
            const bool lb = (f == 0 || !is_ident_char(result[f - 1]));
            const bool rb = (f + param.size() >= result.size() ||
                             !is_ident_char(result[f + param.size()]));
            if (lb && rb) {
                out += result.substr(p, f - p);
                out += arg;
                p = f + param.size();
            } else {
                out.push_back(result[f]);
                p = f + 1;
            }
        }
        result = std::move(out);
    }
    return result;
}

// Expand all function-like macro calls in text using definitions found in source_texts.
// Iterates until no further expansion occurs (max 10 passes).
std::string expand_macros(const std::string& text,
                           const std::vector<const std::string*>& source_texts) {
    std::string result = text;
    for (int iter = 0; iter < 10; ++iter) {
        std::string new_result;
        new_result.reserve(result.size() * 2);
        std::size_t pos = 0;
        bool changed = false;
        while (pos < result.size()) {
            if (!std::isalpha(static_cast<unsigned char>(result[pos])) && result[pos] != '_') {
                new_result.push_back(result[pos++]);
                continue;
            }
            const std::size_t id_start = pos;
            while (pos < result.size() && is_ident_char(result[pos])) ++pos;
            const std::string id = result.substr(id_start, pos - id_start);
            const std::size_t after = skip_ws(result, pos);
            if (after < result.size() && result[after] == '(') {
                std::optional<MacroDef> def;
                for (const auto* src : source_texts) {
                    def = find_macro_def(id, *src);
                    if (def.has_value()) break;
                }
                if (def.has_value()) {
                    const auto close = find_matching_delimiter(result, after, '(', ')');
                    if (close.has_value()) {
                        const std::string args_text = result.substr(after + 1, *close - after - 1);
                        auto actual_args = split_top_level(args_text, ',');
                        for (auto& a : actual_args) a = trim_copy(a);
                        new_result += expand_macro_call(*def, actual_args);
                        pos = *close + 1;
                        changed = true;
                        continue;
                    }
                }
            }
            new_result += result.substr(id_start, pos - id_start);
        }
        result = std::move(new_result);
        if (!changed) break;
    }
    return result;
}

struct ArrayParamInfo {
    std::string cpp_type;
    std::string initializer_content;  // after macro expansion, ready to parse
};

// Find std::array<vivid::Param<TYPE>, N> ARRAY_NAME = {{...}} and return the
// initializer content after expanding macros.
std::optional<ArrayParamInfo> find_array_param_info(
    const std::string& array_name,
    const std::vector<const std::string*>& combined_texts) {
    for (const auto* text_ptr : combined_texts) {
        const auto& text = *text_ptr;
        std::size_t search_pos = 0;
        while (search_pos < text.size()) {
            const std::size_t name_pos = text.find(array_name, search_pos);
            if (name_pos == std::string::npos) break;
            search_pos = name_pos + array_name.size();
            // Word boundary check
            if (name_pos > 0 && is_ident_char(text[name_pos - 1])) continue;
            if (search_pos < text.size() && is_ident_char(text[search_pos])) continue;
            // Look backward (up to 300 chars) for "std::array"
            const std::size_t back_limit = (name_pos > 300) ? name_pos - 300 : 0;
            const std::size_t arr_pos = text.rfind("std::array", name_pos);
            if (arr_pos == std::string::npos || arr_pos < back_limit) continue;
            // "Param<" must appear between arr_pos and name_pos
            const std::size_t param_pos = text.find("Param<", arr_pos);
            if (param_pos == std::string::npos || param_pos >= name_pos) continue;
            const std::size_t tmpl_open = param_pos + 5;
            const auto tmpl_close = find_matching_delimiter(text, tmpl_open, '<', '>');
            if (!tmpl_close.has_value() || *tmpl_close >= name_pos) continue;
            const std::string cpp_type = normalize_cpp_type(
                text.substr(tmpl_open + 1, *tmpl_close - tmpl_open - 1));
            if (map_param_type(cpp_type).empty()) continue;
            // After array_name, find optional '=' then '{{'
            std::size_t cursor = skip_ws(text, search_pos);
            if (cursor < text.size() && text[cursor] == '=') cursor = skip_ws(text, cursor + 1);
            if (cursor >= text.size() || text[cursor] != '{') continue;
            const auto outer_close = find_matching_delimiter(text, cursor, '{', '}');
            if (!outer_close.has_value()) continue;
            // outer_content is everything between the outer { and its matching }
            const std::string outer_content = text.substr(cursor + 1, *outer_close - cursor - 1);
            // Strip one more level of braces for double-brace init {{...}}
            const std::string trimmed_outer = trim_copy(outer_content);
            std::string init_content;
            if (!trimmed_outer.empty() && trimmed_outer.front() == '{') {
                const auto inner_close = find_matching_delimiter(trimmed_outer, 0, '{', '}');
                if (inner_close.has_value()) {
                    init_content = trimmed_outer.substr(1, *inner_close - 1);
                } else {
                    init_content = trimmed_outer;
                }
            } else {
                init_content = trimmed_outer;
            }
            // Expand macros using all combined_texts as macro sources
            const std::string expanded = expand_macros(init_content, combined_texts);
            return ArrayParamInfo{cpp_type, expanded};
        }
    }
    return std::nullopt;
}

// Parse an expanded array initializer into ParamSpec entries.
// Each element is a {name, default, min, max} brace-init tuple.
std::vector<ParamSpec> parse_array_init_content(const std::string& array_name,
                                                  const std::string& cpp_type,
                                                  const std::string& expanded_content) {
    std::vector<ParamSpec> out;
    int idx = 0;
    std::size_t pos = 0;
    while (pos < expanded_content.size()) {
        const std::size_t brace_open = expanded_content.find('{', pos);
        if (brace_open == std::string::npos) break;
        const auto brace_close = find_matching_delimiter(expanded_content, brace_open, '{', '}');
        if (!brace_close.has_value()) break;
        const std::string elem = expanded_content.substr(brace_open + 1, *brace_close - brace_open - 1);
        auto parts = split_top_level(elem, ',');
        if (!parts.empty()) {
            ParamSpec spec;
            spec.cpp_type = cpp_type;
            spec.vivid_param_type = map_param_type(cpp_type);
            spec.variable_name = array_name + "_" + std::to_string(idx);
            const std::string raw_name = trim_copy(parts[0]);
            const std::string concat = eval_c_string_concat(raw_name);
            spec.param_name = concat.empty() ? unquote_string_literal(raw_name) : concat;
            if (cpp_type == "float" || cpp_type == "int") {
                if (parts.size() >= 2) spec.default_value_expr = trim_copy(parts[1]);
                if (parts.size() >= 3) spec.min_value_expr    = trim_copy(parts[2]);
                if (parts.size() >= 4) spec.max_value_expr    = trim_copy(parts[3]);
                if (cpp_type == "int" && parts.size() >= 3) {
                    const std::string choices_arg = trim_copy(parts[2]);
                    if (!choices_arg.empty() && choices_arg.front() == '{') {
                        spec.min_value_expr = "0";
                        spec.choice_label_exprs =
                            split_top_level(strip_outer_braces(choices_arg), ',');
                        spec.max_value_expr = spec.choice_label_exprs.empty()
                            ? "0" : std::to_string(spec.choice_label_exprs.size() - 1);
                    }
                }
            } else if (cpp_type == "bool") {
                if (parts.size() >= 2) spec.default_value_expr = trim_copy(parts[1]);
                spec.min_value_expr = "0.0f";
                spec.max_value_expr = "1.0f";
            }
            if (!spec.param_name.empty()) {
                out.push_back(std::move(spec));
                ++idx;
            }
        }
        pos = *brace_close + 1;
    }
    return out;
}

// Process a collect_params body in order, handling both direct push_backs and
// range-for loops over std::array<Param<TYPE>, N> members.
// class_texts: source texts containing array declarations and macro definitions.
std::vector<ParamSpec> process_collect_body(
    const std::string& body_text,
    const std::unordered_map<std::string, ParamSpec>& by_var_name,
    const std::vector<const std::string*>& class_texts) {
    std::vector<ParamSpec> out;
    std::size_t pos = 0;
    while (pos < body_text.size()) {
        // Find next "for" keyword or "push_back", whichever comes first
        const std::size_t push_pos = body_text.find("push_back", pos);
        std::size_t for_pos = std::string::npos;
        {
            std::size_t fp = pos;
            while ((fp = body_text.find("for", fp)) != std::string::npos) {
                const bool lb = (fp == 0 || !is_ident_char(body_text[fp - 1]));
                const bool rb = (fp + 3 >= body_text.size() || !is_ident_char(body_text[fp + 3]));
                if (lb && rb) { for_pos = fp; break; }
                fp += 3;
            }
        }
        if (push_pos == std::string::npos && for_pos == std::string::npos) break;
        const std::size_t next = std::min(
            push_pos == std::string::npos ? body_text.size() : push_pos,
            for_pos  == std::string::npos ? body_text.size() : for_pos);

        if (next == for_pos) {
            // Range-for loop: for (auto& p : ARRAY) out.push_back(&p)
            const std::size_t paren_open = skip_ws(body_text, for_pos + 3);
            if (paren_open >= body_text.size() || body_text[paren_open] != '(') {
                pos = for_pos + 3;
                continue;
            }
            const auto paren_close = find_matching_delimiter(body_text, paren_open, '(', ')');
            if (!paren_close.has_value()) break;
            const std::string for_header =
                body_text.substr(paren_open + 1, *paren_close - paren_open - 1);
            const std::size_t colon_pos = for_header.find(':');
            // Find for-body (braced or single-statement)
            std::size_t body_start = skip_ws(body_text, *paren_close + 1);
            std::size_t after_body;
            std::string for_body;
            if (body_start < body_text.size() && body_text[body_start] == '{') {
                const auto bclose = find_matching_delimiter(body_text, body_start, '{', '}');
                if (!bclose.has_value()) break;
                for_body = body_text.substr(body_start + 1, *bclose - body_start - 1);
                after_body = *bclose + 1;
            } else {
                const std::size_t semi = body_text.find(';', body_start);
                if (semi == std::string::npos) break;
                for_body = body_text.substr(body_start, semi - body_start);
                after_body = semi + 1;
            }
            // Range-for with a push_back body → expand the array
            if (colon_pos != std::string::npos &&
                for_body.find("push_back") != std::string::npos) {
                const std::string array_name = trim_copy(for_header.substr(colon_pos + 1));
                if (!array_name.empty()) {
                    const auto info = find_array_param_info(array_name, class_texts);
                    if (info.has_value()) {
                        auto arr_params = parse_array_init_content(
                            array_name, info->cpp_type, info->initializer_content);
                        for (auto& p : arr_params) out.push_back(std::move(p));
                    }
                }
            }
            pos = after_body;
        } else {
            // Direct push_back — not inside a for-loop
            const std::size_t open_paren = body_text.find('(', push_pos + 9);
            if (open_paren == std::string::npos) { pos = push_pos + 1; continue; }
            const auto close_paren = find_matching_delimiter(body_text, open_paren, '(', ')');
            if (!close_paren.has_value()) break;
            const std::string arg =
                trim_copy(body_text.substr(open_paren + 1, *close_paren - open_paren - 1));
            const std::string var_name = strip_ampersand(arg);
            const auto found = by_var_name.find(var_name);
            if (found != by_var_name.end()) out.push_back(found->second);
            pos = *close_paren + 1;
        }
    }
    return out;
}

std::string source_slice(const std::string& source, uint32_t start_byte, uint32_t end_byte) {
    if (start_byte >= end_byte || end_byte > source.size()) {
        return {};
    }
    return source.substr(start_byte, end_byte - start_byte);
}

std::string normalize_port_expr(std::string expr) {
    expr = trim_copy(std::move(expr));
    if (!expr.empty() && expr.front() == '{') {
        return "VividPortDescriptor" + expr;
    }
    return expr;
}

void apply_param_helper_call(ParamSpec& spec,
                             const std::string& helper_name,
                             const std::vector<std::string>& args) {
    if (helper_name == "description" && args.size() >= 2) {
        spec.description_expr = args[1];
    } else if (helper_name == "semantic_tag" && args.size() >= 2) {
        spec.semantic_tag_expr = args[1];
    } else if (helper_name == "semantic_shape" && args.size() >= 2) {
        spec.semantic_shape_expr = args[1];
    } else if (helper_name == "semantic_unit" && args.size() >= 2) {
        spec.semantic_unit_expr = args[1];
    } else if (helper_name == "semantic_intent" && args.size() >= 2) {
        spec.semantic_intent_expr = args[1];
    } else if (helper_name == "asset_kind" && args.size() >= 2) {
        spec.asset_kind_expr = args[1];
    } else if (helper_name == "param_group" && args.size() >= 2) {
        spec.group_expr = args[1];
    } else if (helper_name == "display_hint" && args.size() >= 2) {
        spec.display_hint_expr = args[1];
    } else if (helper_name == "layout_row" && args.size() >= 3) {
        spec.layout_columns_expr = args[1];
        spec.layout_column_index_expr = args[2];
    } else if (helper_name == "param_widget" && args.size() >= 3) {
        spec.widget_id_expr = args[1];
        spec.widget_span_expr = args[2];
    } else if (helper_name == "repeat_group" && args.size() >= 3) {
        spec.repeat_group_expr = args[1];
        spec.repeat_group_idx_expr = args[2];
    } else if ((helper_name == "visible_when_eq" ||
                helper_name == "visible_when_in") && args.size() >= 3) {
        spec.visible_when_param_expr = args[1] + ".name";
        spec.visible_when_op_expr = "VIVID_PARAM_VIS_EQ";
        spec.visible_when_value_exprs = parse_value_list_exprs(args[2]);
    } else if ((helper_name == "visible_when_ne" ||
                helper_name == "visible_when_not_in") && args.size() >= 3) {
        spec.visible_when_param_expr = args[1] + ".name";
        spec.visible_when_op_expr = "VIVID_PARAM_VIS_NE";
        spec.visible_when_value_exprs = parse_value_list_exprs(args[2]);
    }
}

std::vector<ParamSpec> parse_param_declarations(const std::string& type_body_text) {
    std::vector<ParamSpec> params;
    std::size_t pos = 0;
    while ((pos = type_body_text.find("Param<", pos)) != std::string::npos) {
        if (pos >= 2 && type_body_text.substr(pos - 2, 2) == "->") {
            pos += 5;
            continue;
        }

        const std::size_t template_open = pos + 5;
        const auto template_close = find_matching_delimiter(type_body_text, template_open, '<', '>');
        if (!template_close.has_value()) {
            break;
        }

        ParamSpec spec;
        spec.cpp_type = normalize_cpp_type(
            type_body_text.substr(template_open + 1, *template_close - template_open - 1));
        spec.vivid_param_type = map_param_type(spec.cpp_type);
        if (spec.vivid_param_type.empty()) {
            pos = *template_close + 1;
            continue;
        }

        std::size_t cursor = skip_ws(type_body_text, *template_close + 1);
        std::size_t name_start = cursor;
        while (cursor < type_body_text.size() && is_ident_char(type_body_text[cursor])) {
            ++cursor;
        }
        if (cursor == name_start) {
            pos = *template_close + 1;
            continue;
        }
        spec.variable_name = type_body_text.substr(name_start, cursor - name_start);
        cursor = skip_ws(type_body_text, cursor);
        if (cursor >= type_body_text.size() ||
            (type_body_text[cursor] != '{' && type_body_text[cursor] != '(')) {
            pos = cursor;
            continue;
        }

        const char open_ch = type_body_text[cursor];
        const char close_ch = open_ch == '{' ? '}' : ')';
        const auto init_close = find_matching_delimiter(type_body_text, cursor, open_ch, close_ch);
        if (!init_close.has_value()) {
            break;
        }
        auto args = split_top_level(
            type_body_text.substr(cursor + 1, *init_close - cursor - 1), ',');
        if (args.empty()) {
            pos = *init_close + 1;
            continue;
        }

        spec.param_name = unquote_string_literal(args[0]);
        if (spec.cpp_type == "float" || spec.cpp_type == "int") {
            if (args.size() >= 2) spec.default_value_expr = args[1];
            if (args.size() >= 3) spec.min_value_expr = args[2];
            if (args.size() >= 4) spec.max_value_expr = args[3];
            if (spec.cpp_type == "int" && args.size() >= 3) {
                const std::string choices_arg = trim_copy(args[2]);
                if (choices_arg.front() == '{') {
                    spec.min_value_expr = "0";
                    spec.choice_label_exprs = split_top_level(strip_outer_braces(choices_arg), ',');
                    if (!spec.choice_label_exprs.empty()) {
                        spec.max_value_expr = std::to_string(spec.choice_label_exprs.size() - 1);
                    } else {
                        spec.max_value_expr = "0";
                    }
                } else if (!choices_arg.empty() && choices_arg.back() == ')') {
                    // Function call returning std::vector<std::string> — dynamic choices.
                    spec.dynamic_choices_expr = choices_arg;
                    spec.min_value_expr = "0";
                    spec.max_value_expr = "0";  // patched at runtime via init code
                }
            }
        } else if (spec.cpp_type == "bool") {
            if (args.size() >= 2) spec.default_value_expr = args[1];
            spec.min_value_expr = "0.0f";
            spec.max_value_expr = "1.0f";
        } else if (spec.cpp_type == "FilePath" || spec.cpp_type == "TextValue") {
            spec.default_string_expr = args.size() >= 2 ? args[1] : "\"\"";
        }

        params.push_back(std::move(spec));
        pos = *init_close + 1;
    }
    return params;
}

void apply_param_helper_calls(const std::string& class_text,
                              std::unordered_map<std::string, ParamSpec>& by_var_name) {
    static const std::vector<std::string> kHelperNames = {
        "description",
        "semantic_tag",
        "semantic_shape",
        "semantic_unit",
        "semantic_intent",
        "asset_kind",
        "param_group",
        "display_hint",
        "layout_row",
        "param_widget",
        "repeat_group",
        "visible_when_eq",
        "visible_when_ne",
        "visible_when_in",
        "visible_when_not_in",
    };

    for (const auto& helper_name : kHelperNames) {
        const std::string needle = "vivid::" + helper_name;
        std::size_t pos = 0;
        while ((pos = class_text.find(needle, pos)) != std::string::npos) {
            std::size_t open_paren = class_text.find('(', pos + needle.size());
            if (open_paren == std::string::npos) {
                break;
            }
            const auto close_paren = find_matching_delimiter(class_text, open_paren, '(', ')');
            if (!close_paren.has_value()) {
                break;
            }
            auto args = split_top_level(
                class_text.substr(open_paren + 1, *close_paren - open_paren - 1), ',');
            if (!args.empty()) {
                const std::string variable_name = extract_identifier(args[0]);
                auto it = by_var_name.find(variable_name);
                if (it != by_var_name.end()) {
                    apply_param_helper_call(it->second, helper_name, args);
                    // visible_when_* stores "varname.name" — resolve to a string literal
                    // so the generated global static descriptor doesn't reference a member.
                    if ((helper_name == "visible_when_eq" || helper_name == "visible_when_ne" ||
                         helper_name == "visible_when_in" || helper_name == "visible_when_not_in") &&
                        args.size() >= 2) {
                        const std::string ctrl_var = trim_copy(args[1]);
                        auto ctrl_it = by_var_name.find(ctrl_var);
                        if (ctrl_it != by_var_name.end() && !ctrl_it->second.param_name.empty()) {
                            it->second.visible_when_param_expr =
                                "\"" + ctrl_it->second.param_name + "\"";
                        }
                    }
                }
            }
            pos = *close_paren + 1;
        }
    }
}

void apply_metadata_assignments(const std::string& body_text, OperatorMetadataSpec& metadata) {
    std::vector<std::string> statements = split_top_level(body_text, ';');
    for (auto& statement : statements) {
        statement = trim_copy(statement);
        if (statement.empty()) {
            continue;
        }
        const std::size_t eq = statement.find('=');
        if (eq == std::string::npos) {
            continue;
        }
        const std::string lhs = trim_copy(statement.substr(0, eq));
        const std::string rhs = trim_copy(statement.substr(eq + 1));
        if (lhs == "name") {
            metadata.name_expr = rhs;
        } else if (lhs == "display_name") {
            metadata.display_name_expr = rhs;
        } else if (lhs == "summary") {
            metadata.summary_expr = rhs;
        } else if (lhs == "keywords") {
            metadata.keyword_exprs = split_top_level(strip_outer_braces(rhs), ',');
        }
    }
}

} // namespace

void DescriptorBuilder::add_extra_source(const std::filesystem::path& path) {
    std::ifstream f(path);
    if (!f.is_open()) return;
    extra_source_texts_.emplace_back(
        (std::istreambuf_iterator<char>(f)),
         std::istreambuf_iterator<char>());
}

DescriptorResult DescriptorBuilder::build_from_file(const std::filesystem::path& cpp_source_path) {
    DescriptorResult result;
    result.source_path = cpp_source_path;

    if (!std::filesystem::exists(cpp_source_path)) {
        result.error_message = "File does not exist: " + cpp_source_path.string();
        return result;
    }

    SourceSyntaxRecord record = SourceSyntaxParser::parse(cpp_source_path.string());
    if (!record.valid) {
        result.error_message = "Failed to parse source file.";
        return result;
    }

    process_record(record, result);
    if (!result.error_message.empty()) {
        return result;
    }

    result.generated_cpp = render_registration_cpp(result);
    result.success = !result.generated_cpp.empty();
    if (!result.success && result.error_message.empty()) {
        result.error_message = "Failed to render generated registration source.";
    }
    return result;
}

void DescriptorBuilder::process_record(const SourceSyntaxRecord& record, DescriptorResult& result) {
    for (const auto& include : record.include_targets) {
        result.includes.push_back(include.quoted_path);
    }

    auto is_operator_base = [](const std::string& name) {
        return name == "OperatorBase" || name == "WgslFilterBase" ||
               name == "AudioProcessable" || name == "GpuProcessable" ||
               name == "FrameProcessable";
    };
    for (const auto& type_def : record.type_definitions) {
        for (const auto& base : type_def.base_class_names) {
            if (is_operator_base(base)) {
                if (!result.operator_class_name.empty()) {
                    result.error_message = "Multiple operator subclasses found in " +
                        result.source_path.filename().string() +
                        " — each file must define exactly one operator.";
                    return;
                }
                result.operator_class_name = type_def.name;
                break;
            }
        }
    }

    if (result.operator_class_name.empty()) {
        // Secondary fallback: operator struct is in an included header (e.g. macro.h).
        // VIVID_DEFINE_OP in the .cpp identifies the class name; populate_class_context
        // will locate the type in the headers.
        for (const auto& call : record.register_calls) {
            if (call.macro_name == "VIVID_DEFINE_OP") {
                result.operator_class_name = call.type_name;
                break;
            }
        }
    }

    if (result.operator_class_name.empty()) {
        result.error_message = "No operator class found in " +
            result.source_path.filename().string() +
            " (expected a struct inheriting from OperatorBase, WgslFilterBase, "
            "AudioProcessable, GpuProcessable, or FrameProcessable).";
        return;
    }

    ClassContext context;
    if (!populate_class_context(record, result.source_path, result.operator_class_name, context, result.error_message)) {
        return;
    }

    result.stable_name_expr = result.operator_class_name + "::kName";
    if (context.type_definition) {
        for (const auto& base : context.type_definition->base_class_names) {
            if (base == "AudioProcessable") result.has_process_audio = true;
            if (base == "GpuProcessable") result.has_process_gpu = true;
            if (base == "FrameProcessable") result.has_process_frame = true;
        }
    }
    // Text-based fallback for base class detection (e.g., .mm files where tree-sitter
    // didn't produce a type_definition due to ObjC++ syntax).
    if (!result.has_process_gpu && !result.has_process_audio && !result.has_process_frame
        && !context.class_text.empty()) {
        if (context.class_text.find("GpuProcessable") != std::string::npos)
            result.has_process_gpu = true;
        if (context.class_text.find("AudioProcessable") != std::string::npos)
            result.has_process_audio = true;
        if (context.class_text.find("FrameProcessable") != std::string::npos)
            result.has_process_frame = true;
    }

    populate_param_specs(record, context, result);
    populate_port_specs(record, context, result);
    populate_metadata_specs(record, result);

    // Detect via inline method bodies, VIVID_THUMBNAIL/INSPECTOR/EDITOR macros in primary source,
    // or method definitions in extra source files (EXTRA_CODEGEN_SOURCES). The macro fallback
    // is now unnecessary when the editor file is listed as an extra source — codegen detects
    // the method body directly.
    auto extra_defines = [&](const char* method_name) -> bool {
        for (const auto& extra : extra_source_texts_)
            if (extra.find(method_name) != std::string::npos) return true;
        return false;
    };
    result.has_draw_thumbnail = context.methods.count("draw_thumbnail") > 0
        || record.raw_source.find("VIVID_THUMBNAIL(") != std::string::npos
        || extra_defines("draw_thumbnail");
    result.has_draw_inspector = context.methods.count("draw_inspector") > 0
        || record.raw_source.find("VIVID_INSPECTOR(") != std::string::npos
        || record.raw_source.find("VIVID_INSPECTOR_FULL_MODE(") != std::string::npos
        || extra_defines("draw_inspector");
    result.has_draw_editor    = context.methods.count("draw_editor") > 0
        || record.raw_source.find("VIVID_EDITOR(") != std::string::npos
        || extra_defines("draw_editor");
    if (result.has_draw_inspector)
        result.inspector_full_mode =
            record.raw_source.find("VIVID_INSPECTOR_FULL_MODE(") != std::string::npos;
}

bool DescriptorBuilder::populate_class_context(const SourceSyntaxRecord& record,
                                               const std::filesystem::path& source_path,
                                               const std::string& class_name,
                                               ClassContext& context,
                                               std::string& error_message) {
    const SourceSyntaxRecord* effective_record = &record;

    for (const auto& type_def : record.type_definitions) {
        if (type_def.name == class_name) {
            context.type_definition = &type_def;
            context.type_range = type_def.range;
            break;
        }
    }

    // If not found in the main source, try included local headers.
    if (!context.type_range.has_value()) {
        const std::filesystem::path source_dir = source_path.parent_path();
        for (const auto& inc : record.include_targets) {
            const std::filesystem::path header_path = source_dir / inc.quoted_path;
            if (!std::filesystem::exists(header_path)) continue;
            SourceSyntaxRecord hdr = SourceSyntaxParser::parse(header_path.string());
            if (!hdr.valid) continue;
            bool found_in_header = false;
            for (const auto& type_def : hdr.type_definitions) {
                if (type_def.name == class_name) {
                    found_in_header = true;
                    break;
                }
            }
            if (found_in_header) {
                context.owned_header_record = std::move(hdr);
                effective_record = &*context.owned_header_record;
                for (const auto& td : effective_record->type_definitions) {
                    if (td.name == class_name) {
                        context.type_definition = &td;
                        context.type_range = td.range;
                        break;
                    }
                }
            }
            if (context.type_range.has_value()) break;
        }
    }

    // Text-based fallback: for .mm files or other sources with non-C++ syntax that
    // confuses tree-sitter (Objective-C++ @import, id<T>, etc.), search raw source directly.
    if (!context.type_range.has_value()) {
        const std::string& raw = record.raw_source;
        for (const char* keyword : {"struct ", "class "}) {
            const std::string marker = std::string(keyword) + class_name;
            std::size_t search_pos = 0;
            while (search_pos < raw.size()) {
                const std::size_t found = raw.find(marker, search_pos);
                if (found == std::string::npos) break;
                const std::size_t after = found + marker.size();
                if (after < raw.size() &&
                    (std::isspace(static_cast<unsigned char>(raw[after])) ||
                     raw[after] == ':' || raw[after] == '{')) {
                    const std::size_t brace_start = raw.find('{', after);
                    if (brace_start != std::string::npos) {
                        const auto brace_end = find_matching_delimiter(raw, brace_start, '{', '}');
                        if (brace_end.has_value()) {
                            SourceRange range;
                            range.start_byte = static_cast<uint32_t>(found);
                            range.end_byte = static_cast<uint32_t>(*brace_end + 1);
                            context.type_range = range;
                            context.raw_source = &record.raw_source;
                            break;
                        }
                    }
                }
                search_pos = after;
            }
            if (context.type_range.has_value()) break;
        }
    }

    if (!context.type_range.has_value()) {
        error_message = "Could not find type definition for " + class_name + ".";
        return false;
    }

    if (!context.raw_source) context.raw_source = &effective_record->raw_source;

    context.class_text = source_slice(
        *context.raw_source,
        context.type_range->start_byte,
        context.type_range->end_byte);
    if (context.class_text.empty()) {
        error_message = "Failed to slice class source for " + class_name + ".";
        return false;
    }

    const std::size_t body_open = context.class_text.find('{');
    if (body_open == std::string::npos) {
        error_message = "Could not locate class body for " + class_name + ".";
        return false;
    }
    const auto body_close = find_matching_delimiter(context.class_text, body_open, '{', '}');
    if (!body_close.has_value()) {
        error_message = "Could not match class body braces for " + class_name + ".";
        return false;
    }
    context.type_body_text = context.class_text.substr(body_open + 1, *body_close - body_open - 1);

    for (const auto& method : effective_record->method_definitions) {
        if (method.range.start_byte >= context.type_range->start_byte &&
            method.range.end_byte <= context.type_range->end_byte) {
            context.methods[method.name] = method;
        }
    }
    for (const auto& constant : effective_record->member_constants) {
        if (constant.range.start_byte >= context.type_range->start_byte &&
            constant.range.end_byte <= context.type_range->end_byte) {
            context.constants[constant.name] = constant;
        }
    }

    // Search included headers for base class definitions so populate_param_specs /
    // populate_port_specs can find collect_params / collect_ports that live in a
    // base class (e.g. LFO, DrumSequencerCore, ClockCore, etc.).
    {
        const std::size_t header_end = context.class_text.find('{');
        const std::string class_header = (header_end != std::string::npos)
            ? context.class_text.substr(0, header_end) : context.class_text;
        const std::size_t colon_pos = class_header.find(':');
        if (colon_pos != std::string::npos) {
            static const std::unordered_set<std::string> kSkipBases = {
                "OperatorBase", "AudioProcessable", "GpuProcessable", "FrameProcessable"
            };
            const auto base_parts = split_top_level(class_header.substr(colon_pos + 1), ',');
            std::vector<std::string> base_names;
            for (const auto& part : base_parts) {
                std::string name = trim_copy(part);
                for (const char* qual : {"virtual ", "public ", "protected ", "private "}) {
                    const std::string q(qual);
                    if (name.rfind(q, 0) == 0) name = trim_copy(name.substr(q.size()));
                }
                if (name.find("::") != std::string::npos) continue;
                if (kSkipBases.count(name) || name.empty()) continue;
                base_names.push_back(std::move(name));
            }

            if (!base_names.empty()) {
                const std::filesystem::path source_dir = source_path.parent_path();
                for (const auto& inc : record.include_targets) {
                    const std::filesystem::path hdr_path = source_dir / inc.quoted_path;
                    if (!std::filesystem::exists(hdr_path)) continue;
                    std::ifstream hf(hdr_path);
                    if (!hf.is_open()) continue;
                    const std::string hdr_text(
                        (std::istreambuf_iterator<char>(hf)),
                         std::istreambuf_iterator<char>());

                    for (auto it_b = base_names.begin(); it_b != base_names.end(); ) {
                        bool found_this = false;
                        for (const char* kw : {"struct ", "class "}) {
                            if (found_this) break;
                            const std::string marker = std::string(kw) + *it_b;
                            std::size_t pos = 0;
                            while (pos < hdr_text.size()) {
                                const std::size_t found = hdr_text.find(marker, pos);
                                if (found == std::string::npos) break;
                                const std::size_t after = found + marker.size();
                                if (after < hdr_text.size() &&
                                    (std::isspace(static_cast<unsigned char>(hdr_text[after])) ||
                                     hdr_text[after] == ':' || hdr_text[after] == '{')) {
                                    const std::size_t brace_start = hdr_text.find('{', after);
                                    if (brace_start != std::string::npos) {
                                        const auto brace_end = find_matching_delimiter(
                                            hdr_text, brace_start, '{', '}');
                                        if (brace_end.has_value()) {
                                            context.base_class_text += hdr_text.substr(
                                                found, *brace_end - found + 1);
                                            context.base_class_text += "\n";
                                            found_this = true;
                                            break;
                                        }
                                    }
                                }
                                pos = after;
                            }
                        }
                        if (found_this) it_b = base_names.erase(it_b);
                        else ++it_b;
                    }
                    if (base_names.empty()) break;
                }
            }
        }
    }

    return true;
}

void DescriptorBuilder::populate_param_specs(const SourceSyntaxRecord& record,
                                             const ClassContext& context,
                                             DescriptorResult& result) {
    // Parse Param<> declarations from the registered class body and any base classes.
    auto declared_params = parse_param_declarations(context.type_body_text);
    if (!context.base_class_text.empty()) {
        auto base_params = parse_param_declarations(context.base_class_text);
        for (auto& p : base_params) {
            declared_params.push_back(std::move(p));
        }
    }

    std::unordered_map<std::string, ParamSpec> by_var_name;
    for (auto& param : declared_params) {
        by_var_name.emplace(param.variable_name, std::move(param));
    }

    apply_param_helper_calls(context.class_text, by_var_name);
    if (!context.base_class_text.empty()) {
        apply_param_helper_calls(context.base_class_text, by_var_name);
    }
    // Also apply from the main .cpp source (catches out-of-line constructors like ParametricEQ).
    if (context.raw_source != &record.raw_source && !record.raw_source.empty()) {
        apply_param_helper_calls(record.raw_source, by_var_name);
    }

    // Build class_texts for array-member and macro lookups (used by process_collect_body).
    // Order matters: put the richest source first (base class header with macro #defines).
    std::vector<const std::string*> class_texts;
    if (!context.base_class_text.empty()) class_texts.push_back(&context.base_class_text);
    if (!context.class_text.empty()) class_texts.push_back(&context.class_text);
    if (context.raw_source && !context.raw_source->empty()) class_texts.push_back(context.raw_source);
    if (context.raw_source != &record.raw_source && !record.raw_source.empty())
        class_texts.push_back(&record.raw_source);
    for (const auto& extra : extra_source_texts_)
        if (!extra.empty()) class_texts.push_back(&extra);

    auto it = context.methods.find("collect_params");
    if (it == context.methods.end() || it->second.body_start_byte == 0) {
        // Text-based fallback: search class_text, base_class_text, then raw source.
        // Require '(' immediately after 'collect_params' (only whitespace allowed between)
        // so that mentions in comments (e.g. "sit AFTER bar_sync in collect_params so...")
        // are not mistaken for definitions.
        auto try_collect = [&](const std::string& text) -> bool {
            if (text.empty()) return false;
            std::size_t search_pos = 0;
            while (search_pos < text.size()) {
                const std::size_t method_pos = text.find("collect_params", search_pos);
                if (method_pos == std::string::npos) return false;
                search_pos = method_pos + 14; // advance past "collect_params"
                // The first non-whitespace after 'collect_params' must be '('.
                std::size_t p = search_pos;
                while (p < text.size() && std::isspace(static_cast<unsigned char>(text[p]))) ++p;
                if (p >= text.size() || text[p] != '(') continue;
                // Find the matching ')' for the parameter list.
                const auto paren_end = find_matching_delimiter(text, p, '(', ')');
                if (!paren_end.has_value()) continue;
                // After ')', look for '{' vs ';' to distinguish definition vs declaration.
                const std::size_t body_brace = text.find('{', *paren_end + 1);
                const std::size_t decl_semi  = text.find(';', *paren_end + 1);
                if (decl_semi != std::string::npos &&
                    (body_brace == std::string::npos || decl_semi < body_brace)) {
                    continue; // forward declaration — skip
                }
                if (body_brace == std::string::npos) return false;
                const auto brace_end = find_matching_delimiter(text, body_brace, '{', '}');
                if (!brace_end.has_value()) return false;
                const std::string body_text =
                    text.substr(body_brace + 1, *brace_end - body_brace - 1);
                result.has_collect_params = true;
                auto new_params = process_collect_body(body_text, by_var_name, class_texts);
                for (auto& param : new_params) result.params.push_back(std::move(param));
                return true;
            }
            return false;
        };

        if (!try_collect(context.class_text) &&
            !try_collect(context.base_class_text) &&
            !try_collect(record.raw_source)) {
            for (const auto& extra : extra_source_texts_) {
                if (try_collect(extra)) break;
            }
        }
        return;
    }

    result.has_collect_params = true;
    const std::string body_text = source_slice(
        *context.raw_source, it->second.body_start_byte, it->second.body_end_byte);
    auto new_params = process_collect_body(body_text, by_var_name, class_texts);
    for (auto& param : new_params) result.params.push_back(std::move(param));
}

void DescriptorBuilder::populate_port_specs(const SourceSyntaxRecord& record,
                                            const ClassContext& context,
                                            DescriptorResult& result) {
    // Helper: extract port exprs and advanced flags from a collect_ports body string.
    auto process_port_body = [&](const std::string& body_text) {
        std::vector<std::size_t> pb_positions;
        {
            std::size_t pos = 0;
            while (pos < body_text.size()) {
                const std::size_t found = body_text.find("push_back(", pos);
                if (found == std::string::npos) break;
                pb_positions.push_back(found);
                pos = found + 10;
            }
        }

        for (const auto& arg : extract_push_back_arguments(body_text)) {
            result.port_exprs.push_back(normalize_port_expr(arg));
        }

        if (body_text.find("append_analysis_ports") != std::string::npos) {
            result.port_exprs.push_back(
                "VividPortDescriptor{\"rms\", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT,"
                " VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, \"analysis\"}");
            result.port_exprs.push_back(
                "VividPortDescriptor{\"peak\", VIVID_PORT_SCALAR, VIVID_PORT_OUTPUT,"
                " VIVID_PORT_TRANSPORT_SIGNAL, 0, nullptr, 0, 0.0f, nullptr, \"analysis\"}");
            result.port_exprs.push_back(
                "VividPortDescriptor{.name=\"waveform\", .type=VIVID_PORT_SCALAR, .direction=VIVID_PORT_OUTPUT,"
                " .transport=VIVID_PORT_TRANSPORT_LANE_ARRAY, .semantic_tag=\"analysis\","
                " .multiplicity=VIVID_MULTIPLICITY_MANY}");
        }

        result.port_advanced_flags.assign(result.port_exprs.size(), false);
        {
            std::size_t pos = 0;
            while (pos < body_text.size()) {
                const std::size_t ab_pos = body_text.find("advanced_breakout(", pos);
                if (ab_pos == std::string::npos) break;
                pos = ab_pos + 18;
                int pb_idx = -1;
                for (int i = static_cast<int>(pb_positions.size()) - 1; i >= 0; --i) {
                    if (pb_positions[i] < ab_pos) { pb_idx = i; break; }
                }
                if (pb_idx >= 0 && pb_idx < static_cast<int>(result.port_exprs.size())) {
                    result.port_advanced_flags[static_cast<std::size_t>(pb_idx)] = true;
                }
            }
        }
    };

    auto it = context.methods.find("collect_ports");
    if (it == context.methods.end() || it->second.body_start_byte == 0) {
        // Text-based fallback: search class_text, base_class_text, then raw source.
        // Require '(' immediately after 'collect_ports' (only whitespace allowed between)
        // so that mentions in comments are not mistaken for definitions.
        auto try_collect = [&](const std::string& text) -> bool {
            if (text.empty()) return false;
            std::size_t search_pos = 0;
            while (search_pos < text.size()) {
                const std::size_t method_pos = text.find("collect_ports", search_pos);
                if (method_pos == std::string::npos) return false;
                search_pos = method_pos + 13; // advance past "collect_ports"
                // The first non-whitespace after 'collect_ports' must be '('.
                std::size_t p = search_pos;
                while (p < text.size() && std::isspace(static_cast<unsigned char>(text[p]))) ++p;
                if (p >= text.size() || text[p] != '(') continue;
                // Find the matching ')' for the parameter list.
                const auto paren_end = find_matching_delimiter(text, p, '(', ')');
                if (!paren_end.has_value()) continue;
                // After ')', look for '{' vs ';' to distinguish definition vs declaration.
                const std::size_t body_brace = text.find('{', *paren_end + 1);
                const std::size_t decl_semi  = text.find(';', *paren_end + 1);
                if (decl_semi != std::string::npos &&
                    (body_brace == std::string::npos || decl_semi < body_brace)) {
                    continue; // forward declaration — skip
                }
                if (body_brace == std::string::npos) return false;
                const auto brace_end = find_matching_delimiter(text, body_brace, '{', '}');
                if (!brace_end.has_value()) return false;
                result.has_collect_ports = true;
                process_port_body(text.substr(body_brace + 1, *brace_end - body_brace - 1));
                return true;
            }
            return false;
        };

        if (!try_collect(context.class_text) &&
            !try_collect(context.base_class_text) &&
            !try_collect(record.raw_source)) {
            for (const auto& extra : extra_source_texts_) {
                if (try_collect(extra)) break;
            }
        }
        return;
    }

    result.has_collect_ports = true;
    process_port_body(source_slice(
        *context.raw_source, it->second.body_start_byte, it->second.body_end_byte));
}

void DescriptorBuilder::populate_metadata_specs(const SourceSyntaxRecord& record,
                                                DescriptorResult& result) {
    for (const auto& call : record.register_calls) {
        if (call.macro_name != "VIVID_DEFINE_OP" ||
            call.type_name != result.operator_class_name ||
            call.body_start_byte == 0 ||
            call.body_end_byte <= call.body_start_byte + 1) {
            continue;
        }
        result.has_vivid_define_op = true;
        const std::string body_text = source_slice(
            record.raw_source, call.body_start_byte + 1, call.body_end_byte - 1);
        apply_metadata_assignments(body_text, result.metadata);
        return;
    }
}

std::string DescriptorBuilder::render_registration_cpp(const DescriptorResult& result) const {
    std::ostringstream out;
    const std::string class_name = result.operator_class_name;
    const std::string source_path = result.source_path.lexically_normal().generic_string();

    out << "// Generated by operator_codegen. Do not edit.\n";
    out << "#define VIVID_CODEGEN_ACTIVE 1\n";
    out << "#include \"" << escape_for_c_string(source_path) << "\"\n";
    out << "#include <type_traits>\n\n";
    out << "namespace {\n";

    std::vector<std::string> param_choice_array_names(result.params.size());
    std::vector<std::string> param_visibility_array_names(result.params.size());
    for (std::size_t i = 0; i < result.params.size(); ++i) {
        const auto& param = result.params[i];
        if (!param.choice_label_exprs.empty()) {
            param_choice_array_names[i] =
                "vivid_codegen_" + class_name + "_param_" + std::to_string(i) + "_choices";
            out << "static const char* " << param_choice_array_names[i] << "[] = {";
            for (std::size_t j = 0; j < param.choice_label_exprs.size(); ++j) {
                if (j > 0) out << ", ";
                out << param.choice_label_exprs[j];
            }
            out << "};\n";
        }
        if (!param.visible_when_value_exprs.empty()) {
            param_visibility_array_names[i] =
                "vivid_codegen_" + class_name + "_param_" + std::to_string(i) + "_visible_values";
            out << "static const int32_t " << param_visibility_array_names[i] << "[] = {";
            for (std::size_t j = 0; j < param.visible_when_value_exprs.size(); ++j) {
                if (j > 0) out << ", ";
                out << param.visible_when_value_exprs[j];
            }
            out << "};\n";
        }
    }

    std::string keyword_array_name;
    if (!result.metadata.keyword_exprs.empty()) {
        keyword_array_name = "vivid_codegen_" + class_name + "_keywords";
        out << "static const char* " << keyword_array_name << "[] = {";
        for (std::size_t i = 0; i < result.metadata.keyword_exprs.size(); ++i) {
            if (i > 0) out << ", ";
            out << result.metadata.keyword_exprs[i];
        }
        out << "};\n";
    }

    if (!result.params.empty()) {
        out << "static VividParamDescriptor vivid_codegen_" << class_name << "_params[] = {\n";
        for (std::size_t i = 0; i < result.params.size(); ++i) {
            const auto& param = result.params[i];
            const std::string choice_ptr = param_choice_array_names[i].empty()
                ? "nullptr" : param_choice_array_names[i];
            const std::string choice_count = param.choice_label_exprs.empty()
                ? "0" : std::to_string(param.choice_label_exprs.size());
            const std::string visible_ptr = param_visibility_array_names[i].empty()
                ? "nullptr" : param_visibility_array_names[i];
            const std::string visible_count = param.visible_when_value_exprs.empty()
                ? "0" : std::to_string(param.visible_when_value_exprs.size());
            out << "    {"
                << "\"" << escape_for_c_string(param.param_name) << "\", "
                << param.vivid_param_type << ", "
                << param.default_value_expr << ", "
                << param.min_value_expr << ", "
                << param.max_value_expr << ", "
                << choice_ptr << ", "
                << choice_count << ", "
                << param.default_string_expr << ", "
                << param.group_expr << ", "
                << param.display_hint_expr << ", "
                << param.layout_columns_expr << ", "
                << param.layout_column_index_expr << ", "
                << param.semantic_tag_expr << ", "
                << param.semantic_shape_expr << ", "
                << param.semantic_unit_expr << ", "
                << param.semantic_intent_expr << ", "
                << param.description_expr << ", "
                << param.asset_kind_expr << ", "
                << param.visible_when_param_expr << ", "
                << param.visible_when_op_expr << ", "
                << visible_ptr << ", "
                << visible_count << ", "
                << param.widget_id_expr << ", "
                << param.widget_span_expr << ", "
                << param.repeat_group_expr << ", "
                << param.repeat_group_idx_expr
                << "},\n";
        }
        out << "};\n";
    }

    if (!result.port_exprs.empty()) {
        out << "static VividPortDescriptor vivid_codegen_" << class_name << "_ports[] = {\n";
        for (const auto& port_expr : result.port_exprs) {
            out << "    " << port_expr << ",\n";
        }
        out << "};\n";
    }

    const std::string params_ptr = result.params.empty()
        ? "nullptr"
        : "vivid_codegen_" + class_name + "_params";
    const std::string ports_ptr = result.port_exprs.empty()
        ? "nullptr"
        : "vivid_codegen_" + class_name + "_ports";
    const std::string name_expr = result.metadata.name_expr.empty()
        ? result.stable_name_expr
        : result.metadata.name_expr;
    const std::string display_name_expr = result.metadata.display_name_expr.empty()
        ? "vivid::detail::get_display_name<" + class_name + ">()"
        : result.metadata.display_name_expr;
    const std::string summary_expr = result.metadata.summary_expr.empty()
        ? "vivid::detail::get_summary<" + class_name + ">()"
        : result.metadata.summary_expr;
    const std::string keywords_ptr = keyword_array_name.empty()
        ? "vivid::detail::get_keywords_data<" + class_name + ">()"
        : keyword_array_name;
    const std::string keyword_count = keyword_array_name.empty()
        ? "vivid::detail::get_keywords_count<" + class_name + ">()"
        : std::to_string(result.metadata.keyword_exprs.size());

    // Collect params that have dynamic choices (function-call initializer).
    bool has_dynamic_choices = false;
    for (const auto& param : result.params) {
        if (!param.dynamic_choices_expr.empty()) { has_dynamic_choices = true; break; }
    }
    // Collect port indices that need VIVID_PORT_DISPLAY_ADVANCED.
    std::vector<std::size_t> advanced_port_indices;
    for (std::size_t i = 0; i < result.port_advanced_flags.size(); ++i) {
        if (result.port_advanced_flags[i]) advanced_port_indices.push_back(i);
    }
    const bool has_advanced_ports = !advanced_port_indices.empty();
    const bool needs_runtime_init = has_dynamic_choices || has_advanced_ports ||
                                    !result.params.empty();

    out << "\nstatic const VividOperatorDescriptor* vivid_codegen_descriptor_" << class_name
        << "() {\n";

    if (needs_runtime_init) {
        out << "    static bool vivid_codegen_desc_initialized_ = false;\n";
        out << "    if (!vivid_codegen_desc_initialized_) {\n";
        out << "        vivid_codegen_desc_initialized_ = true;\n";
        for (std::size_t i = 0; i < result.params.size(); ++i) {
            const auto& param = result.params[i];
            if (param.dynamic_choices_expr.empty()) continue;
            const std::string storage = "vivid_codegen_" + class_name + "_param_" +
                                        std::to_string(i) + "_dyn_choices_storage";
            const std::string ptrs    = "vivid_codegen_" + class_name + "_param_" +
                                        std::to_string(i) + "_dyn_choices_ptrs";
            out << "        static std::vector<std::string> " << storage
                << " = " << param.dynamic_choices_expr << ";\n";
            out << "        static std::vector<const char*> " << ptrs << ";\n";
            out << "        for (const auto& s : " << storage << ") "
                << ptrs << ".push_back(s.c_str());\n";
            out << "        " << "vivid_codegen_" << class_name << "_params[" << i << "].choice_labels"
                << " = " << ptrs << ".data();\n";
            out << "        " << "vivid_codegen_" << class_name << "_params[" << i << "].choice_count"
                << " = static_cast<uint32_t>(" << ptrs << ".size());\n";
            out << "        " << "vivid_codegen_" << class_name << "_params[" << i << "].max_value"
                << " = static_cast<float>(" << ptrs << ".size() - 1);\n";
        }
        for (std::size_t idx : advanced_port_indices) {
            out << "        vivid_codegen_" << class_name << "_ports[" << idx
                << "].display_hint = VIVID_PORT_DISPLAY_ADVANCED;\n";
        }
        if (!result.params.empty()) {
            out << "        " << class_name << " vivid_codegen_param_source;\n";
            out << "        std::vector<vivid::ParamBase*> vivid_codegen_param_ptrs;\n";
            out << "        vivid_codegen_param_source.collect_params(vivid_codegen_param_ptrs);\n";
            out << "        size_t vivid_codegen_param_count = vivid_codegen_param_ptrs.size();\n";
            out << "        if (vivid_codegen_param_count > " << result.params.size()
                << ") vivid_codegen_param_count = " << result.params.size() << ";\n";
            out << "        for (size_t i = 0; i < vivid_codegen_param_count; ++i) {\n";
            out << "            if (vivid_codegen_param_ptrs[i])\n";
            out << "                vivid_codegen_" << class_name
                << "_params[i].display_hint = vivid_codegen_param_ptrs[i]->display_hint;\n";
            out << "        }\n";
        }
        out << "    }\n";
    }

    out << "    static const VividOperatorDescriptor desc = {\n";
    out << "        " << name_expr << ",\n";
    out << "        " << result.params.size() << ",\n";
    out << "        " << params_ptr << ",\n";
    out << "        " << result.port_exprs.size() << ",\n";
    out << "        " << ports_ptr << ",\n";
    out << "        vivid::detail::get_time_dependent<" << class_name << ">() ? 1 : 0,\n";
    out << "        std::is_base_of_v<vivid::AudioProcessable, " << class_name << "> ? 1 : 0,\n";
    out << "        std::is_base_of_v<vivid::GpuProcessable, " << class_name << "> ? 1 : 0,\n";
    out << "        std::is_base_of_v<vivid::FrameProcessable, " << class_name << "> ? 1 : 0,\n";
    out << "        vivid::detail::get_lane_behavior<" << class_name << ">(),\n";
    out << "        vivid::detail::get_strategy_independent<" << class_name << ">() ? 1 : 0,\n";
    out << "        " << display_name_expr << ",\n";
    out << "        " << keywords_ptr << ",\n";
    out << "        " << keyword_count << ",\n";
    out << "        " << summary_expr << ",\n";
    out << "        vivid::detail::get_multiplicity_behavior<" << class_name << ">(),\n";
    out << "    };\n";
    out << "    return &desc;\n";
    out << "}\n";
    out << "} // namespace\n\n";
    out << "VIVID_INTERNAL_EXPORTS_WITH_DESCRIPTOR(" << class_name
        << ", vivid_codegen_descriptor_" << class_name << "(), \"v2\")\n";

    if (result.has_draw_thumbnail) {
        out << "\nextern \"C\" void vivid_draw_thumbnail(\n"
            << "    void* instance, const VividThumbnailContext* ctx) {\n"
            << "    auto* inst = static_cast<_VividInstance*>(instance);\n"
            << "    _vivid_sync_params(inst,\n"
            << "        const_cast<float*>(ctx ? ctx->param_values : nullptr),\n"
            << "        const_cast<const char**>(ctx ? ctx->file_param_values : nullptr),\n"
            << "        ctx ? ctx->file_param_count : 0);\n"
            << "    inst->op.draw_thumbnail(ctx);\n"
            << "}\n";
    }
    if (result.has_draw_inspector) {
        out << "\nextern \"C\" uint32_t vivid_inspector_mode() { return "
            << (result.inspector_full_mode ? "VIVID_INSPECTOR_FULL" : "VIVID_INSPECTOR_STANDARD")
            << "; }\n"
            << "extern \"C\" void vivid_draw_inspector(\n"
            << "    void* instance, VividInspectorContext* ctx) {\n"
            << "    auto* inst = static_cast<_VividInstance*>(instance);\n"
            << "    _vivid_sync_params(inst,\n"
            << "        const_cast<float*>(ctx ? ctx->param_values : nullptr),\n"
            << "        const_cast<const char**>(ctx ? ctx->string_param_values : nullptr),\n"
            << "        ctx ? ctx->string_param_count : 0);\n"
            << "    inst->op.draw_inspector(ctx);\n"
            << "}\n";
    }
    if (result.has_draw_editor) {
        out << "\nextern \"C\" VividEditorMetadata vivid_editor_metadata() {\n"
            << "    return " << class_name << "::editor_metadata();\n"
            << "}\n"
            << "extern \"C\" void vivid_draw_editor(\n"
            << "    void* instance, VividEditorContext* ctx) {\n"
            << "    auto* inst = static_cast<_VividInstance*>(instance);\n"
            << "    _vivid_sync_params(inst,\n"
            << "        const_cast<float*>(ctx ? ctx->param_values : nullptr),\n"
            << "        const_cast<const char**>(ctx ? ctx->string_param_values : nullptr),\n"
            << "        ctx ? ctx->string_param_count : 0);\n"
            << "    inst->op.draw_editor(ctx);\n"
            << "}\n";
    }

    return out.str();
}

} // namespace codegen
} // namespace vivid
