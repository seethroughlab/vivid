#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>
#include <optional>
#include "descriptor_builder.h"
#include "uniform_codegen.h"

void print_usage(const char* program_name) {
    std::cout << "Usage: " << program_name << " [options]\n"
              << "Options:\n"
              << "  --input <path>         Path to the operator source file (.cpp)\n"
              << "  --output <path>        Path to the generated registration file\n"
              << "  --uniform-output <path>  Path to the generated uniform header\n"
              << "  --wgsl <path>          Path to the shader file (.wgsl)\n"
              << "  --extra-source <path>  Extra source file to search for collect_params/collect_ports\n"
              << "  --help                 Show this help message\n";
}

int main(int argc, char* argv[]) {
    std::string input_path;
    std::string output_path;
    std::string uniform_output_path;
    std::string wgsl_path;
    std::vector<std::string> extra_sources;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        } else if (arg == "--input" && i + 1 < argc) {
            input_path = argv[++i];
        } else if (arg == "--output" && i + 1 < argc) {
            output_path = argv[++i];
        } else if (arg == "--uniform-output" && i + 1 < argc) {
            uniform_output_path = argv[++i];
        } else if (arg == "--wgsl" && i + 1 < argc) {
            wgsl_path = argv[++i];
        } else if (arg == "--extra-source" && i + 1 < argc) {
            extra_sources.push_back(argv[++i]);
        }
    }

    if (input_path.empty() || output_path.empty()) {
        std::cerr << "Error: --input and --output are required.\n";
        print_usage(argv[0]);
        return 1;
    }

    std::cout << "Operator Codegen Tool\n"
              << "---------------------\n"
              << "Input:  " << input_path << "\n"
              << "Output: " << output_path << "\n";
    if (!wgsl_path.empty()) {
        std::cout << "WGSL:   " << wgsl_path << "\n";
    }

    // 1. Descriptor Building (Path 2A)
    vivid::codegen::DescriptorBuilder descriptor_builder;
    for (const auto& extra : extra_sources) {
        descriptor_builder.add_extra_source(extra);
    }
    auto desc_result = descriptor_builder.build_from_file(input_path);

    if (!desc_result.success) {
        std::cerr << "\n[Error] Descriptor building failed:\n" << desc_result.error_message << "\n";
        return 1;
    }

    std::cout << "\n[Status] Successfully parsed operator metadata.\n";
    std::cout << "Operator Class: "
              << (desc_result.operator_class_name.empty() ? "Unknown" : desc_result.operator_class_name)
              << "\n";
    std::cout << "Includes Found: " << desc_result.includes.size() << "\n";

    vivid::codegen::UniformCodeGen uniform_builder;
    auto uni_result = uniform_builder.generate_from_operator_source(
        input_path,
        wgsl_path.empty() ? std::optional<std::filesystem::path>{}
                          : std::optional<std::filesystem::path>{wgsl_path},
        desc_result.operator_class_name.empty()
            ? "GeneratedUniforms"
            : desc_result.operator_class_name + "Uniforms");
    if (!uni_result.success) {
        std::cerr << "\n[Error] Uniform codegen failed:\n" << uni_result.error_message << "\n";
        return 1;
    }
    if (uni_result.has_uniforms) {
        std::cout << "\n[Status] Successfully parsed WGSL uniforms.\n";
        std::cout << "Struct Name: " << uni_result.struct_name << "\n";
        std::cout << "Members Found: " << uni_result.members.size() << "\n";
        std::cout << "Uniform Bytes: " << uni_result.struct_size << "\n";
    }

    std::ofstream output(output_path, std::ios::binary);
    if (!output.is_open()) {
        std::cerr << "\n[Error] Failed to open output file for writing: " << output_path << "\n";
        return 1;
    }
    output << desc_result.generated_cpp;
    if (uni_result.has_uniforms && desc_result.has_process_gpu) {
        // Include the generated uniforms header (written alongside the registration file).
        if (!uniform_output_path.empty()) {
            const std::string uniforms_filename =
                std::filesystem::path(uniform_output_path).filename().string();
            output << "#include \"" << uniforms_filename << "\"\n";
        }
        output << uni_result.generated_export_cpp;
    }
    output.close();

    if (!uniform_output_path.empty()) {
        std::ofstream uniform_output(uniform_output_path, std::ios::binary);
        if (!uniform_output.is_open()) {
            std::cerr << "\n[Error] Failed to open uniform output file for writing: "
                      << uniform_output_path << "\n";
            return 1;
        }
        if (uni_result.has_uniforms && desc_result.has_process_gpu) {
            uniform_output << uni_result.generated_header;
        } else {
            uniform_output << "#pragma once\n// No generated uniforms for this operator.\n";
        }
        uniform_output.close();
    }

    std::cout << "\n[Status] Wrote generated registration file.\n";

    return 0;
}
