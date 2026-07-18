#pragma once
#include "cli/control_json.h"     // operator_to_json + VividOperatorDescriptor + nlohmann::json
#include "audio/vst3_host.h"      // session_available_audio_op_*
#include "gpu/op_runtime.h"       // OpRegistry::descriptor_for

// ADR-0023 step 7 — shared operator-catalog enumeration. The native-audio-op discovery is emitted by
// BOTH `list_audio_operators` (control_handlers_audio.cpp, the back-compat surface) AND the unified
// `unified_operator_catalog()` (control_handlers_introspection.cpp). This helper is the single place
// that turns the session's available native audio ops into JSON, so the two can't drift on the shape.
// The unified catalog decorates each base entry with domain/format/spawn; the compat endpoint uses it
// as-is.
namespace vivid::control_json {

// One JSON entry per available native audio op of the given direction (`want_source` 1 = instruments /
// 0 = effects), each carrying the full operator schema (params + ports + semantics) when a descriptor
// exists, else a minimal {name, kind}. `reg` may be null (then every entry is the minimal form).
inline nlohmann::json native_audio_ops(vivid::session::Session* s, vivid::OpRegistry* reg,
                                       int want_source, const char* kind) {
    nlohmann::json arr = nlohmann::json::array();
    if (!s) return arr;
    for (int i = 0, n = vivid::session::session_available_audio_op_count(s, want_source); i < n; ++i) {
        const char* nm = vivid::session::session_available_audio_op_name(s, want_source, i);
        const VividOperatorDescriptor* d = reg ? reg->descriptor_for(nm ? nm : "") : nullptr;
        arr.push_back(d ? operator_to_json(*d, kind)
                        : nlohmann::json({ {"name", nm ? nm : ""}, {"kind", kind} }));
    }
    return arr;
}

}  // namespace vivid::control_json
