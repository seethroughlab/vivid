/**
 * @file vivid_c.cpp
 * @brief C API implementation for Vivid Core
 */

#include <vivid/vivid_c.h>
#include <vivid/context.h>
#include <vivid/chain.h>
#include <vivid/operator.h>
#include <vivid/hot_reload.h>
#include <vivid/video_exporter.h>
#include <vivid/operator_registry.h>

#include <string>
#include <mutex>
#include <cstring>

// Thread-local error message
static thread_local std::string s_lastError;

// Version info
static const char* VIVID_VERSION = "0.1.0";
static const int VIVID_API_VERSION = 1;

// =============================================================================
// Internal helper functions
// =============================================================================

static void setError(const char* msg) {
    s_lastError = msg ? msg : "";
}

static void setError(const std::string& msg) {
    s_lastError = msg;
}

static vivid::Context* toContext(VividContext* ctx) {
    return reinterpret_cast<vivid::Context*>(ctx);
}

static VividContext* fromContext(vivid::Context* ctx) {
    return reinterpret_cast<VividContext*>(ctx);
}

static vivid::Chain* toChain(VividChain* chain) {
    return reinterpret_cast<vivid::Chain*>(chain);
}

static VividChain* fromChain(vivid::Chain* chain) {
    return reinterpret_cast<VividChain*>(chain);
}

static vivid::Operator* toOperator(VividOperator* op) {
    return reinterpret_cast<vivid::Operator*>(op);
}

static VividOperator* fromOperator(vivid::Operator* op) {
    return reinterpret_cast<VividOperator*>(op);
}

// Convert C++ OutputKind to C enum
static VividOutputKind convertOutputKind(vivid::OutputKind kind) {
    switch (kind) {
        case vivid::OutputKind::Texture:    return VIVID_OUTPUT_TEXTURE;
        case vivid::OutputKind::CpuPixels:  return VIVID_OUTPUT_CPU_PIXELS;
        case vivid::OutputKind::Value:      return VIVID_OUTPUT_VALUE;
        case vivid::OutputKind::ValueArray: return VIVID_OUTPUT_VALUE_ARRAY;
        case vivid::OutputKind::Geometry:   return VIVID_OUTPUT_GEOMETRY;
        case vivid::OutputKind::Camera:     return VIVID_OUTPUT_CAMERA;
        case vivid::OutputKind::Light:      return VIVID_OUTPUT_LIGHT;
        case vivid::OutputKind::Audio:      return VIVID_OUTPUT_AUDIO;
        case vivid::OutputKind::AudioValue: return VIVID_OUTPUT_AUDIO_VALUE;
        case vivid::OutputKind::Event:      return VIVID_OUTPUT_EVENT;
        default:                            return VIVID_OUTPUT_TEXTURE;
    }
}

// Convert C++ ParamType to C enum
static VividParamType convertParamType(vivid::ParamType type) {
    switch (type) {
        case vivid::ParamType::Float:      return VIVID_PARAM_FLOAT;
        case vivid::ParamType::Int:        return VIVID_PARAM_INT;
        case vivid::ParamType::Bool:       return VIVID_PARAM_BOOL;
        case vivid::ParamType::Vec2:       return VIVID_PARAM_VEC2;
        case vivid::ParamType::Vec3:       return VIVID_PARAM_VEC3;
        case vivid::ParamType::Vec4:       return VIVID_PARAM_VEC4;
        case vivid::ParamType::Color:      return VIVID_PARAM_COLOR;
        case vivid::ParamType::String:     return VIVID_PARAM_STRING;
        case vivid::ParamType::FilePath:   return VIVID_PARAM_FILE_PATH;
        case vivid::ParamType::Enum:       return VIVID_PARAM_ENUM;
        case vivid::ParamType::ADSR:       return VIVID_PARAM_ADSR;
        case vivid::ParamType::DeviceList: return VIVID_PARAM_DEVICE_LIST;
        default:                           return VIVID_PARAM_FLOAT;
    }
}

// =============================================================================
// Internal context wrapper (holds HotReload for project loading)
// =============================================================================

struct VividContextInternal {
    vivid::Context* context = nullptr;
    std::unique_ptr<vivid::HotReload> hotReload;
    std::string projectPath;
    std::string compileError;
    bool hasProject = false;

    ~VividContextInternal() {
        delete context;
    }
};

static VividContextInternal* toInternal(VividContext* ctx) {
    return reinterpret_cast<VividContextInternal*>(ctx);
}

static VividContext* fromInternal(VividContextInternal* internal) {
    return reinterpret_cast<VividContext*>(internal);
}

// =============================================================================
// Error Handling
// =============================================================================

VIVID_C_API const char* vivid_get_last_error(void) {
    return s_lastError.empty() ? nullptr : s_lastError.c_str();
}

VIVID_C_API void vivid_clear_error(void) {
    s_lastError.clear();
}

// =============================================================================
// Context Lifecycle
// =============================================================================

VIVID_C_API VividResult vivid_context_create_external(
    VividWGPUDevice device,
    VividWGPUQueue queue,
    const VividContextConfig* config,
    VividContext** out_ctx
) {
    if (!device || !queue || !out_ctx) {
        setError("Invalid argument: device, queue, and out_ctx must not be NULL");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    if (!config) {
        setError("Invalid argument: config must not be NULL");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    try {
        auto* internal = new VividContextInternal();
        internal->context = new vivid::Context(
            static_cast<WGPUDevice>(device),
            static_cast<WGPUQueue>(queue),
            config->width,
            config->height
        );
        internal->hotReload = std::make_unique<vivid::HotReload>();

        *out_ctx = fromInternal(internal);
        return VIVID_OK;
    } catch (const std::exception& e) {
        setError(std::string("Failed to create context: ") + e.what());
        return VIVID_ERROR_INTERNAL;
    }
}

VIVID_C_API void vivid_context_destroy(VividContext* ctx) {
    if (ctx) {
        delete toInternal(ctx);
    }
}

// =============================================================================
// Project Loading
// =============================================================================

VIVID_C_API VividResult vivid_context_load_project(VividContext* ctx, const char* path) {
    if (!ctx || !path) {
        setError("Invalid argument");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    auto* internal = toInternal(ctx);

    try {
        // Find chain.cpp in the project directory
        std::string projectPath = path;
        std::string chainPath = projectPath + "/chain.cpp";

        internal->projectPath = projectPath;
        internal->hotReload->setSourceFile(chainPath);

        if (!internal->hotReload->reload()) {
            internal->compileError = internal->hotReload->getError();
            internal->hasProject = false;
            setError(internal->compileError);
            return VIVID_ERROR_COMPILE_FAILED;
        }

        // Reset the chain and call setup
        internal->context->resetChain();
        internal->context->setChainPath(chainPath);

        auto setupFn = internal->hotReload->getSetupFn();
        if (setupFn) {
            setupFn(*internal->context);
        }

        internal->compileError.clear();
        internal->hasProject = true;
        return VIVID_OK;
    } catch (const std::exception& e) {
        internal->compileError = e.what();
        internal->hasProject = false;
        setError(e.what());
        return VIVID_ERROR_LOAD_FAILED;
    }
}

VIVID_C_API VividResult vivid_context_reload(VividContext* ctx) {
    if (!ctx) {
        setError("Invalid argument");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    auto* internal = toInternal(ctx);

    if (!internal->hasProject) {
        setError("No project loaded");
        return VIVID_ERROR_NO_CHAIN;
    }

    try {
        // Preserve states from current chain
        if (internal->context->hasChain()) {
            internal->context->preserveStates(internal->context->chain());
        }

        // Force reload
        internal->hotReload->forceReload();
        if (!internal->hotReload->reload()) {
            internal->compileError = internal->hotReload->getError();
            setError(internal->compileError);
            return VIVID_ERROR_COMPILE_FAILED;
        }

        // Reset and setup
        internal->context->resetChain();
        internal->context->clearRegisteredOperators();

        auto setupFn = internal->hotReload->getSetupFn();
        if (setupFn) {
            setupFn(*internal->context);
        }

        // Restore states
        if (internal->context->hasChain()) {
            internal->context->restoreStates(internal->context->chain());
        }

        internal->compileError.clear();
        return VIVID_OK;
    } catch (const std::exception& e) {
        internal->compileError = e.what();
        setError(e.what());
        return VIVID_ERROR_LOAD_FAILED;
    }
}

VIVID_C_API VividResult vivid_context_unload_project(VividContext* ctx) {
    if (!ctx) {
        setError("Invalid argument");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    auto* internal = toInternal(ctx);
    internal->context->resetChain();
    internal->context->clearRegisteredOperators();
    internal->hotReload = std::make_unique<vivid::HotReload>();
    internal->projectPath.clear();
    internal->compileError.clear();
    internal->hasProject = false;
    return VIVID_OK;
}

VIVID_C_API VividCompileStatus vivid_context_get_compile_status(VividContext* ctx) {
    VividCompileStatus status = {true, nullptr, 0, 0};

    if (!ctx) {
        return status;
    }

    auto* internal = toInternal(ctx);

    if (!internal->compileError.empty()) {
        status.success = false;
        status.message = internal->compileError.c_str();

        // Try to extract line/column from error
        auto& errors = internal->hotReload->getCompileErrors();
        if (!errors.empty()) {
            status.error_line = errors[0].line;
            status.error_column = errors[0].column;
        }
    }

    return status;
}

VIVID_C_API bool vivid_context_has_project(VividContext* ctx) {
    if (!ctx) return false;
    return toInternal(ctx)->hasProject;
}

VIVID_C_API const char* vivid_context_get_project_path(VividContext* ctx) {
    if (!ctx) return nullptr;
    auto* internal = toInternal(ctx);
    return internal->projectPath.empty() ? nullptr : internal->projectPath.c_str();
}

// =============================================================================
// Frame Processing
// =============================================================================

VIVID_C_API VividResult vivid_context_process_frame(VividContext* ctx, double dt) {
    if (!ctx) {
        setError("Invalid argument");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    auto* internal = toInternal(ctx);

    if (!internal->hasProject) {
        setError("No project loaded");
        return VIVID_ERROR_NO_CHAIN;
    }

    try {
        // Inject time delta (headless mode)
        internal->context->injectDeltaTime(dt);
        internal->context->beginFrame();

        // Call update function
        auto updateFn = internal->hotReload->getUpdateFn();
        if (updateFn) {
            updateFn(*internal->context);
        }

        internal->context->endFrame();
        return VIVID_OK;
    } catch (const std::exception& e) {
        setError(e.what());
        return VIVID_ERROR_INTERNAL;
    }
}

VIVID_C_API uint64_t vivid_context_get_frame(VividContext* ctx) {
    if (!ctx) return 0;
    return toInternal(ctx)->context->frame();
}

VIVID_C_API double vivid_context_get_time(VividContext* ctx) {
    if (!ctx) return 0.0;
    return toInternal(ctx)->context->time();
}

VIVID_C_API void vivid_context_reset_time(VividContext* ctx) {
    if (ctx) {
        toInternal(ctx)->context->resetTime();
    }
}

// =============================================================================
// Resolution Management
// =============================================================================

VIVID_C_API VividResult vivid_context_set_resolution(VividContext* ctx, int width, int height) {
    if (!ctx) {
        setError("Invalid argument");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    toInternal(ctx)->context->setRenderResolution(width, height);
    return VIVID_OK;
}

VIVID_C_API int vivid_context_get_width(VividContext* ctx) {
    if (!ctx) return 0;
    return toInternal(ctx)->context->renderWidth();
}

VIVID_C_API int vivid_context_get_height(VividContext* ctx) {
    if (!ctx) return 0;
    return toInternal(ctx)->context->renderHeight();
}

// =============================================================================
// Input Injection
// =============================================================================

VIVID_C_API void vivid_context_set_mouse_position(VividContext* ctx, float x, float y) {
    if (ctx) {
        toInternal(ctx)->context->injectMousePosition(x, y);
    }
}

VIVID_C_API void vivid_context_set_mouse_button(VividContext* ctx, int button, bool pressed) {
    if (ctx) {
        toInternal(ctx)->context->injectMouseButton(button, pressed);
    }
}

VIVID_C_API void vivid_context_set_key(VividContext* ctx, int keycode, bool pressed) {
    if (ctx) {
        toInternal(ctx)->context->injectKeyState(keycode, pressed);
    }
}

VIVID_C_API void vivid_context_add_scroll(VividContext* ctx, float dx, float dy) {
    if (ctx) {
        toInternal(ctx)->context->injectScroll(dx, dy);
    }
}

// =============================================================================
// Chain Access
// =============================================================================

VIVID_C_API VividChain* vivid_context_get_chain(VividContext* ctx) {
    if (!ctx) return nullptr;
    auto* internal = toInternal(ctx);
    if (!internal->hasProject || !internal->context->hasChain()) {
        return nullptr;
    }
    return fromChain(&internal->context->chain());
}

VIVID_C_API VividWGPUTextureView vivid_context_get_output_view(VividContext* ctx) {
    if (!ctx) return nullptr;
    return toInternal(ctx)->context->outputTexture();
}

VIVID_C_API VividWGPUTexture vivid_context_get_output_texture(VividContext* ctx) {
    if (!ctx) return nullptr;
    auto* internal = toInternal(ctx);
    if (!internal->hasProject || !internal->context->hasChain()) {
        return nullptr;
    }
    return internal->context->chain().outputTexture();
}

// =============================================================================
// Operator Iteration
// =============================================================================

VIVID_C_API int vivid_chain_get_operator_count(VividChain* chain) {
    if (!chain) return 0;
    return static_cast<int>(toChain(chain)->operatorNames().size());
}

VIVID_C_API VividOperator* vivid_chain_get_operator_by_index(VividChain* chain, int index) {
    if (!chain || index < 0) return nullptr;
    auto* c = toChain(chain);
    const auto& names = c->operatorNames();
    if (index >= static_cast<int>(names.size())) return nullptr;
    return fromOperator(c->getByName(names[index]));
}

VIVID_C_API VividOperator* vivid_chain_get_operator_by_name(VividChain* chain, const char* name) {
    if (!chain || !name) return nullptr;
    return fromOperator(toChain(chain)->getByName(name));
}

VIVID_C_API VividOperator* vivid_chain_get_output_operator(VividChain* chain) {
    if (!chain) return nullptr;
    return fromOperator(toChain(chain)->getOutput());
}

// =============================================================================
// Operator Information
// =============================================================================

// Thread-local storage for operator names (to return stable pointers)
static thread_local std::string s_operatorName;
static thread_local std::string s_operatorTypeName;
static thread_local std::string s_inputName;

VIVID_C_API const char* vivid_operator_get_name(VividOperator* op) {
    // We need access to the chain to get the name - but we don't have it here
    // Return the type name instead (which the operator knows)
    if (!op) return "";
    s_operatorName = toOperator(op)->name();
    return s_operatorName.c_str();
}

VIVID_C_API const char* vivid_operator_get_type_name(VividOperator* op) {
    if (!op) return "";
    s_operatorTypeName = toOperator(op)->name();
    return s_operatorTypeName.c_str();
}

VIVID_C_API VividOutputKind vivid_operator_get_output_kind(VividOperator* op) {
    if (!op) return VIVID_OUTPUT_TEXTURE;
    return convertOutputKind(toOperator(op)->outputKind());
}

VIVID_C_API bool vivid_operator_is_bypassed(VividOperator* op) {
    if (!op) return false;
    return toOperator(op)->isBypassed();
}

VIVID_C_API void vivid_operator_set_bypassed(VividOperator* op, bool bypassed) {
    if (op) {
        toOperator(op)->setBypassed(bypassed);
    }
}

// =============================================================================
// Operator Outputs (Textures)
// =============================================================================

VIVID_C_API VividWGPUTextureView vivid_operator_get_output_view(VividOperator* op) {
    if (!op) return nullptr;
    return toOperator(op)->outputView();
}

VIVID_C_API VividWGPUTexture vivid_operator_get_output_texture(VividOperator* op) {
    if (!op) return nullptr;
    return toOperator(op)->outputTexture();
}

VIVID_C_API bool vivid_operator_get_texture_info(VividOperator* op, VividTextureInfo* out_info) {
    if (!op || !out_info) return false;

    auto* cppOp = toOperator(op);
    WGPUTexture tex = cppOp->outputTexture();
    if (!tex) return false;

    // Get texture info from WebGPU
    out_info->width = static_cast<int>(wgpuTextureGetWidth(tex));
    out_info->height = static_cast<int>(wgpuTextureGetHeight(tex));
    out_info->format = static_cast<int>(wgpuTextureGetFormat(tex));
    out_info->has_alpha = true;  // Assume RGBA

    return true;
}

VIVID_C_API float vivid_operator_get_output_value(VividOperator* op) {
    if (!op) return 0.0f;
    return toOperator(op)->outputValue();
}

// =============================================================================
// Operator Parameters
// =============================================================================

// Thread-local storage for parameter info
static thread_local std::vector<vivid::ParamDecl> s_paramDecls;
static thread_local std::vector<std::vector<const char*>> s_enumLabels;

VIVID_C_API int vivid_operator_get_param_count(VividOperator* op) {
    if (!op) return 0;
    return static_cast<int>(toOperator(op)->params().size());
}

VIVID_C_API bool vivid_operator_get_param_decl(VividOperator* op, int index, VividParamDecl* out_decl) {
    if (!op || !out_decl || index < 0) return false;

    auto* cppOp = toOperator(op);
    auto params = cppOp->params();
    if (index >= static_cast<int>(params.size())) return false;

    const auto& p = params[index];

    out_decl->name = p.name.c_str();
    out_decl->type = convertParamType(p.type);
    out_decl->min_val = p.minVal;
    out_decl->max_val = p.maxVal;
    std::memcpy(out_decl->default_val, p.defaultVal, sizeof(float) * 4);
    out_decl->string_default = p.stringDefault.c_str();

    // Handle enum labels
    if (!p.enumLabels.empty()) {
        // Store in thread-local to keep pointers valid
        s_enumLabels.resize(params.size());
        s_enumLabels[index].clear();
        for (const auto& label : p.enumLabels) {
            s_enumLabels[index].push_back(label.c_str());
        }
        out_decl->enum_count = static_cast<int>(s_enumLabels[index].size());
        out_decl->enum_labels = s_enumLabels[index].data();
    } else {
        out_decl->enum_count = 0;
        out_decl->enum_labels = nullptr;
    }

    return true;
}

VIVID_C_API bool vivid_operator_get_param(VividOperator* op, const char* name, float out_value[4]) {
    if (!op || !name || !out_value) return false;
    return toOperator(op)->getParam(name, out_value);
}

VIVID_C_API bool vivid_operator_set_param(VividOperator* op, const char* name, const float value[4]) {
    if (!op || !name || !value) return false;
    return toOperator(op)->setParam(name, value);
}

VIVID_C_API const char* vivid_operator_get_param_string(VividOperator* op, const char* name) {
    // String parameters are not yet implemented in the base Operator API
    // Would need extension to support this
    (void)op;
    (void)name;
    return nullptr;
}

VIVID_C_API bool vivid_operator_set_param_string(VividOperator* op, const char* name, const char* value) {
    // String parameters are not yet implemented in the base Operator API
    (void)op;
    (void)name;
    (void)value;
    return false;
}

// =============================================================================
// Operator Inputs
// =============================================================================

VIVID_C_API int vivid_operator_get_input_count(VividOperator* op) {
    if (!op) return 0;
    return static_cast<int>(toOperator(op)->inputCount());
}

VIVID_C_API VividOperator* vivid_operator_get_input(VividOperator* op, int index) {
    if (!op || index < 0) return nullptr;
    return fromOperator(toOperator(op)->getInput(index));
}

VIVID_C_API const char* vivid_operator_get_input_name(VividOperator* op, int index) {
    if (!op || index < 0) return "";
    s_inputName = toOperator(op)->getInputName(index);
    return s_inputName.c_str();
}

// =============================================================================
// Operator Registry
// =============================================================================

// Thread-local storage for registry names
static thread_local std::string s_registryName;
static thread_local std::string s_registryCategory;

VIVID_C_API int vivid_registry_get_operator_count(void) {
    return static_cast<int>(vivid::OperatorRegistry::instance().operators().size());
}

VIVID_C_API const char* vivid_registry_get_operator_name(int index) {
    const auto& ops = vivid::OperatorRegistry::instance().operators();
    if (index < 0 || index >= static_cast<int>(ops.size())) return "";
    s_registryName = ops[index].name;
    return s_registryName.c_str();
}

VIVID_C_API const char* vivid_registry_get_operator_category(int index) {
    const auto& ops = vivid::OperatorRegistry::instance().operators();
    if (index < 0 || index >= static_cast<int>(ops.size())) return "";
    s_registryCategory = ops[index].category;
    return s_registryCategory.c_str();
}

// =============================================================================
// Snapshot/Capture
// =============================================================================

VIVID_C_API VividResult vivid_context_capture_snapshot(VividContext* ctx, const char* path) {
    if (!ctx || !path) {
        setError("Invalid argument");
        return VIVID_ERROR_INVALID_ARGUMENT;
    }

    auto* internal = toInternal(ctx);

    if (!internal->hasProject || !internal->context->hasChain()) {
        setError("No project loaded");
        return VIVID_ERROR_NO_CHAIN;
    }

    try {
        WGPUTexture tex = internal->context->chain().outputTexture();
        if (!tex) {
            setError("No output texture");
            return VIVID_ERROR_INTERNAL;
        }

        if (vivid::VideoExporter::saveSnapshot(
            internal->context->device(),
            internal->context->queue(),
            tex,
            path
        )) {
            return VIVID_OK;
        } else {
            setError("Failed to save snapshot");
            return VIVID_ERROR_INTERNAL;
        }
    } catch (const std::exception& e) {
        setError(e.what());
        return VIVID_ERROR_INTERNAL;
    }
}

VIVID_C_API VividResult vivid_operator_capture_snapshot(VividOperator* op, const char* path) {
    // This requires access to the context for device/queue
    // For now, return error - would need to track context in operator
    (void)op;
    (void)path;
    setError("Not implemented: operator snapshot requires context access");
    return VIVID_ERROR_INTERNAL;
}

// =============================================================================
// Version Information
// =============================================================================

VIVID_C_API const char* vivid_get_version(void) {
    return VIVID_VERSION;
}

VIVID_C_API int vivid_get_api_version(void) {
    return VIVID_API_VERSION;
}
