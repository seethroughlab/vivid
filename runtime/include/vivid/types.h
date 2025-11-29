#pragma once
#include <glm/glm.hpp>
#include <string>
#include <variant>
#include <vector>
#include <cstdint>

namespace vivid {

/**
 * @brief Lightweight texture handle for operators.
 *
 * Textures are created via Context::createTexture() and managed by the runtime.
 * Operators store and pass Texture handles; the actual GPU resources are internal.
 */
struct Texture {
    void* handle = nullptr;  ///< Opaque pointer to internal GPU resources.
    int width = 0;           ///< Texture width in pixels.
    int height = 0;          ///< Texture height in pixels.

    /// @brief Check if this texture handle is valid.
    bool valid() const { return handle != nullptr && width > 0 && height > 0; }
};

/**
 * @brief Variant type for parameter values.
 *
 * Used in ParamDecl to specify default, min, and max values for operator parameters.
 */
using ParamValue = std::variant<
    float,
    int,
    bool,
    glm::vec2,
    glm::vec3,
    glm::vec4,
    std::string
>;

/**
 * @brief Declaration of an operator parameter for editor introspection.
 *
 * Return these from Operator::params() to expose tweakable parameters.
 * Use helper functions floatParam(), intParam(), etc. from params.h.
 */
struct ParamDecl {
    std::string name;       ///< Display name of the parameter.
    ParamValue defaultValue;///< Default value.
    ParamValue minValue;    ///< Minimum value (for sliders).
    ParamValue maxValue;    ///< Maximum value (for sliders).
};

/**
 * @brief Types of output an operator can produce.
 *
 * Determines how the output is previewed in VS Code.
 */
enum class OutputKind {
    Texture,    ///< 2D image/texture (shown as thumbnail).
    Value,      ///< Single numeric value (shown as number with sparkline).
    ValueArray, ///< Array of values (shown as waveform).
    Geometry    ///< 3D geometry (future: wireframe preview).
};

/**
 * @brief Information about a node for the editor.
 *
 * Contains metadata used by the VS Code extension to display node info.
 */
struct NodeInfo {
    std::string id;                          ///< Unique operator identifier.
    int sourceLine = 0;                      ///< Line number in source file.
    OutputKind kind = OutputKind::Texture;   ///< Type of output.
    std::vector<ParamDecl> params;           ///< Parameter declarations.
};

/**
 * @brief Video codec types.
 */
enum class VideoCodecType {
    Unknown,
    Standard,   ///< H.264, H.265, ProRes, VP9, etc.
    HAP,        ///< HAP (DXT1/BC1)
    HAPAlpha,   ///< HAP Alpha (DXT5/BC3)
    HAPQ,       ///< HAP Q (Scaled DXT5)
    HAPQAlpha   ///< HAP Q Alpha
};

/**
 * @brief Video file metadata.
 */
struct VideoInfo {
    int width = 0;              ///< Video width in pixels.
    int height = 0;             ///< Video height in pixels.
    double duration = 0.0;      ///< Total duration in seconds.
    double frameRate = 0.0;     ///< Frames per second.
    int64_t frameCount = 0;     ///< Total frame count.
    VideoCodecType codecType = VideoCodecType::Unknown;
    bool hasAudio = false;      ///< Whether video has audio track.
    std::string codecName;      ///< Human-readable codec name.
};

/**
 * @brief Opaque handle to a video player.
 *
 * Created via Context::createVideoPlayer(). Manages video decoding
 * and frame extraction. The actual implementation is platform-specific.
 */
struct VideoPlayer {
    void* handle = nullptr;     ///< Opaque pointer to internal player.

    /// @brief Check if this video player handle is valid.
    bool valid() const { return handle != nullptr; }
};

} // namespace vivid
