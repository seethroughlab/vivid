#include "tools/operator_codegen/descriptor_builder.h"
#include "tools/operator_codegen/uniform_codegen.h"

#include <filesystem>
#include <iostream>
#include <optional>
#include <string>

namespace fs = std::filesystem;

static int failures = 0;

static void check(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << "\n";
        ++failures;
    }
}

int main() {
    const fs::path repo_root = fs::path(VIVID_SOURCE_DIR);
    const fs::path noise_cpp = repo_root / "operators/gpu/noise/noise.cpp";

    vivid::codegen::DescriptorBuilder builder;
    auto result = builder.build_from_file(noise_cpp);

    check(result.success, "descriptor build succeeds for noise.cpp");
    if (!result.success) {
        std::cerr << result.error_message << "\n";
    }
    check(result.operator_class_name == "Noise", "operator class is Noise");
    check(result.has_vivid_register, "noise uses VIVID_REGISTER");
    check(result.has_vivid_define_op, "noise uses VIVID_DEFINE_OP");
    check(result.generated_cpp.find("VIVID_INTERNAL_EXPORTS_WITH_DESCRIPTOR(Noise") != std::string::npos,
          "generated file emits V2 exports");
    check(result.generated_cpp.find("\"v2\"") != std::string::npos,
          "generated file tags registration mode as v2");
    check(result.generated_cpp.find("\"NoiseTexture\"") != std::string::npos,
          "generated file preserves stable operator name");

    vivid::codegen::UniformCodeGen uniform_builder;
    auto uniform_result = uniform_builder.generate_from_operator_source(
        noise_cpp, std::nullopt, "NoiseUniforms");
    check(uniform_result.success, "uniform codegen succeeds for noise.cpp");
    check(uniform_result.has_uniforms, "noise.cpp exposes inline WGSL uniforms");
    check(uniform_result.struct_size == 64, "noise uniform layout stays 64 bytes");
    check(uniform_result.members.size() == 14, "noise uniform layout exposes all WGSL members");
    check(uniform_result.generated_header.find("struct alignas(8) NoiseUniforms") != std::string::npos,
          "generated uniform header declares the expected NoiseUniforms alignment");
    check(uniform_result.generated_export_cpp.find("vivid_generated_uniform_layout") != std::string::npos,
          "generated uniform metadata export is emitted");

    {
        std::string error;
        auto layout = vivid::parse_wgsl_uniform_layout(R"(
            struct Uniforms {
                gain: f32,
                weights: array<f32, 4>,
                taps: array<vec2f, 2>,
                basis: array<mat3x2f, 2>,
            };
        )", error, "Uniforms");
        check(layout.has_value(), "WGSL parser supports uniform arrays");
        if (!layout) {
            std::cerr << error << "\n";
        } else {
            check(layout->alignment == 16, "uniform array layout raises struct alignment to 16 bytes");
            check(layout->size == 176, "uniform array layout computes the expected total struct size");
            check(layout->members.size() == 4, "uniform array fixture exposes four members");
            if (layout->members.size() == 4) {
                check(layout->members[0].offset == 0, "scalar member keeps offset 0");
                check(layout->members[1].offset == 16, "array<f32,4> starts at the next 16-byte boundary");
                check(layout->members[1].alignment == 16, "array<f32,4> reports 16-byte alignment");
                check(layout->members[1].size == 64, "array<f32,4> reports 64-byte size");
                check(layout->members[2].offset == 80, "array<vec2f,2> follows the prior array stride");
                check(layout->members[2].alignment == 16, "array<vec2f,2> reports 16-byte alignment");
                check(layout->members[2].size == 32, "array<vec2f,2> reports 32-byte size");
                check(layout->members[3].offset == 112, "array<mat3x2f,2> offset respects uniform alignment");
                check(layout->members[3].alignment == 16, "array<mat3x2f,2> reports 16-byte alignment");
                check(layout->members[3].size == 48, "array<mat3x2f,2> uses corrected per-element stride");
            }
        }
    }

    if (failures != 0) {
        std::cerr << failures << " operator_codegen test failure(s)\n";
        return 1;
    }

    std::cout << "operator_codegen smoke test passed\n";
    return 0;
}
