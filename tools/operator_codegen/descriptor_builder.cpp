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
            if (spec.cpp_type == "int" &&
                args.size() >= 3 &&
                trim_copy(args[2]).front() == '{') {
                spec.min_value_expr = "0";
                spec.choice_label_exprs = split_top_level(strip_outer_braces(args[2]), ',');
                if (!spec.choice_label_exprs.empty()) {
                    spec.max_value_expr = std::to_string(spec.choice_label_exprs.size() - 1);
                } else {
                    spec.max_value_expr = "0";
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

    for (const auto& call : record.register_calls) {
        if (call.macro_name == "VIVID_REGISTER_V2") {
            result.operator_class_name = call.type_name;
            result.has_vivid_register_v2 = true;
            break;
        }
    }
    if (result.operator_class_name.empty()) {
        for (const auto& call : record.register_calls) {
            if (call.macro_name == "VIVID_DEFINE_OP") {
                result.operator_class_name = call.type_name;
                break;
            }
        }
    }
    if (result.operator_class_name.empty()) {
        for (const auto& call : record.register_calls) {
            if (call.macro_name == "VIVID_REGISTER") {
                result.operator_class_name = call.type_name;
                break;
            }
        }
    }

    if (result.operator_class_name.empty()) {
        result.error_message = "Could not determine operator class from registration macros.";
        return;
    }
    if (!result.has_vivid_register_v2) {
        result.error_message = "CODEGEN operators must declare VIVID_REGISTER_V2(" +
            result.operator_class_name + ").";
        return;
    }

    ClassContext context;
    if (!populate_class_context(record, result.operator_class_name, context, result.error_message)) {
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

    populate_param_specs(record, context, result);
    populate_port_specs(record, context, result);
    populate_metadata_specs(record, result);
}

bool DescriptorBuilder::populate_class_context(const SourceSyntaxRecord& record,
                                               const std::string& class_name,
                                               ClassContext& context,
                                               std::string& error_message) {
    for (const auto& type_def : record.type_definitions) {
        if (type_def.name == class_name) {
            context.type_definition = &type_def;
            context.type_range = type_def.range;
            break;
        }
    }
    if (!context.type_range.has_value()) {
        error_message = "Could not find type definition for " + class_name + ".";
        return false;
    }

    context.class_text = source_slice(
        record.raw_source,
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

    for (const auto& method : record.method_definitions) {
        if (method.range.start_byte >= context.type_range->start_byte &&
            method.range.end_byte <= context.type_range->end_byte) {
            context.methods[method.name] = method;
        }
    }
    for (const auto& constant : record.member_constants) {
        if (constant.range.start_byte >= context.type_range->start_byte &&
            constant.range.end_byte <= context.type_range->end_byte) {
            context.constants[constant.name] = constant;
        }
    }
    return true;
}

void DescriptorBuilder::populate_param_specs(const SourceSyntaxRecord& record,
                                             const ClassContext& context,
                                             DescriptorResult& result) {
    auto declared_params = parse_param_declarations(context.type_body_text);
    std::unordered_map<std::string, ParamSpec> by_var_name;
    for (auto& param : declared_params) {
        by_var_name.emplace(param.variable_name, std::move(param));
    }

    apply_param_helper_calls(context.class_text, by_var_name);

    auto it = context.methods.find("collect_params");
    if (it == context.methods.end() || it->second.body_start_byte == 0) {
        return;
    }

    result.has_collect_params = true;
    const std::string body_text = source_slice(
        record.raw_source, it->second.body_start_byte, it->second.body_end_byte);
    for (const auto& arg : extract_push_back_arguments(body_text)) {
        const std::string variable_name = strip_ampersand(arg);
        auto found = by_var_name.find(variable_name);
        if (found != by_var_name.end()) {
            result.params.push_back(found->second);
        }
    }
}

void DescriptorBuilder::populate_port_specs(const SourceSyntaxRecord& record,
                                            const ClassContext& context,
                                            DescriptorResult& result) {
    auto it = context.methods.find("collect_ports");
    if (it == context.methods.end() || it->second.body_start_byte == 0) {
        return;
    }

    result.has_collect_ports = true;
    const std::string body_text = source_slice(
        record.raw_source, it->second.body_start_byte, it->second.body_end_byte);
    for (const auto& arg : extract_push_back_arguments(body_text)) {
        result.port_exprs.push_back(normalize_port_expr(arg));
    }
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
        ? "nullptr"
        : result.metadata.display_name_expr;
    const std::string summary_expr = result.metadata.summary_expr.empty()
        ? "nullptr"
        : result.metadata.summary_expr;
    const std::string keywords_ptr = keyword_array_name.empty() ? "nullptr" : keyword_array_name;
    const std::string keyword_count = keyword_array_name.empty()
        ? "0"
        : std::to_string(result.metadata.keyword_exprs.size());

    out << "\nstatic const VividOperatorDescriptor* vivid_codegen_descriptor_" << class_name
        << "() {\n";
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
    out << "    };\n";
    out << "    return &desc;\n";
    out << "}\n";
    out << "} // namespace\n\n";
    out << "VIVID_INTERNAL_EXPORTS_WITH_DESCRIPTOR(" << class_name
        << ", vivid_codegen_descriptor_" << class_name << "(), \"v2\")\n";

    return out.str();
}

} // namespace codegen
} // namespace vivid
