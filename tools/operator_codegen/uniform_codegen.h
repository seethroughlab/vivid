#pragma once

#include "runtime/gpu/wgsl_uniform_layout.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include <filesystem>

namespace vivid {
namespace codegen {

struct UniformMember {
    std::string name;
    std::string wgsl_type;
    std::string cpp_declaration;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t alignment = 0;
};

struct UniformCodeGenResult {
    bool success = false;
    bool has_uniforms = false;
    std::string error_message;

    std::string struct_name = "Uniforms";
    std::string cpp_struct_name;
    std::vector<UniformMember> members;
    uint32_t struct_size = 0;
    uint32_t struct_alignment = 1;
    std::string generated_header;
    std::string generated_export_cpp;
};

class UniformCodeGen {
public:
    UniformCodeGen() = default;

    UniformCodeGenResult generate_from_wgsl(const std::filesystem::path& wgsl_path,
                                            const std::string& cpp_struct_name);
    UniformCodeGenResult generate_from_operator_source(
        const std::filesystem::path& cpp_source_path,
        const std::optional<std::filesystem::path>& wgsl_path,
        const std::string& cpp_struct_name);
};

} // namespace codegen
} // namespace vivid
