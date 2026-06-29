#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Vivid Value Model — canonical vocabulary (Lane-Value Clean-Break, Phase 0)
 * =============================================================================
 *
 * This header LOCKS the vocabulary for the clean-break that replaces the lane
 * system with a first-class value model. It is currently INERT: nothing
 * #includes it yet, it does not change the ABI, and the runtime still uses the
 * lane API. Phase 1 wires these into the descriptor/context/codegen and bumps
 * VIVID_OPERATOR_ABI_VERSION; later phases delete the lane surfaces.
 *
 * The model: every runtime value carries four orthogonal properties —
 *   1. payload type   (VividValueType)            — what the value IS
 *   2. multiplicity   (VividMultiplicity)         — one value, or many
 *   3. identity       (VividIdentityMode)         — how "many" elements are named
 *   4. storage policy (VividStorageKind)          — where the bytes live
 * and each operator DECLARES how it transforms multiplicity/identity
 *   (VividMultiplicityBehavior) instead of relying on runtime lane heuristics.
 *
 * Invariants (locked):
 *   - Multiplicity is a property of VALUES, never of payload-specific port types.
 *     There is no "lane array" or "string lanes" port type; a port is a payload
 *     type that may carry Scalar or Many.
 *   - Graph EDGES carry value type + multiplicity + identity lineage + storage
 *     requirement. OPERATORS declare the multiplicities they accept and produce.
 *   - Audio channel count is AUDIO PAYLOAD LAYOUT, not value multiplicity. A
 *     stereo audio value is one value with 2 channels, not two values.
 *
 * The full contract, the old(lane)->new(value) mapping table, identity
 * semantics per behavior, and the migration checklist live in
 * docs/runtime/value-model.md.
 * ===========================================================================*/

/* The format of `VIVID_OPERATOR_ABI_VERSION` is owned by types.h; Phase 1 bumps
 * it (from 5) when these descriptors/contexts become part of the ABI. */

/* -----------------------------------------------------------------------------
 * 1. Payload type — what a value is. (Replaces the payload half of the old
 *    port-type enum; the old VIVID_PORT_LANE_ARRAY / VIVID_PORT_STRING_LANES no
 *    longer exist — "many floats"/"many strings" are Float/String + Many.)
 * --------------------------------------------------------------------------- */
typedef uint32_t VividValueType;
#define VIVID_VALUE_FLOAT    0u  // scalar/control float payload
#define VIVID_VALUE_AUDIO    1u  // audio sample block (carries a channel layout)
#define VIVID_VALUE_TEXTURE  2u  // GPU texture/view
#define VIVID_VALUE_STRING   3u  // UTF-8 string payload
#define VIVID_VALUE_CUSTOM   4u  // package-defined opaque payload (by value or ref)

/* -----------------------------------------------------------------------------
 * 2. Multiplicity — one value, or many of the same payload type.
 *    (Replaces LaneSet.lane_count==1 vs >1; the count itself is a separate
 *    runtime field, not encoded in the type.)
 * --------------------------------------------------------------------------- */
typedef uint32_t VividMultiplicity;
#define VIVID_MULTIPLICITY_SCALAR  0u  // exactly one value
#define VIVID_MULTIPLICITY_MANY    1u  // zero or more values (count carried at runtime)

/* -----------------------------------------------------------------------------
 * 3. Identity mode — how the elements of a Many value are named across frames.
 *    (Replaces LaneSet.identity_bearing + the positional/scalar distinction.)
 *      None        — scalar; no element identity.
 *      Positional  — Many identified by index 0..N-1; reordering changes meaning;
 *                    per-element state is positional (the old lane_set_id==0,
 *                    count>1 case).
 *      StableIds   — Many whose elements carry stable identity tokens that
 *                    survive reorder/compaction/recompile (voice-like). Backs the
 *                    successor to vivid_lane_state(); identity is keyed on the
 *                    token, NOT on a node index (fixes the lane-state-across-
 *                    recompile gap where identity broke when node indices moved).
 * --------------------------------------------------------------------------- */
typedef uint32_t VividIdentityMode;
#define VIVID_IDENTITY_NONE        0u
#define VIVID_IDENTITY_POSITIONAL  1u
#define VIVID_IDENTITY_STABLE_IDS  2u

/* -----------------------------------------------------------------------------
 * 4. Storage kind — where a value's bytes live / how they are transported.
 *    (Replaces lane-buffer/string-lane/GPU-promotion/bridge-slot special cases
 *    with one storage-policy axis. GPU "promotion" becomes simply
 *    VIVID_STORAGE_GPU; the bridge becomes VIVID_STORAGE_BRIDGE_SLOT.)
 * --------------------------------------------------------------------------- */
typedef uint32_t VividStorageKind;
#define VIVID_STORAGE_CPU          0u  // frame-side CPU arena
#define VIVID_STORAGE_AUDIO_BLOCK  1u  // audio-rate sample block
#define VIVID_STORAGE_GPU          2u  // GPU storage/texture (was lane GPU promotion)
#define VIVID_STORAGE_BRIDGE_SLOT  3u  // fixed-capacity cross-cadence bridge slot
#define VIVID_STORAGE_STRING_STORE 4u  // string-owning storage
#define VIVID_STORAGE_CUSTOM       5u  // package-defined storage for custom payloads

/* -----------------------------------------------------------------------------
 * 5. Operator multiplicity behavior — what an operator declares about how it
 *    transforms multiplicity and identity. Replaces VividLaneBehavior
 *    {POINTWISE, STRUCTURAL, REDUCTION, KERNEL} + the strategy_independent flag.
 *    The runtime DERIVES the execution mode (was LaneExecutionStrategy
 *    {Scalar, InstancePerLane, LoopBased}) from this declaration plus domain —
 *    execution strategy is no longer a public concept.
 *
 *      ScalarOnly — accepts one value, emits one value.            (was: n/a)
 *      Map        — same op per element; preserves count+identity. (was POINTWISE)
 *      Reduce     — many -> one; identity collapses (must be explicit). (was REDUCTION)
 *      Generate   — scalar/control -> many; mints new identity.    (was STRUCTURAL, expanding)
 *      Collect    — several scalar inputs -> one Many output.      (was STRUCTURAL, gathering)
 *      Preserve   — forwards multiplicity + identity unchanged.    (was POINTWISE pass-through)
 *      Kernel     — sees the WHOLE Many in one invocation (cross-element /
 *                   neighborhood) and may preserve or change count; CANNOT be
 *                   element-lifted.                                (was KERNEL)
 *
 *    NOTE (open decision, see value-model.md): Kernel is retained as a distinct
 *    behavior rather than folded into Map/Reduce, because the executor must NOT
 *    element-lift a cross-element operator. To confirm at Phase-0 sign-off.
 * --------------------------------------------------------------------------- */
typedef uint32_t VividMultiplicityBehavior;
#define VIVID_MULTIPLICITY_SCALAR_ONLY  0u
#define VIVID_MULTIPLICITY_MAP          1u
#define VIVID_MULTIPLICITY_REDUCE       2u
#define VIVID_MULTIPLICITY_GENERATE     3u
#define VIVID_MULTIPLICITY_COLLECT      4u
#define VIVID_MULTIPLICITY_PRESERVE     5u
#define VIVID_MULTIPLICITY_KERNEL       6u

#ifdef __cplusplus
}  // extern "C"
#endif
