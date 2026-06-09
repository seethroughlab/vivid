#include "runtime/operators/builtin_operators.h"
#include "runtime/operators/operator_registry.h"
#include "runtime/audio/audio_device_list.h"
#include "operator_api/types.h"
#include "operator_api/note_types.h"
#include "operator_api/port_type_registry.h"
#include "operator_api/type_id.h"

// ============================================================================
// audio_out — explicit audio sink node
// ============================================================================

static const VividPortDescriptor audio_out_ports[] = {
    { "input", VIVID_PORT_AUDIO_BUFFER, VIVID_PORT_INPUT },
};

// Choice labels are populated at registration time from AudioDeviceList.
// The pointers point into AudioDeviceList's storage, which lives for the
// process lifetime.
static VividParamDescriptor audio_out_params[] = {
    { "device", VIVID_PARAM_INT, 0, 0, 0,
      nullptr, 0 },
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
    0,                        // has_process_frame
    0,                        // strategy_independent
};

static const VividOperatorDescriptor* audio_out_descriptor() { return &audio_out_desc; }
static void* audio_out_create()  { return new char; }    // trivial instance (non-null)
static void  audio_out_destroy(void* p) { delete static_cast<char*>(p); }
static void  audio_out_process(void*, VividFrameContext*) { /* no-op */ }

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
    0,                      // has_process_frame
};

static const VividOperatorDescriptor* video_out_descriptor() { return &video_out_desc; }
static void* video_out_create()  { return new char; }
static void  video_out_destroy(void* p) { delete static_cast<char*>(p); }
static void  video_out_process(void*, VividFrameContext*) { /* no-op */ }

// ============================================================================
// Registration
// ============================================================================

static void register_core_custom_types() {
    const VividPortTypeInfo notes_info = vivid_custom_type_info<VividNoteBuffer>();
    vivid_register_port_type(&notes_info);
}

namespace vivid {
void sync_audio_out_device_choices() {
    auto& dev_list = AudioDeviceList::instance();
    audio_out_params[0].choice_labels = dev_list.label_ptrs();
    audio_out_params[0].choice_count  = dev_list.count();
    audio_out_params[0].max_value     = dev_list.count() > 0
        ? static_cast<float>(dev_list.count() - 1) : 0.0f;
    // Pin the current snapshot so the pointers we just stashed in the
    // descriptor stay valid until the next sync.
    dev_list.pin_active_for_descriptor();
}
}

void register_builtin_operators(vivid::OperatorRegistry& registry) {
    register_core_custom_types();

    // Populate the audio_out device dropdown from the system's playback
    // devices. AudioDeviceList holds the canonical snapshot.
    vivid::AudioDeviceList::instance().refresh();
    vivid::sync_audio_out_device_choices();

    registry.register_builtin("audio_out",
        audio_out_descriptor, audio_out_create, audio_out_destroy, audio_out_process);
    registry.register_builtin("video_out",
        video_out_descriptor, video_out_create, video_out_destroy, video_out_process);
}
