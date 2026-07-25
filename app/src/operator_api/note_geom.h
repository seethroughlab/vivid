#pragma once
// A track's live MIDI notes as a first-class PORT VALUE, so the note stream flows between visual nodes
// on a custom-ref edge — a Notes source node produces it, a generic Instancer (or any consumer)
// reads it. Mirrors geom.h / VividMesh: the host routes VividNoteSet through the value channel
// (VividGpuContext.custom_inputs / custom_outputs). A PRODUCER owns the note array as a persistent op
// member and publishes a pointer each frame; a CONSUMER reads it within the same topo-ordered pass
// (never keep it past process_gpu).
#include "operator_api/gpu_operator.h"
#include "operator_api/note_bus.h"   // VividActiveNote

typedef struct VividNoteSet {
    const VividActiveNote* notes;   // owned by the producer, valid for this frame
    uint32_t               count;   // number of currently-held notes
} VividNoteSet;

#ifdef __cplusplus
#include "operator_api/type_id.h"
VIVID_DECLARE_CUSTOM_REF_TYPE(VividNoteSet,
                              "seethroughlab.vivid.note_set_v1",
                              "VividNoteSet",
                              false);

namespace vivid::notes {

// Read the note set on custom-ref INPUT ordinal `ord`. Returns nullptr when unwired.
inline const VividNoteSet* input_notes(const VividGpuContext* c, uint32_t ord = 0) {
    if (!c || !c->custom_inputs || ord >= c->custom_input_count) return nullptr;
    return static_cast<const VividNoteSet*>(c->custom_inputs[ord]);
}
// Publish `n` on custom-ref OUTPUT ordinal `ord`. `n` (and the note array it points at) must be an
// operator member kept alive for the rest of this frame — downstream reads it in the same run_chain pass.
inline void publish_notes(const VividGpuContext* c, uint32_t ord, VividNoteSet* n) {
    if (!c || !c->custom_outputs || ord >= c->custom_output_count) return;
    c->custom_outputs[ord] = n;
}

}  // namespace vivid::notes
#endif
