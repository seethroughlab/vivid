#pragma once

#include "operator_api/types.h"

#include <string>
#include <vector>

namespace vivid {

struct DescriptorValidationIssue {
    std::string code;
    std::string message;
};

// Stable issue codes returned in DescriptorValidationIssue::code. Exposed as
// named constants (rather than inline string literals) so tooling, tests, and
// LLM-facing docs can reference them and so a refactor can't silently rename a
// code. Documented in docs/OPERATOR-DESCRIPTOR-VALIDATION.md.
namespace validation_codes {
inline constexpr const char* kNullDescriptor               = "null_descriptor";
inline constexpr const char* kMissingName                  = "missing_name";
inline constexpr const char* kNullParams                   = "null_params";
inline constexpr const char* kNullPorts                    = "null_ports";
inline constexpr const char* kMissingCapability            = "missing_capability";
inline constexpr const char* kInvalidMultiplicityBehavior  = "invalid_multiplicity_behavior";
inline constexpr const char* kParamMissingName             = "param_missing_name";
inline constexpr const char* kDuplicateParamName           = "duplicate_param_name";
inline constexpr const char* kParamMissingChoiceLabels     = "param_missing_choice_labels";
inline constexpr const char* kParamMissingDefaultString    = "param_missing_default_string";
// Ph5 audit P2-01: numeric-range + type sanity (previously unchecked — an inverted range, an
// out-of-range default, or a garbage type integer passed validation clean).
inline constexpr const char* kParamInvalidType             = "param_invalid_type";
inline constexpr const char* kParamInvalidRange            = "param_invalid_range";
inline constexpr const char* kParamDefaultOutOfRange       = "param_default_out_of_range";
inline constexpr const char* kPortMissingName              = "port_missing_name";
inline constexpr const char* kDuplicatePortName            = "duplicate_port_name";
inline constexpr const char* kCustomPortMissingTypeName    = "custom_port_missing_type_name";
// Audio operator port-shape rules. The audio runtime is single-stereo-port: it feeds an effect
// exactly one stereo input and takes exactly one stereo output; extra audio ports and non-stereo
// channel counts are silently ignored. These make that contract explicit instead of a silent mismatch.
inline constexpr const char* kAudioTooManyInputPorts       = "audio_too_many_input_ports";
inline constexpr const char* kAudioTooManyOutputPorts      = "audio_too_many_output_ports";
inline constexpr const char* kAudioMissingOutputPort       = "audio_missing_output_port";
inline constexpr const char* kAudioNonStereoChannels       = "audio_non_stereo_channels";
inline constexpr const char* kUniformLayoutMissingName     = "uniform_layout_missing_name";
inline constexpr const char* kUniformLayoutEmpty           = "uniform_layout_empty";
inline constexpr const char* kUniformLayoutNot16ByteAligned = "uniform_layout_not_16_byte_aligned";
inline constexpr const char* kUniformLayoutMissingMembers  = "uniform_layout_missing_members";
inline constexpr const char* kUniformMemberMissingName     = "uniform_member_missing_name";
inline constexpr const char* kDuplicateUniformMemberName   = "duplicate_uniform_member_name";
inline constexpr const char* kUniformMemberMissingType     = "uniform_member_missing_type";
inline constexpr const char* kUniformMemberZeroSize        = "uniform_member_zero_size";
inline constexpr const char* kUniformMemberZeroAlignment   = "uniform_member_zero_alignment";
inline constexpr const char* kUniformMemberOutOfOrder      = "uniform_member_out_of_order";
inline constexpr const char* kUniformMemberOutOfBounds     = "uniform_member_out_of_bounds";
} // namespace validation_codes

std::vector<DescriptorValidationIssue> validate_descriptor(
    const VividOperatorDescriptor* desc,
    const char* registration_mode = nullptr,
    const VividGeneratedUniformLayout* uniform_layout = nullptr);

} // namespace vivid
