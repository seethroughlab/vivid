#pragma once

#include "value_model.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* =============================================================================
 * Vivid Value Views & Outputs — operator-facing value transport
 * (Lane-Value Clean-Break, Phase 1)
 * =============================================================================
 *
 * Successors to the lane views/outputs (VividLaneView / VividLaneOutput /
 * VividStringLaneView / VividStringLaneOutput in types.h). A VividValueView is
 * one immutable input value that may be Scalar or Many of any payload type; a
 * VividValueOutput is the runtime-owned builder an operator writes its output
 * into. The value envelope (type/multiplicity/identity/storage) travels with the
 * data instead of being implied by a special port type.
 *
 * Phase 1 is ADDITIVE: these are defined and compile, but the process contexts
 * (VividFrameContext / VividAudioContext / VividGpuContext) do not expose a
 * `values[]` array yet — that wiring is Phase 4. Only the Phase-1 example
 * operators include this header so far. The lane API remains the live path.
 * ===========================================================================*/

/* One immutable input value. `data` points at the payload, interpreted by
 * `value_type`:
 *   VIVID_VALUE_FLOAT   -> const float*            (value_count floats)
 *   VIVID_VALUE_STRING  -> const char* const*      (value_count C-strings)
 *   VIVID_VALUE_AUDIO   -> const float*            (audio block; channel layout is payload, not multiplicity)
 *   VIVID_VALUE_TEXTURE -> runtime texture handle  (opaque)
 *   VIVID_VALUE_CUSTOM  -> package-defined opaque payload
 * Use the typed helpers below rather than casting `data` directly. */
typedef struct VividValueView {
    const void*       data;           /* payload pointer; interpretation per value_type */
    uint32_t          value_count;    /* number of values (1 = scalar; N for Many) */
    VividValueType    value_type;     /* float / audio / texture / string / custom */
    VividMultiplicity multiplicity;   /* scalar / many */
    VividIdentityMode identity_mode;  /* none / positional / stable_ids */
    VividStorageKind  storage_kind;   /* where the bytes live */
    uint32_t          flags;          /* reserved, must be 0 */
} VividValueView;

/* Runtime-owned output builder. The operator calls resize(count) to obtain a
 * writable payload buffer (typed per the port's value_type), fills it (or, for
 * String payloads, calls set_string per element), then commit(count) to publish.
 * Mirrors VividLaneOutput / VividStringLaneOutput, unified. */
typedef struct VividValueOutput {
    void*  handle;                                       /* runtime-owned, opaque */
    void*  (*resize)(void* handle, uint32_t count);      /* writable payload buffer (per value_type), or NULL */
    void   (*commit)(void* handle, uint32_t count);      /* publish `count` values */
    void   (*set_string)(void* handle, uint32_t index, const char* value); /* String payload only; copies */
} VividValueOutput;

/* ---- Typed read helpers (return NULL on type/arg mismatch) ---------------- */
static inline const float* vivid_value_floats(const VividValueView* v) {
    return (v && v->value_type == VIVID_VALUE_FLOAT) ? (const float*)v->data : (const float*)0;
}
static inline const float* vivid_value_audio(const VividValueView* v) {
    return (v && v->value_type == VIVID_VALUE_AUDIO) ? (const float*)v->data : (const float*)0;
}
static inline const char* const* vivid_value_strings(const VividValueView* v) {
    return (v && v->value_type == VIVID_VALUE_STRING) ? (const char* const*)v->data : (const char* const*)0;
}
static inline uint32_t vivid_value_count(const VividValueView* v) {
    return v ? v->value_count : 0u;
}

/* ---- Typed write helpers -------------------------------------------------- */
/* Resize the output for `count` float values and return the writable buffer. */
static inline float* vivid_value_output_floats(VividValueOutput* o, uint32_t count) {
    return (o && o->resize) ? (float*)o->resize(o->handle, count) : (float*)0;
}
/* Publish `count` values previously written to the resize() buffer. */
static inline void vivid_value_output_commit(VividValueOutput* o, uint32_t count) {
    if (o && o->commit) o->commit(o->handle, count);
}
/* Set string element `index` for a String-payload output (call after resize). */
static inline void vivid_value_output_set_string(VividValueOutput* o, uint32_t index, const char* value) {
    if (o && o->set_string) o->set_string(o->handle, index, value);
}

#ifdef __cplusplus
}  /* extern "C" */
#endif
