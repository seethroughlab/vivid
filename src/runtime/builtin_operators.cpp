#include "runtime/builtin_operators.h"
#include "runtime/operator_registry.h"
#include "operator_api/types.h"
#include "operator_api/midi_types.h"
#include "operator_api/port_type_registry.h"
#include "operator_api/type_id.h"

// ============================================================================
// audio_out — explicit audio sink node
// ============================================================================

static const VividPortDescriptor audio_out_ports[] = {
    { "input", VIVID_PORT_AUDIO, VIVID_PORT_INPUT },
};

static const char* audio_out_device_labels[] = { "Default" };

static VividParamDescriptor audio_out_params[] = {
    { "device", VIVID_PARAM_INT, 0, 0, 0,
      audio_out_device_labels, 1 },
};

static const VividOperatorDescriptor audio_out_desc = {
    "audio_out",
    1,                      // param_count
    audio_out_params,
    1,                      // port_count
    audio_out_ports,
    1,                      // time_dependent
    1,                      // has_process_audio
    0,                      // has_process_gpu
    0, nullptr,             // embedded_op_slots
    VIVID_ENV_AUDIO,        // execution_env
    VIVID_CADENCE_FRAME_ONLY, // cadence_capability
    0,                      // has_process_frame
};

static const VividOperatorDescriptor* audio_out_descriptor() { return &audio_out_desc; }
static void* audio_out_create()  { return new char; }    // trivial instance (non-null)
static void  audio_out_destroy(void* p) { delete static_cast<char*>(p); }
static void  audio_out_process(void*, VividProcessContext*) { /* no-op */ }

// ============================================================================
// video_out — explicit video sink node (texture input + fit mode)
// ============================================================================

static const VividPortDescriptor video_out_ports[] = {
    { "input", VIVID_PORT_TEXTURE, VIVID_PORT_INPUT },
};

static const char* video_out_fit_labels[] = { "Fit", "Fill", "Stretch" };
static const char* video_out_display_labels[] = { "Current", "Primary", "Secondary" };

static VividParamDescriptor video_out_params[] = {
    { "fit_mode", VIVID_PARAM_INT, 0, 0, 2,
      video_out_fit_labels, 3 },
    { "launch", VIVID_PARAM_BOOL, 0, 0, 1,
      nullptr, 0 },
    { "display_target", VIVID_PARAM_INT, 0, 0, 2,
      video_out_display_labels, 3 },
};

static const VividOperatorDescriptor video_out_desc = {
    "video_out",
    3,                      // param_count
    video_out_params,
    1,                      // port_count
    video_out_ports,
    1,                      // time_dependent
    0,                      // has_process_audio
    1,                      // has_process_gpu
    0, nullptr,             // embedded_op_slots
    VIVID_ENV_GPU,          // execution_env
    VIVID_CADENCE_FRAME_ONLY, // cadence_capability
    0,                      // has_process_frame
};

static const VividOperatorDescriptor* video_out_descriptor() { return &video_out_desc; }
static void* video_out_create()  { return new char; }
static void  video_out_destroy(void* p) { delete static_cast<char*>(p); }
static void  video_out_process(void*, VividProcessContext*) { /* no-op */ }

// ============================================================================
// Registration
// ============================================================================

static void register_core_custom_types() {
    const VividPortTypeInfo midi_info = vivid_custom_type_info<VividMidiBuffer>();
    vivid_register_port_type(&midi_info);
}

void register_builtin_operators(vivid::OperatorRegistry& registry) {
    register_core_custom_types();

    registry.register_builtin("audio_out",
        audio_out_descriptor, audio_out_create, audio_out_destroy, audio_out_process);
    registry.register_builtin("video_out",
        video_out_descriptor, video_out_create, video_out_destroy, video_out_process);
}
