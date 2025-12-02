#pragma once
#include <glm/glm.hpp>
#include <string>
#include <variant>
#include <vector>
#include <cstdint>

// Export/import macro for Windows DLL support
// When building the vivid runtime, VIVID_BUILDING_RUNTIME should be defined
// When building operators that link against vivid, it should not be defined
#ifdef _WIN32
    #ifdef VIVID_BUILDING_RUNTIME
        #define VIVID_API __declspec(dllexport)
    #else
        #define VIVID_API __declspec(dllimport)
    #endif
#else
    #define VIVID_API
#endif

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

/**
 * @brief Information about a camera device.
 */
struct CameraDevice {
    std::string deviceId;       ///< Unique device identifier
    std::string name;           ///< Human-readable device name
    bool isDefault = false;     ///< True if this is the system default camera
};

/**
 * @brief Camera capture metadata.
 */
struct CameraInfo {
    int width = 0;              ///< Capture width in pixels.
    int height = 0;             ///< Capture height in pixels.
    float frameRate = 0.0f;     ///< Capture frame rate.
    std::string deviceName;     ///< Name of the active device.
    bool isCapturing = false;   ///< True if actively capturing.
};

/**
 * @brief Opaque handle to a camera capture.
 *
 * Created via Context::createCamera(). Manages camera capture
 * and frame extraction. The actual implementation is platform-specific.
 */
struct Camera {
    void* handle = nullptr;     ///< Opaque pointer to internal capture.

    /// @brief Check if this camera handle is valid.
    bool valid() const { return handle != nullptr; }
};

/**
 * @brief Raw image data loaded from a file.
 *
 * Used for CPU-side image processing before uploading to GPU.
 * Pixels are always in RGBA format (4 bytes per pixel).
 */
struct ImageData {
    std::vector<uint8_t> pixels;  ///< Pixel data in RGBA format.
    int width = 0;                ///< Image width in pixels.
    int height = 0;               ///< Image height in pixels.
    int channels = 0;             ///< Original channel count (1, 3, or 4).

    /// @brief Check if this image data is valid.
    bool valid() const { return !pixels.empty() && width > 0 && height > 0; }
};

/**
 * @brief 2D circle instance data for instanced rendering.
 *
 * Used with Context::drawCircles() for efficient rendering of many circles.
 */
struct Circle2D {
    glm::vec2 position;     ///< Center position (0-1 normalized).
    float radius;           ///< Radius in normalized coordinates.
    float _pad = 0.0f;      ///< Padding for GPU alignment.
    glm::vec4 color;        ///< RGBA color.

    Circle2D() = default;
    Circle2D(glm::vec2 pos, float r, glm::vec4 c)
        : position(pos), radius(r), _pad(0.0f), color(c) {}
    Circle2D(float x, float y, float r, float red, float green, float blue, float alpha = 1.0f)
        : position(x, y), radius(r), _pad(0.0f), color(red, green, blue, alpha) {}
};

} // namespace vivid
