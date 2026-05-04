#pragma once

#include "operator_api/types.h"

#include <string>
#include <vector>

namespace vivid {

struct DescriptorValidationIssue {
    std::string code;
    std::string message;
};

std::vector<DescriptorValidationIssue> validate_descriptor(
    const VividOperatorDescriptor* desc,
    const char* registration_mode = nullptr,
    const VividGeneratedUniformLayout* uniform_layout = nullptr);

} // namespace vivid
