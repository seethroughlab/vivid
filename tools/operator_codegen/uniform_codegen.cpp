#include "uniform_codegen.h"

#include <cstddef>
#include <fstream>
#include <sstream>

namespace vivid {
namespace codegen {

namespace {

std::optional<std::string> read_file(const std::filesystem::path& path,
                                     std::string& error_message) {
    if (!std::filesystem::exists(path)) {
        error_message = "File does not exist: " + path.string();
        return std::nullopt;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input.is_open()) {
        error_message = "Could not open file for reading: " + path.string();
        return std::nullopt;
    }
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

std::string render_header(const WgslUniformLayout& layout,
                          const std::string& cpp_struct_name) {
    std::ostringstream out;
    out << "#pragma once\n\n";
    out << "#include <cstddef>\n";
    out << "#include <cstdint>\n\n";
    out << "struct alignas(" << layout.alignment << ") " << cpp_struct_name << " {\n";

    uint32_t cursor = 0;
    uint32_t padding_index = 0;
    for (const auto& member : layout.members) {
        if (member.offset > cursor) {
            out << "    std::byte _pad" << padding_index++ << "["
                << (member.offset - cursor) << "]{};\n";
            cursor = member.offset;
        }
        const std::size_t bracket = member.cpp_declaration.find('[');
        if (bracket == std::string::npos) {
            out << "    " << member.cpp_declaration << " " << member.name << ";\n";
        } else {
            out << "    " << member.cpp_declaration.substr(0, bracket) << " "
                << member.name << member.cpp_declaration.substr(bracket) << ";\n";
        }
        cursor = member.offset + member.size;
    }
    if (layout.size > cursor) {
        out << "    std::byte _pad" << padding_index++ << "["
            << (layout.size - cursor) << "]{};\n";
    }
    out << "};\n\n";
    out << "static_assert(sizeof(" << cpp_struct_name << ") == " << layout.size
        << ", \"Generated uniform struct size mismatch\");\n";
    out << "static_assert(alignof(" << cpp_struct_name << ") == " << layout.alignment
        << ", \"Generated uniform struct alignment mismatch\");\n";
    for (const auto& member : layout.members) {
        out << "static_assert(offsetof(" << cpp_struct_name << ", " << member.name << ") == "
            << member.offset << ", \"Generated uniform member offset mismatch for "
            << member.name << "\");\n";
    }
    return out.str();
}

std::string render_layout_export(const WgslUniformLayout& layout,
                                 const std::string& cpp_struct_name) {
    std::ostringstream out;
    out << "\nnamespace {\n";
    if (!layout.members.empty()) {
        out << "static const VividGeneratedUniformMember vivid_codegen_" << cpp_struct_name
            << "_uniform_members[] = {\n";
        for (const auto& member : layout.members) {
            out << "    {"
                << "\"" << member.name << "\", "
                << "\"" << member.wgsl_type << "\", "
                << "static_cast<uint32_t>(offsetof(" << cpp_struct_name << ", " << member.name << ")), "
                << "static_cast<uint32_t>(sizeof(((" << cpp_struct_name << "*)nullptr)->"
                << member.name << ")), "
                << member.alignment
                << "},\n";
        }
        out << "};\n";
    }
    out << "static const VividGeneratedUniformLayout vivid_codegen_" << cpp_struct_name
        << "_uniform_layout = {\n";
    out << "    \"" << layout.struct_name << "\",\n";
    out << "    static_cast<uint32_t>(sizeof(" << cpp_struct_name << ")),\n";
    out << "    static_cast<uint32_t>(alignof(" << cpp_struct_name << ")),\n";
    out << "    " << layout.members.size() << ",\n";
    out << "    "
        << (layout.members.empty()
                ? "nullptr"
                : "vivid_codegen_" + cpp_struct_name + "_uniform_members")
        << ",\n";
    out << "};\n";
    out << "} // namespace\n\n";
    out << "extern \"C\" const VividGeneratedUniformLayout* vivid_generated_uniform_layout() {\n";
    out << "    return &vivid_codegen_" << cpp_struct_name << "_uniform_layout;\n";
    out << "}\n";
    return out.str();
}

UniformCodeGenResult build_result_from_layout(const WgslUniformLayout& layout,
                                              const std::string& cpp_struct_name) {
    UniformCodeGenResult result;
    result.success = true;
    result.has_uniforms = true;
    result.struct_name = layout.struct_name;
    result.cpp_struct_name = cpp_struct_name;
    result.struct_size = layout.size;
    result.struct_alignment = layout.alignment;
    result.generated_header = render_header(layout, cpp_struct_name);
    result.generated_export_cpp = render_layout_export(layout, cpp_struct_name);
    result.members.reserve(layout.members.size());
    for (const auto& member : layout.members) {
        result.members.push_back({
            member.name,
            member.wgsl_type,
            member.cpp_declaration,
            member.offset,
            member.size,
            member.alignment,
        });
    }
    return result;
}

} // namespace

UniformCodeGenResult UniformCodeGen::generate_from_wgsl(
    const std::filesystem::path& wgsl_path,
    const std::string& cpp_struct_name) {
    UniformCodeGenResult result;
    std::string error_message;
    auto contents = read_file(wgsl_path, error_message);
    if (!contents) {
        result.error_message = std::move(error_message);
        return result;
    }

    auto layout = parse_wgsl_uniform_layout(*contents, error_message, "Uniforms");
    if (!layout) {
        result.error_message = std::move(error_message);
        return result;
    }
    return build_result_from_layout(*layout, cpp_struct_name);
}

UniformCodeGenResult UniformCodeGen::generate_from_operator_source(
    const std::filesystem::path& cpp_source_path,
    const std::optional<std::filesystem::path>& wgsl_path,
    const std::string& cpp_struct_name) {
    if (wgsl_path && !wgsl_path->empty()) {
        return generate_from_wgsl(*wgsl_path, cpp_struct_name);
    }

    UniformCodeGenResult result;
    std::string error_message;
    auto contents = read_file(cpp_source_path, error_message);
    if (!contents) {
        result.error_message = std::move(error_message);
        return result;
    }

    auto layout = extract_wgsl_uniform_layout_from_cpp_source(*contents, error_message, "Uniforms");
    if (!layout) {
        result.success = error_message.empty();
        result.has_uniforms = false;
        result.cpp_struct_name = cpp_struct_name;
        result.error_message = std::move(error_message);
        return result;
    }
    return build_result_from_layout(*layout, cpp_struct_name);
}

} // namespace codegen
} // namespace vivid
