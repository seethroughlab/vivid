#include "runtime/builtin_operators.h"
#include "runtime/operator_registry.h"
#include "operator_api/types.h"

// ============================================================================
// audio_out — explicit audio sink node
// ============================================================================

static const VividPortDescriptor audio_out_ports[] = {
    { "input", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_INPUT },
    { "left",  VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_INPUT },
    { "right", VIVID_PORT_AUDIO_FLOAT, VIVID_PORT_INPUT },
};

static const char* audio_out_device_labels[] = { "Default" };

static VividParamDescriptor audio_out_params[] = {
    { "device", VIVID_PARAM_INT, 0, 0, 0,
      audio_out_device_labels, 1 },
};

static const VividOperatorDescriptor audio_out_desc = {
    "audio_out",
    VIVID_DOMAIN_AUDIO,
    1,                  // param_count
    audio_out_params,
    3,                  // port_count
    audio_out_ports,
    1,                  // time_dependent
};

static const VividOperatorDescriptor* audio_out_descriptor() { return &audio_out_desc; }
static void* audio_out_create()  { return new char; }    // trivial instance (non-null)
static void  audio_out_destroy(void* p) { delete static_cast<char*>(p); }
static void  audio_out_process(void*, VividProcessContext*) { /* no-op */ }

// ============================================================================
// video_out — explicit video sink node (texture input + fit mode)
// ============================================================================

static const VividPortDescriptor video_out_ports[] = {
    { "input", VIVID_PORT_GPU_TEXTURE, VIVID_PORT_INPUT },
};

static const char* video_out_fit_labels[] = { "Fit", "Fill", "Stretch" };
static const char* video_out_display_labels[] = { "Current", "Primary", "Secondary" };

static VividParamDescriptor video_out_params[] = {
    { "fit_mode", VIVID_PARAM_INT, 0, 0, 2,
      video_out_fit_labels, 3 },
    { "fullscreen", VIVID_PARAM_BOOL, 0, 0, 1,
      nullptr, 0 },
    { "display_target", VIVID_PARAM_INT, 0, 0, 2,
      video_out_display_labels, 3 },
};

static const VividOperatorDescriptor video_out_desc = {
    "video_out",
    VIVID_DOMAIN_GPU,
    3,                  // param_count
    video_out_params,
    1,                  // port_count
    video_out_ports,
    1,                  // time_dependent
};

static const VividOperatorDescriptor* video_out_descriptor() { return &video_out_desc; }
static void* video_out_create()  { return new char; }
static void  video_out_destroy(void* p) { delete static_cast<char*>(p); }
static void  video_out_process(void*, VividProcessContext*) { /* no-op */ }

// ============================================================================
// Registration
// ============================================================================

void register_builtin_operators(vivid::OperatorRegistry& registry) {
    registry.register_builtin("audio_out",
        audio_out_descriptor, audio_out_create, audio_out_destroy, audio_out_process);
    registry.register_builtin("video_out",
        video_out_descriptor, video_out_create, video_out_destroy, video_out_process);
}
