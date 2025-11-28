#pragma once
#include <glm/glm.hpp>
#include <string>
#include <variant>
#include <vector>
#include <cstdint>

namespace vivid {

// Texture handle - lightweight struct for operators to pass around
// The actual GPU resources are managed internally by the runtime
struct Texture {
    void* handle = nullptr;  // Opaque pointer to internal texture data
    int width = 0;
    int height = 0;

    bool valid() const { return handle != nullptr && width > 0 && height > 0; }
};

// Parameter value types that operators can expose
using ParamValue = std::variant<
    float,
    int,
    bool,
    glm::vec2,
    glm::vec3,
    glm::vec4,
    std::string
>;

// Parameter declaration for introspection
struct ParamDecl {
    std::string name;
    ParamValue defaultValue;
    ParamValue minValue;
    ParamValue maxValue;
};

// Output types that operators can produce
enum class OutputKind {
    Texture,    // 2D image/texture
    Value,      // Single numeric value
    ValueArray, // Array of values (e.g., audio samples, positions)
    Geometry    // 3D geometry (future)
};

// Information about a node for the editor
struct NodeInfo {
    std::string id;
    int sourceLine = 0;
    OutputKind kind = OutputKind::Texture;
    std::vector<ParamDecl> params;
};

} // namespace vivid
