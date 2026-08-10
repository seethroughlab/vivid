#pragma once
/* Operator authoring helpers — port-descriptor builders and param readers.

   These exist so operators stop copy-pasting the same local `tex_port()` / `aud_port()` and the
   `param_values ? param_values[i] : def` lambda into every file (the former was duplicated across
   ~25 core-visuals sources alone). They are pure `inline`/`template` convenience over the C ABI in
   types.h — no new struct, field, or export, so nothing about the ABI changes. `operator.h`
   includes this, so any operator that includes `operator_api/operator.h` gets them for free.

   Scope note: texture and stereo-audio ports plus the three param readers cover the overwhelmingly
   common cases. Scene ports live in the vivid-3d SDK (they need VividSceneFragment, a 3D type that
   must not leak into the lean core), and note/control ports are built with several varied fields in
   the built-in audio ops only — both are intentionally left out here rather than guessed. */
#include "operator_api/types.h"
#include <cmath>

namespace vivid {

// ---- Texture ports (VIVID_PORT_TEXTURE / VIVID_VALUE_TEXTURE, scalar) -----------------------
// Byte-for-byte the descriptor the core-visuals `tex_port()` copies produce.
inline VividPortDescriptor texture_port(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_TEXTURE; p.direction = dir;
    p.value_type = VIVID_VALUE_TEXTURE; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    return p;
}
inline VividPortDescriptor texture_input(const char* name = "input") {
    return texture_port(name, VIVID_PORT_INPUT);
}
inline VividPortDescriptor texture_output(const char* name = "texture") {
    return texture_port(name, VIVID_PORT_OUTPUT);
}

// ---- Stereo audio ports (VIVID_PORT_AUDIO_BUFFER / VIVID_VALUE_AUDIO, 2ch) ------------------
// The single shape the audio runtime feeds; extra/non-stereo audio ports are rejected at load
// (operator_descriptor_validation). Matches example-audio's `aud_port()`.
inline VividPortDescriptor audio_port_stereo(const char* name, VividPortDirection dir) {
    VividPortDescriptor p{};
    p.name = name; p.type = VIVID_PORT_AUDIO_BUFFER; p.direction = dir;
    p.value_type = VIVID_VALUE_AUDIO; p.multiplicity = VIVID_MULTIPLICITY_SCALAR;
    p.channels = 2;
    return p;
}
inline VividPortDescriptor audio_input_stereo(const char* name = "input") {
    return audio_port_stereo(name, VIVID_PORT_INPUT);
}
inline VividPortDescriptor audio_output_stereo(const char* name = "output") {
    return audio_port_stereo(name, VIVID_PORT_OUTPUT);
}

// ---- Param readers -------------------------------------------------------------------------
// Read the LIVE value the host wrote into `ctx->param_values[index]` (automation / mappings),
// indexed in collect_params() order, with a fallback when the host passes no param array. This is
// the null-check form the process contexts (GPU / audio / frame) use — none of them carry a param
// count, so the operator's own descriptor order bounds `index`. Templated on the context type so
// one helper serves every process callback.
template <class Ctx>
inline float param_value(const Ctx* ctx, int index, float fallback) {
    return (ctx && ctx->param_values) ? ctx->param_values[index] : fallback;
}
template <class Ctx>
inline int param_int(const Ctx* ctx, int index, int fallback) {
    return (ctx && ctx->param_values) ? static_cast<int>(std::lround(ctx->param_values[index])) : fallback;
}
template <class Ctx>
inline bool param_bool(const Ctx* ctx, int index, bool fallback) {
    return (ctx && ctx->param_values) ? (ctx->param_values[index] > 0.5f) : fallback;
}

} // namespace vivid
