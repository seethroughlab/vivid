#pragma once

#include "runtime/core/source_syntax_parser.h"

#include <filesystem>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace vivid {
namespace codegen {

struct ParamSpec {
    std::string variable_name;
    std::string param_name;
    std::string cpp_type;
    std::string vivid_param_type;
    std::string default_value_expr = "0";
    std::string min_value_expr = "0";
    std::string max_value_expr = "0";
    std::string default_string_expr = "nullptr";
    std::vector<std::string> choice_label_exprs;
    std::string group_expr = "nullptr";
    std::string display_hint_expr = "VIVID_DISPLAY_DEFAULT";
    std::string layout_columns_expr = "0";
    std::string layout_column_index_expr = "0";
    std::string semantic_tag_expr = "nullptr";
    std::string semantic_shape_expr = "nullptr";
    std::string semantic_unit_expr = "nullptr";
    std::string semantic_intent_expr = "nullptr";
    std::string description_expr = "nullptr";
    std::string asset_kind_expr = "nullptr";
    std::string visible_when_param_expr = "nullptr";
    std::string visible_when_op_expr = "VIVID_PARAM_VIS_ALWAYS";
    std::vector<std::string> visible_when_value_exprs;
    std::string widget_id_expr = "nullptr";
    std::string widget_span_expr = "0";
    std::string repeat_group_expr = "nullptr";
    std::string repeat_group_idx_expr = "0";
};

struct OperatorMetadataSpec {
    std::string name_expr;
    std::string display_name_expr;
    std::vector<std::string> keyword_exprs;
    std::string summary_expr;
};

struct DescriptorResult {
    bool success = false;
    std::string error_message;

    std::filesystem::path source_path;
    std::string operator_class_name;
    std::string stable_name_expr;
    bool has_vivid_define_op = false;
    bool has_vivid_register_v2 = false;
    bool has_process_audio = false;
    bool has_process_gpu = false;
    bool has_process_frame = false;
    bool has_collect_params = false;
    bool has_collect_ports = false;

    std::vector<std::string> includes;
    std::vector<ParamSpec> params;
    std::vector<std::string> port_exprs;
    OperatorMetadataSpec metadata;
    std::string generated_cpp;
};

class DescriptorBuilder {
public:
    DescriptorBuilder() = default;

    DescriptorResult build_from_file(const std::filesystem::path& cpp_source_path);

private:
    struct ClassContext {
        const TypeDefinition* type_definition = nullptr;
        std::optional<SourceRange> type_range;
        std::unordered_map<std::string, MethodDefinition> methods;
        std::unordered_map<std::string, MemberConstant> constants;
        std::string class_text;
        std::string type_body_text;
    };

    void process_record(const SourceSyntaxRecord& record, DescriptorResult& result);
    bool populate_class_context(const SourceSyntaxRecord& record,
                                const std::string& class_name,
                                ClassContext& context,
                                std::string& error_message);
    void populate_param_specs(const SourceSyntaxRecord& record,
                              const ClassContext& context,
                              DescriptorResult& result);
    void populate_port_specs(const SourceSyntaxRecord& record,
                             const ClassContext& context,
                             DescriptorResult& result);
    void populate_metadata_specs(const SourceSyntaxRecord& record,
                                 DescriptorResult& result);
    std::string render_registration_cpp(const DescriptorResult& result) const;
};

} // namespace codegen
} // namespace vivid
