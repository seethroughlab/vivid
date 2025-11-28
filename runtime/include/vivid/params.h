#pragma once
#include "types.h"

namespace vivid {

// Helper functions to create parameter declarations

inline ParamDecl floatParam(const std::string& name, float def, float min = 0.0f, float max = 1.0f) {
    return {name, def, min, max};
}

inline ParamDecl intParam(const std::string& name, int def, int min = 0, int max = 100) {
    return {name, def, min, max};
}

inline ParamDecl boolParam(const std::string& name, bool def) {
    return {name, def, false, true};
}

inline ParamDecl vec2Param(const std::string& name, glm::vec2 def,
                           glm::vec2 min = glm::vec2(0), glm::vec2 max = glm::vec2(1)) {
    return {name, def, min, max};
}

inline ParamDecl vec3Param(const std::string& name, glm::vec3 def,
                           glm::vec3 min = glm::vec3(0), glm::vec3 max = glm::vec3(1)) {
    return {name, def, min, max};
}

inline ParamDecl vec4Param(const std::string& name, glm::vec4 def,
                           glm::vec4 min = glm::vec4(0), glm::vec4 max = glm::vec4(1)) {
    return {name, def, min, max};
}

inline ParamDecl colorParam(const std::string& name, glm::vec3 def) {
    return {name, def, glm::vec3(0), glm::vec3(1)};
}

inline ParamDecl stringParam(const std::string& name, const std::string& def) {
    return {name, def, std::string(), std::string()};
}

} // namespace vivid
