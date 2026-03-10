#pragma once

#include "operator_api/types.h"
#include <string>
#include <vector>

struct VividPortSpec {
    std::string        name;
    VividPortType      type;
    VividPortDirection direction = VIVID_PORT_OUTPUT;
    uint32_t           handle_type_id = 0;  // non-zero when type == VIVID_PORT_HANDLE
};

struct VividParamSpec {
    std::string    name;
    VividParamType type = VIVID_PARAM_FLOAT;
    float          default_value = 0.0f;
    float          min_value = 0.0f;
    float          max_value = 1.0f;
    std::string    default_string;  // for FILE/TEXT types
};

struct VividCreateOperatorRequest {
    std::string name;
    VividDomain domain = VIVID_DOMAIN_CONTROL;
    std::string variant;          // "", "composite", "empty"
    std::string destination;      // "auto", "project", "core"
    std::vector<VividPortSpec> ports;   // full port list (inputs + outputs); empty = domain defaults
    std::vector<VividParamSpec> params; // empty = use template defaults
};
