/**
 * @file vivid_c.h
 * @brief C API for Vivid Core
 *
 * This header provides a pure C interface to vivid-core functionality,
 * enabling embedding in applications like Tauri IDE with external WebGPU
 * device/queue management.
 *
 * ## Memory Ownership Rules
 *
 * | Item              | Owner  | Lifetime                        |
 * |-------------------|--------|----------------------------------|
 * | VividContext      | Caller | Until vivid_context_destroy()    |
 * | VividChain        | Context| While project loaded             |
 * | VividOperator     | Chain  | While chain alive                |
 * | WGPUDevice/Queue  | Caller | Caller manages GPU context       |
 * | WGPUTextureView   | Operator| While operator alive            |
 * | String pointers   | Library| Until owning object destroyed    |
 *
 * ## Usage Example
 *
 * @code
 * // Create context with external GPU device
 * VividContext* ctx = NULL;
 * VividContextConfig config = { .width = 1920, .height = 1080 };
 * VividResult result = vivid_context_create_external(device, queue, &config, &ctx);
 * if (result != VIVID_OK) {
 *     printf("Error: %s\n", vivid_get_last_error());
 *     return;
 * }
 *
 * // Load a project
 * result = vivid_context_load_project(ctx, "/path/to/project");
 * VividCompileStatus status = vivid_context_get_compile_status(ctx);
 * if (!status.success) {
 *     printf("Compile error: %s\n", status.message);
 * }
 *
 * // Process frames
 * vivid_context_process_frame(ctx, 0.016);  // 60fps
 *
 * // Iterate operators
 * VividChain* chain = vivid_context_get_chain(ctx);
 * int count = vivid_chain_get_operator_count(chain);
 * for (int i = 0; i < count; i++) {
 *     VividOperator* op = vivid_chain_get_operator_by_index(chain, i);
 *     WGPUTextureView view = vivid_operator_get_output_view(op);
 *     // Use texture view for preview...
 * }
 *
 * // Cleanup
 * vivid_context_destroy(ctx);
 * @endcode
 */

#ifndef VIVID_C_H
#define VIVID_C_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * API Version
 *
 * The API version is incremented when breaking changes occur. Embedders can
 * check VIVID_API_VERSION at compile time and vivid_get_api_version() at
 * runtime to ensure compatibility.
 *
 * Version History:
 *   1 - Initial stable API (2025-01)
 *       - Context lifecycle (create_external, create_with_window, destroy)
 *       - Project loading (load, reload, unload)
 *       - Frame processing (process_frame, render_frame)
 *       - Operator iteration and parameter access
 *       - Input injection for embedded use
 *       - Visualizer control (visibility, selection)
 * ============================================================================ */

#define VIVID_API_VERSION 1
#define VIVID_VERSION_MAJOR 0
#define VIVID_VERSION_MINOR 1
#define VIVID_VERSION_PATCH 0
#define VIVID_VERSION_STRING "0.1.0"

/* ============================================================================
 * Platform-specific export macros
 * ============================================================================ */

#ifdef _WIN32
    #ifdef VIVID_C_BUILDING_DLL
        #define VIVID_C_API __declspec(dllexport)
    #else
        #define VIVID_C_API __declspec(dllimport)
    #endif
#else
    #define VIVID_C_API __attribute__((visibility("default")))
#endif

/* ============================================================================
 * Opaque WebGPU types (for C compatibility)
 * These match the wgpu-native C API types
 * ============================================================================ */

typedef void* VividWGPUDevice;
typedef void* VividWGPUQueue;
typedef void* VividWGPUTextureView;
typedef void* VividWGPUTexture;

/* ============================================================================
 * Opaque Handle Types
 * ============================================================================ */

/** @brief Opaque context handle */
typedef struct VividContext VividContext;

/** @brief Opaque chain handle */
typedef struct VividChain VividChain;

/** @brief Opaque operator handle */
typedef struct VividOperator VividOperator;

/* ============================================================================
 * Result Codes
 * ============================================================================ */

/** @brief Result codes for API calls */
typedef enum VividResult {
    VIVID_OK = 0,                       /**< Success */
    VIVID_ERROR_INVALID_ARGUMENT = 1,   /**< Invalid argument passed */
    VIVID_ERROR_NOT_INITIALIZED = 2,    /**< Context not initialized */
    VIVID_ERROR_LOAD_FAILED = 3,        /**< Project load failed */
    VIVID_ERROR_COMPILE_FAILED = 4,     /**< Compilation failed */
    VIVID_ERROR_NO_CHAIN = 5,           /**< No chain loaded */
    VIVID_ERROR_OPERATOR_NOT_FOUND = 6, /**< Operator not found */
    VIVID_ERROR_PARAM_NOT_FOUND = 7,    /**< Parameter not found */
    VIVID_ERROR_NOT_SUPPORTED = 8,      /**< Operation not supported on this platform */
    VIVID_ERROR_INTERNAL = 99           /**< Internal error */
} VividResult;

/* ============================================================================
 * Output Kind Enum (matches vivid::OutputKind)
 * ============================================================================ */

/** @brief Output type classification for operators */
typedef enum VividOutputKind {
    VIVID_OUTPUT_TEXTURE = 0,      /**< GPU texture output (most common) */
    VIVID_OUTPUT_CPU_PIXELS = 1,   /**< CPU pixel buffer */
    VIVID_OUTPUT_VALUE = 2,        /**< Single float value */
    VIVID_OUTPUT_VALUE_ARRAY = 3,  /**< Array of float values */
    VIVID_OUTPUT_GEOMETRY = 4,     /**< 3D geometry */
    VIVID_OUTPUT_CAMERA = 5,       /**< Camera configuration */
    VIVID_OUTPUT_LIGHT = 6,        /**< Light source */
    VIVID_OUTPUT_AUDIO = 7,        /**< Audio buffer output */
    VIVID_OUTPUT_AUDIO_VALUE = 8,  /**< Audio analysis values */
    VIVID_OUTPUT_EVENT = 9         /**< Event stream */
} VividOutputKind;

/* ============================================================================
 * Parameter Type Enum (matches vivid::ParamType)
 * ============================================================================ */

/** @brief Parameter types for UI/serialization */
typedef enum VividParamType {
    VIVID_PARAM_FLOAT = 0,      /**< Single float value */
    VIVID_PARAM_INT = 1,        /**< Integer value */
    VIVID_PARAM_BOOL = 2,       /**< Boolean toggle */
    VIVID_PARAM_VEC2 = 3,       /**< 2D vector (x, y) */
    VIVID_PARAM_VEC3 = 4,       /**< 3D vector (x, y, z) */
    VIVID_PARAM_VEC4 = 5,       /**< 4D vector (x, y, z, w) */
    VIVID_PARAM_COLOR = 6,      /**< RGBA color (0-1 range) */
    VIVID_PARAM_STRING = 7,     /**< Text string */
    VIVID_PARAM_FILE_PATH = 8,  /**< File path */
    VIVID_PARAM_ENUM = 9,       /**< Enumeration (dropdown) */
    VIVID_PARAM_ADSR = 10,      /**< ADSR envelope */
    VIVID_PARAM_DEVICE_LIST = 11 /**< Dynamic device list */
} VividParamType;

/* ============================================================================
 * Configuration Structures
 * ============================================================================ */

/** @brief Configuration for creating a context */
typedef struct VividContextConfig {
    int width;              /**< Render width in pixels */
    int height;             /**< Render height in pixels */
    bool enable_validation; /**< Enable WebGPU validation (debug) */
} VividContextConfig;

/** @brief Compilation status */
typedef struct VividCompileStatus {
    bool success;           /**< True if compilation succeeded */
    const char* message;    /**< Error message (NULL if success) */
    int error_line;         /**< Line number of first error (0 if success) */
    int error_column;       /**< Column of first error (0 if success) */
} VividCompileStatus;

/** @brief Texture information */
typedef struct VividTextureInfo {
    int width;              /**< Texture width in pixels */
    int height;             /**< Texture height in pixels */
    int format;             /**< WebGPU texture format (WGPUTextureFormat) */
    bool has_alpha;         /**< True if texture has alpha channel */
} VividTextureInfo;

/** @brief Parameter declaration for introspection */
typedef struct VividParamDecl {
    const char* name;       /**< Parameter name */
    VividParamType type;    /**< Parameter type */
    float min_val;          /**< Minimum value */
    float max_val;          /**< Maximum value */
    float default_val[4];   /**< Default value(s) */
    const char* string_default; /**< Default string (for String/FilePath) */
    int enum_count;         /**< Number of enum labels */
    const char** enum_labels; /**< Array of enum label strings */
} VividParamDecl;

/* ============================================================================
 * Error Handling
 * ============================================================================ */

/**
 * @brief Get the last error message
 * @return Error string, or NULL if no error
 *
 * Thread-safe. Each thread has its own error string.
 */
VIVID_C_API const char* vivid_get_last_error(void);

/**
 * @brief Clear the last error
 */
VIVID_C_API void vivid_clear_error(void);

/* ============================================================================
 * Context Lifecycle
 * ============================================================================ */

/**
 * @brief Create a context with external WebGPU device and queue
 * @param device WebGPU device handle (owned by caller)
 * @param queue WebGPU queue handle (owned by caller)
 * @param config Context configuration
 * @param out_ctx Output pointer to receive context handle
 * @return VIVID_OK on success, error code otherwise
 *
 * Creates a headless context that uses the provided WebGPU device and queue.
 * This is the primary entry point for embedding Vivid in external applications.
 *
 * The caller is responsible for:
 * - Creating and managing the WebGPU device and queue
 * - Ensuring device/queue remain valid until vivid_context_destroy()
 * - Calling vivid_context_destroy() when done
 */
VIVID_C_API VividResult vivid_context_create_external(
    VividWGPUDevice device,
    VividWGPUQueue queue,
    const VividContextConfig* config,
    VividContext** out_ctx
);

/**
 * @brief Create a context with a native window handle
 * @param native_window Native window handle (NSWindow* on macOS, HWND on Windows)
 * @param config Context configuration
 * @param out_ctx Output pointer to receive context handle
 * @return VIVID_OK on success, error code otherwise
 *
 * Creates a context that owns the WebGPU device, surface, and rendering pipeline.
 * This is the preferred entry point for IDE integration where vivid-core manages
 * all GPU rendering including the chain visualizer UI.
 *
 * The caller is responsible for:
 * - Providing a valid native window handle
 * - Calling vivid_context_render_frame() each frame
 * - Calling vivid_context_resize() when window size changes
 * - Calling vivid_context_destroy() when done
 */
VIVID_C_API VividResult vivid_context_create_with_window(
    void* native_window,
    const VividContextConfig* config,
    VividContext** out_ctx
);

/**
 * @brief Render a complete frame (chain output + visualizer UI)
 * @param ctx Context handle
 * @return VIVID_OK on success, error code otherwise
 *
 * This renders the chain output and overlays the node graph visualizer.
 * Call this once per frame after processing input events.
 * Only valid for contexts created with vivid_context_create_with_window().
 */
VIVID_C_API VividResult vivid_context_render_frame(VividContext* ctx);

/**
 * @brief Resize the rendering surface
 * @param ctx Context handle
 * @param width New width in pixels
 * @param height New height in pixels
 * @return VIVID_OK on success, error code otherwise
 *
 * Call this when the window size changes.
 * Only valid for contexts created with vivid_context_create_with_window().
 */
VIVID_C_API VividResult vivid_context_resize_surface(VividContext* ctx, int width, int height);

/**
 * @brief Set visualizer UI visibility
 * @param ctx Context handle
 * @param visible True to show node graph/inspector, false to show chain output only
 *
 * When hidden, only the chain output is rendered (useful for fullscreen preview).
 */
VIVID_C_API void vivid_context_set_visualizer_visible(VividContext* ctx, bool visible);

/**
 * @brief Check if visualizer UI is visible
 * @param ctx Context handle
 * @return True if visualizer is visible
 */
VIVID_C_API bool vivid_context_is_visualizer_visible(VividContext* ctx);

/**
 * @brief Get the name of the currently selected operator in the visualizer
 * @param ctx Context handle
 * @return Operator name string, or NULL if no operator is selected
 *
 * The returned string is valid until the selection changes or the context is destroyed.
 */
VIVID_C_API const char* vivid_context_get_selected_operator(VividContext* ctx);

/**
 * @brief Select an operator in the visualizer by name
 * @param ctx Context handle
 * @param name Operator name to select
 *
 * The selection will be applied on the next render frame.
 */
VIVID_C_API void vivid_context_select_operator(VividContext* ctx, const char* name);

/**
 * @brief Destroy a context and free all resources
 * @param ctx Context to destroy
 *
 * After this call, the context handle is invalid. For contexts created with
 * create_external, the WebGPU device/queue remain valid (owned by caller).
 * For contexts created with create_with_window, all GPU resources are freed.
 */
VIVID_C_API void vivid_context_destroy(VividContext* ctx);

/* ============================================================================
 * Project Loading
 * ============================================================================ */

/**
 * @brief Load a project from a directory path
 * @param ctx Context handle
 * @param path Path to project directory (contains chain.cpp)
 * @return VIVID_OK on success, error code otherwise
 *
 * This compiles chain.cpp and loads the resulting library.
 * Check vivid_context_get_compile_status() for compilation errors.
 */
VIVID_C_API VividResult vivid_context_load_project(VividContext* ctx, const char* path);

/**
 * @brief Set the vivid installation root directory
 * @param ctx Context handle
 * @param path Path to vivid root (directory containing modules/ and build/)
 * @return VIVID_OK on success, error code otherwise
 *
 * For embedded use: tells the hot-reload compiler where to find vivid headers
 * and libraries. Call this before load_project() when vivid is embedded as a
 * submodule and the executable is not in the standard vivid directory structure.
 */
VIVID_C_API VividResult vivid_context_set_root_dir(VividContext* ctx, const char* path);

/**
 * @brief Configure asset search paths before creating a context
 * @param vivid_root Path to vivid root (directory containing modules/ and build/)
 * @return VIVID_OK on success, error code otherwise
 *
 * Call this BEFORE vivid_context_create_*() when vivid is embedded as a
 * submodule. This ensures shaders and fonts can be found during initialization.
 */
VIVID_C_API VividResult vivid_configure_asset_paths(const char* vivid_root);

/**
 * @brief Reload the current project
 * @param ctx Context handle
 * @return VIVID_OK on success, error code otherwise
 *
 * Recompiles and reloads the chain, preserving operator states where possible.
 */
VIVID_C_API VividResult vivid_context_reload(VividContext* ctx);

/**
 * @brief Unload the current project
 * @param ctx Context handle
 * @return VIVID_OK on success, error code otherwise
 */
VIVID_C_API VividResult vivid_context_unload_project(VividContext* ctx);

/**
 * @brief Get compilation status
 * @param ctx Context handle
 * @return Compilation status struct
 *
 * The message pointer is valid until the next load/reload operation.
 */
VIVID_C_API VividCompileStatus vivid_context_get_compile_status(VividContext* ctx);

/**
 * @brief Check if a project is loaded
 * @param ctx Context handle
 * @return True if a project is loaded and ready
 */
VIVID_C_API bool vivid_context_has_project(VividContext* ctx);

/**
 * @brief Get the loaded project path
 * @param ctx Context handle
 * @return Project path, or NULL if no project loaded
 */
VIVID_C_API const char* vivid_context_get_project_path(VividContext* ctx);

/* ============================================================================
 * Frame Processing
 * ============================================================================ */

/**
 * @brief Process a single frame
 * @param ctx Context handle
 * @param dt Delta time since last frame (seconds)
 * @return VIVID_OK on success, error code otherwise
 *
 * This runs the chain's update function and processes all operators.
 */
VIVID_C_API VividResult vivid_context_process_frame(VividContext* ctx, double dt);

/**
 * @brief Get the current frame number
 * @param ctx Context handle
 * @return Frame count (0-indexed)
 */
VIVID_C_API uint64_t vivid_context_get_frame(VividContext* ctx);

/**
 * @brief Get elapsed time
 * @param ctx Context handle
 * @return Time in seconds since project load
 */
VIVID_C_API double vivid_context_get_time(VividContext* ctx);

/**
 * @brief Reset time and frame counter
 * @param ctx Context handle
 */
VIVID_C_API void vivid_context_reset_time(VividContext* ctx);

/* ============================================================================
 * Resolution Management
 * ============================================================================ */

/**
 * @brief Set render resolution
 * @param ctx Context handle
 * @param width Width in pixels
 * @param height Height in pixels
 * @return VIVID_OK on success
 */
VIVID_C_API VividResult vivid_context_set_resolution(VividContext* ctx, int width, int height);

/**
 * @brief Get render width
 * @param ctx Context handle
 * @return Width in pixels
 */
VIVID_C_API int vivid_context_get_width(VividContext* ctx);

/**
 * @brief Get render height
 * @param ctx Context handle
 * @return Height in pixels
 */
VIVID_C_API int vivid_context_get_height(VividContext* ctx);

/* ============================================================================
 * Input Injection (for embedded use)
 * ============================================================================ */

/**
 * @brief Set mouse position
 * @param ctx Context handle
 * @param x X position in pixels
 * @param y Y position in pixels
 */
VIVID_C_API void vivid_context_set_mouse_position(VividContext* ctx, float x, float y);

/**
 * @brief Set mouse button state
 * @param ctx Context handle
 * @param button Button index (0=left, 1=right, 2=middle)
 * @param pressed True if button is pressed
 */
VIVID_C_API void vivid_context_set_mouse_button(VividContext* ctx, int button, bool pressed);

/**
 * @brief Set key state
 * @param ctx Context handle
 * @param keycode GLFW key code
 * @param pressed True if key is pressed
 */
VIVID_C_API void vivid_context_set_key(VividContext* ctx, int keycode, bool pressed);

/**
 * @brief Add scroll delta
 * @param ctx Context handle
 * @param dx Horizontal scroll delta
 * @param dy Vertical scroll delta
 */
VIVID_C_API void vivid_context_add_scroll(VividContext* ctx, float dx, float dy);

/* ============================================================================
 * Chain Access
 * ============================================================================ */

/**
 * @brief Get the chain from a context
 * @param ctx Context handle
 * @return Chain handle, or NULL if no project loaded
 */
VIVID_C_API VividChain* vivid_context_get_chain(VividContext* ctx);

/**
 * @brief Get the output texture view from the chain
 * @param ctx Context handle
 * @return Output texture view, or NULL if no output
 */
VIVID_C_API VividWGPUTextureView vivid_context_get_output_view(VividContext* ctx);

/**
 * @brief Get the output texture from the chain
 * @param ctx Context handle
 * @return Output texture, or NULL if no output
 */
VIVID_C_API VividWGPUTexture vivid_context_get_output_texture(VividContext* ctx);

/**
 * @brief Get the WebGPU device
 * @param ctx Context handle
 * @return WGPUDevice handle, or NULL if context not initialized
 */
VIVID_C_API VividWGPUDevice vivid_context_get_device(VividContext* ctx);

/**
 * @brief Get the WebGPU queue
 * @param ctx Context handle
 * @return WGPUQueue handle, or NULL if context not initialized
 */
VIVID_C_API VividWGPUQueue vivid_context_get_queue(VividContext* ctx);

/* ============================================================================
 * IOSurface Sharing (macOS only)
 * ============================================================================ */

/**
 * @brief Create an IOSurface-backed texture for the chain output
 * @param ctx Context handle
 * @param out_iosurface Output IOSurfaceRef (caller does NOT own, valid until context destroyed)
 * @param out_width Output texture width
 * @param out_height Output texture height
 * @return VIVID_OK on success, VIVID_ERROR_NOT_SUPPORTED on non-macOS platforms
 *
 * Creates or returns existing IOSurface-backed texture that receives chain output.
 * Call this once to set up sharing, then the IOSurface updates automatically each frame.
 * The IOSurface can be used to create a Metal texture in another context for zero-copy
 * GPU texture sharing.
 *
 * macOS only - returns VIVID_ERROR_NOT_SUPPORTED on other platforms.
 */
VIVID_C_API VividResult vivid_context_get_output_iosurface(
    VividContext* ctx,
    void** out_iosurface,
    int* out_width,
    int* out_height
);

/**
 * @brief Check if IOSurface sharing is supported on this platform
 * @return true on macOS, false on other platforms
 */
VIVID_C_API bool vivid_iosurface_supported(void);

/**
 * @brief Create an IOSurface sharing state
 * @return Opaque state handle, or NULL on failure
 *
 * Use this to create IOSurface sharing for individual operators.
 * Each state manages its own IOSurface and Metal resources.
 * Call vivid_iosurface_destroy_state when done.
 */
VIVID_C_API void* vivid_iosurface_create_state(void);

/**
 * @brief Destroy an IOSurface sharing state
 * @param state State handle from vivid_iosurface_create_state
 */
VIVID_C_API void vivid_iosurface_destroy_state(void* state);

/**
 * @brief Update an IOSurface from a texture
 * @param state IOSurface state handle
 * @param device WebGPU device
 * @param queue WebGPU queue
 * @param texture Source texture to copy to IOSurface
 * @param out_iosurface Output IOSurface ref (valid until next call or destroy)
 * @param out_width Output texture width
 * @param out_height Output texture height
 * @return VIVID_OK on success
 *
 * Copies the texture data to the IOSurface. The IOSurface is created
 * or resized as needed to match the texture dimensions.
 */
VIVID_C_API VividResult vivid_iosurface_update_from_texture(
    void* state,
    VividWGPUDevice device,
    VividWGPUQueue queue,
    VividWGPUTexture texture,
    void** out_iosurface,
    int* out_width,
    int* out_height
);

/* ============================================================================
 * Operator Iteration
 * ============================================================================ */

/**
 * @brief Get number of operators in the chain
 * @param chain Chain handle
 * @return Number of operators
 */
VIVID_C_API int vivid_chain_get_operator_count(VividChain* chain);

/**
 * @brief Get operator by index
 * @param chain Chain handle
 * @param index Operator index (0-based)
 * @return Operator handle, or NULL if index out of range
 */
VIVID_C_API VividOperator* vivid_chain_get_operator_by_index(VividChain* chain, int index);

/**
 * @brief Get operator by name
 * @param chain Chain handle
 * @param name Operator name
 * @return Operator handle, or NULL if not found
 */
VIVID_C_API VividOperator* vivid_chain_get_operator_by_name(VividChain* chain, const char* name);

/**
 * @brief Get operator instance name by index
 * @param chain Chain handle
 * @param index Operator index (0-based)
 * @return Operator instance name, or empty string if index out of bounds
 * @note Returns the instance name (e.g., "noise") not the type name (e.g., "Noise")
 */
VIVID_C_API const char* vivid_chain_get_operator_name_by_index(VividChain* chain, int index);

/**
 * @brief Get the output operator (final output of the chain)
 * @param chain Chain handle
 * @return Output operator handle, or NULL if no output set
 */
VIVID_C_API VividOperator* vivid_chain_get_output_operator(VividChain* chain);

/* ============================================================================
 * Operator Information
 * ============================================================================ */

/**
 * @brief Get operator name
 * @param op Operator handle
 * @return Operator name string
 */
VIVID_C_API const char* vivid_operator_get_name(VividOperator* op);

/**
 * @brief Get operator type name (e.g., "Noise", "Blur")
 * @param op Operator handle
 * @return Type name string
 */
VIVID_C_API const char* vivid_operator_get_type_name(VividOperator* op);

/**
 * @brief Get operator output kind
 * @param op Operator handle
 * @return Output kind enum value
 */
VIVID_C_API VividOutputKind vivid_operator_get_output_kind(VividOperator* op);

/**
 * @brief Check if operator is bypassed
 * @param op Operator handle
 * @return True if bypassed
 */
VIVID_C_API bool vivid_operator_is_bypassed(VividOperator* op);

/**
 * @brief Set operator bypass state
 * @param op Operator handle
 * @param bypassed True to bypass
 */
VIVID_C_API void vivid_operator_set_bypassed(VividOperator* op, bool bypassed);

/* ============================================================================
 * Operator Outputs (Textures)
 * ============================================================================ */

/**
 * @brief Get operator output texture view
 * @param op Operator handle
 * @return Texture view, or NULL if not a texture operator
 *
 * The returned view is valid while the operator exists and has an output.
 * Use this for previewing operator outputs in the IDE.
 */
VIVID_C_API VividWGPUTextureView vivid_operator_get_output_view(VividOperator* op);

/**
 * @brief Get operator output texture
 * @param op Operator handle
 * @return Texture, or NULL if not a texture operator
 */
VIVID_C_API VividWGPUTexture vivid_operator_get_output_texture(VividOperator* op);

/**
 * @brief Get texture information
 * @param op Operator handle
 * @param out_info Output texture info struct
 * @return True if operator has texture output
 */
VIVID_C_API bool vivid_operator_get_texture_info(VividOperator* op, VividTextureInfo* out_info);

/**
 * @brief Get operator output value (for Value operators)
 * @param op Operator handle
 * @return Output value, or 0.0 if not a value operator
 */
VIVID_C_API float vivid_operator_get_output_value(VividOperator* op);

/* ============================================================================
 * Operator Parameters
 * ============================================================================ */

/**
 * @brief Get number of parameters
 * @param op Operator handle
 * @return Parameter count
 */
VIVID_C_API int vivid_operator_get_param_count(VividOperator* op);

/**
 * @brief Get parameter declaration by index
 * @param op Operator handle
 * @param index Parameter index
 * @param out_decl Output parameter declaration
 * @return True if index is valid
 *
 * The string pointers in VividParamDecl are valid while the operator exists.
 */
VIVID_C_API bool vivid_operator_get_param_decl(VividOperator* op, int index, VividParamDecl* out_decl);

/**
 * @brief Get parameter value
 * @param op Operator handle
 * @param name Parameter name
 * @param out_value Output array for value (4 floats)
 * @return True if parameter exists
 */
VIVID_C_API bool vivid_operator_get_param(VividOperator* op, const char* name, float out_value[4]);

/**
 * @brief Set parameter value
 * @param op Operator handle
 * @param name Parameter name
 * @param value Value array (1-4 floats depending on type)
 * @return True if parameter was set successfully
 */
VIVID_C_API bool vivid_operator_set_param(VividOperator* op, const char* name, const float value[4]);

/**
 * @brief Get parameter string value
 * @param op Operator handle
 * @param name Parameter name
 * @return String value, or NULL if not a string parameter
 */
VIVID_C_API const char* vivid_operator_get_param_string(VividOperator* op, const char* name);

/**
 * @brief Set parameter string value
 * @param op Operator handle
 * @param name Parameter name
 * @param value String value
 * @return True if parameter was set successfully
 */
VIVID_C_API bool vivid_operator_set_param_string(VividOperator* op, const char* name, const char* value);

/* ============================================================================
 * Operator Inputs
 * ============================================================================ */

/**
 * @brief Get number of inputs
 * @param op Operator handle
 * @return Input count
 */
VIVID_C_API int vivid_operator_get_input_count(VividOperator* op);

/**
 * @brief Get input operator by index
 * @param op Operator handle
 * @param index Input index
 * @return Input operator, or NULL if not connected
 */
VIVID_C_API VividOperator* vivid_operator_get_input(VividOperator* op, int index);

/**
 * @brief Get input name/label
 * @param op Operator handle
 * @param index Input index
 * @return Input name, or empty string if not named
 */
VIVID_C_API const char* vivid_operator_get_input_name(VividOperator* op, int index);

/* ============================================================================
 * Operator Registry (available operators)
 * ============================================================================ */

/**
 * @brief Get number of registered operator types
 * @return Count of available operator types
 */
VIVID_C_API int vivid_registry_get_operator_count(void);

/**
 * @brief Get registered operator type name by index
 * @param index Registry index
 * @return Operator type name (e.g., "Noise", "Blur")
 */
VIVID_C_API const char* vivid_registry_get_operator_name(int index);

/**
 * @brief Get operator category by index
 * @param index Registry index
 * @return Category name (e.g., "Generator", "Effect")
 */
VIVID_C_API const char* vivid_registry_get_operator_category(int index);

/* ============================================================================
 * Snapshot/Capture
 * ============================================================================ */

/**
 * @brief Capture current output to a PNG file
 * @param ctx Context handle
 * @param path Output file path
 * @return VIVID_OK on success
 */
VIVID_C_API VividResult vivid_context_capture_snapshot(VividContext* ctx, const char* path);

/**
 * @brief Capture operator output to a PNG file
 * @param op Operator handle
 * @param path Output file path
 * @return VIVID_OK on success
 */
VIVID_C_API VividResult vivid_operator_capture_snapshot(VividOperator* op, const char* path);

/* ============================================================================
 * Version Information
 * ============================================================================ */

/**
 * @brief Get Vivid version string
 * @return Version string (e.g., "0.1.0")
 */
VIVID_C_API const char* vivid_get_version(void);

/**
 * @brief Get Vivid API version number
 * @return API version (increments on breaking changes)
 */
VIVID_C_API int vivid_get_api_version(void);

#ifdef __cplusplus
}
#endif

#endif /* VIVID_C_H */
