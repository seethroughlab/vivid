#include "operator_api/operator_descriptor_validation.h"

#include <unordered_set>

namespace vivid {

namespace {

std::string safe_str(const char* text) {
    return text ? text : "";
}

bool empty_cstr(const char* text) {
    return text == nullptr || *text == '\0';
}

} // namespace

std::vector<DescriptorValidationIssue> validate_descriptor(
    const VividOperatorDescriptor* desc,
    const char* registration_mode,
    const VividGeneratedUniformLayout* uniform_layout) {
    namespace vc = validation_codes;
    std::vector<DescriptorValidationIssue> issues;
    if (!desc) {
        issues.push_back({vc::kNullDescriptor, "vivid_descriptor returned null"});
        return issues;
    }

    if (empty_cstr(desc->name)) {
        issues.push_back({vc::kMissingName, "descriptor name is missing or empty"});
    }
    if (desc->param_count > 0 && desc->params == nullptr) {
        issues.push_back({vc::kNullParams, "param_count is non-zero but params is null"});
    }
    if (desc->port_count > 0 && desc->ports == nullptr) {
        issues.push_back({vc::kNullPorts, "port_count is non-zero but ports is null"});
    }
    if (!desc->has_process_audio && !desc->has_process_gpu && !desc->has_process_frame) {
        issues.push_back({vc::kMissingCapability, "descriptor declares no execution capability"});
    }
    if (desc->multiplicity_behavior > VIVID_MULTIPLICITY_KERNEL) {
        issues.push_back({vc::kInvalidMultiplicityBehavior,
            "multiplicity_behavior is out of range (" + std::to_string(desc->multiplicity_behavior) + ")"});
    }

    std::unordered_set<std::string> param_names;
    if (desc->params) {
        for (uint32_t i = 0; i < desc->param_count; ++i) {
            const auto& param = desc->params[i];
            if (empty_cstr(param.name)) {
                issues.push_back({
                    vc::kParamMissingName,
                    "param[" + std::to_string(i) + "] is missing a name"
                });
                continue;
            }
            const std::string name = safe_str(param.name);
            if (!param_names.insert(name).second) {
                issues.push_back({
                    vc::kDuplicateParamName,
                    "duplicate param name '" + name + "'"
                });
            }
            if (param.choice_count > 0 && param.choice_labels == nullptr) {
                issues.push_back({
                    vc::kParamMissingChoiceLabels,
                    "param '" + name + "' has choice_count but null choice_labels"
                });
            }
            if ((param.type == VIVID_PARAM_FILE || param.type == VIVID_PARAM_TEXT) &&
                param.default_string == nullptr) {
                issues.push_back({
                    vc::kParamMissingDefaultString,
                    "param '" + name + "' is file/text but default_string is null"
                });
            }
        }
    }

    std::unordered_set<std::string> input_ports;
    std::unordered_set<std::string> output_ports;
    int audio_in_ports = 0, audio_out_ports = 0;
    if (desc->ports) {
        for (uint32_t i = 0; i < desc->port_count; ++i) {
            const auto& port = desc->ports[i];
            if (empty_cstr(port.name)) {
                issues.push_back({
                    vc::kPortMissingName,
                    "port[" + std::to_string(i) + "] is missing a name"
                });
                continue;
            }
            const std::string name = safe_str(port.name);
            auto& set = port.direction == VIVID_PORT_OUTPUT ? output_ports : input_ports;
            if (!set.insert(name).second) {
                issues.push_back({
                    vc::kDuplicatePortName,
                    "duplicate " + std::string(port.direction == VIVID_PORT_OUTPUT ? "output" : "input") +
                        " port name '" + name + "'"
                });
            }
            if ((port.transport == VIVID_PORT_TRANSPORT_CUSTOM_REF ||
                 port.transport == VIVID_PORT_TRANSPORT_CUSTOM_VALUE) &&
                empty_cstr(port.type_name)) {
                issues.push_back({
                    vc::kCustomPortMissingTypeName,
                    "custom port '" + name + "' is missing type_name"
                });
            }
            // Audio port shape: the runtime handles only stereo (2) or unspecified (0=auto).
            if (port.type == VIVID_PORT_AUDIO_BUFFER) {
                (port.direction == VIVID_PORT_OUTPUT ? audio_out_ports : audio_in_ports)++;
                if (port.channels != 0 && port.channels != 2) {
                    issues.push_back({
                        vc::kAudioNonStereoChannels,
                        "audio port '" + name + "' declares " + std::to_string(port.channels) +
                            " channels; the audio runtime handles only stereo (2) or 0 (auto)"
                    });
                }
            }
        }
    }

    // An audio operator (effect: audio in->out; instrument/generator: ->out) must present exactly
    // the single-stereo-port shape the runtime feeds it: at most one audio input, exactly one audio
    // output. Extra audio ports would be silently dropped — reject them here so authors find out at
    // load, not by hearing nothing on ports 1+.
    if (desc->has_process_audio) {
        if (audio_in_ports > 1) {
            issues.push_back({vc::kAudioTooManyInputPorts,
                "audio operator declares " + std::to_string(audio_in_ports) +
                    " audio input ports; the runtime feeds only one (stereo)"});
        }
        if (audio_out_ports > 1) {
            issues.push_back({vc::kAudioTooManyOutputPorts,
                "audio operator declares " + std::to_string(audio_out_ports) +
                    " audio output ports; the runtime reads only one (stereo)"});
        }
        if (audio_out_ports == 0) {
            issues.push_back({vc::kAudioMissingOutputPort,
                "audio operator declares no audio output port (it must produce a stereo output)"});
        }
    }

    if (uniform_layout) {
        if (empty_cstr(uniform_layout->struct_name)) {
            issues.push_back({
                vc::kUniformLayoutMissingName,
                "generated uniform layout is missing a struct name"
            });
        }
        if (uniform_layout->byte_size == 0) {
            issues.push_back({
                vc::kUniformLayoutEmpty,
                "generated uniform layout has zero size"
            });
        } else if ((uniform_layout->byte_size % 16u) != 0u) {
            issues.push_back({
                vc::kUniformLayoutNot16ByteAligned,
                "generated uniform layout size must be a multiple of 16 bytes"
            });
        }
        if (uniform_layout->member_count > 0 && uniform_layout->members == nullptr) {
            issues.push_back({
                vc::kUniformLayoutMissingMembers,
                "generated uniform layout declares members but members is null"
            });
        }
        if (uniform_layout->members) {
            uint32_t previous_offset = 0;
            bool have_previous = false;
            std::unordered_set<std::string> uniform_names;
            for (uint32_t i = 0; i < uniform_layout->member_count; ++i) {
                const auto& member = uniform_layout->members[i];
                if (empty_cstr(member.name)) {
                    issues.push_back({
                        vc::kUniformMemberMissingName,
                        "generated uniform member[" + std::to_string(i) + "] is missing a name"
                    });
                    continue;
                }
                const std::string name = safe_str(member.name);
                if (!uniform_names.insert(name).second) {
                    issues.push_back({
                        vc::kDuplicateUniformMemberName,
                        "generated uniform layout has duplicate member '" + name + "'"
                    });
                }
                if (empty_cstr(member.wgsl_type)) {
                    issues.push_back({
                        vc::kUniformMemberMissingType,
                        "generated uniform member '" + name + "' is missing a WGSL type"
                    });
                }
                if (member.size == 0) {
                    issues.push_back({
                        vc::kUniformMemberZeroSize,
                        "generated uniform member '" + name + "' has zero size"
                    });
                }
                if (member.alignment == 0) {
                    issues.push_back({
                        vc::kUniformMemberZeroAlignment,
                        "generated uniform member '" + name + "' has zero alignment"
                    });
                }
                if (have_previous && member.offset < previous_offset) {
                    issues.push_back({
                        vc::kUniformMemberOutOfOrder,
                        "generated uniform member '" + name + "' offsets are not monotonic"
                    });
                }
                if (member.offset + member.size > uniform_layout->byte_size) {
                    issues.push_back({
                        vc::kUniformMemberOutOfBounds,
                        "generated uniform member '" + name + "' exceeds the declared layout size"
                    });
                }
                previous_offset = member.offset;
                have_previous = true;
            }
        }
    }

    return issues;
}

} // namespace vivid
