#pragma once
// The GENERIC reactive-signal edge — so a draw op never refers to its input. Instead of note-specific
// edges, a producer emits a `VividSignal` and a consumer reads it, agnostic to where it came from: a
// Notes node emits it from MIDI today; a Beat or onset node could emit it from other signals tomorrow,
// driving the exact same Instancer / Emitter / … unchanged.
//
// A VividElement is the atom: `pos` a normalized primary axis (0..1 — pitch, position, phase…), `amp`
// an intensity (0..1 — velocity, energy, weight), `id` a stable identity (a re-fire of the same pos
// gets a NEW id, so consumers can tell a re-strike from a sustain). A VividSignal bundles TWO streams
// on ONE edge: `active` = the persistent live SET (membership — consumers age it, fade on leaving:
// Instancer, Solids), `fired` = discrete FIRES this frame (consumers act once per item: Emitter spawns
// a burst per fire). One edge, so one source node feeds every consumer; each reads the stream it wants.
#include "operator_api/gpu_operator.h"
#include <stdint.h>

typedef struct { float pos; float amp; int id; } VividElement;

typedef struct VividSignal {
    const VividElement* active;        // persistent live set (membership); valid for this frame
    uint32_t            active_count;
    const VividElement* fired;         // discrete fires this frame
    uint32_t            fired_count;
} VividSignal;

#ifdef __cplusplus
#include "operator_api/type_id.h"
VIVID_DECLARE_CUSTOM_REF_TYPE(VividSignal, "seethroughlab.vivid.signal_v1", "VividSignal", false);

namespace vivid::elements {

// Read the signal on custom-ref INPUT ordinal `ord`. Returns nullptr when unwired.
inline const VividSignal* input_signal(const VividGpuContext* c, uint32_t ord = 0) {
    if (!c || !c->custom_inputs || ord >= c->custom_input_count) return nullptr;
    return static_cast<const VividSignal*>(c->custom_inputs[ord]);
}
// Publish `s` on custom-ref OUTPUT ordinal `ord`. `s` (and the arrays it points at) must be operator
// members kept alive for the rest of this frame — downstream reads them in the same run_chain pass.
inline void publish_signal(const VividGpuContext* c, uint32_t ord, VividSignal* s) {
    if (!c || !c->custom_outputs || ord >= c->custom_output_count) return;
    c->custom_outputs[ord] = s;
}

}  // namespace vivid::elements
#endif
