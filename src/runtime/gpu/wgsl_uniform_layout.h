#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace vivid {

struct WgslUniformMemberLayout {
    std::string name;
    std::string wgsl_type;
    std::string cpp_declaration;
    uint32_t offset = 0;
    uint32_t size = 0;
    uint32_t alignment = 0;
};

struct WgslUniformLayout {
    std::string struct_name = "Uniforms";
    std::vector<WgslUniformMemberLayout> members;
    uint32_t size = 0;
    uint32_t alignment = 1;
};

std::optional<WgslUniformLayout> parse_wgsl_uniform_layout(
    const std::string& wgsl_source,
    std::string& error,
    const std::string& struct_name = "Uniforms");

// Returns nullopt with an empty error string when no inline WGSL uniform
// source is present in the C++ file.
std::optional<WgslUniformLayout> extract_wgsl_uniform_layout_from_cpp_source(
    const std::string& cpp_source,
    std::string& error,
    const std::string& struct_name = "Uniforms");

} // namespace vivid
