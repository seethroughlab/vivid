#pragma once
#include "types.h"

namespace vivid {

/**
 * @file params.h
 * @brief Helper functions for creating parameter declarations.
 *
 * Use these functions in Operator::params() to expose tweakable parameters
 * to the VS Code extension.
 *
 * @code
 * std::vector<ParamDecl> params() override {
 *     return {
 *         floatParam("scale", scale_, 0.1f, 10.0f),
 *         intParam("count", count_, 1, 16),
 *         boolParam("enabled", enabled_)
 *     };
 * }
 * @endcode
 */

/**
 * @brief Create a float parameter declaration.
 * @param name Display name.
 * @param def Default value.
 * @param min Minimum value (default 0).
 * @param max Maximum value (default 1).
 */
inline ParamDecl floatParam(const std::string& name, float def, float min = 0.0f, float max = 1.0f) {
    return {name, def, min, max};
}

/**
 * @brief Create an integer parameter declaration.
 * @param name Display name.
 * @param def Default value.
 * @param min Minimum value (default 0).
 * @param max Maximum value (default 100).
 */
inline ParamDecl intParam(const std::string& name, int def, int min = 0, int max = 100) {
    return {name, def, min, max};
}

/**
 * @brief Create a boolean parameter declaration.
 * @param name Display name.
 * @param def Default value.
 */
inline ParamDecl boolParam(const std::string& name, bool def) {
    return {name, def, false, true};
}

/**
 * @brief Create a 2D vector parameter declaration.
 * @param name Display name.
 * @param def Default value.
 * @param min Minimum value (default (0,0)).
 * @param max Maximum value (default (1,1)).
 */
inline ParamDecl vec2Param(const std::string& name, glm::vec2 def,
                           glm::vec2 min = glm::vec2(0), glm::vec2 max = glm::vec2(1)) {
    return {name, def, min, max};
}

/**
 * @brief Create a 3D vector parameter declaration.
 * @param name Display name.
 * @param def Default value.
 * @param min Minimum value (default (0,0,0)).
 * @param max Maximum value (default (1,1,1)).
 */
inline ParamDecl vec3Param(const std::string& name, glm::vec3 def,
                           glm::vec3 min = glm::vec3(0), glm::vec3 max = glm::vec3(1)) {
    return {name, def, min, max};
}

/**
 * @brief Create a 4D vector parameter declaration.
 * @param name Display name.
 * @param def Default value.
 * @param min Minimum value (default (0,0,0,0)).
 * @param max Maximum value (default (1,1,1,1)).
 */
inline ParamDecl vec4Param(const std::string& name, glm::vec4 def,
                           glm::vec4 min = glm::vec4(0), glm::vec4 max = glm::vec4(1)) {
    return {name, def, min, max};
}

/**
 * @brief Create a color parameter declaration (RGB, 0-1 range).
 * @param name Display name.
 * @param def Default color value.
 */
inline ParamDecl colorParam(const std::string& name, glm::vec3 def) {
    return {name, def, glm::vec3(0), glm::vec3(1)};
}

/**
 * @brief Create a string parameter declaration.
 * @param name Display name.
 * @param def Default value.
 */
inline ParamDecl stringParam(const std::string& name, const std::string& def) {
    return {name, def, std::string(), std::string()};
}

} // namespace vivid
