// Compile harness for the extracted VST3 host. vst3_host_common.h is a
// header-only, anonymous-namespace unit (load plugin, build ProcessContext,
// event list, state save/restore). Including it here verifies the SDK wiring
// and our minimal VividAudioContext compile before we drive it from the audio
// thread in the next step.
#include "vst3_host_common.h"

namespace vivid_poc {
// Touch a couple of the host entry points so the translation unit isn't
// dead-code-eliminated and the SDK symbols get referenced.
const void* vst3_host_link_check() {
    return reinterpret_cast<const void*>(&vst3_build_process_context);
}
}  // namespace vivid_poc
