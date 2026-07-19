#pragma once
#include <string>
#include <cstddef>
#include <cstdint>

// Native audio-operator runtime. Instantiates OperatorBase+AudioProcessable
// operators from the shared OpRegistry and runs them on the audio thread. Kept in its
// own translation unit so it can include the full operator_api VividAudioContext without
// colliding with the minimal VST3 tempo stub (audio/vivid_audio_context.h) that
// vst3_host.cpp uses. The interface here is primitives + an opaque handle only.
//
// An audio operator is a SOURCE (no audio-input port — an instrument or generator; it
// writes L/R and may read note events) or an EFFECT (has an audio-input port; it
// transforms L/R). Param changes are set from the UI/main thread (lock-free atomics);
// processing is RT-safe (no alloc/lock, block <= kMaxBlock).
namespace vivid {

class OpRegistry;
namespace session { struct NoteEvent; }

struct AudioOp;   // opaque native audio-operator instance

// --- UI/main thread ---
AudioOp*    audio_op_create(OpRegistry& reg, const char* type_name);  // null if not a valid audio op
// Inject PCM + slice regions into a Sampler-style op (RTTI cross-cast to SamplerLoadable).
// Call before the op is published to the audio thread. Returns false if not a sampler.
bool        audio_op_load_sampler(AudioOp*, const float* L, const float* R, size_t n, uint32_t sr,
                                  const uint32_t* slice_starts, const uint32_t* slice_ends,
                                  int nslices, int base_note);
// Enumerate registered audio operators for the device pickers. want_source: true =
// instruments/generators (no audio input), false = effects (has audio input).
int         audio_op_registry_count(OpRegistry& reg, bool want_source);
const char* audio_op_registry_name(OpRegistry& reg, bool want_source, int idx);   // stable registry key
void        audio_op_destroy(AudioOp*);
const char* audio_op_type(const AudioOp*);
bool        audio_op_is_source(const AudioOp*);      // true = instrument/generator (no audio input)
int         audio_op_param_count(const AudioOp*);
const char* audio_op_param_name(const AudioOp*, int i);
int         audio_op_param_hint(const AudioOp*, int i);    // VividDisplayHint (0 = DEFAULT); for compound-widget inspectors
float       audio_op_param_get(const AudioOp*, int i);
float       audio_op_param_min(const AudioOp*, int i);      // Param<> range (for UI normalization)
float       audio_op_param_max(const AudioOp*, int i);
void        audio_op_param_set(AudioOp*, int i, float v);   // any thread (single UI producer); lock-free

// ADR-0015: which registered audio operators are NOTE EFFECTS (notes in -> notes out, no sound).
// The descriptor can't say so yet — an op declares audio ports, not note ports — so note ops mark
// themselves at registration. Marked ops are excluded from the instrument list (they are not
// instruments) and offered as note effects instead.
void audio_op_mark_note_op(const std::string& name);
bool audio_op_is_note_op(const std::string& name);
int  audio_note_op_count(OpRegistry& reg);
const char* audio_note_op_name(OpRegistry& reg, int idx);

// ADR-0022: which registered audio operators are MODULATORS (no audio at all — they emit a 0..1
// control signal). Same escape hatch as note ops above, for the same reason: `audio_op_is_source`
// classifies on "has no audio INPUT port", so a modulator would otherwise be offered as an
// instrument and, wired to Output, be audible as a DC-ish thud. Marked ops are excluded from the
// instrument list and offered as modulators instead.
void audio_op_mark_mod_op(const std::string& name);
bool audio_op_is_mod_op(const std::string& name);
int  audio_mod_op_count(OpRegistry& reg);
const char* audio_mod_op_name(OpRegistry& reg, int idx);

// ADR-0022 P3.3: which registered audio operators are note GENERATORS — algorithmic note SOURCES
// (Euclid / Chord / RandMelody) that read no notes and emit their own from the transport. Same
// escape hatch as note effects/modulators: they declare an audio OUTPUT port so `descriptor_is_source`
// would offer them as instruments; marking excludes them from the instrument list and lists them as
// generators (a scene cell can hold one in place of a clip).
void audio_op_mark_gen_op(const std::string& name);
bool audio_op_is_gen_op(const std::string& name);
int  audio_gen_op_count(OpRegistry& reg);
const char* audio_gen_op_name(OpRegistry& reg, int idx);

// ADR-0022 P3.3: ask a generator to emit note-offs for the voices it is currently sounding (see
// NoteFlushable). Used to release a scene-gated generator's held notes on a scene switch. Writes up
// to `cap` offs into `out`, sets *count. A no-op (count=0) for ops that don't implement NoteFlushable.
void audio_op_note_flush(AudioOp*, session::NoteEvent* out, uint32_t cap, uint32_t* count);

// ADR-0022: one param driven by a control edge for THIS block. The host resolves the effective
// value (control_resolve() — base + modulation, see audio/audio_graph.h) and hands it over here;
// the op sees the modulated value while `pvals` — the param's BASE — is never written.
//
// That split is the whole point: the user's knob keeps its value under modulation, stays
// draggable, and needs nothing restored on disconnect. It also means audio_op_param_get() returns
// the BASE, not what the DSP is currently using. Today those are identical, so nothing can tell;
// once a control edge exists they diverge, and a caller wanting "what is this op ACTUALLY doing"
// needs its own accessor (the visuals graph has had this split for ages: op_param_base_at vs
// op_param_value_at).
struct AudioOpParamOverride {
    int   param = -1;
    float value = 0.f;   // already resolved — the runtime applies it verbatim
};

// --- Audio thread (RT-safe) ---
// Source: writes L/R (ignores input), reading `notes` if it's an instrument.
// Effect: transforms L/R in place. `beats_elapsed` is the transport position.
// `note_out` (ADR-0015, ABI v12): a NOTE-EFFECT operator (arpeggiator / chord / transpose) reads
// `notes` and writes the notes it wants downstream into `note_out` (capacity `note_out_cap`),
// setting *note_out_n. Pass nullptr/0 for ordinary instruments and effects — they ignore it.
// `overrides` (ADR-0022): params driven by control edges this block; applied ON TOP of the base
// values, leaving the base untouched. Pass nullptr/0 for an unmodulated node — which is every
// node in a graph with no control edges.
// `control_out` (ADR-0022, ABI v13): a MODULATOR operator (LFO / envelope) writes its normalized
// 0..1 signal here, `control_out_cap` samples. Pass nullptr/0 for everything else.
void audio_op_process(AudioOp*, float* L, float* R, uint32_t frames, uint32_t sample_rate,
                      float bpm, uint32_t beats_per_bar, double beats_elapsed,
                      const session::NoteEvent* notes, uint32_t note_count,
                      session::NoteEvent* note_out = nullptr, uint32_t note_out_cap = 0,
                      uint32_t* note_out_n = nullptr,
                      const AudioOpParamOverride* overrides = nullptr, uint32_t override_count = 0,
                      float* control_out = nullptr, uint32_t control_out_cap = 0);

}  // namespace vivid
