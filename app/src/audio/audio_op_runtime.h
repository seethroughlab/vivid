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

// --- Audio thread (RT-safe) ---
// Source: writes L/R (ignores input), reading `notes` if it's an instrument.
// Effect: transforms L/R in place. `beats_elapsed` is the transport position.
// `note_out` (ADR-0015, ABI v12): a NOTE-EFFECT operator (arpeggiator / chord / transpose) reads
// `notes` and writes the notes it wants downstream into `note_out` (capacity `note_out_cap`),
// setting *note_out_n. Pass nullptr/0 for ordinary instruments and effects — they ignore it.
void audio_op_process(AudioOp*, float* L, float* R, uint32_t frames, uint32_t sample_rate,
                      float bpm, uint32_t beats_per_bar, double beats_elapsed,
                      const session::NoteEvent* notes, uint32_t note_count,
                      session::NoteEvent* note_out = nullptr, uint32_t note_out_cap = 0,
                      uint32_t* note_out_n = nullptr);

}  // namespace vivid
